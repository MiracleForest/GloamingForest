// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/HollowHillStructure.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/landmark/Spawning.h"

#include <cmath>

namespace gf::core::landmark {

namespace {

/// Mth#inverseLerp(float delta, float start, float end): canSpawnMob's int arguments widen to float
/// (of Mth's float and double overloads the float one is the most specific).
float inverseLerp(float delta, float start, float end) { return (delta - start) / (end - start); }

/// Mth#length(double, double, double) = Math.sqrt(Mth#lengthSquared(double, double, double)):
/// Minecraft 1.21.1 has no float overload with three arguments, so canSpawnMob's floats widen to
/// double. One operation per statement: no fused multiply-add (Java rounds every step).
double length(double x, double y, double z) {
    double xx  = x * x;
    double yy  = y * y;
    double zz  = z * z;
    double sum = xx + yy;
    sum        = sum + zz;
    return std::sqrt(sum);
}

} // namespace

bool hollowHillCanSpawnMob(BlockBox const& startBox, int x, int y, int z) {
    // float hX = Mth.inverseLerp(x, minX, maxX) * 2 - 1, the same for z; hY without the scaling
    float hX = inverseLerp(static_cast<float>(x), static_cast<float>(startBox.minX), static_cast<float>(startBox.maxX));
    hX       = hX * 2.0f;
    hX       = hX - 1.0f;
    float hY = inverseLerp(static_cast<float>(y), static_cast<float>(startBox.minY), static_cast<float>(startBox.maxY));
    float hZ = inverseLerp(static_cast<float>(z), static_cast<float>(startBox.minZ), static_cast<float>(startBox.maxZ));
    hZ       = hZ * 2.0f;
    hZ       = hZ - 1.0f;
    // Mth.length(hX, hY, hZ) < 0.975f: a double compared with the float constant widened
    return length(hX, hY, hZ) < static_cast<double>(0.975f);
}

} // namespace gf::core::landmark
