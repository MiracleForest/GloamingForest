// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of the hydra mortar's hit boxes (src/core/hydra/Mortar.h).

#include "core/hydra/Mortar.h"

#include <gtest/gtest.h>

using namespace gf::core::hydra;

TEST(HydraMortar, JavaBoxes) {
    auto b = mortarBox(V3{10.0, 64.0, -3.0}, 0.0);
    EXPECT_DOUBLE_EQ(b.min.x, 9.625);
    EXPECT_DOUBLE_EQ(b.max.y, 64.75);
    auto p = mortarBox(V3{0.0, 0.0, 0.0}, kMortarPickRadius);
    EXPECT_DOUBLE_EQ(p.max.x - p.min.x, 3.75); // the custom_hit_test
    auto q = mortarBox(V3{0.0, 0.0, 0.0}, kProjectileInflate);
    EXPECT_DOUBLE_EQ(q.max.x - q.min.x, 1.35);
    EXPECT_DOUBLE_EQ(q.min.y, -0.3);
}

TEST(HydraMortar, SegmentClip) {
    auto box = mortarBox(V3{0.0, 0.0, 0.0}, kProjectileInflate); // x, z in [-0.675, 0.675], y in [-0.3, 1.05]
    // straight through the middle
    EXPECT_TRUE(segmentHits(box, V3{-3, 0.4, 0}, V3{3, 0.4, 0}));
    // an arrow passing 1.4 blocks beside it: inside the pick box, outside the projectile box
    EXPECT_FALSE(segmentHits(box, V3{-3, 0.4, 1.4}, V3{3, 0.4, 1.4}));
    EXPECT_TRUE(segmentHits(mortarBox(V3{0, 0, 0}, kMortarPickRadius), V3{-3, 0.4, 1.4}, V3{3, 0.4, 1.4}));
    // stops short of it
    EXPECT_FALSE(segmentHits(box, V3{-3, 0.4, 0}, V3{-1, 0.4, 0}));
    // starts inside
    EXPECT_TRUE(segmentHits(box, V3{0.1, 0.1, 0.1}, V3{0.2, 0.2, 0.2}));
    // a diagonal grazing the corner
    EXPECT_TRUE(segmentHits(box, V3{-1.0, 0.4, 0.0}, V3{0.0, 0.4, 1.0}));
    EXPECT_FALSE(segmentHits(box, V3{-2.0, 0.4, 0.0}, V3{0.0, 0.4, 2.0}));
    // a zero-length segment
    EXPECT_FALSE(segmentHits(box, V3{2, 0.4, 0}, V3{2, 0.4, 0}));
}
TEST(HydraMortar, ProjectileLineOfMotion) {
    V3 feet{0.0, 64.0, 0.0}; // Java projectile box x, z in [-0.675, 0.675], y in [63.7, 65.05]
    // a slow snowball aimed dead centre, stopped by the 3.75 pick box 1.9 blocks short of the centre
    EXPECT_TRUE(projectileReachesJavaBox(feet, V3{-1.9, 64.3, 0.0}, V3{0.4, -0.02, 0.0}, V3{}));
    // the same with Bedrock's motion zeroed at the impact: the look carries the line
    EXPECT_TRUE(projectileReachesJavaBox(feet, V3{-1.9, 64.3, 0.0}, V3{}, V3{1.0, 0.0, 0.0}));
    // reported after its move, already past the centre
    EXPECT_TRUE(projectileReachesJavaBox(feet, V3{0.9, 64.3, 0.0}, V3{2.5, 0.0, 0.0}, V3{}));
    // passing 1.4 blocks beside it (inside the pick box, outside Java's box), slow or fast
    EXPECT_FALSE(projectileReachesJavaBox(feet, V3{-1.5, 64.3, 1.4}, V3{0.3, 0.0, 0.0}, V3{}));
    EXPECT_FALSE(projectileReachesJavaBox(feet, V3{-1.5, 64.3, 1.4}, V3{3.0, 0.0, 0.0}, V3{}));
    // a steep fall beside it
    EXPECT_FALSE(projectileReachesJavaBox(feet, V3{1.2, 65.5, 0.0}, V3{0.0, -1.0, 0.0}, V3{}));
    EXPECT_TRUE(projectileReachesJavaBox(feet, V3{0.2, 65.5, 0.0}, V3{0.0, -1.0, 0.0}, V3{}));
    // no motion and no look: only a projectile inside the box counts
    EXPECT_FALSE(projectileReachesJavaBox(feet, V3{-1.5, 64.3, 0.0}, V3{}, V3{}));
    EXPECT_TRUE(projectileReachesJavaBox(feet, V3{0.1, 64.3, 0.0}, V3{}, V3{}));
    auto s = projectilePath(V3{0, 0, 0}, V3{1.0, 0, 0}, V3{});
    EXPECT_DOUBLE_EQ(s.from.x, -(kMortarPickRadius + kMortarSize + 1.0));
    EXPECT_DOUBLE_EQ(s.to.x, kMortarPickRadius + kMortarSize + 1.0);
}

TEST(HydraMortar, ProjectileLook) {
    // Projectile#updateRotation's mirrored convention: the look is the motion with x and y negated
    // (within Mth's 65536-entry sin table)
    auto l = projectileLook(V3{3.0, 4.0, 0.0});
    EXPECT_NEAR(l.x, -0.6, 1e-4);
    EXPECT_NEAR(l.y, -0.8, 1e-4);
    EXPECT_NEAR(l.z, 0.0, 1e-4);
    l = projectileLook(V3{0.0, 0.0, 2.0});
    EXPECT_NEAR(l.z, 1.0, 1e-4);
    l = projectileLook(V3{1.0, 0.0, 1.0});
    EXPECT_NEAR(l.x, -0.70710678, 1e-4);
    EXPECT_NEAR(l.z, 0.70710678, 1e-4);
    l = projectileLook(V3{});
    EXPECT_DOUBLE_EQ(l.x * l.x + l.y * l.y + l.z * l.z, 0.0);
}
