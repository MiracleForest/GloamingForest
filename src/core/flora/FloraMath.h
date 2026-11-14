// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/PatchBlock.java (shapeFromLong)
//   src/main/java/gloamingforest/block/MasonJarBlock.java (getAnalogOutputSignal)
//   src/main/java/gloamingforest/block/FallenLeavesBlock.java (entityInside)
//   src/main/java/gloamingforest/world/components/feature/BerryBushFeature.java (chooseSize)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U6 flora: the pure arithmetic of upstream's flora blocks (no game types), shared by the unit's runtime
// and gtested in tests/flora.

#include <array>
#include <cmath>
#include <cstdint>

namespace gf::core::flora {

/// Mth.lerpDiscrete(count / maxStack, 0, 15): a mason jar's comparator output.
[[nodiscard]] inline int jarAnalog(int count, int maxStack) {
    if (count <= 0 || maxStack <= 0) return 0;
    float delta = static_cast<float>(count) / static_cast<float>(maxStack);
    return static_cast<int>(std::floor(delta * 14.0f)) + 1;
}

/// RotationSegment.convertToSegment(yaw): 16 segments of 22.5 degrees.
[[nodiscard]] inline int rotationSegment(float degrees) { return static_cast<int>(std::floor(degrees * 16.0f / 360.0f + 0.5f)) & 15; }

/// FallenLeavesBlock#entityInside: the horizontal motion factor of makeStuckInBlock (1 below 3 layers).
[[nodiscard]] inline double fallenLeavesFactor(int layers) { return layers > 2 ? 1.0 - 0.05 * (layers - 2) : 1.0; }

/// BerryBushFeature's node sizes (BushNodeSizes) in WorldUtil.getRandomElementWithWeights order.
enum class BushNode { Large, Medium, Small, Tiny };

/// WorldUtil.getRandomElementWithWeights with LARGE 1, MEDIUM 2, SMALL 4, TINY 3 for nextFloat() `r`.
[[nodiscard]] inline BushNode chooseBushNode(float r) {
    float v = r * 10.0f;
    if ((v -= 1.0f) < 0) return BushNode::Large;
    if ((v -= 2.0f) < 0) return BushNode::Medium;
    if ((v -= 4.0f) < 0) return BushNode::Small;
    return BushNode::Tiny;
}

/// PatchBlock.shapeFromLong: the patch's box in pixels {x0, z0, x1, z1} (y 0..1) for the seed and
/// the connected sides (east and west on x, south and north on z).
[[nodiscard]] inline std::array<int, 4> patchBox(std::int64_t seed, bool north, bool east, bool south, bool west) {
    auto bits  = [seed](int shift) { return static_cast<int>((static_cast<std::uint64_t>(seed) >> shift) & 3u); };
    int  xOff0 = bits(12), xOff1 = bits(15), zOff0 = bits(18), zOff1 = bits(21);
    return {west ? 0 : 1 + xOff1, north ? 0 : 1 + zOff1, east ? 16 : 15 - xOff0, south ? 16 : 15 - zOff0};
}

} // namespace gf::core::flora
