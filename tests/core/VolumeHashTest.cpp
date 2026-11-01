#include <gtest/gtest.h>

#include "core/VolumeHash.h"

using namespace gf::core;

TEST(VolumeHash, Fnv) {
    EXPECT_EQ(fnv1a64(""), 0xcbf29ce484222325ull);
    EXPECT_EQ(fnv1a64("a"), 0xaf63dc4c8601ec8cull);
}

TEST(VolumeHash, DependsOnContentNotPlacement) {
    VolumeHasher a, b, c;
    a.add(0, 0, 0, "minecraft:stone");
    a.add(1, 0, 0, "minecraft:oak_log", {{"pillar_axis", "y"}});
    b.add(0, 0, 0, "minecraft:stone");
    b.add(1, 0, 0, "minecraft:oak_log", {{"pillar_axis", "y"}});
    c.add(0, 0, 0, "minecraft:stone");
    c.add(1, 0, 0, "minecraft:oak_log", {{"pillar_axis", "x"}});
    EXPECT_EQ(a.value(), b.value());
    EXPECT_NE(a.value(), c.value());
    EXPECT_EQ(a.hex().size(), 16u);
    EXPECT_EQ(a.count(), 2);
}

TEST(VolumeHash, StatesAreSorted) {
    VolumeHasher a, b;
    a.add(0, 0, 0, "x:y", {{"b", "1"}, {"a", "2"}});
    b.add(0, 0, 0, "x:y", {{"a", "2"}, {"b", "1"}});
    EXPECT_EQ(a.value(), b.value());
}