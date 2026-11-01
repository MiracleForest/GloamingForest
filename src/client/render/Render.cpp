// "client.render" (F12). Client build only.
//
// One hook per render event (rule R8: units register here):
// - world: LevelRendererCamera::renderBlockEntities, opaque pass: onWorld callbacks, then the block
//   renderers of the tracked block positions (blocks with a renderer are recorded while their chunk
//   is meshed: ClientBlockPipeline::WorldExtractorStep::extractCustomBlock);
// - entities: ActorRenderDispatcher::render, after the entity's own geometry;
// - items: ItemInHandRenderer::renderItem (first / third person), ItemRenderer::render (item
//   entities on the ground), InventoryItemRenderer::render (GUI slots, drawn after the slot batch);
// - tint: ClientBlockPipeline::PositionDependentTintResolver::_get (TF's leaf / grass colours).
//
// Lines and drawGeo tessellate immediately with the engine's materials: lines with "wireframe"
// (depth tested, vertex colour) or, through walls, "ui_fill_gradient" (no depth test); geometry
// with the moving-block alpha-test material and the geometry's own texture (as Levitate's rotors).

#ifdef GF_CLIENT

#include "client/render/Render.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/actor/ActorRenderData.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/block/tessellation_pipeline/client_block_pipeline/PipelineError.h"
#include "mc/client/renderer/block/tessellation_pipeline/client_block_pipeline/WorldExtractorStep.h"
#include "mc/client/renderer/block/tessellation_pipeline/helper/client_block_pipeline/PositionDependentTintResolver.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/BlockActorRenderer.h"
#include "mc/client/renderer/blockactor/MovingBlockActorRenderer.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"

#include "client/Client.h"
#include "client/render/Geo.h"
#include "feature/ext/ui/Client.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::client::render {

namespace {

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

std::once_flag& warnOnce(std::string const& key) {
    static std::mutex                                               m;
    static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> flags;
    std::lock_guard                                                 lock(m);
    auto&                                                           f = flags[key];
    if (!f) f = std::make_unique<std::once_flag>();
    return *f;
}

void warn(std::string const& key, std::string const& message) {
    std::call_once(warnOnce(key), [&] { log().warn("render: {}", message); });
}

// ---- materials and textures ----------------------------------------------------------------------

/// A material of the switchable group, kept for the process (never destroyed at exit).
mce::MaterialPtr* materialOf(char const* name) {
    static std::mutex                                 m;
    static std::map<std::string, mce::MaterialPtr*>   cache;
    std::lock_guard                                   lock(m);
    auto&                                             slot = cache[name];
    if (!slot) slot = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString(name));
    return slot;
}

bool valid(mce::MaterialPtr const* m) { return m && m->mRenderMaterialInfoPtr.get() != nullptr; }

mce::MaterialPtr& lineMaterial(bool throughWalls) {
    if (throughWalls) {
        auto* m = materialOf("ui_fill_gradient");
        if (valid(m)) return *m;
        warn("through_walls_material", "no through-walls line material; lines are depth tested");
    }
    return *materialOf("wireframe");
}

mce::TexturePtr* textureOf(std::string const& path) {
    static std::mutex                                       m;
    static std::unordered_map<std::string, mce::TexturePtr*> cache;
    std::lock_guard                                         lock(m);
    if (auto it = cache.find(path); it != cache.end()) return it->second;
    auto* c = currentClient();
    if (!c) return nullptr;
    auto group = c->getTextureGroup();
    if (!group) return nullptr;
    auto* t = new mce::TexturePtr(group->getTexture(ResourceLocation(Core::PathView(path)), false, std::nullopt, cg::TextureSetLayerType::Color));
    cache.emplace(path, t);
    return t;
}

/// The moving-block alpha-test material (block / entity textures with alpha test).
mce::MaterialPtr const* modelMaterial(RenderContext& ctx) {
    if (!ctx.actors) return nullptr;
    BlockActorRenderDispatcher& dispatcher = ctx.actors->mBlockEntityRenderDispatcher;
    auto* moving     = static_cast<MovingBlockActorRenderer*>(dispatcher.mRenderers->operator[](BlockActorRendererId::MovingBlock).get());
    if (!moving) return nullptr;
    return &moving->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerAlphatest)].get();
}

Vec3 relative(RenderContext const& ctx, Vec3 const& p) { return ctx.itemSpace ? p : Vec3{p.x - ctx.camera.x, p.y - ctx.camera.y, p.z - ctx.camera.z}; }

// ---- registries ----------------------------------------------------------------------------------

struct BlockEntry {
    BlockRenderFn fn;
    Flags         flags;
};
struct ItemEntry {
    ItemRenderFn fn;
    Contexts     contexts;
};
struct PosHash {
    std::size_t operator()(BlockPos const& p) const noexcept {
        return static_cast<std::size_t>(p.x) * 73856093u ^ static_cast<std::size_t>(p.y) * 19349663u ^ static_cast<std::size_t>(p.z) * 83492791u;
    }
};
struct PosEq {
    bool operator()(BlockPos const& a, BlockPos const& b) const noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }
};
struct Registry {
    std::mutex                                              mutex;
    std::unordered_map<std::string, BlockEntry>             blocks;
    std::unordered_map<Block const*, bool>                  blockHasRenderer; ///< per block state (chunk builders)
    std::unordered_set<BlockPos, PosHash, PosEq>            tracked;
    std::unordered_map<std::string, ItemEntry>              items;
    std::unordered_map<std::string, std::vector<EntityRenderFn>> entities;
    std::vector<WorldFn>                                    world;
    std::unordered_map<std::string, tint::TintFn>           tints;
    std::unordered_map<Block const*, bool>                  blockHasTint;
    bool                                                    loggedWorld = false;
    std::atomic<bool>                                       anyBlocks{false}; ///< lock-free checks on chunk builder threads
    std::atomic<bool>                                       anyTints{false};
    int                                                     dim         = -1;
};
Registry& reg() {
    static Registry r;
    return r;
}

/// With reg().mutex held.
bool hasRendererLocked(Registry& r, Block const& block) {
    if (r.blocks.empty()) return false;
    auto [it, fresh] = r.blockHasRenderer.try_emplace(&block, false);
    if (fresh) it->second = r.blocks.contains(game::blockId(block));
    return it->second;
}

RenderContext contextOf(BaseActorRenderContext& rc) {
    RenderContext ctx(rc.mScreenContext);
    ctx.actors       = &rc;
    ctx.camera       = rc.mImpl->mCameraTargetPosition;
    ctx.partialTicks = rc.mFrameAlpha;
    return ctx;
}

} // namespace

// ---- Lines ---------------------------------------------------------------------------------------------

struct Lines::Impl {
    explicit Impl(RenderContext& c) : ctx(c) {}
    RenderContext& ctx;
    struct Seg {
        Vec3     a, b;
        unsigned argb;
    };
    std::vector<Seg> segs;
};

Lines::Lines(RenderContext& ctx) : mImpl(std::make_unique<Impl>(ctx)) {}

void Lines::line(Vec3 const& a, Vec3 const& b, unsigned argb) { mImpl->segs.push_back({a, b, argb}); }

void Lines::box(Vec3 const& lo, Vec3 const& hi, unsigned argb) {
    for (float y : {lo.y, hi.y}) {
        line({lo.x, y, lo.z}, {hi.x, y, lo.z}, argb);
        line({lo.x, y, hi.z}, {hi.x, y, hi.z}, argb);
        line({lo.x, y, lo.z}, {lo.x, y, hi.z}, argb);
        line({hi.x, y, lo.z}, {hi.x, y, hi.z}, argb);
    }
    for (float x : {lo.x, hi.x})
        for (float z : {lo.z, hi.z}) line({x, lo.y, z}, {x, hi.y, z}, argb);
}

Lines::~Lines() {
    auto& im = *mImpl;
    if (im.segs.empty()) return;
    try {
        auto& ctx  = im.ctx;
        Tessellator& tess = ctx.screen.tessellator;
        tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
        // each segment is a thin square bar (Java draws 1 px lines; a bar shows at every distance)
        constexpr float kHalf = 1.0f / 64;
        for (auto const& s : im.segs) {
            float r = static_cast<float>((s.argb >> 16) & 0xFF) / 255.f, g = static_cast<float>((s.argb >> 8) & 0xFF) / 255.f,
                  b = static_cast<float>(s.argb & 0xFF) / 255.f, a = static_cast<float>((s.argb >> 24) & 0xFF) / 255.f;
            Vec3  p0 = relative(ctx, s.a), p1 = relative(ctx, s.b);
            Vec3  d{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 1e-6f) continue;
            Vec3  u{d.x / len, d.y / len, d.z / len};
            Vec3  ref = std::abs(u.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3  pv{u.y * ref.z - u.z * ref.y, u.z * ref.x - u.x * ref.z, u.x * ref.y - u.y * ref.x};
            float pl = std::sqrt(pv.x * pv.x + pv.y * pv.y + pv.z * pv.z);
            pv       = Vec3{pv.x / pl * kHalf, pv.y / pl * kHalf, pv.z / pl * kHalf};
            Vec3 q{u.y * pv.z - u.z * pv.y, u.z * pv.x - u.x * pv.z, u.x * pv.y - u.y * pv.x};
            auto at = [&](Vec3 const& o, float sp, float sq) { return Vec3{o.x + pv.x * sp + q.x * sq, o.y + pv.y * sp + q.y * sq, o.z + pv.z * sp + q.z * sq}; };
            static constexpr float kCorners[5][2]{{1, 1}, {-1, 1}, {-1, -1}, {1, -1}, {1, 1}};
            for (int side = 0; side < 4; ++side) {
                float shade = side % 2 ? .8f : 1.f;
                tess.color(r * shade, g * shade, b * shade, a);
                for (auto const& v : {at(p0, kCorners[side][0], kCorners[side][1]), at(p1, kCorners[side][0], kCorners[side][1]),
                                      at(p1, kCorners[side + 1][0], kCorners[side + 1][1]), at(p0, kCorners[side + 1][0], kCorners[side + 1][1])})
                    tess.vertex(v.x, v.y, v.z);
            }
        }
        MeshHelpers::renderMeshImmediately(ctx.screen, tess, lineMaterial(ctx.throughWalls), OffscreenCaptureDescription{});
    } catch (...) {}
}

// ---- drawGeo ---------------------------------------------------------------------------------------------

void drawGeo(RenderContext& ctx, std::string const& geoId, std::string const& texture, Transform const& transform) {
    auto const* model = geo::find(geoId);
    if (!model) {
        warn("geo:" + geoId, "unknown geometry " + geoId);
        return;
    }
    auto const* material = modelMaterial(ctx);
    if (!material) {
        warn("geo_material", "drawGeo needs a world render context (no model material here)");
        return;
    }
    auto* tex = textureOf(texture);
    if (!tex) return;
    auto& screen = ctx.screen;
    Tessellator& tess = screen.tessellator;
    auto& world  = screen.camera.worldMatrixStack.get();
    auto  ref    = world.push(false);
    Vec3  at     = relative(ctx, transform.translation);
    glm::mat4 m  = glm::translate(glm::mat4{1.f}, glm::vec3{at.x, at.y, at.z});
    m            = glm::rotate(m, glm::radians(transform.rotationDeg.y), glm::vec3{0, 1, 0});
    m            = glm::rotate(m, glm::radians(transform.rotationDeg.x), glm::vec3{1, 0, 0});
    m            = glm::rotate(m, glm::radians(transform.rotationDeg.z), glm::vec3{0, 0, 1});
    m            = glm::scale(m, glm::vec3{transform.scale.x, transform.scale.y, transform.scale.z});
    ref.mat->_m.get() = ref.mat->_m.get() * m;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
    tess.color(1.f, 1.f, 1.f, 1.f);
    auto v3 = [](std::array<float, 3> const& p) { return Vec3{p[0], p[1], p[2]}; };
    auto v2 = [](std::array<float, 2> const& t) { return Vec2{t[0], t[1]}; };
    for (auto const& q : model->quads) {
        // both windings: the material culls, and mirrored geometry flips the winding
        tess.simpleQuad(v3(q.pos[0]), v2(q.uv[0]), v3(q.pos[1]), v2(q.uv[1]), v3(q.pos[2]), v2(q.uv[2]), v3(q.pos[3]), v2(q.uv[3]));
        tess.simpleQuad(v3(q.pos[3]), v2(q.uv[3]), v3(q.pos[2]), v2(q.uv[2]), v3(q.pos[1]), v2(q.uv[1]), v3(q.pos[0]), v2(q.uv[0]));
    }
    TextureVariant variant{*tex};
    MeshHelpers::renderMeshImmediately(screen, tess, *material, variant, OffscreenCaptureDescription{});
}

// ---- registration ----------------------------------------------------------------------------------------

void registerBlockRenderer(std::string const& blockId, BlockRenderFn fn, Flags flags) {
    if (!fn) return;
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    r.blocks[blockId] = {std::move(fn), flags};
    r.anyBlocks       = true;
    r.blockHasRenderer.clear(); // recomputed per block state
}

void registerItemRenderer(std::string const& itemId, ItemRenderFn fn, Contexts contexts) {
    if (!fn) return;
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    r.items[itemId] = {std::move(fn), contexts};
}

void registerEntityRenderer(std::string const& entityId, EntityRenderFn fn) {
    if (!fn) return;
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    r.entities[entityId].push_back(std::move(fn));
}

void onWorld(WorldFn fn) {
    if (!fn) return;
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    r.world.push_back(std::move(fn));
}

std::size_t trackedBlockCount() {
    std::lock_guard lock(reg().mutex);
    return reg().tracked.size();
}

bool hasBlockRenderer(std::string const& blockId) {
    std::lock_guard lock(reg().mutex);
    return reg().blocks.contains(blockId);
}

std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z) {
    tint::TintFn fn;
    {
        std::lock_guard lock(reg().mutex);
        auto            it = reg().tints.find(blockId);
        if (it == reg().tints.end()) return std::nullopt;
        fn = it->second;
    }
    return fn(BlockPos{x, y, z});
}

// ---- per-frame work --------------------------------------------------------------------------------------

namespace {

void renderWorld(BaseActorRenderContext& rc) {
    auto* client = currentClient();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return;
    auto& r = reg();
    std::vector<WorldFn>                                               world;
    std::vector<std::tuple<BlockPos, Block const*, BlockEntry>>        blocks;
    {
        std::lock_guard lock(r.mutex);
        int             dim = static_cast<int>(player->getDimensionId());
        if (dim != r.dim) { // another dimension: its chunks are meshed again
            r.tracked.clear();
            r.dim = dim;
        }
        world = r.world;
        if (!r.tracked.empty()) {
            auto&                 region = player->getDimensionBlockSource();
            std::vector<BlockPos> stale;
            for (auto const& p : r.tracked) {
                if (!region.hasChunksAt(p, 0, false)) {
                    stale.push_back(p);
                    continue;
                }
                auto const& b  = region.getBlock(p);
                auto        it = r.blocks.find(game::blockId(b));
                if (it == r.blocks.end()) {
                    stale.push_back(p);
                    continue;
                }
                blocks.emplace_back(p, &b, it->second);
            }
            for (auto const& p : stale) r.tracked.erase(p);
        }
    }
    if (world.empty() && blocks.empty()) return;
    auto ctx = contextOf(rc);
    for (auto& fn : world) {
        try {
            fn(ctx, ctx.partialTicks);
        } catch (std::exception const& e) {
            warn("world_fn", std::string("a world renderer failed: ") + e.what());
        }
    }
    for (auto& [pos, block, entry] : blocks) {
        ctx.throughWalls = entry.flags.throughWalls;
        try {
            entry.fn(ctx, pos, *block, ctx.partialTicks);
        } catch (std::exception const& e) {
            warn("block_fn", std::string("a block renderer failed: ") + e.what());
        }
    }
    if (!r.loggedWorld) {
        r.loggedWorld = true;
        log().info("render: world pass active ({} world renderers, {} blocks)", world.size(), blocks.size());
    }
}

/// Item renderer of `stack` for `where` (nullptr: none).
std::optional<ItemEntry> itemEntry(ItemStack const& stack, ItemContext where) {
    if (stack.isNull()) return std::nullopt;
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    if (r.items.empty()) return std::nullopt;
    auto it = r.items.find(stack.getTypeName());
    if (it == r.items.end() || !(it->second.contexts & where)) return std::nullopt;
    return it->second;
}

struct PendingGui {
    ItemStack stack;
    float     x, y, size;
};
std::vector<PendingGui>& pendingGui() {
    static std::vector<PendingGui> v; // client (UI) thread only
    return v;
}

} // namespace

} // namespace gf::client::render

namespace gf::client::tint {
void registerBlock(std::string const& blockId, TintFn fn) {
    if (!fn) return;
    auto&           r = render::reg();
    std::lock_guard lock(r.mutex);
    r.tints[blockId] = std::move(fn);
    r.anyTints       = true;
    r.blockHasTint.clear();
}
} // namespace gf::client::tint

// ---- hooks (one per engine event, rule R8) -------------------------------------------------------------------

namespace render_hooks {
using namespace gf::client::render;
namespace r = gf::client::render;

LL_TYPE_INSTANCE_HOOK(
    RenderWorldHook,
    ll::memory::HookPriority::Normal,
    LevelRendererCamera,
    &LevelRendererCamera::$renderBlockEntities,
    void,
    BaseActorRenderContext& renderContext,
    bool                    renderAlphaLayer
) {
    origin(renderContext, renderAlphaLayer);
    if (renderAlphaLayer) return;
    try {
        r::renderWorld(renderContext);
    } catch (...) {}
}

// Chunk meshes: remember the blocks that have a block renderer.
LL_STATIC_HOOK(
    RenderExtractHook,
    ll::memory::HookPriority::Normal,
    &ClientBlockPipeline::WorldExtractorStep::extractCustomBlock,
    std::optional<ClientBlockPipeline::PipelineError>,
    Block const&                                                       block,
    ClientBlockPipeline::WorldExtractorStep::BlockIterationData const& iterData,
    ClientBlockPipeline::WorldExtractorStep::ExtractionData&           extraction,
    ClientBlockPipeline::WorldView::Interface*                         worldView,
    bool                                                               shouldGenerateFaceCulling,
    ClientBlockPipeline::VisualReference                               visualReference,
    ClientBlockPipeline::ITintResolver&                                tintResolver,
    BiomeTintCache*                                                    biomeTintCache,
    BlockPos const&                                                    chunkMinPos,
    bool                                                               checkIgnoreBlockTransforms
) {
    if (visualReference == ClientBlockPipeline::VisualReference::Block && r::reg().anyBlocks) {
        auto&           rg = r::reg();
        std::lock_guard lock(rg.mutex);
        if (r::hasRendererLocked(rg, block)) {
            BlockPos const& rel = iterData.relativePos;
            rg.tracked.insert(BlockPos{chunkMinPos.x + rel.x, chunkMinPos.y + rel.y, chunkMinPos.z + rel.z});
        }
    }
    return origin(block, iterData, extraction, worldView, shouldGenerateFaceCulling, visualReference, tintResolver, biomeTintCache, chunkMinPos, checkIgnoreBlockTransforms);
}

LL_TYPE_INSTANCE_HOOK(
    RenderEntityHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    &ActorRenderDispatcher::render,
    void,
    BaseActorRenderContext& context,
    Actor&                  entity,
    Vec3 const&             cameraTargetPos,
    Vec3 const&             pos,
    Vec2 const&             rot,
    bool                    ignoreLighting
) {
    origin(context, entity, cameraTargetPos, pos, rot, ignoreLighting);
    try {
        std::vector<EntityRenderFn> fns;
        {
            auto&           rg = r::reg();
            std::lock_guard lock(rg.mutex);
            if (rg.entities.empty()) return;
            auto it = rg.entities.find(entity.getTypeName());
            if (it == rg.entities.end()) return;
            fns = it->second;
        }
        auto ctx   = r::contextOf(context);
        ctx.camera = cameraTargetPos;
        for (auto& fn : fns) fn(ctx, entity, ctx.partialTicks);
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    RenderItemInHandHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::renderItem,
    void,
    BaseActorRenderContext& renderContext,
    Actor&                  entity,
    ItemStack const&        item,
    bool                    posAndRotSetByJSON,
    ItemContextFlags        itemFlags,
    bool                    useMatrixAsIs,
    bool                    renderingMainHand
) {
    origin(renderContext, entity, item, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
    try {
        bool first = (static_cast<unsigned>(itemFlags) & static_cast<unsigned>(ItemContextFlags::FirstPersonPass)) != 0;
        auto where = first ? ItemContext::FirstPerson : ItemContext::ThirdPerson;
        auto e     = r::itemEntry(item, where);
        if (!e) return;
        auto ctx      = r::contextOf(renderContext);
        ctx.itemSpace = true;
        e->fn(ctx, item, where);
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    RenderItemEntityHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::$render,
    void,
    BaseActorRenderContext& renderContext,
    ActorRenderData&        entityRenderData
) {
    origin(renderContext, entityRenderData);
    try {
        ItemStack const* stack = entityRenderData.mItemStack;
        if (!stack) return;
        auto e = r::itemEntry(*stack, ItemContext::Ground);
        if (!e) return;
        auto  ctx   = r::contextOf(renderContext);
        auto& world = renderContext.mScreenContext.camera.worldMatrixStack.get();
        auto  ref   = world.push(false);
        Vec3  p     = entityRenderData.position;
        ref.mat->_m.get() = glm::translate(ref.mat->_m.get(), glm::vec3{p.x, p.y, p.z});
        ctx.itemSpace     = true;
        e->fn(ctx, *stack, ItemContext::Ground);
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    RenderGuiItemHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    origin(ctx, client, owner, pass);
    try {
        ItemStack const& stack = mItemInstance;
        if (!r::itemEntry(stack, ItemContext::Gui)) return;
        glm::vec2 pos  = *owner.mCachedPosition;
        glm::vec2 size = *owner.mSize;
        // items are drawn in a shared mesh batch: the renderer runs once the batch is done
        r::pendingGui().push_back({stack, pos.x, pos.y, std::min(size.x, size.y)});
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    RenderGuiBatchHook,
    ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$endSharedMeshBatch,
    void,
    ::ComponentRenderBatch& renderBatch
) {
    origin(renderBatch);
    if (r::pendingGui().empty()) return;
    auto list = std::move(r::pendingGui());
    r::pendingGui().clear();
    try {
        for (auto const& g : list) {
            auto e = r::itemEntry(g.stack, ItemContext::Gui);
            if (!e) continue;
            RenderContext ctx(mScreenContext);
            ctx.ui      = this;
            ctx.guiX    = g.x;
            ctx.guiY    = g.y;
            ctx.guiSize = g.size;
            e->fn(ctx, g.stack, ItemContext::Gui);
        }
    } catch (...) {}
}

// Position-dependent tints of registered blocks (TF's leaves, grass, ...).
LL_TYPE_INSTANCE_HOOK(
    RenderTintHook,
    ll::memory::HookPriority::Normal,
    ClientBlockPipeline::PositionDependentTintResolver,
    &ClientBlockPipeline::PositionDependentTintResolver::$_get,
    mce::Color,
    Block const&    block,
    BlockPos const& blockPos,
    TintMethod      tintMethod,
    BiomeTintCache* biomeTintCache
) {
    if (!r::reg().anyTints) return origin(block, blockPos, tintMethod, biomeTintCache);
    try {
        gf::client::tint::TintFn fn;
        {
            auto&           rg = r::reg();
            std::lock_guard lock(rg.mutex);
            if (!rg.tints.empty()) {
                auto [it, fresh] = rg.blockHasTint.try_emplace(&block, false);
                if (fresh) it->second = rg.tints.contains(gf::game::blockId(block));
                if (it->second) fn = rg.tints[gf::game::blockId(block)];
            }
        }
        if (fn) {
            BlockPos const& min = mMinPos;
            unsigned        rgb = fn(BlockPos{min.x + blockPos.x, min.y + blockPos.y, min.z + blockPos.z});
            return mce::Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f, static_cast<float>(rgb & 0xFF) / 255.f, 1.f};
        }
    } catch (...) {}
    return origin(block, blockPos, tintMethod, biomeTintCache);
}

} // namespace render_hooks

namespace gf::client::render {
namespace {
class RenderModule {
public:
    void load() {
        geo::setRoot(gf::Mod::getInstance().getSelf().getModDir());
        if (!mExit) {
            mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
                auto&           rg = reg();
                std::lock_guard lock(rg.mutex);
                rg.tracked.clear();
                rg.dim = -1;
            });
        }
    }
    void install() {
        load();
        render_hooks::RenderWorldHook::hook();
        render_hooks::RenderExtractHook::hook();
        render_hooks::RenderEntityHook::hook();
        render_hooks::RenderItemInHandHook::hook();
        render_hooks::RenderItemEntityHook::hook();
        render_hooks::RenderGuiItemHook::hook();
        render_hooks::RenderGuiBatchHook::hook();
        render_hooks::RenderTintHook::hook();
    }
    void uninstall() {
        render_hooks::RenderTintHook::unhook();
        render_hooks::RenderGuiBatchHook::unhook();
        render_hooks::RenderGuiItemHook::unhook();
        render_hooks::RenderItemEntityHook::unhook();
        render_hooks::RenderItemInHandHook::unhook();
        render_hooks::RenderEntityHook::unhook();
        render_hooks::RenderExtractHook::unhook();
        render_hooks::RenderWorldHook::unhook();
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mExit = nullptr;
    }

private:
    ll::event::ListenerPtr mExit;
};
} // namespace
} // namespace gf::client::render

GF_REGISTER_MODULE_ORDERED(gf::client::render::RenderModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT
