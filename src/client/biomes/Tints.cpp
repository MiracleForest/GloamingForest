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

#include <atomic>
#include <cstdint>
#include <string>
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

/// The biome id of a Biome (cached per biome object while the world lasts: the registry's biomes
/// live as long as the level, and another world may reuse their addresses).
std::string const& idOf(Biome const& b) {
    thread_local std::unordered_map<Biome const*, std::string> ids;
    thread_local std::uint64_t                                 world = ~std::uint64_t(0);
    if (auto now = feature::biomes::worldGeneration(); now != world) {
        ids.clear();
        world = now;
    }
    auto [it, fresh] = ids.try_emplace(&b);
    if (fresh) {
        auto const& name = b.mHash->getString();
        it->second       = name.find(':') == std::string::npos ? "minecraft:" + name : name;
    }
    return it->second;
}

/// The flat colours of TF's biome JSON for the biomes that have one without a modifier (the fire
/// swamp, the snowy forest's grass). Not the snowy forest's foliage: in a cold biome Bedrock's leaf
/// colour is not a colour but the coordinates of its seasons map (textures/environment/seasons:
/// green leaves with snow), and a white "colour" there draws the map's red corner.
std::optional<int> flatColour(std::string_view biome, bool foliage) {
    if (biome == "twilightforest:fire_swamp") return foliage ? 0x64260F : 0x572E23;
    if (biome == "twilightforest:snowy_forest" && !foliage) return 0xFFFFFF;
    return std::nullopt;
}

/// TF's colour of `biome` at (x, z) (nullopt: TF has none, the colour map's: the game's).
std::optional<int> tfColourOf(std::string const& biome, int x, int z, bool foliage) {
    auto v = foliage ? colors::foliage(biome, kEnchantedJson, x, z) : colors::grass(biome, kEnchantedJson, x, z);
    return v ? v : flatColour(biome, foliage);
}

int tfColour(std::string const& biome, int x, int z, bool foliage, unsigned game) {
    return tfColourOf(biome, x, z, foliage).value_or(static_cast<int>(game));
}

/// TF's colour at `pos` with the biomes read from `region`: every column of the blend gets TF's
/// colour of its biome (modifiers, position-dependent colours, flat colours) or, without one, the
/// game's (nullopt: no column has a TF colour, the game's colour stays).
std::optional<unsigned> fromRegion(BlockSource& region, BlockPos const& pos, unsigned game, bool foliage) {
    if (foliage) {
        // leaves in TF's cold biomes: the game's tint addresses its seasons map (see flatColour)
        std::string const& here = idOf(region.getBiome(pos));
        if (here == "twilightforest:snowy_forest" || here == "twilightforest:glacier") return std::nullopt;
    }
    bool any = false;
    int  c   = colors::blend(pos.x, pos.z, radius(), [&](int x, int z) {
        std::string const& biome = idOf(region.getBiome(BlockPos{x, pos.y, z}));
        auto               tf    = tfColourOf(biome, x, z, foliage);
        if (!tf) return static_cast<int>(game);
        any = true;
        if (colors::isPositional(biome)) noteDrawn(biome, foliage);
        return *tf;
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
