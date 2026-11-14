// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U1 `biomes`, pure part: the spawn costs of upstream's biomes (BiomeHelper's addMobCharge, Java's
// MobSpawnSettings.MobSpawnCost) as Minecraft 1.21.1's natural spawner applies them: the spawn
// potential (PotentialCalculator) of the mobs already there, each adding its type's charge in its
// biome at its block, and NaturalSpawner.SpawnState#canSpawn: a mob type with a cost in the biome
// at a position may spawn there while its charge times the potential stays within its energy budget.

#include <vector>

namespace gf::core::biomes {

/// Java's PotentialCalculator: point charges at block positions.
class PotentialCalculator {
public:
    /// PotentialCalculator#addCharge (a zero charge is not kept).
    void addCharge(int x, int y, int z, double charge);
    /// PotentialCalculator#getPotentialEnergyChange: `charge` times the sum of charge / distance
    /// over the charges (a charge at the position itself: infinity; BlockPos#distSqr of the corners).
    [[nodiscard]] double potentialEnergyChange(int x, int y, int z, double charge) const;
    void                 clear() { mCharges.clear(); }
    [[nodiscard]] std::size_t size() const { return mCharges.size(); }

private:
    struct Charge {
        int    x, y, z;
        double charge;
    };
    std::vector<Charge> mCharges;
};

/// NaturalSpawner.SpawnState#canSpawn for a type with a spawn cost (charge, energyBudget).
[[nodiscard]] bool withinBudget(PotentialCalculator const& potential, int x, int y, int z, double charge, double energyBudget);

} // namespace gf::core::biomes
