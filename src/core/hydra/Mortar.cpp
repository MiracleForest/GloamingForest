// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/projectile/HydraMortar.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
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

} // namespace gf::core::hydra