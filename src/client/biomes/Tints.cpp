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
// colours are flat in TF too: TF's biome JSON stores the modifiers' results). The three
// position-dependent biomes are recognised here by the game's own blended colour at the block: a
// block whose whole blend lies in one of them gets exactly that biome's static colour from the
// client biome file, and is then drawn with TF's colours, blended as Java blends them. A block
// whose blend straddles a border keeps the game's blend of the static colours (Java blends TF's
// colours there). The client knows no biome source (remote worlds), so this needs no seed.

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

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
// position-dependent biomes: the markers of a blend that lies wholly inside one of them.
constexpr unsigned kSpookyGrass             = 0xC45123;
constexpr unsigned kSpookyFoliage           = 0xFF8501;
constexpr unsigned kDarkForestCenterGrass   = 0x667540;
constexpr unsigned kDarkForestCenterFoliage = 0xF9821E;
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

/// TF's colour at `pos` when the game's blended colour `game` marks a position-dependent biome.
std::optional<unsigned> positional(BlockPos const& pos, unsigned game, bool foliage) {
    char const* biome = nullptr;
    if (near(game, foliage ? kSpookyFoliage : kSpookyGrass)) biome = "twilightforest:spooky_forest";
    else if (near(game, foliage ? kDarkForestCenterFoliage : kDarkForestCenterGrass)) biome = "twilightforest:dark_forest_center";
    else if (near(game, kEnchantedStatic)) biome = "twilightforest:enchanted_forest";
    if (!biome) return std::nullopt;
    // one line per biome and kind, for the e2e scenario (the tints run on chunk builder threads)
    static std::atomic<unsigned> logged{0};
    unsigned bit = (biome[16] == 'p' ? 1u : biome[16] == 'a' ? 2u : 4u) << (foliage ? 3 : 0);
    if (!(logged.fetch_or(bit) & bit)) log().info("biomes: client draws TF's {} colours of {}", foliage ? "foliage" : "grass", biome);
    int c = colors::blend(pos.x, pos.z, radius(), [&](int x, int z) {
        auto v = foliage ? colors::foliage(biome, kEnchantedJson, x, z) : colors::grass(biome, kEnchantedJson, x, z);
        return v.value_or(static_cast<int>(game));
    });
    return static_cast<unsigned>(c);
}

/// The first distinct game colours a tint sees (at most 24 lines per session): what the markers are
/// compared with, for the e2e log.
void noteGameColour(bool foliage, unsigned c) {
    static std::mutex                 m;
    static std::vector<unsigned>      seen;
    std::lock_guard                   lock(m);
    unsigned                          key = c | (foliage ? 0x1000000u : 0u);
    if (seen.size() >= 24 || std::find(seen.begin(), seen.end(), key) != seen.end()) return;
    seen.push_back(key);
    log().info("biomes: game {} colour #{:06X} seen by the tint", foliage ? "foliage" : "grass", c);
}

unsigned grassAt(BlockPos const& pos, tint::GameColors const& game) {
    auto g = game.grass();
    if (!g) return kDefaultGrass;
    noteGameColour(false, *g);
    if (!enabled()) return *g;
    return positional(pos, *g, false).value_or(*g);
}

unsigned foliageAt(BlockPos const& pos, tint::GameColors const& game) {
    auto f = game.foliage();
    if (!f) return kDefaultFoliage;
    noteGameColour(true, *f);
    if (!enabled()) return *f;
    return positional(pos, *f, true).value_or(*f);
}

// Vanilla blocks drawn with the biome grass / foliage colour (Java: BlockColors with
// BiomeColors.getAverageGrassColor / getAverageFoliageColor), Bedrock ids.
constexpr char const* kGrassBlocks[] = {
    "minecraft:grass_block",
    "minecraft:short_grass",
    "minecraft:tall_grass",
    "minecraft:fern",
    "minecraft:large_fern",
    "minecraft:reeds",
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

struct BiomeTintsModule {
    void install() {
        for (char const* id : kGrassBlocks) tint::registerBlock(id, tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return grassAt(p, g); }});
        for (char const* id : kFoliageBlocks)
            tint::registerBlock(id, tint::GameTintFn{[](BlockPos const& p, tint::GameColors const& g) { return foliageAt(p, g); }});
        log().info("biomes: TF biome colours on {} grass and {} foliage blocks", std::size(kGrassBlocks), std::size(kFoliageBlocks));
    }
    void uninstall() {
        for (char const* id : kGrassBlocks) tint::unregisterBlock(id);
        for (char const* id : kFoliageBlocks) tint::unregisterBlock(id);
    }
};

} // namespace gf::client::biomes

namespace gf::biomes::ext {
unsigned grassColor(BlockPos const& pos, gf::client::tint::GameColors const& game) { return gf::client::biomes::grassAt(pos, game); }
unsigned foliageColor(BlockPos const& pos, gf::client::tint::GameColors const& game) { return gf::client::biomes::foliageAt(pos, game); }
} // namespace gf::biomes::ext

GF_REGISTER_MODULE(gf::client::biomes::BiomeTintsModule);

#endif
