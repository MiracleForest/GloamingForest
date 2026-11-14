// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// (Minecraft 1.21.1's PotentialCalculator and SpawnState#canSpawn, which upstream's spawn costs feed.)
#include "core/biomes/SpawnCost.h"

#include <cmath>
#include <limits>

namespace gf::core::biomes {

void PotentialCalculator::addCharge(int x, int y, int z, double charge) {
    if (charge != 0.0) mCharges.push_back(Charge{x, y, z, charge});
}

double PotentialCalculator::potentialEnergyChange(int x, int y, int z, double charge) const {
    if (charge == 0.0) return 0.0;
    double d = 0.0;
    for (auto const& c : mCharges) {
        // PointCharge#getPotentialChange: Vec3i#distSqr (corner to corner, doubles)
        double dx = static_cast<double>(c.x) - x, dy = static_cast<double>(c.y) - y, dz = static_cast<double>(c.z) - z;
        double sq = dx * dx + dy * dy + dz * dz;
        d        += sq == 0.0 ? std::numeric_limits<double>::infinity() : c.charge / std::sqrt(sq);
    }
    return d * charge;
}

bool withinBudget(PotentialCalculator const& potential, int x, int y, int z, double charge, double energyBudget) {
    return potential.potentialEnergyChange(x, y, z, charge) <= energyBudget;
}

} // namespace gf::core::biomes
