// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of upstream's decoration clearance and structure terraforming (F6): DecorationClearance,
// AvoidLandmarkModifier, BoxDensityFunction, CustomDensitySource, WorldUtil#adjustForTerrain
// (upstream commit 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "core/landmark/Clearance.h"

using namespace gf::core;
using namespace gf::core::landmark;

TEST(LandmarkClearance, DecorationConfigDefaultsAndJson) {
    auto none = DecorationConfig::fromJson(nullptr);
    EXPECT_FLOAT_EQ(none.chunkClearanceRadius, 1.0f);
    EXPECT_TRUE(none.surfaceDecorations && none.undergroundDecorations && none.vegetation);
    EXPECT_FALSE(none.adjustElevation);
    auto lich = DecorationConfig::fromJson(nlohmann::json::parse(
        R"({"adjust_structure_elevation":true,"allow_biome_surface_decorations":false,"allow_biome_underground_decorations":true,"allow_biome_vegetation":false,"chunk_clearance_radius":0.0})"
    ));
    EXPECT_FLOAT_EQ(lich.chunkClearanceRadius, 0.0f);
    EXPECT_FALSE(lich.surfaceDecorations);
    EXPECT_TRUE(lich.undergroundDecorations);
    EXPECT_FALSE(lich.vegetation);
    EXPECT_TRUE(lich.adjustElevation);
    // a surface feature is blocked by the lich tower, an underground one is not
    EXPECT_FALSE(clearFromStructureZone(lich, {.surface = true}));
    EXPECT_TRUE(clearFromStructureZone(lich, {.underground = true}));
    EXPECT_FALSE(clearFromStructureZone(lich, {.vegetation = true}));
}

TEST(LandmarkClearance, PlacementIsBlockedByRadius) {
    BlockBox start{0, 0, 0, 31, 20, 31}; // center (16, 10, 16)
    EXPECT_EQ(boxCenter(start).x, 16);
    EXPECT_EQ(boxCenter(start).z, 16);
    // radius 2 chunks: |dx| < 32 and |dz| < 32 around the center
    EXPECT_TRUE(placementIsBlocked(16 + 31, 0, 16, start, {}, 2.0f, 0));
    EXPECT_FALSE(placementIsBlocked(16 + 32, 0, 16, start, {}, 2.0f, 0));
    EXPECT_TRUE(placementIsBlocked(16 + 32, 0, 16, start, {}, 2.0f, 1)); // additional clearance
    EXPECT_FALSE(placementIsBlocked(16, 0, 16 - 40, start, {}, 2.5f, 0));
    EXPECT_TRUE(placementIsBlocked(16, 0, 16 - 39, start, {}, 2.5f, 0));
}

TEST(LandmarkClearance, PlacementIsBlockedByPieces) {
    BlockBox                    start{0, 0, 0, 40, 40, 40};
    std::vector<ClearancePiece> pieces{{{10, 0, 10, 14, 30, 14}, false}, {{20, 0, 20, 30, 5, 30}, true}};
    EXPECT_TRUE(placementIsBlocked(12, 5, 12, start, pieces, 0.0f, 0));
    EXPECT_FALSE(placementIsBlocked(16, 5, 12, start, pieces, 0.0f, 0));
    EXPECT_TRUE(placementIsBlocked(16, 5, 12, start, pieces, 0.0f, 2));
    // a utility piece that allows features never blocks
    EXPECT_FALSE(placementIsBlocked(25, 2, 25, start, pieces, 0.0f, 0));
    EXPECT_EQ(greatestAxalDistance({0, 0, 0, 4, 4, 4}, 7, 2, -3), 3);
}

TEST(LandmarkClearance, AdjustedBoundingBox) {
    BlockBox b{0, 0, 0, 9, 9, 9};
    EXPECT_EQ(adjustBoundingBox(b, TerrainAdjustment::None), b);
    EXPECT_EQ(adjustBoundingBox(b, terrainAdjustmentOf("beard_thin")), (BlockBox{-12, -12, -12, 21, 21, 21}));
    EXPECT_EQ(terrainAdjustmentOf("bury"), TerrainAdjustment::Bury);
    EXPECT_EQ(terrainAdjustmentOf("beard_box"), TerrainAdjustment::BeardBox);
    EXPECT_EQ(terrainAdjustmentOf("encapsulate"), TerrainAdjustment::Encapsulate);
    EXPECT_EQ(terrainAdjustmentOf("whatever"), TerrainAdjustment::None);
}

TEST(LandmarkClearance, BeardifierKernels) {
    EXPECT_DOUBLE_EQ(buryContribution(0, 0, 0), 1.0);
    EXPECT_DOUBLE_EQ(buryContribution(3, 0, 0), 0.5);
    EXPECT_DOUBLE_EQ(buryContribution(6, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(buryContribution(0, 0, 9), 0.0);
    EXPECT_EQ(beardContribution(12, 0, 0, 0), 0.0); // outside the 24^3 kernel
    EXPECT_EQ(beardContribution(0, -13, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(beardContribution(3, 1, 2, 1), beardContribution(-3, 1, -2, 1)); // symmetric in x / z
    EXPECT_LT(beardContribution(0, 0, 0, 0), 0.0);  // above the bottom: pulls the terrain down
    EXPECT_GT(beardContribution(0, -2, 0, -2), 0.0); // below: fills
}

TEST(LandmarkClearance, BoxDensity) {
    auto bury = BoxDensity::make({0, 10, 0, 9, 20, 9}, -5, -5, TerrainAdjustment::Bury);
    EXPECT_EQ(bury.minY, 5);
    EXPECT_EQ(bury.maxY, 15);
    EXPECT_DOUBLE_EQ(bury.compute(5, 5, 5), 1.0);  // inside, at the bottom
    EXPECT_DOUBLE_EQ(bury.compute(40, 5, 5), 0.0); // far away
    auto none = BoxDensity::make({0, 0, 0, 1, 1, 1}, 0, 0, TerrainAdjustment::None);
    EXPECT_DOUBLE_EQ(none.compute(0, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(combinedBoxDensity({bury, bury}, 5, 5, 5), 2.0);
    auto beard = BoxDensity::make({0, 0, 0, 9, 9, 9}, 0, 0, TerrainAdjustment::BeardBox);
    EXPECT_GE(beard.compute(5, 5, 5), -4.0);
    EXPECT_LE(beard.compute(5, 5, 5), 4.0);
}

TEST(LandmarkClearance, InvertedPyramid) {
    // a 40x40 start box (16 after removing the 12-block inflation on both sides)
    InvertedPyramid pyramid{{-12, -12, -12, 27, 30, 27}, 0, 4};
    // well below the structure: untouched (min(0, max(+1, ...)) = 0)
    EXPECT_DOUBLE_EQ(pyramid.compute(8, -40, 8), 0.0);
    // above the base at the center: negative (terrain removed)
    EXPECT_LT(pyramid.compute(8, 20, 8), 0.0);
    // never positive
    for (int y = -30; y < 60; y += 7)
        for (int x = -40; x < 60; x += 9) EXPECT_LE(pyramid.compute(x, y, 8), 0.0);
}

TEST(LandmarkClearance, LerpDiscreteIsMths) {
    // Mth#lerpDiscrete: start + floor(delta * (span - 1)) + (delta > 0 ? 1 : 0)
    EXPECT_EQ(lerpDiscrete(0.0f, 10, 20), 10);
    EXPECT_EQ(lerpDiscrete(1.0f, 10, 20), 20);
    EXPECT_EQ(lerpDiscrete(0.5f, 0, 15), 8);
    EXPECT_EQ(lerpDiscrete(1.0f / 3.0f, 0, 15), 5);
    EXPECT_EQ(lerpDiscrete(2.0f / 3.0f, 0, 15), 10);
    EXPECT_EQ(lerpDiscrete(1.0f / 3.0f, -24, 39), -3);
}

TEST(LandmarkClearance, AdjustForTerrainSamplesAndWeights) {
    // WorldUtil#adjustForTerrain: a 4x4 grid over the area, z rows outer
    auto cols = terrainSampleColumns(0, 0, 15, 15, 4);
    ASSERT_EQ(cols.size(), 16u);
    EXPECT_EQ(cols[0], (std::pair<int, int>{0, 0}));
    EXPECT_EQ(cols[1], (std::pair<int, int>{5, 0}));
    EXPECT_EQ(cols[3], (std::pair<int, int>{15, 0}));
    EXPECT_EQ(cols[4], (std::pair<int, int>{0, 5}));
    EXPECT_EQ(cols[15], (std::pair<int, int>{15, 15}));
    // heights x + z: sorted 30, 25, 25, 20, ... weighted 1..16 -> 1470 / 136 = 10.81 -> 11
    auto h = adjustForTerrain(0, 0, 15, 15, 4, [](int x, int z) -> std::optional<int> { return x + z; });
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(*h, 11);
    // Math.round: half up, also below zero
    EXPECT_EQ(weightedTerrainHeight({0, 3, 0}), 1);   // 3 / 6 = 0.5
    EXPECT_EQ(weightedTerrainHeight({-3, 0, 0}), -1); // -9 / 6 = -1.5
    EXPECT_EQ(weightedTerrainHeight({}), 0);
    // an unknown sample makes the whole height unknown
    auto partial = adjustForTerrain(0, 0, 15, 15, 4, [](int x, int) -> std::optional<int> {
        if (x == 15) return std::nullopt;
        return 4;
    });
    EXPECT_FALSE(partial.has_value());
    EXPECT_FALSE(adjustForTerrain(0, 0, 15, 15, 4, nullptr).has_value());
    // the center-chunk overload: the chunk of (x, z) widened by the radius (the lich tower's 32, 4)
    int  minX = 1 << 30, maxX = -(1 << 30), minZ = 1 << 30, maxZ = -(1 << 30), n = 0;
    auto flat = adjustForTerrainAroundChunk(-1, -17, 32, 4, [&](int x, int z) -> std::optional<int> {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
        ++n;
        return 7;
    });
    EXPECT_EQ(flat, std::optional<int>(7));
    EXPECT_EQ(n, 16);
    EXPECT_EQ(minX, -16 - 32);
    EXPECT_EQ(maxX, -1 + 32);
    EXPECT_EQ(minZ, -32 - 32);
    EXPECT_EQ(maxZ, -17 + 32);
}
