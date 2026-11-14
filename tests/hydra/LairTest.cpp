// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of src/core/hydra/Lair (U24): the hydra lair terraformer and speleothem lattice.
#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <utility>

#include "core/hydra/Lair.h"

using namespace gf::core;
using namespace gf::core::hydra;

namespace {
// HydraLairComponent's box for a start at (0, 10, 0): (x - 7 - 26 .. x - 7 + 42, y .. y + 17)
BlockBox lairBox() { return BlockBox{-33, 10, -33, 35, 27, 35}; }
} // namespace

TEST(HydraLair, BoxCenter) {
    auto c = boxCenter(lairBox());
    EXPECT_EQ(c.x, 1);
    EXPECT_EQ(c.y, 19);
    EXPECT_EQ(c.z, 1);
}

TEST(HydraLair, FunctionsMatchJava) {
    // HollowHillFunction: zero outside the radius, clamped to [-1, 1] inside
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 50, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 0, 0, 0), 1.0);
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 0, 100, 0), -1.0);
    // FocusedDensityFunction: near at the center, far beyond the radius
    EXPECT_NEAR(focusedDensity({0, 0, 0}, 10.0F, -5.0F, 3.0F, 0, 0, 0), -5.0 + (std::sqrt(0.75) / 10.0) * 8.0, 1e-5);
    EXPECT_DOUBLE_EQ(focusedDensity({0, 0, 0}, 10.0F, -5.0F, 3.0F, 40, 0, 0), 3.0);
}

TEST(HydraLair, Terraformer) {
    auto box = lairBox();
    // far from the lair: nothing above y 0 and nothing below -1
    EXPECT_DOUBLE_EQ(lairTerraformer(box, 400, 30, 400), 0.0);
    EXPECT_DOUBLE_EQ(lairTerraformer(box, 400, -10, 400), 0.0);
    // inside the hollow, above the floor: carved (negative)
    EXPECT_LT(lairTerraformer(box, 1, 14, 1), 0.0);
    // the mound's shell above the hollow: filled (positive)
    EXPECT_GT(lairTerraformer(box, 1, 33, 1), 0.0);
    // above the mound: nothing
    EXPECT_DOUBLE_EQ(lairTerraformer(box, 1, 40, 1), 0.0);
}

TEST(HydraLair, Lattice) {
    BlockBox chunk{0, 0, 0, 15, 255, 15};
    auto     pts = lairLattice(chunk, 0);
    ASSERT_FALSE(pts.empty());
    std::set<std::pair<int, int>> seen;
    for (auto const& p : pts) {
        EXPECT_GE(p.x, -8);
        EXPECT_LE(p.x, 23);
        EXPECT_EQ(p.y, 0);
        seen.insert({p.x, p.z});
    }
    EXPECT_EQ(seen.size(), pts.size());
    // the same lattice point seen from two chunks is the same point
    auto a = lairLattice(BlockBox{0, 0, 0, 31, 0, 31}, 0);
    auto b = lairLattice(BlockBox{16, 0, 0, 31, 0, 15}, 0);
    std::set<std::pair<int, int>> all;
    for (auto const& p : a) all.insert({p.x, p.z});
    for (auto const& p : b) EXPECT_TRUE(all.contains({p.x, p.z})) << p.x << " " << p.z;
}
