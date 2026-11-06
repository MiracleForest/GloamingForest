// U3 trees: the Java HashSet<BlockPos> model behind TreeFeature#updateLeaves.
#include <gtest/gtest.h>

#include <algorithm>

#include "core/jrand/JavaOrder.h"
#include "core/trees/JavaHashSet.h"

using gf::core::trees::JavaHashSet;
using gf::core::trees::Pos;

TEST(TreesJavaHashSet, HashIsVec3iHashCode) {
    EXPECT_EQ(JavaHashSet::hashOf({1, 2, 3}), (2 + 3 * 31) * 31 + 1);
    EXPECT_EQ(JavaHashSet::hashOf({-5, 70, -12}), gf::core::jrand::blockPosHash(-5, 70, -12));
}

TEST(TreesJavaHashSet, IterationMatchesStaticHashSetOrder) {
    // a tree-sized set of positions: the static order of F5's hashSetOrder (Java-verified)
    std::vector<Pos> in;
    for (int x = -6; x <= 6; ++x)
        for (int y = 60; y < 75; y += 2)
            for (int z = -6; z <= 6; z += 3) in.push_back({x, y, z});
    JavaHashSet set;
    for (auto const& p : in) EXPECT_TRUE(set.add(p));
    EXPECT_FALSE(set.add(in[3]));
    std::vector<int> hashes;
    for (auto const& p : in) hashes.push_back(JavaHashSet::hashOf(p));
    auto order = gf::core::jrand::hashSetOrder(hashes);
    auto got   = set.elements();
    ASSERT_EQ(got.size(), in.size());
    for (std::size_t i = 0; i < order.size(); ++i) EXPECT_EQ(got[i], in[order[i]]) << "at " << i;
}

TEST(TreesJavaHashSet, PollTakesTheFirstAndKeepsTheRestInOrder) {
    JavaHashSet set;
    std::vector<Pos> in{{0, 64, 0}, {1, 64, 0}, {0, 65, 0}, {0, 64, 1}, {16, 64, 0}, {0, 80, 0}};
    for (auto const& p : in) set.add(p);
    auto before = set.elements();
    auto first  = set.pollFirst();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, before.front());
    auto after = set.elements();
    ASSERT_EQ(after.size(), before.size() - 1);
    EXPECT_TRUE(std::equal(after.begin(), after.end(), before.begin() + 1));
    EXPECT_FALSE(set.contains(*first));
    // re-adding goes to the end of its bucket
    EXPECT_TRUE(set.add(*first));
    EXPECT_TRUE(set.contains(*first));
    EXPECT_TRUE(set.remove(*first));
    EXPECT_FALSE(set.remove(*first));
    while (set.pollFirst()) {}
    EXPECT_TRUE(set.empty());
}
