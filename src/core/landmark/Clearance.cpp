// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/util/DecorationClearance.java
//   src/main/java/gloamingforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/BoxDensityFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/gloamingforest/world/components/structures/CustomDensitySource.java
//   src/main/java/gloamingforest/util/BoundingBoxUtils.java
//   src/main/java/gloamingforest/util/WorldUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The vanilla helpers (Beardifier kernels, Mth#fastInvSqrt, Mth#clampedMap, DensityFunctions
// y_clamped_gradient, Mth#lerpDiscrete, Math#round) follow Minecraft 1.21.1 and Java.
#include "core/landmark/Clearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>

namespace gf::core::landmark {

namespace {

double clampedLerp(double start, double end, double delta) {
    if (delta < 0.0) return start;
    if (delta > 1.0) return end;
    return start + delta * (end - start);
}

/// Mth#clampedMap.
double clampedMap(double input, double inMin, double inMax, double outMin, double outMax) {
    return clampedLerp(outMin, outMax, (input - inMin) / (inMax - inMin));
}

/// Mth#fastInvSqrt(double).
double fastInvSqrt(double x) {
    double       half = 0.5 * x;
    std::int64_t i;
    std::memcpy(&i, &x, sizeof i);
    i = 6910469410427058090LL - (i >> 1);
    std::memcpy(&x, &i, sizeof x);
    return x * (1.5 - half * x * x);
}

double computeBeard(int x, double y, int z) {
    double d0 = static_cast<double>(x) * x + y * y + static_cast<double>(z) * z;
    return std::pow(2.718281828459045, -d0 / 16.0);
}

/// Beardifier.BEARD_KERNEL.
std::array<float, 13824> const& beardKernel() {
    static std::array<float, 13824> const kernel = [] {
        std::array<float, 13824> k{};
        for (int i = 0; i < 24; i++)
            for (int j = 0; j < 24; j++)
                for (int l = 0; l < 24; l++) k[i * 24 * 24 + j * 24 + l] = static_cast<float>(computeBeard(j - 12, static_cast<double>(l - 12) + 0.5, i - 12));
        return k;
    }();
    return kernel;
}

bool inKernelRange(int i) { return i >= 0 && i < 24; }

/// DensityFunctions.yClampedGradient.
double yClampedGradient(int y, int fromY, int toY, double fromValue, double toValue) {
    return clampedMap(static_cast<double>(y), static_cast<double>(fromY), static_cast<double>(toY), fromValue, toValue);
}

} // namespace

DecorationConfig DecorationConfig::fromJson(nlohmann::json const& j) {
    DecorationConfig c;
    if (!j.is_object()) return c;
    c.chunkClearanceRadius   = j.value("chunk_clearance_radius", 1.0f);
    c.surfaceDecorations     = j.value("allow_biome_surface_decorations", true);
    c.undergroundDecorations = j.value("allow_biome_underground_decorations", true);
    c.vegetation             = j.value("allow_biome_vegetation", true);
    c.adjustElevation        = j.value("adjust_structure_elevation", false);
    return c;
}

bool clearFromStructureZone(DecorationConfig const& cfg, FeatureOccupancy const& occ) {
    bool surfaceClear     = !occ.surface || cfg.surfaceDecorations;
    bool undergroundClear = !occ.underground || cfg.undergroundDecorations;
    bool vegetationClear  = !occ.vegetation || cfg.vegetation;
    return surfaceClear && undergroundClear && vegetationClear;
}

GlobalPos boxCenter(BlockBox const& b) {
    return {0, b.minX + (b.maxX - b.minX + 1) / 2, b.minY + (b.maxY - b.minY + 1) / 2, b.minZ + (b.maxZ - b.minZ + 1) / 2};
}

int greatestAxalDistance(BlockBox const& box, int x, int y, int z) {
    int cx = std::clamp(x, box.minX, box.maxX);
    int cy = std::clamp(y, box.minY, box.maxY);
    int cz = std::clamp(z, box.minZ, box.maxZ);
    return std::max(std::max(std::abs(cx - x), std::abs(cy - y)), std::abs(cz - z));
}

bool placementIsBlocked(int x, int y, int z, BlockBox const& startBox, std::vector<ClearancePiece> const& pieces, float chunkClearanceRadius, int additionalClearance) {
    if (chunkClearanceRadius <= 0) {
        for (auto const& piece : pieces) {
            if (piece.utilityAllowsFeatures) continue;
            if (greatestAxalDistance(piece.box, x, y, z) <= additionalClearance) return true;
        }
        return false;
    }
    auto  c    = boxCenter(startBox);
    float size = chunkClearanceRadius * 16.0f + static_cast<float>(additionalClearance);
    return static_cast<float>(std::abs(x - c.x)) < size && static_cast<float>(std::abs(z - c.z)) < size;
}

TerrainAdjustment terrainAdjustmentOf(std::string const& name) {
    if (name == "bury") return TerrainAdjustment::Bury;
    if (name == "beard_thin") return TerrainAdjustment::BeardThin;
    if (name == "beard_box") return TerrainAdjustment::BeardBox;
    if (name == "encapsulate") return TerrainAdjustment::Encapsulate;
    return TerrainAdjustment::None;
}

BlockBox adjustBoundingBox(BlockBox const& box, TerrainAdjustment adjustment) {
    if (adjustment == TerrainAdjustment::None) return box;
    return {box.minX - 12, box.minY - 12, box.minZ - 12, box.maxX + 12, box.maxY + 12, box.maxZ + 12};
}

double buryContribution(double x, double y, double z) {
    double d0 = std::sqrt(x * x + y * y + z * z);
    return clampedMap(d0, 0.0, 6.0, 1.0, 0.0);
}

double beardContribution(int x, int y, int z, int height) {
    int i = x + 12;
    int j = y + 12;
    int k = z + 12;
    if (!(inKernelRange(i) && inKernelRange(j) && inKernelRange(k))) return 0.0;
    double d0 = static_cast<double>(height) + 0.5;
    double d1 = static_cast<double>(x) * x + d0 * d0 + static_cast<double>(z) * z;
    double d2 = -d0 * fastInvSqrt(d1 / 2.0) / 2.0;
    return d2 * static_cast<double>(beardKernel()[k * 24 * 24 + i * 24 + j]);
}

BoxDensity BoxDensity::make(BlockBox const& box, int dYMin, int dYMax, TerrainAdjustment adjustment) {
    return {box.minX, box.minY + dYMin, box.minZ, box.maxX, box.maxY + dYMax, box.maxZ, -4.0, 4.0, adjustment};
}

double BoxDensity::compute(int bx, int by, int bz) const {
    // Dist is zero if inside the box
    int xDist = std::max(0, std::max(minX - bx, bx - maxX));
    int zDist = std::max(0, std::max(minZ - bz, bz - maxZ));

    int distAboveBottom = by - minY;
    int yDist           = 0;
    switch (adjustment) {
    case TerrainAdjustment::Bury:
    case TerrainAdjustment::BeardThin: yDist = distAboveBottom; break;
    case TerrainAdjustment::BeardBox:
    case TerrainAdjustment::Encapsulate: yDist = std::max(0, std::max(minY - by, by - maxY)); break;
    default: break;
    }

    double value = 0.0;
    switch (adjustment) {
    case TerrainAdjustment::Bury: value = buryContribution(xDist, yDist * 0.5, zDist); break;
    case TerrainAdjustment::BeardThin:
    case TerrainAdjustment::BeardBox: value = beardContribution(xDist, yDist, zDist, distAboveBottom) * 0.8; break;
    case TerrainAdjustment::Encapsulate: value = buryContribution(xDist * 0.5, yDist * 0.5, zDist * 0.5) * 0.8; break;
    default: break;
    }
    return std::clamp(value, minValue, maxValue);
}

double combinedBoxDensity(std::vector<BoxDensity> const& boxes, int x, int y, int z) {
    double sum = 0.0;
    for (auto const& b : boxes) sum += b.compute(x, y, z);
    return sum;
}

double InvertedPyramid::compute(int x, int y, int z) const {
    int const vanillaBoxInflationFactor = 12; // Structure#adjustBoundingBox inflates by 12
    int       xSpan                     = structureBox.maxX - structureBox.minX + 1;
    int       zSpan                     = structureBox.maxZ - structureBox.minZ + 1;
    int       squareHorizontalSpan      = std::max(xSpan, zSpan) - 2 * vanillaBoxInflationFactor;
    auto      center                    = boxCenter(structureBox);
    int       y0                        = yOffset + structureBox.minY + vanillaBoxInflationFactor;

    // AbsoluteDifferenceFunction.Max(squareHorizontalSpan, centerX + 0.5F, centerZ + 0.5F)
    double cx      = static_cast<double>(static_cast<float>(center.x) + 0.5f);
    double cz      = static_cast<double>(static_cast<float>(center.z) + 0.5f);
    double absDiff = std::min(std::max(std::abs(x - cx), std::abs(z - cz)), static_cast<double>(squareHorizontalSpan));

    double invertedPyramid = yClampedGradient(y, y0 - 1, y0 + squareHorizontalSpan, 1.0, -1.0 - squareHorizontalSpan) + absDiff;
    double shifted         = static_cast<double>(-(squareHorizontalSpan >> 1) - horizontalPadding) + invertedPyramid;
    return std::min(0.0, std::max(yClampedGradient(y, y0 - 1, y0, 1.0, -1.0), shifted));
}

// ---- WorldUtil#adjustForTerrain ---------------------------------------------------------------------

namespace {

/// Java's (int) cast of a float: NaN is 0, out of range saturates.
int javaF2I(float f) {
    if (std::isnan(f)) return 0;
    if (f >= 2147483647.0f) return std::numeric_limits<int>::max();
    if (f <= -2147483648.0f) return std::numeric_limits<int>::min();
    return static_cast<int>(f);
}

/// Mth#floor(float).
int mthFloor(float f) {
    int i = javaF2I(f);
    return f < static_cast<float>(i) ? i - 1 : i;
}

} // namespace

int lerpDiscrete(float delta, int start, int end) {
    int i = end - start;
    return start + mthFloor(delta * static_cast<float>(i - 1)) + (delta > 0.0f ? 1 : 0);
}

std::vector<std::pair<int, int>> terrainSampleColumns(int xMin, int zMin, int xMax, int zMax, int gridLength) {
    std::vector<std::pair<int, int>> out;
    int                              subDivisions = gridLength - 1;
    for (int zStep = 0; zStep <= subDivisions; ++zStep) {
        int zPos = lerpDiscrete(static_cast<float>(zStep) / static_cast<float>(subDivisions), zMin, zMax);
        for (int xStep = 0; xStep <= subDivisions; ++xStep) {
            int xPos = lerpDiscrete(static_cast<float>(xStep) / static_cast<float>(subDivisions), xMin, xMax);
            out.emplace_back(xPos, zPos);
        }
    }
    return out;
}

int weightedTerrainHeight(std::vector<int> heights) {
    if (heights.empty()) return 0; // Java: Math.round(NaN) = 0
    std::sort(heights.begin(), heights.end(), std::greater<>()); // highest to lowest
    double weightedSum = 0, totalWeight = 0;
    for (std::size_t i = 0; i < heights.size(); ++i) {
        double weight = static_cast<double>(i + 1);
        weightedSum += weight * heights[i];
        totalWeight += weight;
    }
    // (int) Math.round(double): half up
    double mean    = weightedSum / totalWeight;
    double rounded = std::floor(mean);
    if (mean - rounded >= 0.5) rounded += 1.0;
    return static_cast<int>(static_cast<std::int64_t>(rounded));
}

std::optional<int> adjustForTerrain(int xMin, int zMin, int xMax, int zMax, int gridLength, ColumnHeightFn const& height) {
    if (!height) return std::nullopt;
    std::vector<int> heights;
    for (auto const& [x, z] : terrainSampleColumns(xMin, zMin, xMax, zMax, gridLength)) {
        auto h = height(x, z);
        if (!h) return std::nullopt;
        heights.push_back(*h);
    }
    return weightedTerrainHeight(std::move(heights));
}

std::optional<int> adjustForTerrainAroundChunk(int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength, ColumnHeightFn const& height) {
    int chunkOriginX = xInCenterChunk & ~0b1111;
    int chunkOriginZ = zInCenterChunk & ~0b1111;
    return adjustForTerrain(chunkOriginX - radiusFromCenterChunk, chunkOriginZ - radiusFromCenterChunk, chunkOriginX + 15 + radiusFromCenterChunk,
                            chunkOriginZ + 15 + radiusFromCenterChunk, gridLength, height);
}

} // namespace gf::core::landmark
