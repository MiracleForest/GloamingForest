// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/FoliageColorHandler.java
//   src/main/java/twilightforest/TFEnumExtensions.java
//   src/main/java/twilightforest/asmhooks/BlockHooks.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U1 `biomes`, client build: TF's position-dependent biome colours on grass, plants and leaves.
//
// Java: a TF biome's grass colour goes through its GrassColorModifier (TFGrassColorModifier-
// EnumExtension: spooky forest, dark forest centre, enchanted forest, swamp, dark forest) and every
// foliage colour lookup through FoliageColorHandler (tf-asm's FoliageColorResolverTransformer), at
// each column of the client's biome blend (ClientLevel#calculateBlockTint, 5x5 by default).
//
// Bedrock: the client biome files give every TF biome its static colours (the swamp and dark forest
// colours are flat in TF too: TF's biome JSON stores the modifiers' results). The tints registered
// here (F12's tint registry, applied to these vanilla blocks by feature/ext/biomes/VanillaTint.cpp)
// read the biome of every column of the blend from the client's chunks (each block's biome is
// synced, so remote worlds need no seed): a column of a position-dependent biome (spooky forest,
// dark forest centre, enchanted forest) gets TF's colour there, any other column the game's own
// colour at the block (its blend of the static colours), and the columns are averaged as Java
// averages them.
// A tint asked without a block source (F12's resolver path for TF's own blocks, which call
// gf::biomes::ext::grassColor / foliageColor) recognises those biomes by the game's colour, which
// is exactly a biome's static colour when the whole blend lies inside it.

#ifdef GF_CLIENT

#include "client/biomes/Tints.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"

#include "client/render/Render.h"
#include "core/biomes/Colors.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::biomes {

namespace colors = core::biomes::colors;
using feature::biomes::BiomesConfig;

namespace {

// The static colours of the client biome files (assets/pack/rp/biomes/biomes/) of the three
// position-dependent biomes: the markers of a blend that lies wholly inside one of them, for tints
// asked without a block source.
constexpr unsigned kSpookyGrass             = 0xC45123;
constexpr unsigned kSpookyFoliage           = 0xFF8501;
constexpr unsigned kDarkForestCenterGrass   = 0x554114;
constexpr unsigned kDarkForestCenterFoliage = 0xE94E14;
constexpr unsigned kEnchantedStatic         = 0x00FF80; ///< grass and foliage (TF's JSON: 0x00FFFF)
constexpr int      kEnchantedJson           = 0x00FFFF; ///< the colour TF's modifier starts from

// GrassColor.getDefaultColor() / FoliageColor.getDefaultColor(): colours without a world
constexpr unsigned kDefaultGrass   = 0x48B518;
constexpr unsigned kDefaultFoliage = 0x48B518;

int radius() {
    int r = config::get<BiomesConfig>().tintBlendRadius;
    return r < 0 ? 0 : r > 7 ? 7 : r;
}

bool enabled() { return config::get<BiomesConfig>().clientTints; }

/// Whether two colours agree within one step per channel (the game's colours pass through floats).
bool near(unsigned a, unsigned b) {
    for (int shift = 0; shift < 24; shift += 8) {
        int d = static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF);
        if (d < -1 || d > 1) return false;
    }
    return true;
}

/// One line per biome and kind, for the e2e scenario (the tints run on chunk builder threads).
void noteDrawn(std::string_view biome, bool foliage) {
    static std::atomic<unsigned> logged{0};
    unsigned bit = (biome == "twilightforest:spooky_forest" ? 1u : biome == "twilightforest:dark_forest_center" ? 2u : 4u) << (foliage ? 3 : 0);
    if (!(logged.fetch_or(bit) & bit)) log().info("biomes: client draws TF's {} colours of {}", foliage ? "foliage" : "grass", biome);
}

/// The biome id of a Biome (cached per biome object: the registry's biomes live as long as the level).
std::string const& idOf(Biome const& b) {
    thread_local std::unordered_map<Biome const*, std::string> ids;
    auto [it, fresh] = ids.try_emplace(&b);
    if (fresh) {
        auto const& name = b.mHash->getString();
        it->second       = name.find(':') == std::string::npos ? "minecraft:" + name : name;
    }
    return it->second;
}

int tfColour(std::string const& biome, int x, int z, bool foliage, unsigned game) {
    auto v = foliage ? colors::foliage(biome, kEnchantedJson, x, z) : colors::grass(biome, kEnchantedJson, x, z);
    return v.value_or(static_cast<int>(game));
}

/// TF's colour at `pos` with the biomes read from `region` (nullopt: no position-dependent biome in
/// the blend, the game's colour stays).
std::optional<unsigned> fromRegion(BlockSource& region, BlockPos const& pos, unsigned game, bool foliage) {
    bool any = false;
    int  c   = colors::blend(pos.x, pos.z, radius(), [&](int x, int z) {
        std::string const& biome = idOf(region.getBiome(BlockPos{x, pos.y, z}));
        if (!colors::isPositional(biome)) return static_cast<int>(game);
        any = true;
        noteDrawn(biome, foliage);
        return tfColour(biome, x, z, foliage, game);
    });
    if (!any) return std::nullopt;
    return static_cast<unsigned>(c);
}

/// TF's colour at `pos` when the game's blended colour `game` marks a position-dependent biome
/// (no block source).
std::optional<unsigned> fromMarker(BlockPos const& pos, unsigned game, bool foliage) {
    char const* biome = nullptr;
    if (near(game, foliage ? kSpookyFoliage : kSpookyGrass)) biome = "twilightforest:spooky_forest";
    else if (near(game, foliage ? kDarkForestCenterFoliage : kDarkForestCenterGrass)) biome = "twilightforest:dark_forest_center";
    else if (near(game, kEnchantedStatic)) biome = "twilightforest:enchanted_forest";
    if (!biome) return std::nullopt;
    noteDrawn(biome, foliage);
    std::string const id = biome;
    return static_cast<unsigned>(colors::blend(pos.x, pos.z, radius(), [&](int x, int z) { return tfColour(id, x, z, foliage, game); }));
}

/// The first distinct game colours a tint sees (at most 24 lines per session), for the e2e log.
void noteGameColour(bool foliage, unsigned c) {
    static std::mutex            m;
    static std::vector<unsigned> seen;
    std::lock_guard              lock(m);
    unsigned                     key = c | (foliage ? 0x1000000u : 0u);
    if (seen.size() >= 24 || std::find(seen.begin(), seen.end(), key) != seen.end()) return;
    seen.push_back(key);
    log().info("biomes: game {} colour #{:06X} seen by the tint", foliage ? "foliage" : "grass", c);
}

unsigned colourAt(BlockSource* region, BlockPos const& pos, std::optional<unsigned> game, bool foliage) {
    if (!game) return foliage ? kDefaultFoliage : kDefaultGrass;
    noteGameColour(foliage, *game);
    if (!enabled()) return *game;
    auto tf = region ? fromRegion(*region, pos, *game, foliage) : fromMarker(pos, *game, foliage);
    return tf.value_or(*game);
}

// Vanilla blocks drawn with the biome grass / foliage colour (Java: BlockColors with
// BiomeColors.getAverageGrassColor / getAverageFoliageColor), Bedrock ids. A fern in a flower pot
// is drawn as the fern block (Bedrock has no potted_fern block), so "minecraft:fern" covers it.
constexpr char const* kGrassBlocks[] = {
    "minecraft:grass_block",
    "minecraft:short_grass",
    "minecraft:tall_grass",
    "minecraft:fern",
    "minecraft:large_fern",
    "minecraft:sugar_cane",
};
constexpr char const* kFoliageBlocks[] = {
    "minecraft:oak_leaves",
    "minecraft:jungle_leaves",
    "minecraft:acacia_leaves",
    "minecraft:dark_oak_leaves",
    "minecraft:mangrove_leaves",
    "minecraft:vine",
};

} // namespace

unsigned grassAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game) { return colourAt(region, pos, game.grass(), false); }

unsigned foliageAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game) { return colourAt(region, pos, game.foliage(), true); }

std::span<char const* const> grassBlocks() { return kGrassBlocks; }
std::span<char const* const> foliageBlocks() { return kFoliageBlocks; }

struct BiomeTintsModule {
    void install() {
        for (char const* id : kGrassBlocks)
            tint::registerBlock(id, tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return grassAt(gf::biomes::ext::tintRegion(), p, g); }});
        for (char const* id : kFoliageBlocks)
            tint::registerBlock(id, tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return foliageAt(gf::biomes::ext::tintRegion(), p, g); }});
        log().info("biomes: TF biome colours on {} grass and {} foliage blocks", std::size(kGrassBlocks), std::size(kFoliageBlocks));
    }
    void uninstall() {
        for (char const* id : kGrassBlocks) tint::unregisterBlock(id);
        for (char const* id : kFoliageBlocks) tint::unregisterBlock(id);
    }
};

} // namespace gf::client::biomes

namespace gf::biomes::ext {
unsigned grassColor(BlockPos const& pos, gf::client::tint::GameColors const& game) { return gf::client::biomes::grassAt(tintRegion(), pos, game); }
unsigned foliageColor(BlockPos const& pos, gf::client::tint::GameColors const& game) { return gf::client::biomes::foliageAt(tintRegion(), pos, game); }
} // namespace gf::biomes::ext

GF_REGISTER_MODULE(gf::client::biomes::BiomeTintsModule);

#endif
