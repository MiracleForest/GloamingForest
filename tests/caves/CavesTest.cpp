// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of U27's game-independent maths (src/core/caves): the yeti cave terraformer, the troll
// cave hollowing distance, the cloud castle's procedural cloud, the falling ice damage and the
// raw brightness the trollsteinn faces read.
#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include "core/caves/Caves.h"
#include "core/jrand/JRand.h"

namespace cc = gf::core::caves;

TEST(CavesTerraformer, AbsoluteDifferenceFunctions) {
    EXPECT_DOUBLE_EQ(cc::absDiffMin(64, 0, 0, 10, -3), 3.0);
    EXPECT_DOUBLE_EQ(cc::absDiffMax(64, 0, 0, 10, -3), 10.0);
    EXPECT_DOUBLE_EQ(cc::absDiffMax(40, 0, 0, 100, 0), 40.0); // capped
    EXPECT_DOUBLE_EQ(cc::yClampedGradient(-5, 0, 29, 40, 32), 40.0);
    EXPECT_DOUBLE_EQ(cc::yClampedGradient(29, 0, 29, 40, 32), 32.0);
    EXPECT_NEAR(cc::yClampedGradient(10, 0, 29, 40, 32), 40.0 - 8.0 * 10.0 / 29.0, 1e-12);
}

TEST(CavesTerraformer, ShapesTheCave) {
    // far outside the 40-block pyramid: nothing
    EXPECT_DOUBLE_EQ(cc::yetiCaveTerraformer(0, 0, 100, 0, 100), 0.0);
    // the room in the middle is carved (negative density)
    EXPECT_LT(cc::yetiCaveTerraformer(0, 0, 0, 10, 0), 0.0);
    // centre at y 10, by hand: delta = 0.020833334f * (40 - 80 / 29), max = 16 * (15 - 46 - 36 / 29) / 64
    double exterior = 40.0 - 8.0 * 10.0 / 29.0;
    double delta    = static_cast<double>(0.020833334F) * exterior;
    double slope    = -46.0 - 2.0 * 18.0 / 29.0;
    double expected = delta * 16.0 * (static_cast<double>(1 / 64.0F) * (15.0 + slope));
    EXPECT_NEAR(cc::yetiCaveTerraformer(0, 0, 0, 10, 0), expected, 1e-9);
    // a diagonal wall 30 blocks out (off the + shaped entrances) is solid
    EXPECT_GT(cc::yetiCaveTerraformer(0, 0, 30, 10, 30), 0.0);
    // the entrance along the x axis stays open
    EXPECT_LT(cc::yetiCaveTerraformer(0, 0, 30, 10, 0), 0.0);
    // the roof cuts everything above y 27
    EXPECT_DOUBLE_EQ(cc::yetiCaveTerraformer(0, 0, 30, 40, 30), 0.0);
    // the centre only moves it
    EXPECT_DOUBLE_EQ(cc::yetiCaveTerraformer(500, -300, 530, 10, -270), cc::yetiCaveTerraformer(0, 0, 30, 10, 30));
}

TEST(CavesTrollCave, HollowDistance) {
    // a 30 x 20 x 30 main cave: (0..29, 0..19, 0..29)
    EXPECT_DOUBLE_EQ(cc::hollowDistance(0, 5, 5, 0, 0, 0, 29, 19, 29), 0.0); // on the wall
    EXPECT_DOUBLE_EQ(cc::hollowDistance(14, 6, 14, 0, 0, 0, 29, 19, 29), std::sqrt(14.0 * 12.0 * 14.0));
    // the floor counts double: y 1 is as far as y 17 is from the ceiling (1 * 2 = 2)
    EXPECT_DOUBLE_EQ(cc::hollowDistance(10, 1, 10, 0, 0, 0, 29, 19, 29), cc::hollowDistance(10, 17, 10, 0, 0, 0, 29, 19, 29));
}

TEST(CavesClouds, DeterministicColumns) {
    gf::core::jrand::LegacyRandom a(42), b(42);
    auto                          ca = cc::generateCloud(3, -2, 0, 0, 180, a);
    auto                          cb = cc::generateCloud(3, -2, 0, 0, 180, b);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); i++) {
        EXPECT_EQ(ca[i].lx, cb[i].lx);
        EXPECT_EQ(ca[i].y, cb[i].y);
        EXPECT_EQ(ca[i].depth, cb[i].depth);
    }
    // the centre chunk of the castle (hx = hz = 0) is cloud all over (every cell within 7 of a centre)
    EXPECT_EQ(ca.size(), 256u);
    std::set<std::pair<int, int>> cols;
    for (auto const& c : ca) {
        cols.insert({c.lx, c.lz});
        EXPECT_TRUE(c.full);
        EXPECT_EQ(c.y, 180); // no raised fringe in the centre chunk
        EXPECT_GE(c.depth, 4);
        EXPECT_LE(c.depth, 6);
    }
    EXPECT_EQ(cols.size(), 256u);
    // far from the centre: nothing
    gf::core::jrand::LegacyRandom r(1);
    EXPECT_TRUE(cc::generateCloud(3, -2, 64, 64, 180, r).empty());
}

TEST(CavesFallingIce, Damage) {
    EXPECT_LT(cc::fallingIceDamage(4.0F, 3), 0.0F); // ceil(-1) < 0: no damage
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(5.0F, 3), 0.0F);
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(10.0F, 2), 5.0F);
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(10.0F, 3), 10.0F);
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(10.0F, 1), 2.0F); // floor(5 * 0.5)
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(10.5F, 0), 0.0F);
    EXPECT_FLOAT_EQ(cc::fallingIceDamage(300.0F, 3), 100.0F); // capped
}

TEST(CavesTrollsteinn, RawBrightness) {
    EXPECT_EQ(cc::rawBrightness(3, 15, 11), 4);
    EXPECT_EQ(cc::rawBrightness(12, 15, 0), 15);
    EXPECT_EQ(cc::rawBrightness(0, 0, 0), 0);
    EXPECT_GT(cc::rawBrightness(8, 0, 0), cc::kTrollsteinnLightThreshold);
    EXPECT_FALSE(cc::rawBrightness(7, 0, 0) > cc::kTrollsteinnLightThreshold);
}
