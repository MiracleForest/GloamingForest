// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
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