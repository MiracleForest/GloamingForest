// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/FoliageColorHandler.java
//   src/main/java/gloamingforest/TFEnumExtensions.java
//   src/main/java/gloamingforest/asmhooks/BlockHooks.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U1 `biomes`, client build: upstream's position-dependent biome colours on grass, plants and leaves.
//
// Java: an upstream biome's grass colour goes through its GrassColorModifier (TFGrassColorModifier-
// EnumExtension: spooky forest, dark forest centre, enchanted forest, swamp, dark forest) and every
// foliage colour lookup through FoliageColorHandler (tf-asm's FoliageColorResolverTransformer), at
// each column of the client's biome blend (ClientLevel#calculateBlockTint, 5x5 by default).
//
// Bedrock: the client biome files give every upstream biome its static colours (vanilla clients). With
// the mod, the tints here (F12's tint registry for vanilla blocks, and every other block the game
// draws with the biome grass or foliage colour, both through feature/ext/biomes/VanillaTint.cpp's
// hooks on the game's tessellation policies) read the biome of every column of the blend from the
// client's chunks (each block's biome is synced, so remote worlds need no seed), give each column
// its own biome's upstream colour (core/biomes/Colors: the modifier or foliage handler, else upstream's JSON
// colour or the colour map at the biome's climate) and average the columns as Java averages them.
// Outside upstream's biomes (other dimensions) the game's colour stays.
// A tint asked without a block source (F12's resolver path for upstream's own blocks, which call
// gf::biomes::ext::grassColor / foliageColor) recognises those biomes by the game's colour, which
// is exactly a biome's static colour when the whole blend lies inside it.

#ifdef GF_CLIENT

#include "client/biomes/Tints.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

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

std::string const& biomeIdOf(Biome const& b) { return feature::biomes::biomeIdOf(b); }

bool isTfBiome(std::string_view id) { return feature::biomes::isTfBiome(id); }

namespace {

// The static colours of the client biome files (assets/pack/rp/biomes/) of the three
// position-dependent biomes: the markers of a blend that lies wholly inside one of them, for tints
// asked without a block source.
constexpr unsigned kSpookyGrass             = 0xC45123;
constexpr unsigned kSpookyFoliage           = 0xFF8501;
constexpr unsigned kDarkForestCenterGrass   = 0x554114;
constexpr unsigned kDarkForestCenterFoliage = 0xE94E14;
constexpr unsigned kEnchantedStatic         = 0x00FF80; ///< grass and foliage (upstream's JSON: 0x00FFFF)

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
    unsigned bit = (biome == "gloamingforest:spooky_forest" ? 1u : biome == "gloamingforest:dark_forest_center" ? 2u : 4u) << (foliage ? 3 : 0);
    if (!(logged.fetch_or(bit) & bit)) log().info("biomes: client draws upstream's {} colours of {}", foliage ? "foliage" : "grass", biome);
}

/// upstream's colour of `biome` at (x, z) before the blend: every upstream biome has one (its modifier, the
/// foliage handler, its JSON colour or the colour map at its climate); nullopt: not an upstream biome.
std::optional<int> tfColourOf(std::string const& biome, int x, int z, bool foliage) {
    return foliage ? colors::foliageAt(biome, x, z) : colors::grassAt(biome, x, z);
}

int tfColour(std::string const& biome, int x, int z, bool foliage, unsigned game) {
    return tfColourOf(biome, x, z, foliage).value_or(static_cast<int>(game));
}

/// upstream's colour at `pos` with the biomes read from `region`: Java's blend of every column's own
/// biome colour (nullopt: the block is not in an upstream biome, so neither is its blend: the game's
/// colour stays). Only the Gloaming Forest has upstream biomes, so the centre column decides; a column of
/// another biome inside a blend (never in a generated world) counts with the game's colour.
std::optional<unsigned> fromRegion(BlockSource& region, BlockPos const& pos, unsigned game, bool foliage) {
    if (!isTfBiome(biomeIdOf(region.getBiome(pos)))) return std::nullopt;
    int c = colors::blend(pos.x, pos.z, radius(), [&](int x, int z) {
        std::string const& biome = biomeIdOf(region.getBiome(BlockPos{x, pos.y, z}));
        auto               tf    = tfColourOf(biome, x, z, foliage);
        if (!tf) return static_cast<int>(game);
        if (colors::isPositional(biome)) noteDrawn(biome, foliage);
        return *tf;
    });
    return static_cast<unsigned>(c);
}

/// upstream's colour at `pos` when the game's blended colour `game` marks a position-dependent biome
/// (no block source).
std::optional<unsigned> fromMarker(BlockPos const& pos, unsigned game, bool foliage) {
    char const* biome = nullptr;
    if (near(game, foliage ? kSpookyFoliage : kSpookyGrass)) biome = "gloamingforest:spooky_forest";
    else if (near(game, foliage ? kDarkForestCenterFoliage : kDarkForestCenterGrass)) biome = "gloamingforest:dark_forest_center";
    else if (near(game, kEnchantedStatic)) biome = "gloamingforest:enchanted_forest";
    if (!biome) return std::nullopt;
    noteDrawn(biome, foliage);
    std::string const id = biome;
    return static_cast<unsigned>(colors::blend(pos.x, pos.z, radius(), [&](int x, int z) { return tfColour(id, x, z, foliage, game); }));
}

unsigned colourAt(BlockSource* region, BlockPos const& pos, std::optional<unsigned> game, bool foliage) {
    if (!game) return foliage ? kDefaultFoliage : kDefaultGrass;
    if (!enabled()) return *game;
    // the game asks a block's tint several times while it builds the block (faces, vertices): the
    // last answer of this thread is kept (a blend reads up to 225 biomes)
    struct Last {
        BlockSource*  region = nullptr;
        BlockPos      pos{0, 0, 0};
        unsigned      game = 0, result = 0;
        bool          foliage = false, valid = false;
        std::uint64_t world   = 0;
    };
    thread_local Last last;
    std::uint64_t     world = feature::biomes::worldGeneration();
    if (last.valid && last.region == region && last.pos == pos && last.game == *game && last.foliage == foliage && last.world == world)
        return last.result;
    auto     tf     = region ? fromRegion(*region, pos, *game, foliage) : fromMarker(pos, *game, foliage);
    unsigned result = tf.value_or(*game);
    last            = Last{region, pos, *game, result, foliage, true, world};
    return result;
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
// upstream's blocks of other units that ColorHandler draws with BiomeColors.getAverageGrassColor (U6's
// fiddlehead and potted fiddlehead), and the textures of their tinted faces (the cross of the
// fiddlehead, the plant of the pot: Java's tintindex faces; the pot and its dirt stay untinted).
constexpr char const* kTfGrassBlocks[] = {
    "gloamingforest:fiddlehead",
    "gloamingforest:potted_fiddlehead",
};
constexpr char const* kTfGrassTextures[] = {
    "tf_block_fiddlehead",
    "tf_block_potted_fiddlehead",
};

} // namespace

unsigned grassAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game) { return colourAt(region, pos, game.grass(), false); }

unsigned foliageAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game) { return colourAt(region, pos, game.foliage(), true); }

std::optional<unsigned> waterAt(BlockSource& region, BlockPos const& pos, unsigned game) {
    if (!enabled()) return std::nullopt;
    // a water block is asked for each of its vertices: the last answer of this thread is kept
    struct Last {
        BlockSource*            region = nullptr;
        BlockPos                pos{0, 0, 0};
        unsigned                game = 0;
        std::optional<unsigned> result;
        bool                    valid = false;
        std::uint64_t           world = 0;
    };
    thread_local Last last;
    std::uint64_t     world = feature::biomes::worldGeneration();
    if (last.valid && last.region == &region && last.pos == pos && last.game == game && last.world == world) return last.result;
    std::optional<unsigned> result;
    if (isTfBiome(biomeIdOf(region.getBiome(pos)))) {
        result = static_cast<unsigned>(colors::blend(pos.x, pos.z, radius(), [&](int x, int z) {
            auto w = colors::water(biomeIdOf(region.getBiome(BlockPos{x, pos.y, z})));
            return w ? *w : static_cast<int>(game);
        }));
    }
    last = Last{&region, pos, game, result, true, world};
    return result;
}

std::span<char const* const> grassBlocks() { return kGrassBlocks; }
std::span<char const* const> foliageBlocks() { return kFoliageBlocks; }
std::span<char const* const> tfGrassBlocks() { return kTfGrassBlocks; }
std::span<char const* const> tfGrassTextures() { return kTfGrassTextures; }

struct BiomeTintsModule {
    void install() {
        auto grass = tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return grassAt(gf::biomes::ext::tintRegion(), p, g); }};
        for (char const* id : kGrassBlocks) tint::registerBlock(id, grass);
        for (char const* id : kTfGrassBlocks) tint::registerBlock(id, grass);
        for (char const* id : kFoliageBlocks)
            tint::registerBlock(id, tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return foliageAt(gf::biomes::ext::tintRegion(), p, g); }});
        log().info(
            "biomes: upstream biome colours on {} grass and {} foliage blocks, and {} upstream grass-tinted blocks",
            std::size(kGrassBlocks),
            std::size(kFoliageBlocks),
            std::size(kTfGrassBlocks)
        );
    }
    void uninstall() {
        for (char const* id : kGrassBlocks) tint::unregisterBlock(id);
        for (char const* id : kTfGrassBlocks) tint::unregisterBlock(id);
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
