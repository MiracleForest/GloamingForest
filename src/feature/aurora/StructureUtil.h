// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
//   src/main/java/twilightforest/util/RotationUtil.java (ROTATIONS)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Small Java helpers the aurora palace and mushroom tower pieces share.

#include <array>
#include <cmath>

#include "feature/structure/Structure.h"

namespace gf::feature::aurora {

/// RotationUtil.ROTATIONS (Rotation.values()).
inline constexpr std::array<structure::Rotation, 4> kRotations{structure::Rotation::None, structure::Rotation::Clockwise90,
                                                                structure::Rotation::Clockwise180, structure::Rotation::Counterclockwise90};

/// Math.round(float).
[[nodiscard]] inline int javaRound(float f) { return static_cast<int>(std::floor(f + 0.5F)); }
/// Mth.sqrt(float).
[[nodiscard]] inline float javaSqrt(float f) { return static_cast<float>(std::sqrt(static_cast<double>(f))); }

/// java.awt.Rectangle#intersects of the (minX, minZ, maxX - minX, maxZ - minZ) rectangles TF builds
/// from two boxes (an empty width or height never intersects).
[[nodiscard]] inline bool awtIntersects(structure::BlockBox const& a, structure::BlockBox const& b) {
    long long tw = a.maxX - a.minX, th = a.maxZ - a.minZ, rw = b.maxX - b.minX, rh = b.maxZ - b.minZ;
    if (rw <= 0 || rh <= 0 || tw <= 0 || th <= 0) return false;
    long long tx = a.minX, ty = a.minZ, rx = b.minX, ry = b.minZ;
    rw += rx;
    rh += ry;
    tw += tx;
    th += ty;
    return (rw < rx || rw > tx) && (rh < ry || rh > ty) && (tw < tx || tw > rx) && (th < ty || th > ry);
}

/// IceTowerWingComponent#isOutOfRange / MushroomTowerWingComponent#isOutOfRange.
[[nodiscard]] inline bool isOutOfRange(structure::BlockBox const& sbb, int nx, int nz, int range) {
    int centerX = sbb.minX + (sbb.maxX - sbb.minX + 1) / 2;
    int centerZ = sbb.minZ + (sbb.maxZ - sbb.minZ + 1) / 2;
    return std::abs(nx - centerX) > range || std::abs(nz - centerZ) > range;
}

/// BoundingBoxUtils.getComponentToAddBoundingBox(..., centerBounds = false).
[[nodiscard]] inline structure::BlockBox componentToAddBoundingBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ,
                                                                   structure::Orientation dir) {
    using structure::BlockBox;
    using structure::Orientation;
    switch (dir) {
    case Orientation::West: return BlockBox::of(x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX);
    case Orientation::North: return BlockBox::of(x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ);
    case Orientation::East: return BlockBox::of(x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX);
    default: return BlockBox::of(x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ);
    }
}

} // namespace gf::feature::aurora
