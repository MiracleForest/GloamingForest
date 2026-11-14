// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/SqrtDensityFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/TanhHillFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/TerrainColumn.java
//   src/main/java/gloamingforest/world/components/layer/BiomeDensitySource.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U2 (terrain): upstream's own density function types and the biome terrain blending
// (BiomeDensitySource#sampleTerrain), bit-exact with Java's float / double split (pure C++, gf-core;
// gtested in tests/terrain). The box density (BoxDensityFunction) is F6's
// core/landmark/Clearance.h (combinedBoxDensity); everything else of the package is here.

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::core::terrain {

// ---- Java Mth (1.21.1) ------------------------------------------------------------------------------
namespace jmth {
/// Java's f2i: truncation toward zero, saturating, NaN -> 0.
[[nodiscard]] int   f2i(float v);
[[nodiscard]] int   d2i(double v);
[[nodiscard]] float sin(float v); ///< Mth.sin (65536-entry table)
[[nodiscard]] float cos(float v); ///< Mth.cos
[[nodiscard]] inline float sqrt(float v);
inline constexpr float kPi = 3.1415927F; ///< Mth.PI
[[nodiscard]] constexpr float clamp(float v, float lo, float hi) { return v < lo ? lo : (v < hi ? v : hi); }
[[nodiscard]] constexpr float clampedLerp(float a, float b, float t) { return t < 0.0F ? a : (t > 1.0F ? b : a + t * (b - a)); }
[[nodiscard]] constexpr float inverseLerp(float v, float a, float b) { return (v - a) / (b - a); }
[[nodiscard]] constexpr float clampedMap(float v, float inMin, float inMax, float outMin, float outMax) {
    return clampedLerp(outMin, outMax, inverseLerp(v, inMin, inMax));
}
} // namespace jmth

// ---- density function types ---------------------------------------------------------------------------
/// gloamingforest:focused (FocusedDensityFunction), all float.
struct Focused {
    float centerX = 0, bottomY = 0, centerZ = 0, radius = 1, nearValue = 0, farValue = 0;
    /// FocusedDensityFunction.fromPos: the block's center.
    [[nodiscard]] static Focused fromPos(int x, int y, int z, float radius, float nearValue, float farValue);
    [[nodiscard]] double compute(int blockX, int blockY, int blockZ) const;
    [[nodiscard]] double minValue() const { return 0.0; }
    [[nodiscard]] double maxValue() const { return radius; }
};

/// gloamingforest:hollow_hill (HollowHillFunction), all float; a negative radius bowls the field.
struct HollowHill {
    float centerX = 0, bottomY = 0, centerZ = 0, radius = 1, heightScale = 1;
    [[nodiscard]] static HollowHill fromPos(int x, int y, int z, float radius, float heightScale);
    [[nodiscard]] double compute(int blockX, int blockY, int blockZ) const;
    [[nodiscard]] double compute(float dX, float dY, float dZ) const;
};

/// gloamingforest:coord_min / coord_max (AbsoluteDifferenceFunction.Min / .Max).
struct AbsoluteDifference {
    bool   isMax = false;
    double max = 0, centerX = 0, centerZ = 0;
    [[nodiscard]] double compute(int blockX, int blockZ) const;
};

/// TanhHillFunction (the fallen trunk's mounds; no registry id).
struct TanhHill {
    float centerX = 0, bottomY = 0, centerZ = 0, radius = 1, heightScale = 1;
    float cosAngleBiasDirection = 1, sinAngleBiasDirection = 0;
    bool  isXOriented = false, isOnRightSide = false;
    /// The constructor taking the angle: Mth.cos / Mth.sin of it are stored.
    [[nodiscard]] static TanhHill withAngle(float centerX, float bottomY, float centerZ, float radius, float heightScale, float angleBiasDirection,
                                            bool isXOriented, bool isOnRightSide);
    [[nodiscard]] double compute(int blockX, int blockY, int blockZ) const;
    [[nodiscard]] double compute(float dX, float dY, float dZ) const;
};

// ---- biome terrain ----------------------------------------------------------------------------------
/// BiomeDensitySource.DensityData.
struct DensityData {
    double depth = 0, scale = 0;
};

/// A TerrainColumn's terrain numbers (its depth / scale / weight are constant density functions in
/// upstream's data) and its vertical biome layers.
struct TerrainColumn {
    std::string                   keyBiome;
    double                        depth = 0, scale = 0, weight = 0;
    std::vector<std::pair<double, std::string>> layers; ///< ascending keys (Double2ObjectAVLTreeMap order)
    /// TerrainColumn#getBiome(quartY): the layer whose key is nearest to the quart y (ties keep the
    /// lower key); no layers: the key biome.
    [[nodiscard]] std::string const& biomeAt(int quartY) const;
};

/// The biome_terrain_data entry (BiomeDensitySource): the columns by key biome.
class TerrainGrid {
public:
    /// From biome_terrain_data JSON ({"biome_landscape": [{key_biome, depth, scale, weight, biome_layers}]}).
    /// A depth / scale / weight that is not a plain number makes the column invalid (skipped).
    [[nodiscard]] static TerrainGrid fromJson(nlohmann::json const& json);
    [[nodiscard]] TerrainColumn const* column(std::string const& keyBiome) const;
    [[nodiscard]] bool                 empty() const { return mColumns.empty(); }
    [[nodiscard]] std::size_t          size() const { return mColumns.size(); }

private:
    std::map<std::string, TerrainColumn> mColumns;
};

/// The key biome (getMainBiome) at quart coordinates.
using KeyBiomeFn = std::function<TerrainColumn const*(int quartX, int quartZ)>;

/// BiomeDensitySource#sampleTerrain(blockX, blockZ): `columnAt` answers the terrain column of a
/// quart (nullptr: absent, the cell is skipped).
[[nodiscard]] DensityData sampleTerrain(int blockX, int blockZ, KeyBiomeFn const& columnAt);

} // namespace gf::core::terrain

namespace gf::core::terrain::jmth {
inline float sqrt(float v) { return static_cast<float>(std::sqrt(static_cast<double>(v))); }
} // namespace gf::core::terrain::jmth
