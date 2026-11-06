// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the weapons' pure math (src/core/weapons/Weapons.h), against TF 4.8's Java.

#include "core/weapons/Weapons.h"

#include <cmath>

#include <gtest/gtest.h>

using namespace gf::core::weapons;

TEST(Weapons, VoxelLineIncludesBothEnds) {
    auto line = voxelLine({0, 0, 0}, {5, 2, -1});
    ASSERT_EQ(line.size(), 6u); // absDx + 1
    EXPECT_EQ(line.front(), (I3{0, 0, 0}));
    EXPECT_EQ(line.back(), (I3{5, 2, -1}));
    for (std::size_t i = 1; i < line.size(); ++i) EXPECT_EQ(line[i].x - line[i - 1].x, 1);
    auto vertical = voxelLine({3, 10, 3}, {3, 0, 3});
    ASSERT_EQ(vertical.size(), 11u);
    EXPECT_EQ(vertical[1], (I3{3, 9, 3}));
    auto single = voxelLine({1, 2, 3}, {1, 2, 3});
    ASSERT_EQ(single.size(), 1u);
}

TEST(Weapons, VoxelLineJavaSteps) {
    // VoxelBresenhamIterator: x major, err1 = 2*dy - dx = 1 > 0 on the first step: y moves with x
    auto line = voxelLine({0, 0, 0}, {4, 3, 0});
    ASSERT_EQ(line.size(), 5u);
    EXPECT_EQ(line[1], (I3{1, 1, 0}));
    EXPECT_EQ(line[4], (I3{4, 3, 0}));
}

TEST(Weapons, MagnetTryOrder) {
    auto const& t = magnetTries();
    EXPECT_EQ(t[0], std::make_pair(0.0F, 0.0F));
    EXPECT_EQ(t[1], std::make_pair(10.0F, 0.0F));
    EXPECT_EQ(t[2], std::make_pair(10.0F, 10.0F));
    EXPECT_EQ(t[3], std::make_pair(0.0F, 10.0F));
    EXPECT_EQ(t[4], std::make_pair(-10.0F, 10.0F));
    EXPECT_EQ(t[5], std::make_pair(-10.0F, 0.0F));
    EXPECT_EQ(t[6], std::make_pair(-10.0F, -10.0F));
    EXPECT_EQ(t[7], std::make_pair(0.0F, -10.0F));
    EXPECT_EQ(t[8], std::make_pair(10.0F, -10.0F));
}

TEST(Weapons, OffsetLookMatchesView) {
    for (float yaw : {0.0F, 45.0F, -120.0F}) {
        for (float pitch : {0.0F, 30.0F, -60.0F}) {
            V3 a = offsetLook(yaw, pitch, 0, 0);
            V3 b = viewVector(pitch, yaw);
            EXPECT_NEAR(a.x, b.x, 1e-5);
            EXPECT_NEAR(a.y, b.y, 1e-5);
            EXPECT_NEAR(a.z, b.z, 1e-5);
        }
    }
    // yaw 0 looks south (+z), pitch 90 looks down
    EXPECT_NEAR(viewVector(0, 0).z, 1.0, 1e-6);
    EXPECT_NEAR(viewVector(90, 0).y, -1.0, 1e-6);
}

TEST(Weapons, VeinLimit) {
    // a 10x10x10 cube of ore: the vein stops at 24
    auto vein = findVein({0, 0, 0}, [](I3 const& p) { return p.x >= 0 && p.x < 10 && p.y >= 0 && p.y < 10 && p.z >= 0 && p.z < 10; });
    EXPECT_EQ(vein.size(), 24u);
    // a 3-block vein
    auto small = findVein({0, 0, 0}, [](I3 const& p) { return p.y == 0 && p.z == 0 && p.x >= 0 && p.x < 3; });
    EXPECT_EQ(small.size(), 3u);
    auto none = findVein({0, 0, 0}, [](I3 const&) { return false; });
    EXPECT_TRUE(none.empty());
}

TEST(Weapons, BowPower) {
    EXPECT_FLOAT_EQ(bowPower(0), 0.0F);
    EXPECT_FLOAT_EQ(bowPower(20), 1.0F);
    EXPECT_FLOAT_EQ(bowPower(100), 1.0F);
    EXPECT_NEAR(bowPower(10), (0.25F + 1.0F) / 3.0F, 1e-6);
    EXPECT_LT(bowPower(1), 0.1F); // under BowItem's 0.1 threshold: no shot
    EXPECT_GE(bowPower(2), 0.1F);
}

TEST(Weapons, SeekerSteerKeepsSpeed) {
    V3   course{2, 0, 0};
    auto s = seekerSteer(course, {1, 1, 0});
    ASSERT_TRUE(s.has_value());
    // length of course * (courseLen/totalLen) + target * (courseLen/totalLen), plus 0.045 up
    V3 t{0.8, 0.8, 0};
    double cl = 2.0, tl = std::sqrt(0.8 * 0.8 * 2), total = std::sqrt(cl * cl + tl * tl);
    EXPECT_NEAR(s->x, 2.0 * cl / total + t.x * cl / total, 1e-9);
    EXPECT_NEAR(s->y, t.y * cl / total + 0.045, 1e-6);
    EXPECT_FALSE(seekerSteer(course, {-1, 0, 0}).has_value()); // behind: give up
    EXPECT_FALSE(seekerSteer(course, {0, 1, 0}).has_value());  // cosine 0
}

TEST(Weapons, SeekerBox) {
    auto [lo, hi] = seekerBox({0, 0, 0}, {0, 0, 2});
    EXPECT_NEAR(hi.z, 10 * std::cos(3.141592653589793 / 6), 1e-5);
    EXPECT_NEAR(lo.x, -5.0, 1e-5);
    EXPECT_NEAR(hi.x, 5.0, 1e-5);
    EXPECT_DOUBLE_EQ(lo.y, -2.5);
    EXPECT_DOUBLE_EQ(hi.y, 2.5);
}

TEST(Weapons, ChainBounce) {
    V3 v = chainBounce({1, -1, 0.5}, 1); // hit the top of a block while falling
    EXPECT_NEAR(v.x, 0.6, 1e-9);
    EXPECT_NEAR(v.y, 0.36, 1e-9);
    EXPECT_NEAR(v.z, 0.3, 1e-9);
    V3 w = chainBounce({1, 0, 0}, 4); // hit a west face moving east
    EXPECT_NEAR(w.x, -0.36, 1e-9);
    V3 u = chainBounce({-1, 0, 0}, 4); // already moving away: only scaled
    EXPECT_NEAR(u.x, -0.6, 1e-9);
}

TEST(Weapons, ChainReturn) {
    V3 r0 = chainReturn({1, 0, 0}, {0, 0, 10}, 0, 0.05);
    EXPECT_NEAR(r0.x, 1.0, 1e-9);
    EXPECT_NEAR(r0.y, -0.05, 1e-9);
    V3 r = chainReturn({1, 0, 0}, {0, 0, 10}, 100, 0.05); // age 1: all toward the owner
    EXPECT_NEAR(r.x, 0.0, 1e-9);
    EXPECT_NEAR(r.z, 2.0, 1e-6);
}

TEST(Weapons, CubeVelocity) {
    V3 v = cubeVelocity({0, 0, 0}, {10, 0, 0});
    EXPECT_NEAR(v.x, 0.5, 1e-6);
    V3 slow = cubeVelocity({0, 0, 0}, {0.2, 0, 0});
    EXPECT_NEAR(slow.x, 0.2, 1e-9);
}

TEST(Weapons, MoonwormRepair) {
    EXPECT_EQ(moonwormRepairDamage(200, 1), 136);
    EXPECT_EQ(moonwormRepairDamage(200, 3), 8);
    EXPECT_EQ(moonwormRepairDamage(10, 1), -54); // ItemStack#setDamageValue clamps to 0
}

TEST(Weapons, CrumbleBox) {
    auto [lo, hi] = crumbleBox({10, 64, -5});
    EXPECT_EQ(lo, (I3{8, 62, -7}));
    EXPECT_EQ(hi, (I3{12, 66, -3}));
}

TEST(Weapons, AllInBoxTruncates) {
    auto p = allInBox({-1.5, 0.2, 0.0}, {0.5, 0.9, 1.0});
    // (int) -1.5 = -1: x -1..0, y 0..0, z 0..1
    EXPECT_EQ(p.size(), 4u);
    EXPECT_EQ(p.front(), (I3{-1, 0, 0}));
    EXPECT_EQ(p[1], (I3{0, 0, 0})); // x fastest
}

TEST(Weapons, IceBombOffsets) {
    auto r2 = iceBombOffsets(2);
    EXPECT_EQ(r2.size(), static_cast<std::size_t>((25 - 4) * 5));
    auto r3 = iceBombOffsets(3);
    EXPECT_EQ(r3.size(), static_cast<std::size_t>((49 - 4) * 7));
}

TEST(Weapons, TripleBowLift) {
    auto l = tripleBowLift();
    EXPECT_NEAR(l[0], -0.15, 1e-12);
    EXPECT_DOUBLE_EQ(l[1], 0.0);
    EXPECT_NEAR(l[2], 0.15, 1e-12);
}
