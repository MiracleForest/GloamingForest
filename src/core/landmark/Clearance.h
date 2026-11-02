// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/DecorationClearance.java
//   src/main/java/twilightforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/twilightforest/world/components/chunkgenerators/BoxDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/twilightforest/world/components/structures/CustomDensitySource.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java
//   src/main/java/twilightforest/util/WorldUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Decoration clearance and terrain shaping around landmarks (F6, pure, gtested): TF's
// DecorationConfig, the AvoidLandmarkModifier decision, and the density contributions TF adds to
// the beardifier (BoxDensityFunction with vanilla's bury / beard kernels, the inverted pyramid of
// CustomDensitySource). Positions are block coordinates; boxes are inclusive (BlockBox).

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

namespace gf::core::landmark {

/// DecorationClearance.DecorationConfig; LandmarkStructure's defaults when a structure has none.
struct DecorationConfig {
    float chunkClearanceRadius = 1.0f;
    bool  surfaceDecorations   = true;
    bool  undergroundDecorations = true;
    bool  vegetation           = true;
    bool  adjustElevation      = false;

    /// The "decoration_clearance" object (null / missing: the defaults).
    [[nodiscard]] static DecorationConfig fromJson(nlohmann::json const& j);
};

/// One piece of a start as the modifier sees it.
struct ClearancePiece {
    BlockBox box;
    bool     utilityAllowsFeatures = false; ///< a UtilityPiece with allow_features
};

/// What a placed feature occupies (AvoidLandmarkModifier's occupies_* flags) and its extra margin.
struct FeatureOccupancy {
    bool surface     = false;
    bool underground = false;
    bool vegetation  = false;
    int  additionalClearance = 0;
};

/// AvoidLandmarkModifier#clearFromStructureZone: the structure allows everything the feature occupies.
[[nodiscard]] bool clearFromStructureZone(DecorationConfig const& cfg, FeatureOccupancy const& occ);
/// AvoidLandmarkModifier#placementIsBlocked for one start: `startBox` is the start's (adjusted)
/// bounding box, `pieces` its pieces.
[[nodiscard]] bool placementIsBlocked(int x, int y, int z, BlockBox const& startBox, std::vector<ClearancePiece> const& pieces, float chunkClearanceRadius, int additionalClearance);
/// BoundingBox#getCenter.
[[nodiscard]] GlobalPos boxCenter(BlockBox const& box);
/// BoundingBoxUtils#greatestAxalDistance.
[[nodiscard]] int greatestAxalDistance(BlockBox const& box, int x, int y, int z);

/// TerrainAdjustment.
enum class TerrainAdjustment { None, Bury, BeardThin, BeardBox, Encapsulate };
[[nodiscard]] TerrainAdjustment terrainAdjustmentOf(std::string const& name);
/// Structure#adjustBoundingBox: inflated by 12 unless the adjustment is none.
[[nodiscard]] BlockBox adjustBoundingBox(BlockBox const& box, TerrainAdjustment adjustment);

/// Beardifier#getBuryContribution.
[[nodiscard]] double buryContribution(double x, double y, double z);
/// Beardifier#getBeardContribution (with vanilla's 24^3 kernel and Mth#fastInvSqrt).
[[nodiscard]] double beardContribution(int x, int y, int z, int height);

/// BoxDensityFunction: one box's density contribution (min / max value clamp included).
struct BoxDensity {
    int               minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    double            minValue = -4.0, maxValue = 4.0;
    TerrainAdjustment adjustment = TerrainAdjustment::None;

    /// BoxDensityFunction#make(box, dYMin, dYMax, adjustment).
    [[nodiscard]] static BoxDensity make(BlockBox const& box, int dYMin, int dYMax, TerrainAdjustment adjustment);
    [[nodiscard]] double compute(int x, int y, int z) const;
};
/// BoxDensityFunction#combine: the sum of every box's contribution.
[[nodiscard]] double combinedBoxDensity(std::vector<BoxDensity> const& boxes, int x, int y, int z);

/// CustomDensitySource#getInvertedPyramidTerraformer for a start box.
struct InvertedPyramid {
    BlockBox structureBox;
    int      yOffset           = 0;
    int      horizontalPadding = 0;
    [[nodiscard]] double compute(int x, int y, int z) const;
};

// ---- terrain height for a structure's elevation (WorldUtil#adjustForTerrain) ----------------------------

/// net.minecraft.util.Mth#lerpDiscrete(delta, start, end).
[[nodiscard]] int lerpDiscrete(float delta, int start, int end);
/// WorldUtil#adjustForTerrain(context, xMin, zMin, xMax, zMax, gridLength, type)'s sample columns, in
/// Java's order (z rows outer, x inner): a gridLength x gridLength grid spanning the area.
[[nodiscard]] std::vector<std::pair<int, int>> terrainSampleColumns(int xMin, int zMin, int xMax, int zMax, int gridLength);
/// WorldUtil#adjustForTerrain's height from the sampled first occupied heights: sorted from highest to
/// lowest and weighted 1, 2, 3, ... (the lower ones weigh more), Math.round of the weighted mean.
[[nodiscard]] int weightedTerrainHeight(std::vector<int> heights);
/// The first occupied height of column (x, z) for the heightmap the caller chose (nullopt: unknown).
using ColumnHeightFn = std::function<std::optional<int>(int x, int z)>;
/// WorldUtil#adjustForTerrain(context, xMin, zMin, xMax, zMax, gridLength, type) over `height`
/// (nullopt when a sample is unknown).
[[nodiscard]] std::optional<int> adjustForTerrain(int xMin, int zMin, int xMax, int zMax, int gridLength, ColumnHeightFn const& height);
/// WorldUtil#adjustForTerrain(context, xInCenterChunk, zInCenterChunk, radiusFromCenterChunk,
/// gridLength): the chunk holding (x, z) widened by the radius (WORLD_SURFACE_WG in Java).
[[nodiscard]] std::optional<int> adjustForTerrainAroundChunk(int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength, ColumnHeightFn const& height);

} // namespace gf::core::landmark
