// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of upstream's map maths (U13): MagicMapItem, MazeMapItem, MapItemSavedData#addDecoration and
// MapBiomesCommand (upstream commit 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <set>
#include <utility>

#include "core/uncraftmaps/Maps.h"

using namespace gf::core::uncraftmaps;

TEST(UncraftMaps, MagicMapCenterSnapsToTheGrid) {
    EXPECT_EQ(magicMapCenter(0, 0), std::make_pair(1024, 1024));       // round(-0.5) = 0
    EXPECT_EQ(magicMapCenter(-1, -1), std::make_pair(-1024, -1024));   // round(-0.50049) = -1
    EXPECT_EQ(magicMapCenter(2047, 2048), std::make_pair(1024, 3072)); // round(0.4995) = 0, round(0.5) = 1
    EXPECT_EQ(magicMapCenter(-3000, 5000), std::make_pair(-3072, 5120));
}

TEST(UncraftMaps, MazeMapCenter) {
    EXPECT_EQ(mazeMapCenter(0, 0, 0), std::make_pair(0, 0));
    EXPECT_EQ(mazeMapCenter(63, -64, 0), std::make_pair(0, 0));
    EXPECT_EQ(mazeMapCenter(64, -65, 0), std::make_pair(128, -128));
    EXPECT_EQ(mazeMapCenter(100, 100, 4), std::make_pair(960, 960)); // 2048 maps: 1024 - 64
}

TEST(UncraftMaps, ViewerPixelTruncates) {
    EXPECT_EQ(viewerPixel(1024.0, 1024, 16), 64);
    EXPECT_EQ(viewerPixel(1040.5, 1024, 16), 65);
    EXPECT_EQ(viewerPixel(1008.0, 1024, 16), 63);
    EXPECT_EQ(viewerPixel(1023.5, 1024, 16), 64); // floor = -1, -1 / 16 = 0 in Java
}

TEST(UncraftMaps, Shade) {
    EXPECT_EQ(shade(0xFFFFFF, 2), 0xFFFFFFFFu);
    EXPECT_EQ(shade(0xFFFFFF, 1), 0xFFDCDCDCu);
    EXPECT_EQ(shade(0xFFFFFF, 0), 0xFFB4B4B4u);
    EXPECT_EQ(shade(0xFFFFFF, 3), 0xFF878787u);
    EXPECT_EQ(shade(0x7FB238, 2), 0xFF7FB238u); // grass
    EXPECT_EQ(argbToAbgr(0xFF112233u), 0xFF332211u);
}

TEST(UncraftMaps, MagicScanIsTheViewCircleWithAFuzzyEdge) {
    std::set<std::pair<int, int>> seen;
    magicMapScan(64, 64, [&](int x, int z) { seen.insert({x, z}); });
    EXPECT_TRUE(seen.contains({64, 64}));
    EXPECT_TRUE(seen.contains({64 + 29, 64}));
    EXPECT_FALSE(seen.contains({64 + 32, 64}));       // radius 32: < 32
    EXPECT_TRUE(seen.contains({64 + 31, 64}));        // fuzzy edge: 95 + 64 odd
    EXPECT_FALSE(seen.contains({64 + 31, 65}));       // even sum: skipped
    EXPECT_FALSE(seen.contains({64 - 32, 64}));       // x starts at viewer - r + 1
    // the corners of the map are never reached from the centre
    EXPECT_FALSE(seen.contains({0, 0}));
    std::set<std::pair<int, int>> edge;
    magicMapScan(0, 0, [&](int x, int z) { edge.insert({x, z}); });
    for (auto const& [x, z] : edge) {
        EXPECT_GE(x, 0);
        EXPECT_GE(z, 0);
    }
}

TEST(UncraftMaps, MagicSampleBlocks) {
    // a map centred at 1024: pixel 0 covers blocks 0..15, sampled every 4 blocks
    EXPECT_EQ(magicSampleBlock(1024, 0), 0);
    EXPECT_EQ(magicSampleBlock(1024, 1), 4);
    EXPECT_EQ(magicSampleBlock(1024, 64 * 4), 1024);
    EXPECT_EQ(magicPixelBlock(1024, 0), 0);
    EXPECT_EQ(magicPixelBlock(1024, 64), 1024);
    EXPECT_EQ(magicPixelBlock(-1024, 0), -2048);
}

TEST(UncraftMaps, MazeScanStripes) {
    // step 3: only column x with x & 15 == 3 (and the column after one that changed)
    std::set<int> columns;
    mazeMapScan(
        64, 64, 16, 3, [](int, int) { return std::optional<std::uint32_t>(1u); },
        [&](int x, int, std::uint32_t) {
            columns.insert(x);
            return false;
        }
    );
    EXPECT_EQ(columns, (std::set<int>{51, 67})); // 51 & 15 == 67 & 15 == 3
    columns.clear();
    mazeMapScan(
        64, 64, 16, 3, [](int, int) { return std::optional<std::uint32_t>(1u); },
        [&](int x, int, std::uint32_t) {
            columns.insert(x);
            return x == 67; // a change carries the scan to the next column
        }
    );
    EXPECT_EQ(columns, (std::set<int>{51, 67, 68}));
    // empty chunks write nothing
    columns.clear();
    mazeMapScan(
        64, 64, 16, 3, [](int, int) { return std::optional<std::uint32_t>(); },
        [&](int x, int, std::uint32_t) {
            columns.insert(x);
            return true;
        }
    );
    EXPECT_TRUE(columns.empty());
}

TEST(UncraftMaps, DecorationPositions) {
    // a landmark at the centre of a scale 4 magic map, rotation 180 -> 8
    auto d = decorationPos(1024, 1024, 1024, 1024, 4, 180.0);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(*d, (DecorationPos{0, 0, 8}));
    auto e = decorationPos(1024 + 256, 1024 - 512, 1024, 1024, 4, 180.0);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->x, 32);
    EXPECT_EQ(e->z, -63); // (int) (-64 + 0.5) truncates
    EXPECT_FALSE(decorationPos(1024 + 16 * 64, 1024, 1024, 1024, 4, 180.0).has_value()); // f = 64 > 63
    EXPECT_TRUE(decorationPos(1024 + 16 * 63, 1024, 1024, 1024, 4, 180.0).has_value());
    EXPECT_EQ(decorationPos(0, 0, 0, 0, 0, -90.0)->rotation, -4);
    EXPECT_EQ(decorationName("gloamingforest:lich_tower", 1280, -768), "gloamingforest:lich_tower_1280_-768");
}

TEST(UncraftMaps, BiomePngColours) {
    EXPECT_EQ(biomePngColor("gloamingforest:forest"), 0xFF00FF00u);
    EXPECT_EQ(biomePngColor("gloamingforest:dark_forest_center"), 0xFF9D4F00u);
    EXPECT_FALSE(biomePngColor("gloamingforest:underground").has_value());
    // the data map colour comes back with red and blue swapped
    EXPECT_EQ(biomePngDataMapColor(0x7FB238u), 0xFF38B27Fu);
}

TEST(UncraftMaps, DecimalFormat) {
    EXPECT_EQ(decimalFormat00(0.0), ".00");
    EXPECT_EQ(decimalFormat00(12.5), "12.50");
    EXPECT_EQ(decimalFormat00(12.345), "12.35"); // 12.345 is 12.3450000000000006394884621841 as a double
    EXPECT_EQ(decimalFormat00(0.125), ".12");    // exact: half even
    EXPECT_EQ(decimalFormat00(0.375), ".38");
    EXPECT_EQ(decimalFormat00(99.999), "100.00");
    EXPECT_EQ(decimalFormat00(87.5), "87.50");
    EXPECT_EQ(decimalFormat00(100.0 / 3.0), "33.33");
}

TEST(UncraftMaps, BiomePngPathAndPlan) {
    EXPECT_EQ(biomePngPath(42, -32, -32, 64, 64), "generated/biomemaps/42/biome_map-42-(-32.-32)-(32.32).png");
    auto ok = biomePngPlan(64, 64);
    EXPECT_FALSE(ok.fails);
    EXPECT_EQ(ok.progressRows, 8); // rows 0, 8, ..., 56
    EXPECT_EQ(ok.completedRows, 64);
    EXPECT_TRUE(biomePngPlan(0, 0).fails);
    EXPECT_TRUE(biomePngPlan(64, 0).fails);
    auto wide = biomePngPlan(64, 32);
    EXPECT_TRUE(wide.fails);
    EXPECT_EQ(wide.progressRows, 0);
    auto tall = biomePngPlan(32, 64);
    EXPECT_TRUE(tall.fails);
    EXPECT_EQ(tall.progressRows, 4); // rows 0, 8, 16, 24 before row 32 fails
    auto tiny = biomePngPlan(4, 4);
    EXPECT_TRUE(tiny.fails); // 4 / 8 = 0: x % 0
    EXPECT_EQ(tiny.completedRows, 1);
    auto eight = biomePngPlan(8, 8);
    EXPECT_FALSE(eight.fails);
    EXPECT_EQ(eight.progressRows, 8);
}
