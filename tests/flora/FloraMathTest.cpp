// U6 (flora): the pure arithmetic of upstream's flora blocks against upstream 4.8's formulas.

#include <gtest/gtest.h>

#include "core/flora/FloraMath.h"

namespace {

using namespace gf::core::flora;

TEST(FloraMath, JarAnalogIsLerpDiscrete) {
    // Mth.lerpDiscrete(delta, 0, 15) = floor(delta * 14) + (delta > 0 ? 1 : 0)
    EXPECT_EQ(jarAnalog(0, 64), 0);
    EXPECT_EQ(jarAnalog(1, 64), 1);
    EXPECT_EQ(jarAnalog(10, 64), 3);
    EXPECT_EQ(jarAnalog(32, 64), 8);
    EXPECT_EQ(jarAnalog(64, 64), 15);
    EXPECT_EQ(jarAnalog(1, 1), 15);
    EXPECT_EQ(jarAnalog(8, 16), 8);
}

TEST(FloraMath, RotationSegments) {
    EXPECT_EQ(rotationSegment(0.0f), 0);
    EXPECT_EQ(rotationSegment(180.0f), 8);
    EXPECT_EQ(rotationSegment(90.0f), 4);
    EXPECT_EQ(rotationSegment(-90.0f), 12);
    EXPECT_EQ(rotationSegment(359.0f), 0);
    EXPECT_EQ(rotationSegment(11.0f), 0);
    EXPECT_EQ(rotationSegment(12.0f), 1);
}

TEST(FloraMath, FallenLeavesSlowDown) {
    EXPECT_DOUBLE_EQ(fallenLeavesFactor(1), 1.0);
    EXPECT_DOUBLE_EQ(fallenLeavesFactor(2), 1.0);
    EXPECT_DOUBLE_EQ(fallenLeavesFactor(3), 0.95);
    EXPECT_DOUBLE_EQ(fallenLeavesFactor(8), 0.7);
}

TEST(FloraMath, BushNodeWeights) {
    // LARGE 1, MEDIUM 2, SMALL 4, TINY 3 of 10
    EXPECT_EQ(chooseBushNode(0.0f), BushNode::Large);
    EXPECT_EQ(chooseBushNode(0.099f), BushNode::Large);
    EXPECT_EQ(chooseBushNode(0.1f), BushNode::Medium);
    EXPECT_EQ(chooseBushNode(0.29f), BushNode::Medium);
    EXPECT_EQ(chooseBushNode(0.3f), BushNode::Small);
    EXPECT_EQ(chooseBushNode(0.69f), BushNode::Small);
    EXPECT_EQ(chooseBushNode(0.7f), BushNode::Tiny);
    EXPECT_EQ(chooseBushNode(0.999f), BushNode::Tiny);
}

TEST(FloraMath, PatchBoxFromSeed) {
    // no connections: insets 1 + bits; seed bits 12..13 = 3 (x+), 15..16 = 1 (x-), 18..19 = 2 (z+), 21..22 = 0 (z-)
    std::int64_t seed = (3ll << 12) | (1ll << 15) | (2ll << 18) | (0ll << 21);
    auto         box  = patchBox(seed, false, false, false, false);
    EXPECT_EQ(box[0], 2);  // x0 = 1 + xOff1
    EXPECT_EQ(box[1], 1);  // z0 = 1 + zOff1
    EXPECT_EQ(box[2], 12); // x1 = 15 - xOff0
    EXPECT_EQ(box[3], 13); // z1 = 15 - zOff0
    auto all = patchBox(seed, true, true, true, true);
    EXPECT_EQ(all, (std::array<int, 4>{0, 0, 16, 16}));
    // a negative seed (Java's long) uses the same bits
    auto neg = patchBox(-1, false, false, false, false);
    EXPECT_EQ(neg, (std::array<int, 4>{4, 4, 12, 12}));
}

} // namespace
