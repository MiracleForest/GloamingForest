// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/YetiCaveStructure.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudComponent.java
//   src/main/java/gloamingforest/entity/projectile/FallingIce.java
//   src/main/java/gloamingforest/block/TrollsteinnBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent maths of U27 (caves): the yeti cave's structure terraformer, the troll
// caves' ellipsoidal hollowing, the cloud castle's procedural cloud, the falling ice damage and the
// trollsteinn light rules. gtested in tests/caves.

#include <cstdint>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::caves {

/// AbsoluteDifferenceFunction.Min / .Max at a block (centre x / z, cap `max`).
[[nodiscard]] double absDiffMin(double max, int centerX, int centerZ, int x, int z);
[[nodiscard]] double absDiffMax(double max, int centerX, int centerZ, int x, int z);
/// DensityFunctions.yClampedGradient(fromY, toY, fromValue, toValue) at block y.
[[nodiscard]] double yClampedGradient(int y, int fromY, int toY, double fromValue, double toValue);

/// YetiCaveStructure#getStructureTerraformer at (x, y, z): the start box's centre x / z (BoundingBox
/// #getCenter; the riser only moves its y, which the function never reads).
[[nodiscard]] double yetiCaveTerraformer(int centerX, int centerZ, int x, int y, int z);

/// TrollCaveMainComponent#hollowCaveMiddle's distance of piece-local (x, y, z) inside
/// (minX..maxX, minY..maxY, minZ..maxZ): sqrt(ex * ey * ez), ey doubled from the floor.
[[nodiscard]] double hollowDistance(int x, int y, int z, int minX, int minY, int minZ, int maxX, int maxY, int maxZ);

/// One 1 x 1 column of a CloudComponent cloud in a chunk (chunk-local x / z).
struct CloudColumn {
    int  lx = 0, lz = 0;
    int  y     = 0;     ///< the top (maxY of the piece, + 1 for some fringe cells)
    int  depth = 4;
    bool full  = false; ///< top + fluffy body + wispy bottom; else the wispy fringe only
};
/// CloudComponent#generateCloud for chunk (chunkX, chunkZ) with the centre offset (hx, hz): the
/// columns to fill (only air is replaced), drawing one nextFloat per 4 x 4 cell from `random`.
/// `centerChunk`: hx == 0 && hz == 0 (fluffy tops).
[[nodiscard]] std::vector<CloudColumn> generateCloud(int chunkX, int chunkZ, int hx, int hz, int cloudHeight, jrand::RandomSource& random);

/// FallingIce#causeFallDamage: the damage of a fall of `distance` blocks at difficulty 0..3
/// (negative: no damage is dealt).
[[nodiscard]] float fallingIceDamage(float distance, int difficulty);

/// TrollsteinnBlock.LIGHT_THRESHOLD: a face is lit when its neighbour's raw brightness exceeds it.
inline constexpr int kTrollsteinnLightThreshold = 7;
/// UnripeTorchClusterBlock.RIPEN_THRESHOLD.
inline constexpr int kRipenThreshold = 6;
/// Level#getMaxLocalRawBrightness from the light values: max(block, sky - darken).
[[nodiscard]] int rawBrightness(int blockLight, int skyLight, int skyDarken);

} // namespace gf::core::caves
