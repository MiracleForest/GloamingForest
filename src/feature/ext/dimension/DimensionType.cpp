// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/generated/resources/data/twilightforest/dimension_type/twilight_forest_type.json
//   src/main/java/twilightforest/init/TFDimensionData.java (twilightDimType)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The dimension_type values and the vanilla rules that read them (DimensionType#brightness,
// Level#updateSkyBrightness, Monster#isDarkEnoughToSpawn), contract extension of "world" (F4).

#include "feature/ext/dimension/DimensionType.h"

#include <algorithm>
#include <cmath>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/dimension/World.h"

namespace gf::world::ext {

namespace {

DimensionTypeValues makeTwilight() {
    DimensionTypeValues v;
    v.id                          = "twilightforest:twilight_forest_type";
    v.fixedTime                   = 13000;
    v.hasSkylight                 = true;
    v.hasCeiling                  = false;
    v.ultrawarm                   = false;
    v.natural                     = true;
    v.coordinateScale             = 1.0 / 8.0;
    v.bedWorks                    = true;
    v.respawnAnchorWorks          = false;
    v.minY                        = -32;
    v.height                      = 32 + 256;
    v.logicalHeight               = 32 + 256;
    v.infiniburn                  = "#minecraft:infiniburn_overworld";
    v.effects                     = "twilightforest:renderer";
    v.ambientLight                = 0.01f;
    v.piglinSafe                  = false;
    v.hasRaids                    = false;
    v.monsterSpawnLightMin        = 0;
    v.monsterSpawnLightMax        = 7;
    v.monsterSpawnBlockLightLimit = 7;
    return v;
}

// Java 1.21.1's built-in types (BuiltinDimensionTypes / DimensionTypes bootstrap)
DimensionTypeValues makeOverworld() {
    DimensionTypeValues v;
    v.id = "minecraft:overworld";
    return v; // the defaults are the overworld's
}

DimensionTypeValues makeNether() {
    DimensionTypeValues v;
    v.id                          = "minecraft:the_nether";
    v.fixedTime                   = 18000;
    v.hasSkylight                 = false;
    v.hasCeiling                  = true;
    v.ultrawarm                   = true;
    v.natural                     = false;
    v.coordinateScale             = 8.0;
    v.bedWorks                    = false;
    v.respawnAnchorWorks          = true;
    v.minY                        = 0;
    v.height                      = 256;
    v.logicalHeight               = 128;
    v.infiniburn                  = "#minecraft:infiniburn_nether";
    v.effects                     = "minecraft:the_nether";
    v.ambientLight                = 0.1f;
    v.piglinSafe                  = true;
    v.hasRaids                    = false;
    v.monsterSpawnLightMin        = 7; // a constant 7
    v.monsterSpawnLightMax        = 7;
    v.monsterSpawnBlockLightLimit = 15;
    return v;
}

DimensionTypeValues makeEnd() {
    DimensionTypeValues v;
    v.id                 = "minecraft:the_end";
    v.fixedTime          = 6000;
    v.hasSkylight        = false;
    v.hasCeiling         = false;
    v.ultrawarm          = false;
    v.natural            = false;
    v.coordinateScale    = 1.0;
    v.bedWorks           = false;
    v.respawnAnchorWorks = false;
    v.minY               = 0;
    v.height             = 256;
    v.logicalHeight      = 256;
    v.infiniburn         = "#minecraft:infiniburn_end";
    v.effects            = "minecraft:the_end";
    v.ambientLight       = 0.0f;
    v.piglinSafe         = false;
    v.hasRaids           = true;
    return v;
}

} // namespace

DimensionTypeValues const& twilightType() {
    static DimensionTypeValues const v = makeTwilight();
    return v;
}

DimensionTypeValues const& dimensionType(int dim) {
    static DimensionTypeValues const overworld = makeOverworld(), nether = makeNether(), end = makeEnd();
    if (dim == 1) return nether;
    if (dim == 2) return end;
    if (dim > 2 && world::isTwilight(DimensionType(dim))) return twilightType();
    return overworld;
}

DimensionTypeValues const& dimensionType(Dimension const& dimension) {
    if (world::isTwilight(dimension)) return twilightType(); // by identifier: any thread
    return dimensionType(static_cast<int>(dimension.getDimensionId()));
}

float brightness(float ambientLight, int lightLevel) {
    float f  = static_cast<float>(std::clamp(lightLevel, 0, 15)) / 15.0f;
    float f1 = f / (4.0f - 3.0f * f);
    return f1 + ambientLight * (1.0f - f1); // Mth.lerp(ambientLight, f1, 1.0F)
}

void brightnessRamp(float ambientLight, float (&ramp)[16]) {
    for (int i = 0; i < 16; ++i) ramp[i] = brightness(ambientLight, i);
}

int skyDarken(float timeOfDay, float rainLevel, float thunderLevel) {
    double d0 = 1.0 - static_cast<double>(rainLevel * 5.0f) / 16.0;
    double d1 = 1.0 - static_cast<double>(thunderLevel * 5.0f) / 16.0;
    double d2 = 0.5 + 2.0 * std::clamp(static_cast<double>(std::cos(timeOfDay * 6.2831855f)), -0.25, 0.25);
    return static_cast<int>((1.0 - d2 * d0 * d1) * 11.0);
}

namespace {

std::pair<float, float> weatherOf(Dimension const& dimension) {
    auto const* w = dimension.mWeather.get();
    if (!w) return {0.0f, 0.0f};
    float rain = w->mRainLevel;
    return {rain, static_cast<float>(w->mLightningLevel) * rain}; // Level#getThunderLevel: thunder * rain
}

} // namespace

int skyDarken(BlockSource& region) {
    auto const& dimension = region.getDimension();
    auto [rain, thunder]  = weatherOf(dimension);
    // the dimension's time of day (a fixed one for the Twilight Forest)
    return skyDarken(dimension.getTimeOfDay(region.getLevel().getTime(), 0.0f), rain, thunder);
}

bool isThundering(BlockSource& region) {
    auto const& dimension = region.getDimension();
    auto const& type      = dimensionType(dimension);
    return type.hasSkylight && !type.hasCeiling && weatherOf(dimension).second > 0.9f;
}

bool isDarkEnoughToSpawn(DimensionTypeValues const& type, int skyLight, int blockLight, int darken, core::jrand::RandomSource& random) {
    if (skyLight > random.nextInt(32)) return false;
    int limit = type.monsterSpawnBlockLightLimit;
    if (limit < 15 && blockLight > limit) return false;
    int raw = std::max(blockLight, skyLight - darken); // LevelReader#getMaxLocalRawBrightness
    return raw <= random.nextIntBetweenInclusive(type.monsterSpawnLightMin, type.monsterSpawnLightMax);
}

bool isDarkEnoughToSpawn(BlockSource& region, BlockPos const& pos, core::jrand::RandomSource& random) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false; // nothing spawns outside the world
    auto pair   = region.getBrightnessPair(pos);
    int  sky    = static_cast<int>(pair.sky->mValue);
    int  block  = static_cast<int>(pair.block->mValue);
    int  darken = isThundering(region) ? 10 : skyDarken(region);
    return isDarkEnoughToSpawn(dimensionType(region.getDimension()), sky, block, darken, random);
}

} // namespace gf::world::ext
