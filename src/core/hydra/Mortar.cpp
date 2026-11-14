// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/projectile/HydraMortar.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/hydra/Mortar.h"

#include <algorithm>
#include <cmath>

namespace gf::core::hydra {

Box mortarBox(V3 pos, double inflate) {
    double h = kMortarSize / 2.0;
    return Box{
        V3{pos.x - h - inflate, pos.y - inflate, pos.z - h - inflate},
        V3{pos.x + h + inflate, pos.y + kMortarSize + inflate, pos.z + h + inflate}
    };
}

bool segmentHits(Box const& box, V3 from, V3 to) {
    double const a[3]  = {from.x, from.y, from.z};
    double const b[3]  = {to.x, to.y, to.z};
    double const lo[3] = {box.min.x, box.min.y, box.min.z};
    double const hi[3] = {box.max.x, box.max.y, box.max.z};
    double       t0 = 0.0, t1 = 1.0;
    for (int i = 0; i < 3; ++i) {
        double d = b[i] - a[i];
        if (std::abs(d) < 1.0E-12) {
            if (a[i] < lo[i] || a[i] > hi[i]) return false;
            continue;
        }
        double u = (lo[i] - a[i]) / d;
        double v = (hi[i] - a[i]) / d;
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return false;
    }
    return true;
}

namespace {
double length(V3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
} // namespace

Segment projectilePath(V3 pos, V3 delta, V3 facing) {
    double step = length(delta);
    V3     dir  = delta;
    double len  = step;
    if (step < 1.0E-4) {
        dir = facing;
        len = length(facing);
        if (len < 1.0E-9) return Segment{pos, pos};
    }
    dir         = V3{dir.x / len, dir.y / len, dir.z / len};
    double span = kMortarPickRadius + kMortarSize + step;
    return Segment{
        V3{pos.x - dir.x * span, pos.y - dir.y * span, pos.z - dir.z * span},
        V3{pos.x + dir.x * span, pos.y + dir.y * span, pos.z + dir.z * span}
    };
}

V3 projectileLook(V3 motion) {
    double h = std::sqrt(motion.x * motion.x + motion.z * motion.z);
    if (h < 1.0E-12 && std::abs(motion.y) < 1.0E-12) return V3{};
    float xRot = static_cast<float>(std::atan2(motion.y, h) * 180.0 / 3.141592653589793);
    float yRot = static_cast<float>(std::atan2(motion.x, motion.z) * 180.0 / 3.141592653589793);
    return viewVector(xRot, yRot);
}

bool projectileReachesJavaBox(V3 mortarFeet, V3 pos, V3 delta, V3 facing) {
    auto s = projectilePath(pos, delta, facing);
    return segmentHits(mortarBox(mortarFeet, kProjectileInflate), s.from, s.to);
}

} // namespace gf::core::hydra