// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/landmarks/LegacyLandmarkPlacements.java
//   src/main/java/twilightforest/util/iterators/XZQuadrantIterator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The landmark grid beyond the contract functions of core/landmark/Landmark.h (F6, pure, gtested):
// Java's float rounding of chunk coordinates, the biome table, the variety list, the center
// scanner used by locate, and java.util.Random (LegacyRandomSource) for the variety pick. Never
// include game headers here (gf-core).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/landmark/Landmark.h"

namespace gf::core::landmark {

/// net.minecraft.world.level.levelgen.LegacyRandomSource (java.util.Random's LCG, Minecraft's
/// BitRandomSource helpers; bit exact). The same source as F5's core/jrand LegacyRandom: the grid
/// keeps this small copy only while core/jrand is F0's GF_STUB on main (its next() returns 0, which
/// would break the oracle goldens). tests/landmark/OracleGridTest.cpp cross-checks the two draw for
/// draw as soon as jrand is real, so the swap to jrand::LegacyRandom is mechanical (A8, rule R18).
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }
    void setSeed(std::int64_t seed);
    /// java.util.Random#next(bits).
    [[nodiscard]] int next(int bits);
    [[nodiscard]] int nextInt();
    /// [0, bound), Java's rejection sampling (bound > 0).
    [[nodiscard]] int nextInt(int bound);
    [[nodiscard]] std::int64_t nextLong();
    [[nodiscard]] float  nextFloat();
    [[nodiscard]] double nextDouble();

private:
    std::uint64_t mSeed = 0;
};

/// Java's `Math.round(chunk / 16F)`: the 16-chunk region (256 blocks) a chunk coordinate rounds to.
[[nodiscard]] int roundRegion(int chunk);

/// LegacyLandmarkPlacements#blockNearLandmarkCenter (Java's precedence kept: `blockX >> 4 + x` is
/// `blockX >> (4 + x)` with the shift distance masked to 5 bits).
[[nodiscard]] bool blockNearCenter(int blockX, int blockZ, int range);
/// LegacyLandmarkPlacements#blockIsInLandmarkCenter.
[[nodiscard]] bool blockIsInCenter(int blockX, int blockZ);

/// The block column whose biome picks the landmark of a chunk's 256-block region
/// (pickLandmarkForChunk: the region center + 8; LandmarkStructure#findValidGenerationPoint
/// samples the same column through the main biome quart (round << 6) + 2).
[[nodiscard]] ColumnXZ biomeSampleColumn(int chunkX, int chunkZ);
/// BIOME_2_STRUCTURES: the landmark of a biome ("" when the biome has none).
[[nodiscard]] std::string biomeLandmark(std::string const& biomeId);
/// Every BIOME_2_STRUCTURES row (biome, landmark).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> const& biomeLandmarks();
/// VARIETY_LANDMARKS (key, weight), in TF's order.
[[nodiscard]] std::vector<std::pair<std::string, int>> const& varietyLandmarks();

/// LegacyLandmarkPlacements#landmarkCenterScanner: the landmark centers (block columns) around
/// `blockX, blockZ` in TF's quadrant order (XZQuadrantIterator over 16-chunk cells, center
/// first), `gridSearchRadius` cells out.
[[nodiscard]] std::vector<ColumnXZ> centerScanner(int blockX, int blockZ, int gridSearchRadius);

/// The landmark keys TF declares (TFStructures) that have no structure in 4.8: no descriptor.
[[nodiscard]] bool isUnimplementedKey(std::string const& key);

} // namespace gf::core::landmark
