// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the landmarks' controlled spawn areas (F6): HollowHillStructure#canSpawnMob, the half dome
// of a hollow hill's start box (TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include "core/landmark/Spawning.h"

using namespace gf::core;
using namespace gf::core::landmark;

TEST(LandmarkSpawning, HollowHillDomeAxes) {
    BlockBox box{0, 0, 0, 40, 20, 40};
    // the middle of the floor: hX = hZ = 0, hY = 0
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 20, 0, 20));
    // y runs 0..1 from the floor to the top: the top is outside, 0.95 of the way up inside
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 20, 20, 20));
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 20, 19, 20));
    // x and z run -1..1 across the box
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 0, 0, 20));
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 40, 0, 20));
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 39, 0, 20)); // 0.95
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 20, 0, 1));
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 20, 0, 40));
}

TEST(LandmarkSpawning, HollowHillDomeIsRound) {
    BlockBox box{0, 0, 0, 40, 20, 40};
    // diagonals: 0.65 on both axes is 0.919 from the middle, 0.7 is 0.990
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 33, 0, 33));
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 34, 0, 34));
    // the corners of the box, low and high
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 0, 0, 0));
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 40, 20, 40));
    // half way up and half way out: sqrt(0.5^2 + 0.5^2 + 0.5^2) = 0.866
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 30, 10, 30));
    // canSpawnMob checks no bounds: just below the floor (hY -0.05) still counts, far beside it not
    EXPECT_TRUE(hollowHillCanSpawnMob(box, 20, -1, 20));
    EXPECT_FALSE(hollowHillCanSpawnMob(box, 60, 0, 20));
}

TEST(LandmarkSpawning, HollowHillDomeInJavaFloats) {
    // Mth.inverseLerp(float): beyond 2^24 the coordinates round to even floats first, as in Java.
    // 16777217 is the float 16777216 (the box's minX): hX = -1, outside, where exact arithmetic would
    // give -0.95, inside; 16777218 is exact (hX = -0.9)
    BlockBox far{16777216, 0, 0, 16777256, 20, 40};
    EXPECT_FALSE(hollowHillCanSpawnMob(far, 16777217, 0, 20));
    EXPECT_TRUE(hollowHillCanSpawnMob(far, 16777218, 0, 20));
    // a box one block wide on an axis: 0 / 0 or x / 0 in Java, never inside
    BlockBox flat{5, 0, 0, 5, 20, 40};
    EXPECT_FALSE(hollowHillCanSpawnMob(flat, 5, 0, 20));
    EXPECT_FALSE(hollowHillCanSpawnMob(flat, 6, 0, 20));
}
