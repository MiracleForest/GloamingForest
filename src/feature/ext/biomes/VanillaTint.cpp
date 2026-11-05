// U1 `biomes`: contract extension of "client.render" (rule R18, new file; to be folded into F12's
// client/render by A8 in W2b). Client build only.
//
// F12's tint registry (gf::client::tint::registerBlock) is applied by its hook on
// ClientBlockPipeline::PositionDependentTintResolver::_get, which the game asks only for blocks of
// the client block pipeline (data-driven blocks). Vanilla blocks drawn by the legacy tessellator
// (grass, short grass, ferns, sugar cane, oak / jungle / acacia / dark oak / mangrove leaves, vines)
// take their biome colour from BiomeColorSampling's tessellation policies instead
// (getTessellationPolicy(tint method).get(block, region, pos, biomeTintCache)), so a tint
// registered for a vanilla block was never asked. This file hooks the grass and foliage policies:
// - in TF's biomes a block with a registered tint gets it (the policy's own colour is the game's
//   colour of its tint method, the other methods are asked from their policies when the tint
//   function wants them);
// - every other block the game draws with the biome grass or (default) foliage colour gets TF's
//   colour there too (Java's BiomeColors.getAverageGrassColor / getAverageFoliageColor at the
//   block: TF's own blocks drawn with the biome colour, such as twilight oak leaves and the
//   fiddlehead, and the probe blocks F12's resolver asks for the game's colour of a TF block).
// Outside TF's biomes (every other dimension) the game's colour stays, after one biome lookup.
// While a tint function runs, gf::biomes::ext::tintRegion() is the block source being tessellated
// (U1 reads the biome at a position from it). Nested policy calls (a tint asking the game's other
// colours) get the game's own colour.
//
// Two more hooks finish TF's biome colours with the client mod:
// - LeavesBlock::isSeasonTinted is false in TF's biomes. In a cold biome (snowy forest, glacier)
//   Bedrock draws leaves through its seasons map (the tint is not a colour but a place in that map
//   plus a snow amount); Java has no seasons, and TF's snowy forest draws its leaves white
//   (#FFFFFF), the glacier the colour map's foliage colour.
// - BiomeColorSampling::getWaterColor answers TF's water colour (BiomeSpecialEffects#waterColor:
//   the swamp's green, the fire swamp's red, ...) for TF's biomes.

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
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/LeavesBlock.h"
#include "mc/world/level/block/TintMethod.h"

#include "client/biomes/Tints.h"
#include "core/biomes/Colors.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::biomes::ext {

namespace {

namespace colors = core::biomes::colors;
using client::biomes::biomeIdOf;
using client::biomes::isTfBiome;

thread_local BlockSource* tlRegion = nullptr;
thread_local int          tlDepth  = 0;

std::atomic<std::uint64_t> gApplied{0};
std::atomic<std::uint64_t> gWaterApplied{0};
std::atomic<std::uint64_t> gSeasonsOff{0};

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

/// The block's id, cached per block state on this thread (builder threads). An entry is checked
/// against the block's serialization hash on every use: another world's block registry (a remote
/// join, another local world) may reuse the address.
std::string const& idOf(Block const& block) {
    struct Entry {
        std::uint64_t hash = 0;
        std::string   id;
    };
    thread_local std::unordered_map<Block const*, Entry> ids;
    if (ids.size() > 65536) ids.clear(); // block states of many worlds
    std::uint64_t hash = block.mSerializationIdHash;
    auto [it, fresh]   = ids.try_emplace(&block);
    if (fresh || it->second.hash != hash) it->second = Entry{hash, game::blockId(block)};
    return it->second.id;
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

/// TF's colour of `block` at `pos` (nullopt: not in a TF biome, or a nested call): its registered
/// tint, else TF's biome colour of the policy's tint method.
std::optional<mce::Color>
dispatch(Block const& block, BlockSource& region, BlockPos const& pos, BiomeTintCache const* cache, TintMethod method, mce::Color const& game) {
    if (tlDepth > 0) return std::nullopt;
    // only TF's biomes have TF colours (one lookup for every block of the other dimensions)
    if (!isTfBiome(biomeIdOf(region.getBiome(pos)))) return std::nullopt;
    std::string const& id = idOf(block);
    PolicyColors       colors(block, region, pos, cache, method, game);
    BlockSource*       previous = tlRegion;
    tlRegion                    = &region;
    ++tlDepth;
    std::optional<unsigned> rgb;
    bool                    registered = false;
    try {
        rgb        = gf::client::render::tintOf(id, pos.x, pos.y, pos.z, colors);
        registered = rgb.has_value();
        if (!rgb) rgb = method == TintMethod::Grass ? client::biomes::grassAt(&region, pos, colors) : client::biomes::foliageAt(&region, pos, colors);
    } catch (...) {}
    --tlDepth;
    tlRegion = previous;
    if (!rgb) return std::nullopt;
    if (gApplied.fetch_add(1, std::memory_order_relaxed) == 0)
        gf::log().info("biomes: vanilla tint hook applied the tint of {} at {} {} {} (#{:06X} over the game's #{:06X})", id, pos.x, pos.y, pos.z, *rgb, rgbOf(game));
    // the first tint that changed the game's colour, per tint method, and the first TF block given
    // TF's biome colour (the e2e scenario waits for them)
    static std::atomic<unsigned> changed{0};
    unsigned                     bit = method == TintMethod::Grass ? 1u : 2u;
    if (*rgb != rgbOf(game) && !(changed.fetch_or(bit) & bit))
        gf::log().info("biomes: vanilla tint hook changed the {} colour of {} at {} {} {} to #{:06X} (the game's #{:06X})", method == TintMethod::Grass ? "grass" : "foliage", id, pos.x, pos.y, pos.z, *rgb, rgbOf(game));
    if (!registered && id.starts_with("twilightforest:") && !(changed.fetch_or(bit << 2) & (bit << 2)))
        gf::log().info("biomes: TF's biome {} colour drawn on {} at {} {} {} (#{:06X})", method == TintMethod::Grass ? "grass" : "foliage", id, pos.x, pos.y, pos.z, *rgb);
    return colorOf(*rgb, game.a);
}

} // namespace

BlockSource* tintRegion() { return tlRegion; }

std::uint64_t vanillaTintsApplied() { return gApplied.load(std::memory_order_relaxed); }

} // namespace gf::biomes::ext

namespace gf::biomes::ext::hooks {

namespace {
namespace colors = core::biomes::colors;
}

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
    // only the biome foliage (oak and its kin) takes TF's colours
    if (static_cast<TintMethod>(mTintMethod) != TintMethod::DefaultFoliage) return game;
    if (auto c = dispatch(block, region, pos, cache, TintMethod::DefaultFoliage, game)) return *c;
    return game;
}

// Java has no seasons: leaves in TF's cold biomes are drawn with their foliage colour.
LL_TYPE_INSTANCE_HOOK(
    TwilightLeavesSeasonsHook,
    ll::memory::HookPriority::Normal,
    LeavesBlock,
    &LeavesBlock::$isSeasonTinted,
    bool,
    Block const&    block,
    BlockSource&    region,
    BlockPos const& p
) {
    bool game = origin(block, region, p);
    if (!game) return false;
    try {
        if (client::biomes::isTfBiome(client::biomes::biomeIdOf(region.getBiome(p)))) {
            if (gSeasonsOff.fetch_add(1, std::memory_order_relaxed) == 0)
                gf::log().info("biomes: leaves at {} {} {} drawn without Bedrock's seasons map (TF's biome {})", p.x, p.y, p.z, client::biomes::biomeIdOf(region.getBiome(p)));
            return false;
        }
    } catch (...) {}
    return true;
}

// BiomeSpecialEffects#waterColor of TF's biomes (the sampler of the water tessellation policy and
// of the game's other water colour lookups).
LL_STATIC_HOOK(
    TwilightWaterColorHook,
    ll::memory::HookPriority::Normal,
    &BiomeColorSampling::getWaterColor,
    int,
    Biome const&    biome,
    BlockPos const& pos
) {
    int game = origin(biome, pos);
    try {
        if (auto w = colors::water(client::biomes::biomeIdOf(biome))) {
            int tf = static_cast<int>((static_cast<unsigned>(game) & 0xFF000000u) | static_cast<unsigned>(*w));
            if (gWaterApplied.fetch_add(1, std::memory_order_relaxed) == 0)
                gf::log().info("biomes: water colour hook applied TF's water of {} at {} {} {} (#{:06X}, the game's #{:08X})", client::biomes::biomeIdOf(biome), pos.x, pos.y, pos.z, *w, static_cast<unsigned>(game));
            return tf;
        }
    } catch (...) {}
    return game;
}

struct VanillaTintModule {
    void install() {
        int a = VanillaGrassTintHook::hook();
        int b = VanillaFoliageTintHook::hook();
        int c = TwilightLeavesSeasonsHook::hook();
        int d = TwilightWaterColorHook::hook();
        gf::log().info(
            "biomes: vanilla tint hooks installed (grass {}, foliage {}, seasons {}, water {})",
            a == 0 ? "ok" : "failed",
            b == 0 ? "ok" : "failed",
            c == 0 ? "ok" : "failed",
            d == 0 ? "ok" : "failed"
        );
    }
    void uninstall() {
        VanillaGrassTintHook::unhook();
        VanillaFoliageTintHook::unhook();
        TwilightLeavesSeasonsHook::unhook();
        TwilightWaterColorHook::unhook();
    }
};

} // namespace gf::biomes::ext::hooks

namespace gf::biomes::ext {
std::uint64_t waterColoursApplied() { return gWaterApplied.load(std::memory_order_relaxed); }
std::uint64_t seasonsOffApplied() { return gSeasonsOff.load(std::memory_order_relaxed); }
} // namespace gf::biomes::ext

GF_REGISTER_MODULE(gf::biomes::ext::hooks::VanillaTintModule);

#endif
