// GloamingForest - U21 (lich): gtests of src/core/lich/TowerMath.

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "core/jrand/JRand.h"
#include "core/lich/TowerMath.h"

using namespace gf::core::lich;

TEST(LichTowerMath, BlockPosAsLong) {
    EXPECT_EQ(blockPosAsLong(0, 0, 0), 0);
    EXPECT_EQ(blockPosAsLong(1, 0, 0), std::int64_t{1} << 38);
    EXPECT_EQ(blockPosAsLong(0, 1, 0), 1);
    EXPECT_EQ(blockPosAsLong(0, 0, 1), std::int64_t{1} << 12);
    // negative coordinates keep their low bits (Java's packing)
    EXPECT_EQ(blockPosAsLong(-1, -1, -1), -1);
}

TEST(LichTowerMath, MthTables) {
    EXPECT_NEAR(mthSin(0.0F), 0.0F, 1e-6F);
    EXPECT_NEAR(mthCos(0.0F), 1.0F, 1e-6F);
    EXPECT_NEAR(mthSin(1.5707964F), 1.0F, 1e-4F);
    EXPECT_NEAR(mthCos(3.1415927F), -1.0F, 1e-4F);
    EXPECT_EQ(javaRound(0.5F), 1);
    EXPECT_EQ(javaRound(-0.5F), 0); // ties toward +infinity
    EXPECT_EQ(javaRound(-1.5F), -1);
    EXPECT_FLOAT_EQ(clampedLerp(7.0F, 11.0F, 0.5F), 9.0F);
    EXPECT_FLOAT_EQ(clampedLerp(7.0F, 11.0F, -1.0F), 7.0F);
}

TEST(LichTowerMath, BossRoomCandles) {
    auto spots = bossRoomCandles(100, 80, -200);
    ASSERT_EQ(spots.size(), 20u);
    for (auto const& s : spots) {
        double r = std::sqrt(double(s.x) * s.x + double(s.z) * s.z);
        EXPECT_GE(r, 6.0);
        EXPECT_LE(r, 11.8);
        EXPECT_GE(s.y, 0);
        EXPECT_LE(s.y, 2);
    }
    // the angle only grows (each step adds nextFloat() < 1 radian), so 20 candles go round the room
    auto again = bossRoomCandles(100, 80, -200);
    for (std::size_t i = 0; i < spots.size(); ++i) {
        EXPECT_EQ(spots[i].x, again[i].x);
        EXPECT_EQ(spots[i].z, again[i].z);
    }
    // the first candle follows the seeded java.util.Random draws
    gf::core::jrand::LegacyRandom r(0);
    r.setSeed(blockPosAsLong(100, 80, -200));
    float angle = r.nextFloat();
    float range = clampedLerp(7.0F, 11.0F, r.nextFloat());
    EXPECT_EQ(spots[0].x, javaRound(mthCos(angle) * range));
    EXPECT_EQ(spots[0].z, javaRound(mthSin(angle) * range));
    EXPECT_EQ(spots[0].y, r.nextInt(3));
}

TEST(LichTowerMath, StairDecay) {
    EXPECT_EQ(stairDecayLevel(1), -1);
    EXPECT_EQ(stairDecayLevel(2), 0); // ceil(-0.5) = 0
    EXPECT_EQ(stairDecayLevel(3), 0);
    EXPECT_EQ(stairDecayLevel(4), 1);
    EXPECT_EQ(stairDecayLevel(5), 1);
    EXPECT_EQ(stairDecayLevel(17), 7);
    EXPECT_EQ(stairDecayLevel(40), 7);
    EXPECT_FLOAT_EQ(stairDecayChance(0), 0.025F);
    EXPECT_FLOAT_EQ(stairDecayChance(7), 0.2F);
}

TEST(LichTowerMath, SegmentPlan) {
    int  calls = 0;
    auto plan  = planSegments(12, 0, [&](int, int) {
        ++calls;
        return 3;
    });
    ASSERT_EQ(plan.size(), 12u);
    EXPECT_TRUE(plan[0].mobBridge);
    EXPECT_FALSE(plan[1].mobBridge);
    EXPECT_TRUE(plan[4].mobBridge); // 3, 2, 1, 0
    EXPECT_FALSE(plan[6].wings);    // 6 > 12 >> 1 is false
    EXPECT_TRUE(plan[7].wings);
    EXPECT_TRUE(plan[11].gallery);
    EXPECT_FALSE(plan[10].gallery);
}

#include "core/lich/Simplex.h"

// org.joml.SimplexNoise#noise(x, y, z) as LichYardBox uses it: bounded, deterministic, continuous
// and not flat.
TEST(LichSimplex, BoundedContinuousAndVaried) {
    float lo = 1.0F, hi = -1.0F;
    for (int i = 0; i < 2000; ++i) {
        float x = float(i % 37) * 0.35F - 3.0F, y = float(i % 11) * 0.7F + 1024.0F, z = float(i / 37) * 0.29F - 7.0F;
        float v = simplex3(x, y, z);
        EXPECT_LE(std::abs(v), 1.05F);
        EXPECT_EQ(v, simplex3(x, y, z));
        EXPECT_LT(std::abs(simplex3(x + 1e-3F, y, z) - v), 0.05F);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    EXPECT_LT(lo, -0.3F);
    EXPECT_GT(hi, 0.3F);
}

// At the origin the first corner's offset is zero and the other three lie beyond the 0.6 radius.
TEST(LichSimplex, ZeroAtTheOrigin) { EXPECT_EQ(simplex3(0.0F, 0.0F, 0.0F), 0.0F); }
