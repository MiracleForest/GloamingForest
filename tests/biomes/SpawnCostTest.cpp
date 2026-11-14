// U1 biomes: the spawn potential of upstream's spawn costs (Java's PotentialCalculator, SpawnState#canSpawn).

#include <gtest/gtest.h>

#include <cmath>

#include "core/biomes/SpawnCost.h"

using gf::core::biomes::PotentialCalculator;
using gf::core::biomes::withinBudget;

TEST(BiomeSpawnCost, EmptyPotentialAlwaysAllows) {
    PotentialCalculator p;
    EXPECT_EQ(p.potentialEnergyChange(0, 64, 0, 0.7), 0.0);
    EXPECT_TRUE(withinBudget(p, 0, 64, 0, 0.7, 0.15));
}

TEST(BiomeSpawnCost, ChargeOverDistance) {
    PotentialCalculator p;
    p.addCharge(0, 0, 0, 0.7);
    p.addCharge(3, 4, 0, 0.0); // a zero charge is not kept
    EXPECT_EQ(p.size(), 1u);
    // distance 5 (corner to corner): 0.7 / 5 * 0.7
    EXPECT_DOUBLE_EQ(p.potentialEnergyChange(3, 4, 0, 0.7), 0.7 / 5.0 * 0.7);
    // the swamp's zombie (charge 0.7, budget 0.15): a zombie within 0.49 / 0.15 = 3.27 blocks forbids it
    EXPECT_FALSE(withinBudget(p, 3, 0, 0, 0.7, 0.15));
    EXPECT_TRUE(withinBudget(p, 4, 0, 0, 0.7, 0.15));
    // the charge's own block: infinite
    EXPECT_TRUE(std::isinf(p.potentialEnergyChange(0, 0, 0, 0.7)));
    EXPECT_FALSE(withinBudget(p, 0, 0, 0, 0.7, 0.15));
    // a type without charge never changes the energy
    EXPECT_EQ(p.potentialEnergyChange(0, 0, 0, 0.0), 0.0);
}

TEST(BiomeSpawnCost, ChargesAdd) {
    PotentialCalculator p;
    for (int i = 0; i < 10; ++i) p.addCharge(100 * (i + 1), 0, 0, 0.75);
    double sum = 0;
    for (int i = 0; i < 10; ++i) sum += 0.75 / (100.0 * (i + 1));
    EXPECT_DOUBLE_EQ(p.potentialEnergyChange(0, 0, 0, 0.8), sum * 0.8);
    p.clear();
    EXPECT_EQ(p.size(), 0u);
}
