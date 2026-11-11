// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/renderer/map/ConqueredMapIconRenderer.java
//   src/main/java/twilightforest/client/renderer/map/MagicMapPlayerIconRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// "client.render" (F12). Client build only.
//
// One hook per render event (rule R8: units register here):
// - world: LevelRendererCamera::renderBlockEntities, opaque pass: onWorld callbacks, then the block
//   renderers of the tracked block positions (the blocks with a renderer are recorded when their
//   render chunk is built: RenderChunkBuilder::build, every 16^3 section the game meshes, whatever
//   tessellator draws the block; a rebuild replaces the section's positions);
// - entities: ActorRenderDispatcher::render, after the entity's own geometry;
// - items: ItemInHandRenderer::renderItem (first / third person), ItemRenderer::render (item
//   entities on the ground), InventoryItemRenderer::render (GUI slots, drawn after the slot batch),
//   BlockActorRenderDispatcher::render of item frames (Fixed, in the item space of Java's
//   ItemFrameRenderer), the humanoid head gear (DataDrivenRenderer_tempComponent_HumanoidAdditional
//   Rendering::_renderHeadGear: Head, in the head pose the game leaves for its head gear);
// - maps: the TF decorations of gf::client::maps on held maps (ItemInHandRenderer::renderFirstPerson)
//   and framed maps (the item frame dispatch), in the space MapRenderer::_getMapInstance sees;
// - tint: ClientBlockPipeline::PositionDependentTintResolver::_get (TF's leaf / grass colours; the
//   game's own colours at the position come from the same resolver, the original function asked for
//   a vanilla block of that tint method, when a tint function asks for them);
// - name tags: ActorRenderDispatcher::extractRenderTextObjects (hideNameTag).
// - worn armour: DataDrivenModel::shouldRenderAttachableOnActor (hideArmor).
//
// The registries are immutable snapshots (copy on write): the render thread and the chunk builder
// threads read them without a lock; each builder thread keeps its own per-block-state cache for the
// snapshot it read. The tracked block positions have a lock of their own.
//
// Lines and drawGeo tessellate immediately with the engine's materials: lines with "wireframe"
// (depth tested, vertex colour) or, through walls, "ui_fill_gradient" (no depth test); geometry
// with the moving-block alpha-test material and the geometry's own texture (as Levitate's rotors),
// lit by the light drawGeo sets before every draw (ActorShaderManager::setupShaderParameters at
// RenderContext::lightAt, or the geometry's own place: the entity constants that material reads).
// Every renderer but onWorld gets the place of its light (lightAt) and the light is applied only
// when something draws with it (drawGeo, or the renderer's own setupLight; feature/ext/ui/Client.h,
// "What a renderer finds at entry").

#ifdef GF_CLIENT

#include "client/render/Render.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/common/Brightness.h"
#include "mc/client/model/models/DataDrivenModel.h"
#include "mc/world/actor/animation/AttachableSlotIndex.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
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
#include "mc/client/renderer/block/BakedBlockLightType.h"
#include "mc/client/renderer/chunks/RenderChunkBuilder.h"
#include "mc/client/renderer/chunks/RenderChunkGeometry.h"
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
#include "mc/deps/core/math/Vec4.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/framebuilder/FrameLightingModelCapabilities.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/deps/minecraft_renderer/objects/NameTagRenderObject.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/animation/BoneOrientation.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/chunk/ChunkViewSource.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/TintMethod.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

#include "client/Client.h"
#include "client/render/Geo.h"
#include "client/render/TexturePath.h"
#include "feature/ext/ui/Client.h"
#include "game/Blocks.h"
#include "game/Items.h"
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
    auto* t = new mce::TexturePtr(group->getTexture(textureLocation(path), false, std::nullopt, cg::TextureSetLayerType::Color));
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
    std::unordered_map<std::string, tint::GameTintFn>            tints; ///< plain TintFns wrapped
    std::unordered_map<std::string, NameTagFn>                   nameTagTypes;   ///< empty fn: every entity of the type
    std::vector<std::pair<std::size_t, NameTagFn>>               nameTagFilters; ///< handle, predicate
    std::vector<std::pair<std::size_t, ArmorFn>>                 armorFilters;   ///< handle, predicate (hideArmor)
};
using SnapshotPtr = std::shared_ptr<Snapshot const>;

struct Registry {
    std::mutex                write; ///< writers only
    std::atomic<SnapshotPtr>  snapshot{std::make_shared<Snapshot const>()};
    std::atomic<std::uint64_t> version{0};
    std::atomic<bool>         anyBlocks{false}, anyTints{false}, anyItems{false}, anyNameTags{false}, anyArmor{false}; ///< fast paths
    std::atomic<std::uint64_t> armorAsked{0}, armorHidden{0}; ///< hideArmor's check (tests, e2e)
    std::size_t               nextNameTagHandle = 1; ///< writers only
    // tracked block positions per render chunk section (keyed by the section's origin): a chunk
    // builder replaces the entry of the section it built, the render thread reads them all
    std::mutex                                                            trackedMutex;
    std::unordered_map<BlockPos, std::vector<BlockPos>, PosHash, PosEq> tracked;
    bool                                         loggedWorld = false;
    bool                                         loggedTracked = false; ///< under trackedMutex
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
    r.anyBlocks   = !next->blocks.empty();
    r.anyTints    = !next->tints.empty();
    r.anyItems    = !next->items.empty();
    r.anyNameTags = !next->nameTagTypes.empty() || !next->nameTagFilters.empty();
    r.anyArmor    = !next->armorFilters.empty();
    r.snapshot.store(std::move(next), std::memory_order_release);
    r.version.fetch_add(1, std::memory_order_acq_rel);
}

/// The snapshot and per-block-state caches of the calling thread (chunk builders): no lock while the
/// registries do not change.
struct ThreadView {
    std::uint64_t                                             version = ~std::uint64_t{0};
    SnapshotPtr                                               snapshot;
    std::unordered_map<Block const*, BlockEntry const*>       blocks; ///< nullptr: no renderer
    std::unordered_map<Block const*, tint::GameTintFn const*> tints;  ///< nullptr: no tint
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

tint::GameTintFn const* tintOfBlock(ThreadView& v, Block const& block) {
    auto [it, fresh] = v.tints.try_emplace(&block, nullptr);
    if (fresh) {
        auto e     = v.snapshot->tints.find(game::blockId(block));
        it->second = e == v.snapshot->tints.end() ? nullptr : &e->second;
    }
    return it->second;
}

/// The game's colours without a world (registry lookups outside a chunk mesh).
class NoGameColors final : public tint::GameColors {
public:
    std::optional<unsigned> foliage() const override { return std::nullopt; }
    std::optional<unsigned> grass() const override { return std::nullopt; }
    std::optional<unsigned> water() const override { return std::nullopt; }
};

unsigned rgbOf(mce::Color const& c) {
    auto ch = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
}

/// The game's colours at the position a tint resolver is asked for: `ask(method)` runs the resolver's
/// own (original) function for a tint method (nullopt: no game colour), at most once per method.
template <class Ask>
class ResolverColors final : public tint::GameColors {
public:
    explicit ResolverColors(Ask& ask) : mAsk(ask) {}
    std::optional<unsigned> foliage() const override { return get(0, TintMethod::DefaultFoliage); }
    std::optional<unsigned> grass() const override { return get(1, TintMethod::Grass); }
    std::optional<unsigned> water() const override { return get(2, TintMethod::Water); }
    [[nodiscard]] bool asked() const { return mHave[0] || mHave[1] || mHave[2]; }

private:
    std::optional<unsigned> get(int i, TintMethod method) const {
        if (!mHave[i]) {
            auto c    = mAsk(method);
            mValue[i] = c ? std::optional<unsigned>(rgbOf(*c)) : std::nullopt;
            mHave[i]  = true;
        }
        return mValue[i];
    }
    Ask&                            mAsk;
    mutable std::optional<unsigned> mValue[3]{};
    mutable bool                    mHave[3]{};
};

RenderContext contextOf(BaseActorRenderContext& rc) {
    RenderContext ctx(rc.mScreenContext);
    ctx.actors       = &rc;
    ctx.camera       = rc.mImpl->mCameraTargetPosition;
    ctx.partialTicks = rc.mFrameAlpha;
    return ctx;
}

std::atomic<std::uint64_t> gLightSetups{0};

/// Vanilla blocks whose own tint is the game's colour of a tint method (oak leaves: foliage, the grass
/// block: grass, water: water), asked in place of the TF block when a tint function wants the game's
/// colour (as F11's LeafTint asks for the foliage). Resolved on the client thread when a world is
/// joined, cleared when it is left (the chunk builders of the next world never see an old Block).
std::atomic<Block const*> gProbes[3]{};

void resolveProbes(bool on) {
    char const* ids[3] = {"minecraft:oak_leaves", "minecraft:grass_block", "minecraft:water"};
    for (int i = 0; i < 3; ++i) gProbes[i].store(on ? game::blockWith(ids[i]) : nullptr, std::memory_order_release);
}

Block const* probeBlock(TintMethod method) {
    int i = method == TintMethod::Grass ? 1 : method == TintMethod::Water ? 2 : 0;
    return gProbes[i].load(std::memory_order_acquire);
}

/// The world position at the middle of block `p`.
Vec3 centreOf(BlockPos const& p) { return Vec3{static_cast<float>(p.x) + .5f, static_cast<float>(p.y) + .5f, static_cast<float>(p.z) + .5f}; }

/// A renderer's context with the place of its light: lightAt / fullBright set; the light itself is
/// applied by what draws with it (drawGeo before every draw, or the renderer's setupLight), so a
/// renderer that draws nothing costs no light lookup (feature/ext/ui/Client.h, "What a renderer finds
/// at entry").
void lightFor(RenderContext& ctx, Vec3 const& at, bool fullBright = false) {
    ctx.lightAt    = at;
    ctx.fullBright = fullBright;
}

/// The screen context's shader colour and dark colour, put back when a pass that set the light for
/// its renderers ends: the game's next draw may read them without setting them (as F11's sign text
/// keeps them). The entity constants themselves cannot be read back; every game renderer sets them
/// before it draws.
class ShaderColours {
public:
    explicit ShaderColours(ScreenContext& screen) : mScreen(screen), mColor(screen.currentShaderColor), mDark(screen.currentShaderDarkColor) {}
    ~ShaderColours() {
        ShaderColor& color = mScreen.currentShaderColor;
        ShaderColor& dark  = mScreen.currentShaderDarkColor;
        color              = mColor;
        color.dirty        = true;
        dark               = mDark;
        dark.dirty         = true;
    }
    ShaderColours(ShaderColours const&)            = delete;
    ShaderColours& operator=(ShaderColours const&) = delete;

private:
    ScreenContext& mScreen;
    ShaderColor    mColor, mDark;
};

} // namespace

/// The material Lines draws its bars with, for world text backgrounds (WorldText.cpp); nullptr when
/// the pack has none.
mce::MaterialPtr const* fillMaterial(bool throughWalls) {
    auto& m = lineMaterial(throughWalls);
    return valid(&m) ? &m : nullptr;
}

bool setupLight(RenderContext& ctx, Vec3 const& at, bool fullBright) {
    if (!ctx.actors) return false;
    IClientInstance& client = ctx.actors->mClientInstance;
    auto*            region = client.getRegion();
    auto*            light  = client.getLightTexture();
    if (!region || !light) return false;
    BlockPos pos{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))};
#if GF_LL_VERSION >= 2660
    // 26.60 dropped the overload that samples the light at a position (with the light volumes):
    // the light at the position, and an uncoloured (white) block light
    ActorShaderManager::setupShaderParameters(ctx.screen, *region, region->getLightColor(pos, Brightness::MIN()), glm::vec4{1.0f}, ctx.partialTicks, fullBright, *light, Vec2{1.0f, 1.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
#else
    ActorShaderManager::setupShaderParameters(ctx.screen, *region, pos, ctx.partialTicks, fullBright, *light, ctx.actors->mLightVolumeManager, Vec2{1.0f, 1.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
#endif
    gLightSetups.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::uint64_t lightSetups() { return gLightSetups.load(std::memory_order_relaxed); }

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
    // the light of this draw (the material multiplies by the entity constants): at lightAt, else at
    // the geometry's own place (world space); never the light the draw before it left
    if (ctx.lightAt) setupLight(ctx, *ctx.lightAt, ctx.fullBright);
    else if (ctx.fullBright) setupLight(ctx, ctx.camera, true);
    else if (!ctx.itemSpace) setupLight(ctx, transform.translation, ctx.fullBright);
    else warn("geo_light", "drawGeo in an item space without RenderContext::lightAt: the light is the item's");
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
    std::size_t     n = 0;
    for (auto const& [section, positions] : reg().tracked) n += positions.size();
    return n;
}

bool hasBlockRenderer(std::string const& blockId) { return snap()->blocks.contains(blockId); }

bool hasItemRenderer(std::string const& itemId, ItemContext where) {
    auto s  = snap();
    auto it = s->items.find(itemId);
    return it != s->items.end() && (it->second.contexts & where);
}

std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z) { return tintOf(blockId, x, y, z, NoGameColors{}); }

std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z, tint::GameColors const& game) {
    auto s  = snap();
    auto it = s->tints.find(blockId);
    if (it == s->tints.end() || !it->second) return std::nullopt;
    return it->second(BlockPos{x, y, z}, game);
}

// ---- name tags ---------------------------------------------------------------------------------------------

void hideNameTag(std::string const& entityId, NameTagFn when) {
    update([&](Snapshot& s) { s.nameTagTypes[entityId] = std::move(when); });
}

std::size_t hideNameTag(NameTagFn when) {
    if (!when) return 0;
    std::size_t handle = 0;
    update([&](Snapshot& s) {
        handle = reg().nextNameTagHandle++; // under the writers' lock
        s.nameTagFilters.emplace_back(handle, std::move(when));
    });
    return handle;
}

bool showNameTag(std::string const& entityId) {
    bool had = false;
    update([&](Snapshot& s) { had = s.nameTagTypes.erase(entityId) > 0; });
    return had;
}

bool showNameTag(std::size_t handle) {
    bool had = false;
    update([&](Snapshot& s) { had = std::erase_if(s.nameTagFilters, [handle](auto const& f) { return f.first == handle; }) > 0; });
    return had;
}

std::optional<NameTagFn> nameTagRule(std::string const& entityId) {
    auto s  = snap();
    auto it = s->nameTagTypes.find(entityId);
    if (it == s->nameTagTypes.end()) return std::nullopt;
    return it->second;
}

bool nameTagHidden(Actor const& entity) {
    if (!reg().anyNameTags.load(std::memory_order_relaxed)) return false;
    auto s = snap();
    if (!s->nameTagTypes.empty()) {
        if (auto it = s->nameTagTypes.find(entity.getTypeName()); it != s->nameTagTypes.end() && (!it->second || it->second(entity))) return true;
    }
    for (auto const& [handle, fn] : s->nameTagFilters) {
        if (fn(entity)) return true;
    }
    return false;
}

// ---- armour hiding -----------------------------------------------------------------------------------------

std::size_t hideArmor(ArmorFn when) {
    if (!when) return 0;
    std::size_t handle = 0;
    update([&](Snapshot& s) {
        handle = reg().nextNameTagHandle++; // one handle counter for every predicate registry
        s.armorFilters.emplace_back(handle, std::move(when));
    });
    return handle;
}

bool showArmor(std::size_t handle) {
    bool had = false;
    update([&](Snapshot& s) { had = std::erase_if(s.armorFilters, [handle](auto const& f) { return f.first == handle; }) > 0; });
    return had;
}

bool armorHidden(ItemStack const& stack) {
    if (!reg().anyArmor.load(std::memory_order_relaxed)) return false;
    auto s = snap();
    for (auto const& [handle, fn] : s->armorFilters) {
        try {
            if (fn(stack)) return true;
        } catch (...) {}
    }
    return false;
}

/// The worn stacks taken from the v2 actor renderer (ArmorV2.cpp).
std::uint64_t armorV2HiddenCount();
bool          armorFiltersRegistered() { return reg().anyArmor.load(std::memory_order_relaxed); }

ArmorChecks armorChecks() { return {reg().armorAsked.load(), reg().armorHidden.load(), armorV2HiddenCount()}; }

// ---- per-frame work --------------------------------------------------------------------------------------

namespace {

/// Set once a subchunk returned by LevelChunk::getSubChunk(y >> 4) carried that absolute index: from
/// then on a missing subchunk is read as an all-air section.
std::atomic<bool> gSubChunkIndexChecked{false};

/// Whether the 16^3 section at height `sectionY` (block y >> 4) of `chunk` may hold a block with a
/// block renderer: false when its subchunk's palettes have none (most sections, so their 4096 blocks
/// are never read), or when it has no subchunk (all air, once the index was seen to be y >> 4). True
/// when it cannot tell.
bool sectionMayHaveRenderer(LevelChunk& chunk, int sectionY, ThreadView& view) {
    try {
        auto* sub = chunk.getSubChunk(static_cast<short>(sectionY));
        if (!sub) return !gSubChunkIndexChecked.load(std::memory_order_relaxed); // no subchunk: air
        if (static_cast<int>(sub->mAbsoluteIndex) != sectionY) {
            warn("subchunk_index", "LevelChunk::getSubChunk(y >> 4) gave another section: block renderer sections are scanned whole");
            return true;
        }
        gSubChunkIndexChecked.store(true, std::memory_order_relaxed);
        std::function<bool(Block const&)> const hasRenderer = [&view](Block const& b) { return blockEntryOf(view, b) != nullptr; };
        bool any = false;
        for (int layer = 0; layer < 2; ++layer) {
            auto* storage = sub->mBlocksReadPtr[layer];
            if (!storage) {
                if (layer == 0) return true; // no standard layer to read: scan
                continue;
            }
            any = any || storage->hasAnyElementMatchingFilterInPalette(hasRenderer);
        }
        return any;
    } catch (...) {
        return true;
    }
}

/// Records the blocks with a block renderer of the render chunk (16^3 section) at `origin`, read from
/// its level chunk, in place of what the section held before (builder threads).
void trackSection(ChunkSource& source, BlockPos const& origin) {
    auto chunk = source.getExistingChunk(ChunkPos{origin.x >> 4, origin.z >> 4});
    if (!chunk) return;
    BlockPos const min  = chunk->mMin;
    auto&          view = threadView();
    int const      x0 = origin.x & ~15, y0 = origin.y & ~15, z0 = origin.z & ~15;
    std::vector<BlockPos> found;
    if (sectionMayHaveRenderer(*chunk, y0 >> 4, view)) {
        for (int y = 0; y < 16; ++y) {
            int const wy = y0 + y;
            if (wy < min.y) continue;
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    Block const& b = chunk->getBlock(ChunkBlockPos{static_cast<uchar>(x), ChunkLocalHeight{static_cast<short>(wy - min.y)}, static_cast<uchar>(z)});
                    if (blockEntryOf(view, b)) found.push_back(BlockPos{x0 + x, wy, z0 + z});
                }
            }
        }
    }
    auto&           rg = reg();
    std::lock_guard lock(rg.trackedMutex);
    BlockPos const  key{x0, y0, z0};
    if (found.empty()) {
        rg.tracked.erase(key);
        return;
    }
    if (!rg.loggedTracked) {
        rg.loggedTracked = true;
        log().info("render: block renderer positions tracked from built render chunks ({} in the section at {} {} {})", found.size(), x0, y0, z0);
    }
    rg.tracked[key] = std::move(found);
}

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
        // another dimension: the positions of the old one go below (no loaded chunk there, or not a
        // block with a renderer); the new one's sections record theirs as they are built
        if (!s->blocks.empty())
            for (auto const& [section, inSection] : r.tracked) positions.insert(positions.end(), inSection.begin(), inSection.end());
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
        for (auto const& p : stale) {
            auto it = r.tracked.find(BlockPos{p.x & ~15, p.y & ~15, p.z & ~15});
            if (it == r.tracked.end()) continue;
            std::erase(it->second, p);
            if (it->second.empty()) r.tracked.erase(it);
        }
    }
    if (s->world.empty() && blocks.empty()) return;
    auto const base = contextOf(rc);
    // every renderer gets a context of its own (what one changes never reaches the next), the light
    // of its own place (never the one the draw before it left) and the shader colours the game left
    // (put back after it; Render.h)
    for (auto const& fn : s->world) {
        // no lightAt: an onWorld callback draws anywhere in the world, so drawGeo lights each of its
        // geometries at that geometry's own translation (a renderer may set lightAt to light by one
        // place)
        RenderContext ctx = base;
        ShaderColours keep(rc.mScreenContext);
        try {
            fn(ctx, ctx.partialTicks);
        } catch (std::exception const& e) {
            warn("world_fn", std::string("a world renderer failed: ") + e.what());
        }
    }
    for (auto const& [pos, block, entry] : blocks) {
        RenderContext ctx = base;
        ctx.throughWalls  = entry->flags.throughWalls;
        ShaderColours keep(rc.mScreenContext);
        lightFor(ctx, centreOf(pos)); // Java's BlockEntityRenderer: the light at the block
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
    if (!blocks.empty()) infoOnce("block_light", "block renderers drawn, each with the light at its block (" + std::to_string(blocks.size()) + " this frame)");
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

/// 1 while a scene renders outside the game's own UI pass, that is while LeviLamina's UI render events
/// run their listeners (a GUI item draw there is the mod's: screen::Canvas::item): RenderUiSceneHook,
/// the outermost hook of ScreenView::render, sets it for the call and puts the old value back after
/// it. 0 inside the game's own UI pass (RenderUiPassHook, the innermost hook: ScreenView::render's
/// original, between the Before and After events) and outside every ScreenView::render, so a GUI item
/// draw of the game or another mod elsewhere keeps the game's path.
int& modUiDepth() {
    static int depth = 0; // UI thread only
    return depth;
}

/// InventoryItemRenderer::getRenderTypeFromItem of a block drawn as a block in the GUI: the chunk the
/// inventory draws in a mesh batch of the terrain atlas (spike S5 round 5: the other chunks are the
/// item atlas and flat block icons).
constexpr int kBlockTypeChunk = 0;

/// Bumped when a world is joined or left (dropGuiAtlas): flushGuiBlock then looks its material and
/// the terrain atlas up again (a world's resource packs, or a pack reload, may replace them).
std::atomic<std::uint64_t> gGuiAtlasGeneration{1};

void dropGuiAtlas() { gGuiAtlasGeneration.fetch_add(1, std::memory_order_acq_rel); }

/// Draws what a GUI block item left open in the screen's tessellator (the geometry of a data-driven
/// block, which the inventory's terrain atlas batch draws at its end) with the UI item material and
/// the terrain atlas.
void flushGuiBlock(ScreenContext& screen, Tessellator& tess) {
    // UI thread only: the handles of the current generation, looked up again after a world change and
    // while the atlas is not there yet (released then on this thread; never destroyed at exit, when
    // the renderer may be gone)
    static mce::MaterialPtr* material   = nullptr;
    static mce::TexturePtr*  atlas      = nullptr;
    static std::uint64_t     generation = 0;
    auto const               now        = gGuiAtlasGeneration.load(std::memory_order_acquire);
    if (generation != now) {
        generation = now;
        delete material;
        delete atlas;
        material = nullptr;
        atlas    = nullptr;
    }
    if (!material) {
        material = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString("ui_item"));
        if (!valid(material)) {
            delete material;
            material = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString("ui_textured"));
        }
    }
    if (!atlas) {
        if (auto* c = currentClient()) {
            if (auto group = c->getTextureGroup())
                atlas = new mce::TexturePtr(group->getTexture(ResourceLocation(Core::PathView("atlas.terrain")), false, std::nullopt, cg::TextureSetLayerType::Color));
        }
    }
    if (!valid(material) || !atlas) {
        warn("gui_block_flush", "no UI item material or terrain atlas: a data-driven GUI block item is left to the next UI draw");
        return;
    }
    TextureVariant tex{*atlas};
    MeshHelpers::renderMeshImmediately(screen, tess, *material, tex, OffscreenCaptureDescription{});
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
    // ItemFrameRenderer: the frame's light; a glow frame's item full bright (getLightVal 15728880)
    ShaderColours keep(rc.mScreenContext);
    lightFor(ctx, centreOf(at), type == BlockActorType::GlowItemFrame);
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
    bool                                            frame; ///< an item frame: Java's MapRenderer `active`
    std::vector<std::pair<std::int64_t, glm::mat4>> maps;  ///< map id, its space
};
std::vector<MapScope>& mapScopes() {
    thread_local std::vector<MapScope> s;
    return s;
}

/// Opens a capture around a render that may draw maps (the first-person pass, an item in hand, an
/// item frame); a map belongs to the innermost one, which draws its decorations when it ends (so in
/// the same render pass and projection as the map). `frame`: the capture of an item frame (a capture
/// opened inside one, e.g. an item render of the frame, is one too).
void beginMapCapture(BaseActorRenderContext& rc, bool frame) {
    auto& scopes = mapScopes();
    bool  inside = !scopes.empty() && scopes.back().frame;
    scopes.push_back({&rc, frame || inside, {}});
}

void drawMapDecorations(BaseActorRenderContext& rc, std::int64_t mapId, glm::mat4 const& mapSpace, bool inFrame);

void endMapCapture() {
    auto& scopes = mapScopes();
    if (scopes.empty()) return;
    auto scope = std::move(scopes.back());
    scopes.pop_back();
    for (auto const& [id, space] : scope.maps) {
        try {
            drawMapDecorations(*scope.rc, id, space, scope.frame);
        } catch (...) {}
    }
}

/// MapRenderer::_getMapInstance inside a capture: remembers the map and the current space.
void captureMap(MapItemSavedData const& data) {
    auto& scopes = mapScopes();
    if (scopes.empty()) return;
    auto&        scope = scopes.back();
    std::int64_t id    = data.mMapId->rawID;
    if (!maps::hasDecorations(id)) return; // every map the game draws, every frame: no copy here
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

/// One textured quad of a map decoration: Java's corners (-1, 1) (1, 1) (1, -1) (-1, -1) with v from
/// the top, at depth `z` of the current matrix; both windings.
void drawMapQuad(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, maps::Icon const& icon, float z) {
    Tessellator& tess = screen.tessellator;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
    tess.color(1.f, 1.f, 1.f, 1.f);
    Vec2 tl{icon.u0, icon.v0}, tr{icon.u1, icon.v0}, br{icon.u1, icon.v1}, bl{icon.u0, icon.v1};
    tess.simpleQuad(Vec3{-1, 1, z}, tl, Vec3{1, 1, z}, tr, Vec3{1, -1, z}, br, Vec3{-1, -1, z}, bl);
    tess.simpleQuad(Vec3{-1, -1, z}, bl, Vec3{1, -1, z}, br, Vec3{1, 1, z}, tr, Vec3{-1, 1, z}, tl);
    TextureVariant variant{tex};
    MeshHelpers::renderMeshImmediately(screen, tess, material, variant, OffscreenCaptureDescription{});
}

/// TF's decorations over a map, as Java's MapRenderer draws a decoration: at (x / 2 + 64, z / 2 + 64)
/// of the 128 x 128 map, turned by rotation * 22.5 degrees, an 8 x 8 quad of the icon at depth
/// index * -0.001. TF's IMapDecorationRenderers: MagicMapPlayerIconRenderer draws its icon at depth
/// -0.3 (topmost), ConqueredMapIconRenderer adds the red X (scale 2, translated (-1, -1, -0.005),
/// depth -0.095) and lets the icon be drawn as well. On a framed map (`inFrame`, Java's `active`) a
/// decoration whose type does not show on item frames is skipped before any renderer runs and takes
/// no depth index, as MapRenderer does.
void drawMapDecorations(BaseActorRenderContext& rc, std::int64_t mapId, glm::mat4 const& mapSpace, bool inFrame) {
    auto snapshot = maps::decorated(mapId); // immutable: no copy of the list or the styles
    if (!snapshot || snapshot->decorations.empty()) return;
    auto const&             list     = snapshot->decorations;
    auto const&             styles   = snapshot->styles;
    auto&                   screen   = rc.mScreenContext;
    mce::MaterialPtr const* material = modelMaterial(&rc); // the alpha-tested model material drawGeo uses
    if (!material) return;
    auto& world = screen.camera.worldMatrixStack.get();
    infoOnce("map_space", "map space " + matrixText(mapSpace));
    auto redX = maps::iconFor("minecraft:red_x");
    int  k    = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto const& d     = list[i];
        auto        style = i < styles.size() ? styles[i] : maps::DecorationStyle{};
        if (inFrame && !style.onFrame.value_or(maps::showOnItemFrame(d.type))) {
            infoOnce("map_frame_skip", "map decoration " + d.type + " not drawn on an item frame");
            continue;
        }
        float index = static_cast<float>(k++);
        glm::mat4   at    = glm::translate(glm::mat4{1.f}, glm::vec3{static_cast<float>(d.x) / 2.f + 64.f, static_cast<float>(d.z) / 2.f + 64.f, 0.f});
        at                = glm::rotate(at, glm::radians(static_cast<float>(d.rotation * 360) / 16.f), glm::vec3{0, 0, 1});
        if (style.conquered && redX) {
            // ConqueredMapIconRenderer: the red X, then the icon (the renderer returns false)
            if (auto* tex = textureOf(redX->texture)) {
                auto      ref = world.push(false);
                glm::mat4 m   = glm::translate(glm::scale(at, glm::vec3{2.f, 2.f, 2.f}), glm::vec3{-1.f, -1.f, -0.005f});
                ref.mat->_m.get() = mapSpace * m;
                drawMapQuad(screen, *material, *tex, *redX, -0.095f);
                infoOnce("map_conquered", "map red X drawn over a conquered icon (" + d.type + ")");
            }
        }
        auto icon = maps::iconFor(d.type);
        if (!icon) {
            warn("map_icon:" + d.type, "no icon for map decoration " + d.type);
            continue;
        }
        auto* tex = textureOf(icon->texture);
        if (!tex) continue;
        auto      ref = world.push(false);
        glm::mat4 m   = glm::translate(at, glm::vec3{0.f, 0.f, -0.02f});
        m             = glm::scale(m, glm::vec3{4.f, 4.f, 3.f});
        m             = glm::translate(m, glm::vec3{-0.125f, 0.125f, 0.f});
        ref.mat->_m.get() = mapSpace * m;
        // MagicMapPlayerIconRenderer: a fixed depth instead of the index's, so it is always on top
        drawMapQuad(screen, *material, *tex, *icon, style.topmost ? -0.3f : index * -0.001f);
        if (style.topmost) infoOnce("map_topmost", "map icon drawn on top (" + d.type + ")");
    }
    infoOnce("map", "map decorations drawn (" + std::to_string(list.size()) + " on map " + std::to_string(mapId) + ")");
}
} // namespace

} // namespace gf::client::render

namespace gf::client::tint {
void registerBlock(std::string const& blockId, TintFn fn) {
    if (!fn) return;
    render::update([&](render::Snapshot& s) { s.tints[blockId] = [fn = std::move(fn)](BlockPos const& pos, GameColors const&) { return fn(pos); }; });
}
void registerBlock(std::string const& blockId, GameTintFn fn) {
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

// Chunk meshes: the blocks that have a block renderer in each render chunk (a 16^3 section) the game
// builds (builder threads). Every section the game meshes is built here, whatever tessellator then
// draws its blocks (the legacy block tessellator for unit cubes and vanilla shapes, the block
// pipeline for custom geometry), so the section is scanned from its level chunk; a rebuild (a block
// placed or broken) replaces the section's positions. A section without such blocks costs one block
// lookup per position (cached per Block).
LL_TYPE_INSTANCE_HOOK(
    RenderChunkBuildHook,
    ll::memory::HookPriority::Normal,
    RenderChunkBuilder,
    &RenderChunkBuilder::build,
    void,
    RenderChunkGeometry&                                       renderChunkGeometry,
    bool                                                       transparentLeaves,
    BakedBlockLightType                                        lightingType,
    bool                                                       forExport,
    mce::framebuilder::FrameLightingModelCapabilities const& lightingModelCapabilities
) {
    origin(renderChunkGeometry, transparentLeaves, lightingType, forExport, lightingModelCapabilities);
    if (forExport || !r::reg().anyBlocks.load(std::memory_order_relaxed)) return;
    try {
        std::shared_ptr<ChunkViewSource> const& local  = mLocalSource;
        ChunkViewSource*                        source = local.get();
        if (!source) return;
        BlockPos const o = renderChunkGeometry.mPosition;
        r::trackSection(*source, o);
    } catch (...) {}
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
        auto base   = r::contextOf(context);
        base.camera = cameraTargetPos;
        Vec3 eyes   = entity.getEyePos(); // Java's getPackedLightCoords: the light at the eyes
        for (auto const& fn : it->second) {
            RenderContext    ctx = base;
            r::ShaderColours keep(context.mScreenContext);
            r::lightFor(ctx, eyes);
            fn(ctx, entity, ctx.partialTicks);
        }
    } catch (...) {}
}

// Name tags (hideNameTag): the name tag of one actor is extracted here for the frame.
LL_TYPE_INSTANCE_HOOK(
    RenderNameTagHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    &ActorRenderDispatcher::extractRenderTextObjects,
    std::vector<NameTagRenderObject>,
    Font&              font,
    Tessellator&       tessellator,
    Actor&             actor,
    std::string const& str,
    Vec3               camTargetPos,
    float              a,
    mce::Color         color
) {
    if (r::reg().anyNameTags.load(std::memory_order_relaxed)) {
        try {
            if (r::nameTagHidden(actor)) {
                r::infoOnce("name_tag:" + actor.getTypeName(), "name tag hidden (" + actor.getTypeName() + ")");
                return {};
            }
        } catch (...) {}
    }
    return origin(font, tessellator, actor, str, camTargetPos, a, color);
}

// Armour hiding (hideArmor; TF's ArmorHooks#cancelArmorRendering): the game asks whether a worn
// stack's attachable is drawn on its wearer.
LL_TYPE_STATIC_HOOK(
    RenderArmorHook,
    ll::memory::HookPriority::Normal,
    DataDrivenModel,
    &DataDrivenModel::shouldRenderAttachableOnActor,
    bool,
    ItemStack const&           itemInstance,
    AttachableSlotIndex const& attachableSlotIndex,
    bool                       isSpectator,
    bool                       isFirstPerson,
    bool                       isRenderingOnMap,
    bool                       lessThanOrEqualToVersion_1_16_210,
    bool                       hideArmor
) {
    bool draw = origin(itemInstance, attachableSlotIndex, isSpectator, isFirstPerson, isRenderingOnMap, lessThanOrEqualToVersion_1_16_210, hideArmor);
    // armour slots only (head, torso, legs, feet, body): held items are never hidden
    if (!draw || static_cast<int>(attachableSlotIndex) >= static_cast<int>(AttachableSlotIndex::CarriedItem)) return draw;
    auto& rg = r::reg();
    if (rg.armorAsked.fetch_add(1, std::memory_order_relaxed) == 0)
        r::infoOnce("armor_check", "worn armour check reached (slot " + std::to_string(static_cast<int>(attachableSlotIndex)) + ", " + itemInstance.getTypeName() + ")");
    if (!rg.anyArmor.load(std::memory_order_relaxed)) return draw;
    try {
        if (r::armorHidden(itemInstance)) {
            rg.armorHidden.fetch_add(1, std::memory_order_relaxed);
            static std::atomic<bool> told{false};
            if (!told.load(std::memory_order_relaxed) && !told.exchange(true))
                r::infoOnce("armor_hidden", "worn armour hidden (" + gf::game::items::idOf(itemInstance) + ")");
            return false;
        }
    } catch (...) {}
    return draw;
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
    r::beginMapCapture(renderContext, false); // a held map is drawn inside
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
        r::ShaderColours keep(renderContext.mScreenContext);
        r::lightFor(ctx, entity.getEyePos()); // ItemInHandRenderer: the holder's light
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
        Actor*           actor = entityRenderData.mActor;
        r::ShaderColours keep(renderContext.mScreenContext);
        if (actor) r::lightFor(ctx, actor->getPosition()); // ItemEntityRenderer: the item entity's light
        else { // the position is relative to the camera (not to its target, which differs in third person)
            Vec3 const cam = renderContext.mImpl->mCameraPosition;
            r::lightFor(ctx, Vec3{p.x + cam.x, p.y + cam.y, p.z + cam.z});
        }
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
        // the light of GUI items: full bright (Java's GUI item lighting, 15728880), set for every
        // renderer (feature/ext/ui/Client.h, "What a renderer finds at entry"); the actor context
        // (drawGeo's material and light) is built once, for the first item that has a renderer
        std::optional<BaseActorRenderContext> actors;
        bool                                  built = false;
        Vec3                                  at{};
        for (auto const& g : list) {
            auto e = r::itemEntry(*s, g.stack->getTypeName(), ItemContext::Gui);
            if (!e) continue;
            if (!built) {
                built = true;
                if (auto* client = gf::client::currentClient()) {
                    actors.emplace(mScreenContext, *client, client->getMinecraftGame());
                    if (auto* player = client->getLocalPlayer()) at = player->getPosition();
                }
            }
            RenderContext ctx(mScreenContext);
            ctx.ui      = this;
            ctx.guiX    = g.x;
            ctx.guiY    = g.y;
            ctx.guiSize = g.size;
            ctx.actors  = actors ? &*actors : nullptr;
            r::ShaderColours keep(mScreenContext);
            r::lightFor(ctx, at, true);
            r::runItem(*e, ctx, *g.stack, ItemContext::Gui);
        }
    } catch (...) {}
}

// Items the mod draws on the GUI (screen::Canvas::item in the screen framework's screens, HUD layers
// and toasts: ItemRenderer::renderGuiItemNew from a UI render event, outside the game's own UI pass).
// That function draws a block item with its own block path, which in a screen of the framework is
// not the inventory's isometric cube (a flatter top, spike S5 round 5). The inventory draws block
// items through renderGuiItemInChunk inside a UI mesh batch of the terrain atlas ("BlockType" chunk:
// the GUI block transform, as Java's "gui" display of block/block.json); the same call is made here,
// and the geometry a data-driven (TF) block leaves in the screen's tessellator for that batch is
// drawn right away with the UI item material and the terrain atlas, as the batch would. Flat block
// items (saplings, ...) and other items keep the game's own path.
LL_TYPE_INSTANCE_HOOK(
    RenderGuiNewHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemNew,
    void,
    BaseActorRenderContext& renderContext,
    ItemStack const&        item,
    int                     frame,
    float                   x,
    float                   y,
    bool                    renderEnchantmentFoil,
    float                   transparency,
    float                   lightMultiplier,
    float                   scale,
    int                     zOrder
) {
    // (an enchanted stack keeps the game's path: renderGuiItemInChunk draws no foil)
    if (r::modUiDepth() > 0 && !renderEnchantmentFoil && item.mBlock && !item.isNull()) {
        try {
            auto type = InventoryItemRenderer::getRenderTypeFromItem(item);
            if (static_cast<int>(type) == r::kBlockTypeChunk) {
                Tessellator& tess = renderContext.mScreenContext.tessellator;
                bool const   open = tess.mTessellating;
                this->renderGuiItemInChunk(renderContext, type, item, x, y, lightMultiplier, transparency, scale, frame, false, zOrder, std::nullopt);
                if (!open && tess.mTessellating) r::flushGuiBlock(renderContext.mScreenContext, tess);
                r::infoOnce("gui_block", "GUI block items drawn with the inventory's block transform (" + item.getTypeName() + ")");
                return;
            }
        } catch (...) {}
    }
    origin(renderContext, item, frame, x, y, renderEnchantmentFoil, transparency, lightMultiplier, scale, zOrder);
}

// A scene's render with LeviLamina's UI render events (outermost hook, so around LeviLamina's own
// hook that fires them): the mod's GUI draws in their listeners count as the mod's (modUiDepth 1) for
// this call only.
LL_TYPE_INSTANCE_HOOK(RenderUiSceneHook, ll::memory::HookPriority::Highest, ScreenView, &ScreenView::render, void, UIRenderContext& uiRenderContext) {
    int& depth = r::modUiDepth();
    int  keep  = depth;
    depth      = 1;
    try {
        origin(uiRenderContext);
    } catch (...) {
        depth = keep;
        throw;
    }
    depth = keep;
}

// The game's own UI pass of a scene (innermost hook, so inside LeviLamina's UI render event hooks):
// its item draws are the game's (RenderGuiNewHook leaves them alone).
LL_TYPE_INSTANCE_HOOK(RenderUiPassHook, ll::memory::HookPriority::Lowest, ScreenView, &ScreenView::render, void, UIRenderContext& uiRenderContext) {
    int& depth = r::modUiDepth();
    int  keep  = depth;
    depth      = 0;
    try {
        origin(uiRenderContext);
    } catch (...) {
        depth = keep;
        throw;
    }
    depth = keep;
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
    r::beginMapCapture(entityRenderContext, true); // a framed map: Java's MapRenderer with active
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
    r::beginMapCapture(entityRenderContext, true); // a framed map: Java's MapRenderer with active
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
        r::ShaderColours keep(renderContext.mScreenContext);
        if (actor) r::lightFor(ctx, actor->getEyePos()); // CustomHeadLayer: the wearer's light
        else r::lightFor(ctx, ctx.camera);
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
    r::beginMapCapture(renderContext, false);
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
            // the game's own colours at this position: the resolver's original function for the tint
            // method asked, for a vanilla block of that method (the biomes' colours blended as for
            // the game's own blocks, whatever the TF block's own material says; computed only when
            // the tint function asks). No probe block yet (no world joined, or the block registry was
            // not there when it was looked up): no game colour (nullopt), so the tint function uses
            // its no-world default rather than what the TF block's own material would give.
            auto ask = [&](TintMethod method) -> std::optional<mce::Color> {
                Block const* probe = r::probeBlock(method);
                if (!probe) {
                    static std::atomic<bool> told{false};
                    if (!told.load(std::memory_order_relaxed) && !told.exchange(true))
                        gf::log().info("render: a tint function asked the game's colours before the probe blocks were resolved: no game colour");
                    return std::nullopt;
                }
                return origin(*probe, blockPos, method, biomeTintCache);
            };
            r::ResolverColors<decltype(ask)> colors(ask);
            BlockPos const                   at{min.x + blockPos.x, min.y + blockPos.y, min.z + blockPos.z};
            unsigned                         rgb = (*fn)(at, colors);
            static std::atomic<bool> logged{false};
            if (colors.asked() && !logged.load(std::memory_order_relaxed) && !logged.exchange(true))
                gf::log().info("render: a tint function read the game's colours ({} at {} {} {} gave #{:06X})", gf::game::blockId(block), at.x, at.y, at.z, rgb);
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
            // The chunk builders cache registry entries by Block pointer: a world loaded later in the
            // same process registers its custom blocks anew, and a freed Block's address may come back
            // as another block, so every level change drops those caches (a new registry version).
            mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
                auto& rg = reg();
                {
                    std::lock_guard lock(rg.trackedMutex);
                    rg.tracked.clear();
                }
                resolveProbes(false);
                dropGuiAtlas();
                rg.version.fetch_add(1, std::memory_order_acq_rel);
            });
            mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](auto&) {
                reg().version.fetch_add(1, std::memory_order_acq_rel);
                resolveProbes(true);
                dropGuiAtlas(); // the world's resource packs may bring another terrain atlas
            });
        }
    }
    void install() {
        load();
        resolveProbes(true); // a world already joined (the server-side enable of a local world)
        render_hooks::RenderWorldHook::hook();
        render_hooks::RenderChunkBuildHook::hook();
        render_hooks::RenderEntityHook::hook();
        render_hooks::RenderItemInHandHook::hook();
        render_hooks::RenderItemEntityHook::hook();
        render_hooks::RenderGuiItemHook::hook();
        render_hooks::RenderGuiBatchHook::hook();
        render_hooks::RenderGuiNewHook::hook();
        render_hooks::RenderUiPassHook::hook();
        render_hooks::RenderUiSceneHook::hook();
        render_hooks::RenderFrameHook::hook();
        render_hooks::RenderFrameAtHook::hook();
        render_hooks::RenderHeadHook::hook();
        render_hooks::RenderMapInstanceHook::hook();
        render_hooks::RenderFirstPersonHook::hook();
        render_hooks::RenderTintHook::hook();
        render_hooks::RenderNameTagHook::hook();
        render_hooks::RenderArmorHook::hook();
    }
    void uninstall() {
        render_hooks::RenderNameTagHook::unhook();
        render_hooks::RenderArmorHook::unhook();
        render_hooks::RenderTintHook::unhook();
        render_hooks::RenderFirstPersonHook::unhook();
        render_hooks::RenderMapInstanceHook::unhook();
        render_hooks::RenderHeadHook::unhook();
        render_hooks::RenderFrameAtHook::unhook();
        render_hooks::RenderFrameHook::unhook();
        render_hooks::RenderUiSceneHook::unhook();
        render_hooks::RenderUiPassHook::unhook();
        render_hooks::RenderGuiNewHook::unhook();
        render_hooks::RenderGuiBatchHook::unhook();
        render_hooks::RenderGuiItemHook::unhook();
        render_hooks::RenderItemEntityHook::unhook();
        render_hooks::RenderItemInHandHook::unhook();
        render_hooks::RenderEntityHook::unhook();
        render_hooks::RenderChunkBuildHook::unhook();
        render_hooks::RenderWorldHook::unhook();
        resolveProbes(false);
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        if (mJoin) ll::event::EventBus::getInstance().removeListener(mJoin);
        mExit = nullptr;
        mJoin = nullptr;
    }

private:
    ll::event::ListenerPtr mExit;
    ll::event::ListenerPtr mJoin;
};
} // namespace
} // namespace gf::client::render

GF_REGISTER_MODULE_ORDERED(gf::client::render::RenderModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT
