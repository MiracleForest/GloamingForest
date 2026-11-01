#include <gtest/gtest.h>

#include "core/GlobalPos.h"
#include "core/Layout.h"

using namespace gf::core;

TEST(Layout, Grid) {
    EXPECT_EQ(centred(100, 100, 20, 10), (Rect{40, 45, 20, 10}));
    EXPECT_EQ(gridCell(8, 8, 9, 18, 10), (Rect{26, 26, 18, 18}));
    EXPECT_EQ(gridIndexAt(8, 8, 9, 27, 18, 27, 27), 10);
    EXPECT_EQ(gridIndexAt(8, 8, 9, 27, 18, 7, 9), -1);
}

TEST(GlobalPos, Boxes) {
    auto b = BlockBox::of(3, 0, 5, 1, 2, 1);
    EXPECT_EQ(b.minX, 1);
    EXPECT_EQ(b.maxX, 3);
    EXPECT_EQ(b.volume(), 3 * 3 * 5);
    EXPECT_TRUE(b.contains(2, 1, 3));
    EXPECT_FALSE(b.contains(4, 1, 3));
    EXPECT_TRUE(b.intersects(BlockBox::of(3, 2, 5, 9, 9, 9)));
    EXPECT_EQ((GlobalPos{1, 2, 3, 4}).key(), "1:2:3:4");
}