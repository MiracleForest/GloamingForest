// U1 `biomes`: contract extension of "client.render" (rule R18, new file; to be folded into F12's
// client/render by A8 in W2b). Client build only.
//
// F12's tint registry (gf::client::tint::registerBlock) is applied by its hook on
// ClientBlockPipeline::PositionDependentTintResolver::_get, which the game asks only for blocks of
// the client block pipeline (data-driven blocks). Vanilla blocks drawn by the legacy tessellator
// (grass, short grass, ferns, sugar cane, oak / jungle / acacia / dark oak / mangrove leaves, vines)
// take their biome colour from BiomeColorSampling's tessellation policies instead
// (getTessellationPolicy(tint method).get(block, region, pos, biomeTintCache)), so a tint
// registered for a vanilla block was never asked. This file hooks the grass and foliage policies
// and applies the registry there too: the policy's own colour is the game's colour of its tint
// method, the other methods are asked from their policies when the tint function wants them.
// While a tint function runs, gf::biomes::ext::tintRegion() is the block source being tessellated
// (U1 reads the biome at a position from it). Nested policy calls (a tint asking the game's other
// colours, F12's resolver asking a probe block) get the game's own colour.

#ifdef GF_CLIENT

#include <atomic>
#include <optional>
#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/world/level/biome/BiomeTintCache.h"
#include "mc/client/world/level/biome/biome_color_sampling/FoliageTessellationPolicy.h"
#include "mc/client/world/level/biome/biome_color_sampling/GrassTessellationPolicy.h"
#include "mc/client/world/level/biome/biome_color_sampling/TessellationPolicy.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/TintMethod.h"

#include "feature/ext/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::biomes::ext {

namespace {

thread_local BlockSource* tlRegion = nullptr;
thread_local int          tlDepth  = 0;

std::atomic<std::uint64_t> gApplied{0};

unsigned rgbOf(mce::Color const& c) {
    auto ch = [](float v) {
        int i = static_cast<int>(v * 255.f + 0.5f);
        return static_cast<unsigned>(i < 0 ? 0 : i > 255 ? 255 : i);
    };
    return ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
}

mce::Color colorOf(unsigned rgb, float alpha) {
    return mce::Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f, static_cast<float>(rgb & 0xFF) / 255.f, alpha};
}

std::atomic<std::uint64_t> gWorld{0}; ///< bumped when a world loads or closes

/// The block's id, cached per block state on this thread (builder threads) while the world lasts
/// (another world's block registry may reuse the addresses).
std::string const& idOf(Block const& block) {
    thread_local std::unordered_map<Block const*, std::string> ids;
    thread_local std::uint64_t                                 world = ~std::uint64_t(0);
    if (auto now = gWorld.load(std::memory_order_acquire); now != world) {
        ids.clear();
        world = now;
    }
    auto [it, fresh] = ids.try_emplace(&block);
    if (fresh) it->second = game::blockId(block);
    return it->second;
}

/// The game's colours at the position being tinted: the hooked policy's own colour, the other tint
/// methods from their policies (asked once each, nested: the hooks pass them through unchanged).
class PolicyColors final : public gf::client::tint::GameColors {
public:
    PolicyColors(Block const& block, BlockSource& region, BlockPos const& pos, BiomeTintCache const* cache, TintMethod own, mce::Color const& ownColor)
    : mBlock(block),
      mRegion(region),
      mPos(pos),
      mCache(cache),
      mOwn(own),
      mOwnColor(rgbOf(ownColor)) {}

    std::optional<unsigned> foliage() const override { return get(0, TintMethod::DefaultFoliage); }
    std::optional<unsigned> grass() const override { return get(1, TintMethod::Grass); }
    std::optional<unsigned> water() const override { return get(2, TintMethod::Water); }

private:
    std::optional<unsigned> get(int i, TintMethod method) const {
        if (method == mOwn) return mOwnColor;
        if (!mAsked[i]) {
            mAsked[i] = true;
            ++tlDepth;
            try {
                mColors[i] = rgbOf(BiomeColorSampling::getTessellationPolicy(method).get(mBlock, mRegion, mPos, mCache));
            } catch (...) {}
            --tlDepth;
        }
        return mColors[i];
    }

    Block const&                    mBlock;
    BlockSource&                    mRegion;
    BlockPos const&                 mPos;
    BiomeTintCache const*           mCache;
    TintMethod                      mOwn;
    unsigned                        mOwnColor;
    mutable bool                    mAsked[3]{};
    mutable std::optional<unsigned> mColors[3];
};

/// The registry's tint of `block` at `pos` (nullopt: none registered, or a nested call).
std::optional<mce::Color>
dispatch(Block const& block, BlockSource& region, BlockPos const& pos, BiomeTintCache const* cache, TintMethod method, mce::Color const& game) {
    if (tlDepth > 0) return std::nullopt;
    std::string const& id = idOf(block);
    PolicyColors       colors(block, region, pos, cache, method, game);
    BlockSource*       previous = tlRegion;
    tlRegion                    = &region;
    ++tlDepth;
    std::optional<unsigned> rgb;
    try {
        rgb = gf::client::render::tintOf(id, pos.x, pos.y, pos.z, colors);
    } catch (...) {}
    --tlDepth;
    tlRegion = previous;
    if (!rgb) return std::nullopt;
    if (gApplied.fetch_add(1, std::memory_order_relaxed) == 0)
        gf::log().info("biomes: vanilla tint hook applied the tint of {} at {} {} {} (#{:06X} over the game's #{:06X})", id, pos.x, pos.y, pos.z, *rgb, rgbOf(game));
    return colorOf(*rgb, game.a);
}

} // namespace

BlockSource* tintRegion() { return tlRegion; }

std::uint64_t vanillaTintsApplied() { return gApplied.load(std::memory_order_relaxed); }

} // namespace gf::biomes::ext

namespace gf::biomes::ext::hooks {

LL_TYPE_INSTANCE_HOOK(
    VanillaGrassTintHook,
    ll::memory::HookPriority::Normal,
    BiomeColorSampling::GrassTessellationPolicy,
    static_cast<mce::Color (BiomeColorSampling::GrassTessellationPolicy::*)(Block const&, BlockSource&, BlockPos const&, BiomeTintCache const*) const>(
        &BiomeColorSampling::GrassTessellationPolicy::$get
    ),
    mce::Color,
    Block const&          block,
    BlockSource&          region,
    BlockPos const&       pos,
    BiomeTintCache const* cache
) {
    mce::Color game = origin(block, region, pos, cache);
    if (auto c = dispatch(block, region, pos, cache, TintMethod::Grass, game)) return *c;
    return game;
}

LL_TYPE_INSTANCE_HOOK(
    VanillaFoliageTintHook,
    ll::memory::HookPriority::Normal,
    BiomeColorSampling::FoliageTessellationPolicy,
    static_cast<mce::Color (BiomeColorSampling::FoliageTessellationPolicy::*)(Block const&, BlockSource&, BlockPos const&, BiomeTintCache const*) const>(
        &BiomeColorSampling::FoliageTessellationPolicy::$get
    ),
    mce::Color,
    Block const&          block,
    BlockSource&          region,
    BlockPos const&       pos,
    BiomeTintCache const* cache
) {
    mce::Color game = origin(block, region, pos, cache);
    // the foliage policy serves every foliage method (birch and evergreen are fixed colours in Java):
    // only the biome foliage (oak and its kin) goes through the registry
    if (static_cast<TintMethod>(mTintMethod) != TintMethod::DefaultFoliage) return game;
    if (auto c = dispatch(block, region, pos, cache, TintMethod::DefaultFoliage, game)) return *c;
    return game;
}

struct VanillaTintModule {
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldReady([] { gWorld.fetch_add(1, std::memory_order_acq_rel); });
            game::db::onWorldClosed([] { gWorld.fetch_add(1, std::memory_order_acq_rel); });
        }
        int a = VanillaGrassTintHook::hook();
        int b = VanillaFoliageTintHook::hook();
        gf::log().info("biomes: vanilla tint hooks installed (grass {}, foliage {})", a == 0 ? "ok" : "failed", b == 0 ? "ok" : "failed");
    }
    void uninstall() {
        VanillaGrassTintHook::unhook();
        VanillaFoliageTintHook::unhook();
    }
};

} // namespace gf::biomes::ext::hooks

GF_REGISTER_MODULE(gf::biomes::ext::hooks::VanillaTintModule);

#endif
