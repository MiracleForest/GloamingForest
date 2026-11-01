// "client.render" (F12). Client build only.
//
// One hook per render event (rule R8: units register here):
// - world: LevelRendererCamera::renderBlockEntities, opaque pass: onWorld callbacks, then the block
//   renderers of the tracked block positions (blocks with a renderer are recorded while their chunk
//   is meshed: ClientBlockPipeline::WorldExtractorStep::extractCustomBlock);
// - entities: ActorRenderDispatcher::render, after the entity's own geometry;
// - items: ItemInHandRenderer::renderItem (first / third person), ItemRenderer::render (item
//   entities on the ground), InventoryItemRenderer::render (GUI slots, drawn after the slot batch),
//   BlockActorRenderDispatcher::render of item frames (Fixed, in the item space of Java's
//   ItemFrameRenderer), the humanoid head gear (DataDrivenRenderer_tempComponent_HumanoidAdditional
//   Rendering::_renderHeadGear: Head, in the head pose the game leaves for its head gear);
// - maps: the TF decorations of gf::client::maps on held maps (ItemInHandRenderer::renderFirstPerson)
//   and framed maps (the item frame dispatch), in the space MapRenderer::_getMapInstance sees;
// - tint: ClientBlockPipeline::PositionDependentTintResolver::_get (TF's leaf / grass colours).
//
// The registries are immutable snapshots (copy on write): the render thread and the chunk builder
// threads read them without a lock; each builder thread keeps its own per-block-state cache for the
// snapshot it read. The tracked block positions have a lock of their own.
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
#include <memory>
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
#include "mc/client/renderer/actor/DataDrivenRenderer_tempComponent_HumanoidAdditionalRendering.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/actor/MapRenderer.h"
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
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/animation/BoneOrientation.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

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

std::once_flag& onceFlag(std::string const& key) {
    static std::mutex                                                       m;
    static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> flags;
    std::lock_guard                                                         lock(m);
    auto&                                                                   f = flags[key];
    if (!f) f = std::make_unique<std::once_flag>();
    return *f;
}

void warn(std::string const& key, std::string const& message) {
    std::call_once(onceFlag("warn:" + key), [&] { log().warn("render: {}", message); });
}

void infoOnce(std::string const& key, std::string const& message) {
    std::call_once(onceFlag("info:" + key), [&] { log().info("render: {}", message); });
}

// ---- materials and textures ----------------------------------------------------------------------

/// A material of the switchable group, kept for the process (never destroyed at exit).
mce::MaterialPtr* materialOf(char const* name) {
    static std::mutex                               m;
    static std::map<std::string, mce::MaterialPtr*> cache;
    std::lock_guard                                 lock(m);
    auto&                                           slot = cache[name];
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
    static std::mutex                                        m;
    static std::unordered_map<std::string, mce::TexturePtr*> cache;
    std::lock_guard                                          lock(m);
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
mce::MaterialPtr const* modelMaterial(BaseActorRenderContext* actors) {
    if (!actors) return nullptr;
    BlockActorRenderDispatcher& dispatcher = actors->mBlockEntityRenderDispatcher;
    auto* moving = static_cast<MovingBlockActorRenderer*>(dispatcher.mRenderers->operator[](BlockActorRendererId::MovingBlock).get());
    if (!moving) return nullptr;
    return &moving->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerAlphatest)].get();
}

Vec3 relative(RenderContext const& ctx, Vec3 const& p) { return ctx.itemSpace ? p : Vec3{p.x - ctx.camera.x, p.y - ctx.camera.y, p.z - ctx.camera.z}; }

/// The matrix as one log line (spaces of the item contexts and maps, spike S5).
std::string matrixText(glm::mat4 const& m) {
    std::string s;
    for (int c = 0; c < 4; ++c) {
        s += c ? " | " : "";
        for (int r = 0; r < 4; ++r) s += fmt::format("{}{:.4g}", r ? "," : "", m[c][r]);
    }
    return s;
}

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

/// One immutable state of every registry (copy on write; readers keep the snapshot they loaded).
struct Snapshot {
    std::unordered_map<std::string, BlockEntry>                  blocks;
    std::unordered_map<std::string, ItemEntry>                   items;
    std::unordered_map<std::string, std::vector<EntityRenderFn>> entities;
    std::vector<WorldFn>                                         world;
    std::unordered_map<std::string, tint::TintFn>                tints;
};
using SnapshotPtr = std::shared_ptr<Snapshot const>;

struct Registry {
    std::mutex                write; ///< writers only
    std::atomic<SnapshotPtr>  snapshot{std::make_shared<Snapshot const>()};
    std::atomic<std::uint64_t> version{0};
    std::atomic<bool>         anyBlocks{false}, anyTints{false}, anyItems{false}; ///< fast paths
    // tracked block positions (chunk builders insert, the render thread scans)
    std::mutex                                   trackedMutex;
    std::unordered_set<BlockPos, PosHash, PosEq> tracked;
    int                                          dim         = -1; ///< render thread
    bool                                         loggedWorld = false;
};
Registry& reg() {
    static Registry r;
    return r;
}

SnapshotPtr snap() { return reg().snapshot.load(std::memory_order_acquire); }

/// Copies the current snapshot, lets `change` edit it and publishes it.
template <class F>
void update(F&& change) {
    auto&           r = reg();
    std::lock_guard lock(r.write);
    auto            next = std::make_shared<Snapshot>(*snap());
    change(*next);
    r.anyBlocks = !next->blocks.empty();
    r.anyTints  = !next->tints.empty();
    r.anyItems  = !next->items.empty();
    r.snapshot.store(std::move(next), std::memory_order_release);
    r.version.fetch_add(1, std::memory_order_acq_rel);
}

/// The snapshot and per-block-state caches of the calling thread (chunk builders): no lock while the
/// registries do not change.
struct ThreadView {
    std::uint64_t                                             version = ~std::uint64_t{0};
    SnapshotPtr                                               snapshot;
    std::unordered_map<Block const*, BlockEntry const*>       blocks; ///< nullptr: no renderer
    std::unordered_map<Block const*, tint::TintFn const*>     tints;  ///< nullptr: no tint
};
ThreadView& threadView() {
    thread_local ThreadView v;
    auto                    now = reg().version.load(std::memory_order_acquire);
    if (v.version != now) {
        v.snapshot = snap();
        v.version  = now;
        v.blocks.clear();
        v.tints.clear();
    }
    return v;
}

BlockEntry const* blockEntryOf(ThreadView& v, Block const& block) {
    auto [it, fresh] = v.blocks.try_emplace(&block, nullptr);
    if (fresh) {
        auto e     = v.snapshot->blocks.find(game::blockId(block));
        it->second = e == v.snapshot->blocks.end() ? nullptr : &e->second;
    }
    return it->second;
}

tint::TintFn const* tintOfBlock(ThreadView& v, Block const& block) {
    auto [it, fresh] = v.tints.try_emplace(&block, nullptr);
    if (fresh) {
        auto e     = v.snapshot->tints.find(game::blockId(block));
        it->second = e == v.snapshot->tints.end() ? nullptr : &e->second;
    }
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
        auto&        ctx   = im.ctx;
        Tessellator& tess  = ctx.screen.tessellator;
        bool         begun = false;
        // each segment is a thin square bar (Java draws 1 px lines; a bar shows at every distance)
        float const half = ctx.lineHalfWidth > 0 ? ctx.lineHalfWidth : 1.0f / 64;
        for (auto const& s : im.segs) {
            float r = static_cast<float>((s.argb >> 16) & 0xFF) / 255.f, g = static_cast<float>((s.argb >> 8) & 0xFF) / 255.f,
                  b = static_cast<float>(s.argb & 0xFF) / 255.f, a = static_cast<float>((s.argb >> 24) & 0xFF) / 255.f;
            Vec3  p0 = relative(ctx, s.a), p1 = relative(ctx, s.b);
            Vec3  d{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 1e-6f) continue;
            if (!begun) tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
            begun = true;
            Vec3  u{d.x / len, d.y / len, d.z / len};
            Vec3  ref = std::abs(u.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3  pv{u.y * ref.z - u.z * ref.y, u.z * ref.x - u.x * ref.z, u.x * ref.y - u.y * ref.x};
            float pl = std::sqrt(pv.x * pv.x + pv.y * pv.y + pv.z * pv.z);
            pv       = Vec3{pv.x / pl * half, pv.y / pl * half, pv.z / pl * half};
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
        if (begun) MeshHelpers::renderMeshImmediately(ctx.screen, tess, lineMaterial(ctx.throughWalls), OffscreenCaptureDescription{});
    } catch (...) {}
}

// ---- drawGeo ---------------------------------------------------------------------------------------------

void drawGeo(RenderContext& ctx, std::string const& geoId, std::string const& texture, Transform const& transform) {
    auto const* model = geo::find(geoId);
    if (!model) {
        warn("geo:" + geoId, "unknown geometry " + geoId);
        return;
    }
    auto const* material = modelMaterial(ctx.actors);
    if (!material) {
        warn("geo_material", "drawGeo needs a world render context (no model material here)");
        return;
    }
    auto* tex = textureOf(texture);
    if (!tex) return;
    auto&        screen = ctx.screen;
    Tessellator& tess   = screen.tessellator;
    auto&        world  = screen.camera.worldMatrixStack.get();
    auto         ref    = world.push(false);
    Vec3         at     = relative(ctx, transform.translation);
    glm::mat4    m      = glm::translate(glm::mat4{1.f}, glm::vec3{at.x, at.y, at.z});
    m                   = glm::rotate(m, glm::radians(transform.rotationDeg.y), glm::vec3{0, 1, 0});
    m                   = glm::rotate(m, glm::radians(transform.rotationDeg.x), glm::vec3{1, 0, 0});
    m                   = glm::rotate(m, glm::radians(transform.rotationDeg.z), glm::vec3{0, 0, 1});
    m                   = glm::scale(m, glm::vec3{transform.scale.x, transform.scale.y, transform.scale.z});
    ref.mat->_m.get()   = ref.mat->_m.get() * m;
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
    update([&](Snapshot& s) { s.blocks[blockId] = {std::move(fn), flags}; });
}

void registerItemRenderer(std::string const& itemId, ItemRenderFn fn, Contexts contexts) {
    if (!fn) return;
    update([&](Snapshot& s) { s.items[itemId] = {std::move(fn), contexts}; });
}

void registerEntityRenderer(std::string const& entityId, EntityRenderFn fn) {
    if (!fn) return;
    update([&](Snapshot& s) { s.entities[entityId].push_back(std::move(fn)); });
}

void onWorld(WorldFn fn) {
    if (!fn) return;
    update([&](Snapshot& s) { s.world.push_back(std::move(fn)); });
}

bool unregisterBlockRenderer(std::string const& blockId) {
    bool had = false;
    update([&](Snapshot& s) { had = s.blocks.erase(blockId) > 0; });
    return had;
}

bool unregisterItemRenderer(std::string const& itemId) {
    bool had = false;
    update([&](Snapshot& s) { had = s.items.erase(itemId) > 0; });
    return had;
}

bool unregisterEntityRenderers(std::string const& entityId) {
    bool had = false;
    update([&](Snapshot& s) { had = s.entities.erase(entityId) > 0; });
    return had;
}

std::size_t trackedBlockCount() {
    std::lock_guard lock(reg().trackedMutex);
    return reg().tracked.size();
}

bool hasBlockRenderer(std::string const& blockId) { return snap()->blocks.contains(blockId); }

bool hasItemRenderer(std::string const& itemId, ItemContext where) {
    auto s  = snap();
    auto it = s->items.find(itemId);
    return it != s->items.end() && (it->second.contexts & where);
}

std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z) {
    auto s  = snap();
    auto it = s->tints.find(blockId);
    if (it == s->tints.end() || !it->second) return std::nullopt;
    return it->second(BlockPos{x, y, z});
}

// ---- per-frame work --------------------------------------------------------------------------------------

namespace {

void renderWorld(BaseActorRenderContext& rc) {
    auto* client = currentClient();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return;
    auto& r = reg();
    auto  s = snap();
    // the tracked positions: copied under their lock, checked without it
    std::vector<BlockPos> positions;
    {
        std::lock_guard lock(r.trackedMutex);
        int             dim = static_cast<int>(player->getDimensionId());
        if (dim != r.dim) { // another dimension: its chunks are meshed again
            r.tracked.clear();
            r.dim = dim;
        }
        if (!s->blocks.empty()) positions.assign(r.tracked.begin(), r.tracked.end());
    }
    std::vector<std::tuple<BlockPos, Block const*, BlockEntry const*>> blocks;
    std::vector<BlockPos>                                              stale;
    if (!positions.empty()) {
        auto& region = player->getDimensionBlockSource();
        for (auto const& p : positions) {
            if (!region.hasChunksAt(p, 0, false)) {
                stale.push_back(p);
                continue;
            }
            auto const& b  = region.getBlock(p);
            auto        it = s->blocks.find(game::blockId(b));
            if (it == s->blocks.end()) {
                stale.push_back(p);
                continue;
            }
            blocks.emplace_back(p, &b, &it->second);
        }
    }
    if (!stale.empty()) {
        std::lock_guard lock(r.trackedMutex);
        for (auto const& p : stale) r.tracked.erase(p);
    }
    if (s->world.empty() && blocks.empty()) return;
    auto ctx = contextOf(rc);
    for (auto const& fn : s->world) {
        try {
            fn(ctx, ctx.partialTicks);
        } catch (std::exception const& e) {
            warn("world_fn", std::string("a world renderer failed: ") + e.what());
        }
    }
    for (auto const& [pos, block, entry] : blocks) {
        ctx.throughWalls = entry->flags.throughWalls;
        try {
            entry->fn(ctx, pos, *block, ctx.partialTicks);
        } catch (std::exception const& e) {
            warn("block_fn", std::string("a block renderer failed: ") + e.what());
        }
    }
    if (!r.loggedWorld) {
        r.loggedWorld = true;
        log().info("render: world pass active ({} world renderers, {} blocks)", s->world.size(), blocks.size());
    }
}

/// The item renderer of `itemId` for `where` (nullptr: none); valid while `s` is held.
ItemEntry const* itemEntry(Snapshot const& s, std::string const& itemId, ItemContext where) {
    if (s.items.empty()) return nullptr;
    auto it = s.items.find(itemId);
    if (it == s.items.end() || !(it->second.contexts & where)) return nullptr;
    return &it->second;
}

void runItem(ItemEntry const& e, RenderContext& ctx, ItemStack const& stack, ItemContext where) {
    try {
        e.fn(ctx, stack, where);
    } catch (std::exception const& ex) {
        warn("item_fn", std::string("an item renderer failed: ") + ex.what());
    }
}

struct PendingGui {
    ItemStack const* stack; ///< the slot renderer's own stack: alive until its batch ends (never copied)
    float            x, y, size;
};
std::vector<PendingGui>& pendingGui() {
    static std::vector<PendingGui> v; // client (UI) thread only
    return v;
}

/// An ItemStack with the item, count, aux and user data of `item` (the renderers take stacks).
std::optional<ItemStack> stackOf(ItemStackBase const& item) {
    auto const* type = item.mItem.get();
    if (!type) return std::nullopt;
    ItemStack s;
    s.reinit(*type, std::max<int>(1, item.mCount), item.getAuxValue());
    if (item.mUserData) s.setUserData(item.mUserData->clone());
    return s;
}

// ---- Fixed: items in item frames ----------------------------------------------------------------------

/// Java's ItemFrameRenderer before ItemRenderer.render: the block centre, turned to the frame's
/// facing (xRot, 180 - yRot), 7/16 towards the wall, the item rotation (8 steps about z), scale 0.5.
glm::mat4 frameItemSpace(Vec3 const& blockMinRel, int facing, float rotationDeg) {
    // Bedrock facing_direction: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east; Java's 2D data
    // value (S 0, W 1, N 2, E 3) gives yRot = value * 90; up / down turn xRot -90 / 90
    float xRot = 0, yRot = 0;
    switch (facing) {
    case 0: xRot = 90; break;
    case 1: xRot = -90; break;
    case 2: yRot = 180; break;
    case 3: yRot = 0; break;
    case 4: yRot = 90; break;
    case 5: yRot = 270; break;
    default: break;
    }
    glm::mat4 m = glm::translate(glm::mat4{1.f}, glm::vec3{blockMinRel.x + .5f, blockMinRel.y + .5f, blockMinRel.z + .5f});
    m           = glm::rotate(m, glm::radians(xRot), glm::vec3{1, 0, 0});
    m           = glm::rotate(m, glm::radians(180.f - yRot), glm::vec3{0, 1, 0});
    m           = glm::translate(m, glm::vec3{0, 0, 0.4375f});
    int steps   = static_cast<int>(std::lround(rotationDeg / 45.f)) & 7;
    m           = glm::rotate(m, glm::radians(static_cast<float>(steps) * 45.f), glm::vec3{0, 0, 1});
    return glm::scale(m, glm::vec3{.5f, .5f, .5f});
}

/// Nesting of the dispatcher's render overloads on this thread.
int& frameDispatchDepth() {
    thread_local int depth = 0;
    return depth;
}

void renderFramed(BaseActorRenderContext& rc, IVanillaRenderBlockActorComponent& component, Block const& block) {
    if (!reg().anyItems) return;
    auto type = component.getBlockActorType();
    if (type != BlockActorType::ItemFrame && type != BlockActorType::GlowItemFrame) return;
    auto&               frame = static_cast<ItemFrameBlockActor&>(component.getBlockActor());
    ItemInstance const& item  = frame.mItem;
    if (item.isNull()) return;
    auto s = snap();
    auto e = itemEntry(*s, item.getTypeName(), ItemContext::Fixed);
    if (!e) return;
    auto stack = stackOf(item);
    if (!stack) return;
    auto            ctx = contextOf(rc);
    BlockPos const& at  = component.getBlockActorPosition();
    Vec3 const      min{static_cast<float>(at.x) - ctx.camera.x, static_cast<float>(at.y) - ctx.camera.y, static_cast<float>(at.z) - ctx.camera.z};
    auto&           world = rc.mScreenContext.camera.worldMatrixStack.get();
    auto            ref   = world.push(false);
    ref.mat->_m.get()     = ref.mat->_m.get() * frameItemSpace(min, game::intState(block, "facing_direction").value_or(2), frame.mRotation);
    ctx.itemSpace         = true;
    ctx.lineHalfWidth     = 1.0f / 32; // the space is scaled by 0.5
    runItem(*e, ctx, *stack, ItemContext::Fixed);
    infoOnce("fixed", "item frame renderer dispatched (" + item.getTypeName() + ")");
}

// ---- maps -------------------------------------------------------------------------------------------------

// The maps a held-map or item-frame render drew (MapRenderer::render itself cannot be hooked on
// 1.26.51: even a pass-through detour of it crashes the client, spike S5). The game looks up a map's
// instance (MapRenderer::_getMapInstance) with the map's own space on the world matrix; the
// decorations are drawn in that space once the enclosing render returned, so they lie on top.
struct MapScope {
    BaseActorRenderContext*                         rc;
    std::vector<std::pair<std::int64_t, glm::mat4>> maps; ///< map id, its space
};
std::vector<MapScope>& mapScopes() {
    thread_local std::vector<MapScope> s;
    return s;
}

/// Opens a capture around a render that may draw maps (the first-person pass, an item in hand, an
/// item frame); a map belongs to the innermost one, which draws its decorations when it ends (so in
/// the same render pass and projection as the map).
void beginMapCapture(BaseActorRenderContext& rc) { mapScopes().push_back({&rc, {}}); }

void drawMapDecorations(BaseActorRenderContext& rc, std::int64_t mapId, glm::mat4 const& mapSpace);

void endMapCapture() {
    auto& scopes = mapScopes();
    if (scopes.empty()) return;
    auto scope = std::move(scopes.back());
    scopes.pop_back();
    for (auto const& [id, space] : scope.maps) {
        try {
            drawMapDecorations(*scope.rc, id, space);
        } catch (...) {}
    }
}

/// MapRenderer::_getMapInstance inside a capture: remembers the map and the current space.
void captureMap(MapItemSavedData const& data) {
    auto& scopes = mapScopes();
    if (scopes.empty()) return;
    auto&        scope = scopes.back();
    std::int64_t id    = data.mMapId->rawID;
    if (maps::decorationsOf(id).empty()) return;
    infoOnce("map_capture", "map " + std::to_string(id) + " with TF decorations rendered (" + std::to_string(scopes.size()) + " capture scopes)");
    glm::mat4 space = scope.rc->mScreenContext.camera.worldMatrixStack.get().top()._m.get();
    for (auto& [mid, s] : scope.maps) {
        if (mid == id) {
            s = space;
            return;
        }
    }
    scope.maps.emplace_back(id, space);
}

/// TF's decorations over a map, as Java's MapRenderer draws a decoration: at (x / 2 + 64, z / 2 + 64)
/// of the 128 x 128 map, turned by rotation * 22.5 degrees, an 8 x 8 quad of the icon texture.
void drawMapDecorations(BaseActorRenderContext& rc, std::int64_t mapId, glm::mat4 const& mapSpace) {
    auto list = maps::decorationsOf(mapId);
    if (list.empty()) return;
    auto&                   screen   = rc.mScreenContext;
    mce::MaterialPtr const* material = modelMaterial(&rc); // the alpha-tested model material drawGeo uses
    if (!material) return;
    auto& world = screen.camera.worldMatrixStack.get();
    infoOnce("map_space", "map space " + matrixText(mapSpace));
    Tessellator& tess = screen.tessellator;
    int          k    = 0;
    for (auto const& d : list) {
        auto icon = maps::iconOf(d.type);
        if (icon.empty()) {
            warn("map_icon:" + d.type, "no icon for map decoration " + d.type);
            continue;
        }
        auto* tex = textureOf(icon);
        if (!tex) continue;
        auto      ref = world.push(false);
        glm::mat4 m   = glm::translate(glm::mat4{1.f}, glm::vec3{static_cast<float>(d.x) / 2.f + 64.f, static_cast<float>(d.z) / 2.f + 64.f, -0.02f});
        m             = glm::rotate(m, glm::radians(static_cast<float>(d.rotation * 360) / 16.f), glm::vec3{0, 0, 1});
        m             = glm::scale(m, glm::vec3{4.f, 4.f, 3.f});
        m             = glm::translate(m, glm::vec3{-0.125f, 0.125f, 0.f});
        ref.mat->_m.get() = mapSpace * m;
        float z           = static_cast<float>(k++) * -0.001f;
        tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
        tess.color(1.f, 1.f, 1.f, 1.f);
        // Java's corners (-1, 1) (1, 1) (1, -1) (-1, -1) with v from the top; both windings
        tess.simpleQuad(Vec3{-1, 1, z}, Vec2{0, 0}, Vec3{1, 1, z}, Vec2{1, 0}, Vec3{1, -1, z}, Vec2{1, 1}, Vec3{-1, -1, z}, Vec2{0, 1});
        tess.simpleQuad(Vec3{-1, -1, z}, Vec2{0, 1}, Vec3{1, -1, z}, Vec2{1, 1}, Vec3{1, 1, z}, Vec2{1, 0}, Vec3{-1, 1, z}, Vec2{0, 0});
        TextureVariant variant{*tex};
        MeshHelpers::renderMeshImmediately(screen, tess, *material, variant, OffscreenCaptureDescription{});
    }
    infoOnce("map", "map decorations drawn (" + std::to_string(list.size()) + " on map " + std::to_string(mapId) + ")");
}

} // namespace

} // namespace gf::client::render

namespace gf::client::tint {
void registerBlock(std::string const& blockId, TintFn fn) {
    if (!fn) return;
    render::update([&](render::Snapshot& s) { s.tints[blockId] = std::move(fn); });
}
bool unregisterBlock(std::string const& blockId) {
    bool had = false;
    render::update([&](render::Snapshot& s) { had = s.tints.erase(blockId) > 0; });
    return had;
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

// Chunk meshes: remember the blocks that have a block renderer (builder threads).
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
    if (visualReference == ClientBlockPipeline::VisualReference::Block && r::reg().anyBlocks.load(std::memory_order_relaxed)) {
        try {
            auto& view = r::threadView();
            if (r::blockEntryOf(view, block)) {
                BlockPos const& rel = iterData.relativePos;
                auto&           rg  = r::reg();
                std::lock_guard lock(rg.trackedMutex);
                rg.tracked.insert(BlockPos{chunkMinPos.x + rel.x, chunkMinPos.y + rel.y, chunkMinPos.z + rel.z});
            }
        } catch (...) {}
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
        auto s = r::snap();
        if (s->entities.empty()) return;
        auto it = s->entities.find(entity.getTypeName());
        if (it == s->entities.end()) return;
        auto ctx   = r::contextOf(context);
        ctx.camera = cameraTargetPos;
        for (auto const& fn : it->second) fn(ctx, entity, ctx.partialTicks);
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
    r::beginMapCapture(renderContext); // a held map is drawn inside
    try {
        origin(renderContext, entity, item, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
    } catch (...) {
        r::endMapCapture();
        throw;
    }
    r::endMapCapture();
    if (!r::reg().anyItems || item.isNull()) return;
    try {
        bool first = (static_cast<unsigned>(itemFlags) & static_cast<unsigned>(ItemContextFlags::FirstPersonPass)) != 0;
        auto where = first ? ItemContext::FirstPerson : ItemContext::ThirdPerson;
        auto s     = r::snap();
        auto e     = r::itemEntry(*s, item.getTypeName(), where);
        if (!e) return;
        auto ctx      = r::contextOf(renderContext);
        ctx.itemSpace = true;
        r::runItem(*e, ctx, item, where);
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
    if (!r::reg().anyItems) return;
    try {
        ItemStack const* stack = entityRenderData.mItemStack;
        if (!stack || stack->isNull()) return;
        auto s = r::snap();
        auto e = r::itemEntry(*s, stack->getTypeName(), ItemContext::Ground);
        if (!e) return;
        auto  ctx         = r::contextOf(renderContext);
        auto& world       = renderContext.mScreenContext.camera.worldMatrixStack.get();
        auto  ref         = world.push(false);
        Vec3  p           = entityRenderData.position;
        ref.mat->_m.get() = glm::translate(ref.mat->_m.get(), glm::vec3{p.x, p.y, p.z});
        ctx.itemSpace     = true;
        r::runItem(*e, ctx, *stack, ItemContext::Ground);
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
    if (!r::reg().anyItems) return;
    try {
        ItemStack const& stack = mItemInstance;
        if (stack.isNull() || !r::hasItemRenderer(stack.getTypeName(), ItemContext::Gui)) return;
        glm::vec2 pos  = *owner.mCachedPosition;
        glm::vec2 size = *owner.mSize;
        // items are drawn in a shared mesh batch: the renderer runs once the batch is done
        r::pendingGui().push_back({&stack, pos.x, pos.y, std::min(size.x, size.y)});
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
        auto s = r::snap();
        for (auto const& g : list) {
            auto e = r::itemEntry(*s, g.stack->getTypeName(), ItemContext::Gui);
            if (!e) continue;
            RenderContext ctx(mScreenContext);
            ctx.ui      = this;
            ctx.guiX    = g.x;
            ctx.guiY    = g.y;
            ctx.guiSize = g.size;
            r::runItem(*e, ctx, *g.stack, ItemContext::Gui);
        }
    } catch (...) {}
}

// Item frames (ItemContext::Fixed): block actors are drawn through the dispatcher.
using DispatchRender = void (BlockActorRenderDispatcher::*)(BaseActorRenderContext&, BlockSource&, IVanillaRenderBlockActorComponent&, Block const&, bool, mce::MaterialPtr const&, mce::ClientTexture const*, int, std::optional<dragon::RenderMetadata>);
using DispatchRenderAt = void (BlockActorRenderDispatcher::*)(BaseActorRenderContext&, BlockSource&, IVanillaRenderBlockActorComponent&, Block const&, Vec3 const&, BlockPos const&, bool, mce::MaterialPtr const&, mce::ClientTexture const*, int, std::optional<dragon::RenderMetadata>);

LL_TYPE_INSTANCE_HOOK(
    RenderFrameHook,
    ll::memory::HookPriority::Normal,
    BlockActorRenderDispatcher,
    static_cast<DispatchRender>(&BlockActorRenderDispatcher::render),
    void,
    BaseActorRenderContext&                 entityRenderContext,
    BlockSource&                            renderSource,
    IVanillaRenderBlockActorComponent&      renderComponent,
    Block const&                            block,
    bool                                    renderAlphaLayer,
    mce::MaterialPtr const&                 forcedMat,
    mce::ClientTexture const*               forceTex,
    int                                     breakingAmount,
    std::optional<dragon::RenderMetadata>   renderMetadata
) {
    // the overload may forward to the one with a render position: one dispatch per frame block
    ++r::frameDispatchDepth();
    r::beginMapCapture(entityRenderContext);
    try {
        origin(entityRenderContext, renderSource, renderComponent, block, renderAlphaLayer, forcedMat, forceTex, breakingAmount, std::move(renderMetadata));
    } catch (...) {
        r::endMapCapture();
        --r::frameDispatchDepth();
        throw;
    }
    r::endMapCapture();
    --r::frameDispatchDepth();
    if (renderAlphaLayer || breakingAmount > 0) return;
    try {
        r::renderFramed(entityRenderContext, renderComponent, block);
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    RenderFrameAtHook,
    ll::memory::HookPriority::Normal,
    BlockActorRenderDispatcher,
    static_cast<DispatchRenderAt>(&BlockActorRenderDispatcher::render),
    void,
    BaseActorRenderContext&               entityRenderContext,
    BlockSource&                          renderSource,
    IVanillaRenderBlockActorComponent&    renderComponent,
    Block const&                          block,
    Vec3 const&                           renderPos,
    BlockPos const&                       worldPos,
    bool                                  renderAlphaLayer,
    mce::MaterialPtr const&               forcedMat,
    mce::ClientTexture const*             forceTex,
    int                                   breakingAmount,
    std::optional<dragon::RenderMetadata> renderMetadata
) {
    r::beginMapCapture(entityRenderContext);
    try {
        origin(entityRenderContext, renderSource, renderComponent, block, renderPos, worldPos, renderAlphaLayer, forcedMat, forceTex, breakingAmount, std::move(renderMetadata));
    } catch (...) {
        r::endMapCapture();
        throw;
    }
    r::endMapCapture();
    if (renderAlphaLayer || breakingAmount > 0 || r::frameDispatchDepth() > 0) return;
    try {
        r::renderFramed(entityRenderContext, renderComponent, block);
    } catch (...) {}
}

// Items worn on the head (ItemContext::Head): after the game's own head gear, in the head pose it
// leaves on the world matrix, moved to the head's centre as Java's CustomHeadLayer.translateToHead.
LL_TYPE_INSTANCE_HOOK(
    RenderHeadHook,
    ll::memory::HookPriority::Normal,
    DataDrivenRenderer_tempComponent_HumanoidAdditionalRendering,
    &DataDrivenRenderer_tempComponent_HumanoidAdditionalRendering::_renderHeadGear,
    void,
    BaseActorRenderContext&          renderContext,
    dragon::RenderMetadata const     renderMetadata,
    ItemStack const&                 headGear,
    Actor*                           actor,
    std::vector<BoneOrientation>*    boneOrientations
) {
    origin(renderContext, renderMetadata, headGear, actor, boneOrientations);
    if (!r::reg().anyItems || headGear.isNull()) return;
    try {
        auto s = r::snap();
        auto e = r::itemEntry(*s, headGear.getTypeName(), ItemContext::Head);
        if (!e) return;
        // the game leaves the head's pose on the world matrix (spike S5: blocks, y down, at the neck
        // pivot, turned with the head bone): Java's state after head.translateAndRotate
        auto  ctx   = r::contextOf(renderContext);
        auto& world = renderContext.mScreenContext.camera.worldMatrixStack.get();
        r::infoOnce("head_space", "head space " + r::matrixText(world.top()._m.get()));
        auto ref = world.push(false);
        // CustomHeadLayer.translateToHead: up to the head's centre, turned to face forward, y up
        glm::mat4 m       = glm::translate(glm::mat4{1.f}, glm::vec3{0.f, -0.25f, 0.f});
        m                 = glm::rotate(m, glm::radians(180.f), glm::vec3{0, 1, 0});
        m                 = glm::scale(m, glm::vec3{0.625f, -0.625f, -0.625f});
        ref.mat->_m.get() = ref.mat->_m.get() * m;
        ctx.itemSpace     = true;
        ctx.lineHalfWidth = 1.0f / 40;
        r::runItem(*e, ctx, headGear, ItemContext::Head);
        r::infoOnce("head", "head item renderer dispatched (" + headGear.getTypeName() + ")");
    } catch (...) {}
}

// Maps (held and framed): the instance lookup records the map's space inside a capture; the first
// person pass (held maps) and the item frame dispatch (framed maps) open the captures.
LL_TYPE_INSTANCE_HOOK(
    RenderMapInstanceHook,
    ll::memory::HookPriority::Normal,
    MapRenderer,
    &MapRenderer::_getMapInstance,
    MapRenderer::MapInstance&,
    MapItemSavedData const& data
) {
    auto& instance = origin(data);
    try {
        r::captureMap(data);
    } catch (...) {}
    return instance;
}

LL_TYPE_INSTANCE_HOOK(
    RenderFirstPersonHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::renderFirstPerson,
    void,
    BaseActorRenderContext& renderContext,
    Matrix const&           prevProj,
    ItemContextFlags        itemFlags
) {
    r::beginMapCapture(renderContext);
    try {
        origin(renderContext, prevProj, itemFlags);
    } catch (...) {
        r::endMapCapture();
        throw;
    }
    r::endMapCapture();
}

// Position-dependent tints of registered blocks (TF's leaves, grass, ...): builder threads.
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
    if (!r::reg().anyTints.load(std::memory_order_relaxed)) return origin(block, blockPos, tintMethod, biomeTintCache);
    try {
        auto& view = r::threadView();
        if (auto const* fn = r::tintOfBlock(view, block)) {
            BlockPos const& min = mMinPos;
            unsigned        rgb = (*fn)(BlockPos{min.x + blockPos.x, min.y + blockPos.y, min.z + blockPos.z});
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
                std::lock_guard lock(rg.trackedMutex);
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
        render_hooks::RenderFrameHook::hook();
        render_hooks::RenderFrameAtHook::hook();
        render_hooks::RenderHeadHook::hook();
        render_hooks::RenderMapInstanceHook::hook();
        render_hooks::RenderFirstPersonHook::hook();
        render_hooks::RenderTintHook::hook();
    }
    void uninstall() {
        render_hooks::RenderTintHook::unhook();
        render_hooks::RenderFirstPersonHook::unhook();
        render_hooks::RenderMapInstanceHook::unhook();
        render_hooks::RenderHeadHook::unhook();
        render_hooks::RenderFrameAtHook::unhook();
        render_hooks::RenderFrameHook::unhook();
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
