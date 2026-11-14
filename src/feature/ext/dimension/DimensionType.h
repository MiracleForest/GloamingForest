// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/generated/resources/data/gloamingforest/dimension_type/gloaming_forest_type.json
//   src/main/java/gloamingforest/init/GloamingDimensionData.java (gloamingDimType)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract extension of "world" (rule R18: F4, new files only; folded into the contract by A8 in
// W2b). upstream's dimension_type values the Bedrock dimension definition can't hold, for the units
// whose code reads them on Java through DimensionType:
//   - ambient_light 0.01: DimensionType#brightness (Level#getLightLevelDependentMagicValue: mob AI,
//     the light texture). F4 also writes it into the dimension's brightness ramp (server, and the
//     client mod's light texture).
//   - monster_spawn_light_level uniform 0..7 and monster_spawn_block_light_limit 7:
//     Monster#isDarkEnoughToSpawn, for the natural spawner (F5), spawners (F8) and monsters' own
//     spawn rules (U17 and the boss units).
// The other values (fixed_time, coordinate_scale, bed_works, has_raids, ...) are applied by F4
// (feature/dimension/World.h: fixedTime(), coordinateScale()).
//
//     if (!gf::world::ext::isDarkEnoughToSpawn(region, pos, random)) return false;

#include <optional>
#include <string>

#include "core/jrand/JRand.h"

class BlockPos;
class BlockSource;
class Dimension;

namespace gf::world::ext {

/// A dimension_type (Java's built-in overworld / the_nether / the_end and upstream's gloaming_forest_type).
struct DimensionTypeValues {
    std::string              id;
    std::optional<long long> fixedTime;
    bool                     hasSkylight        = true;
    bool                     hasCeiling         = false;
    bool                     ultrawarm          = false;
    bool                     natural            = true;
    double                   coordinateScale    = 1.0;
    bool                     bedWorks           = true;
    bool                     respawnAnchorWorks = false;
    int                      minY               = -64;
    int                      height             = 384;
    int                      logicalHeight      = 384;
    std::string              infiniburn         = "#minecraft:infiniburn_overworld";
    std::string              effects            = "minecraft:overworld";
    float                    ambientLight       = 0.0f;
    bool                     piglinSafe         = false;
    bool                     hasRaids           = true;
    int                      monsterSpawnLightMin        = 0; ///< monster_spawn_light_level (uniform, inclusive)
    int                      monsterSpawnLightMax        = 7;
    int                      monsterSpawnBlockLightLimit = 0;
};

/// upstream's gloaming_forest_type.
[[nodiscard]] DimensionTypeValues const& gloamingType();
/// The dimension_type of an engine dimension id: 0 overworld, 1 the_nether, 2 the_end, the Gloaming
/// Forest's id gloaming_forest_type; any other custom dimension reads as the overworld.
[[nodiscard]] DimensionTypeValues const& dimensionType(int dim);
[[nodiscard]] DimensionTypeValues const& dimensionType(Dimension const& dimension);

/// DimensionType#brightness: the brightness of a light level 0..15 with the ambient light
/// (Mth.lerp(ambientLight, f / (4 - 3f), 1) with f = light / 15).
[[nodiscard]] float brightness(float ambientLight, int lightLevel);
/// The 16 brightness values of a dimension (DimensionType.fillBrightnessRamp).
void brightnessRamp(float ambientLight, float (&ramp)[16]);

/// Level#updateSkyBrightness: the sky darkening for a time of day (DimensionType#timeOfDay, 0..1)
/// and the rain and thunder levels (0..1).
[[nodiscard]] int skyDarken(float timeOfDay, float rainLevel, float thunderLevel);
/// Level#getSkyDarken of the region's dimension now (its time of day and weather).
[[nodiscard]] int skyDarken(BlockSource& region);
/// Level#isThundering of the region's dimension (sky light, no ceiling, thunder level above 0.9).
[[nodiscard]] bool isThundering(BlockSource& region);

/// Monster#isDarkEnoughToSpawn from the light values: the sky light must not beat nextInt(32), the
/// block light must stay within the block light limit, and the raw brightness (the sky light
/// darkened by `darken`, 10 while thundering) must not beat the monster spawn light test.
[[nodiscard]] bool isDarkEnoughToSpawn(
    DimensionTypeValues const& type,
    int                        skyLight,
    int                        blockLight,
    int                        darken,
    core::jrand::RandomSource& random
);
/// Monster#isDarkEnoughToSpawn at `pos` in the region's dimension (the engine's light).
[[nodiscard]] bool isDarkEnoughToSpawn(BlockSource& region, BlockPos const& pos, core::jrand::RandomSource& random);

} // namespace gf::world::ext
