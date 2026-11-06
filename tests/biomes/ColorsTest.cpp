// U1 biomes: TF's biome colours (BiomeColorAlgorithms, FoliageColorHandler, ColorUtil#blendColors).

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "core/biomes/Colors.h"
#include "core/landmark/Landmark.h"

namespace c = gf::core::biomes::colors;

TEST(BiomeColors, FlatColoursAreTheBiomeJsonColours) {
    // TF's biome JSON stores the modifiers' results (BiomeMaker): swamp #5C694E / #496137, dark
    // forest #4B6754 / #3B5E3F (docs/spec/biomes.md section 6)
    EXPECT_EQ(c::swamp(false), 0x5C694E);
    EXPECT_EQ(c::swamp(true), 0x496137);
    EXPECT_EQ(c::darkForest(false), 0x4B6754);
    EXPECT_EQ(c::darkForest(true), 0x3B5E3F);
}

TEST(BiomeColors, BlendColorsIsJavaIntArithmetic) {
    EXPECT_EQ(c::blendColors(0xC43323, 0x5BC423, 0.0), 0xC43323);
    // ratio 1: f2 = 256, f1 = 0
    EXPECT_EQ(c::blendColors(0xC43323, 0x5BC423, 1.0), 0x5BC423);
    // ratio 0.5: (a * 128 + b * 128) >> 8 per masked pair of channels
    int r = c::blendColors(0xFF0101, 0x49FF01, 0.5);
    EXPECT_EQ(r, ((((0xFF0101 & 0xFF00FF) * 128 + (0x49FF01 & 0xFF00FF) * 128) >> 8) & 0xFF00FF) |
                     ((((0xFF0101 & 0xFF00) * 128 + (0x49FF01 & 0xFF00) * 128) >> 8) & 0xFF00));
}

TEST(BiomeColors, EnchantedBandsAroundTheLandmarkCenter) {
    auto center = gf::core::landmark::nearestCenter(0, 0);
    // at the centre: dist 0 -> 255 - 0 = 255 in the blue byte, red / green kept
    EXPECT_EQ(c::enchanted(0x00FFFF, center.x, center.z), 0x00FFFF);
    // 16 blocks east: dist 16 -> c = 256 % 512 = 256 > 255 -> 511 - 256 = 255 -> 255 - 255 = 0
    EXPECT_EQ(c::enchanted(0x00FFFF, center.x + 16, center.z) & 0xFF, 0);
    // 8 blocks: c = 128 -> 255 - 128 = 127
    EXPECT_EQ(c::enchanted(0x00FFFF, center.x + 8, center.z) & 0xFF, 127);
    // the grass modifier and the foliage handler both use it on the biome's colour
    EXPECT_EQ(c::grass("twilightforest:enchanted_forest", 0x00FFFF, center.x + 8, center.z), 0x00FF7F);
    EXPECT_EQ(c::foliage("twilightforest:enchanted_forest", 0x00FFFF, center.x + 8, center.z), 0x00FF7F);
    // negative coordinates use Java's truncating x / 16
    int x = -17, z = -1;
    auto nc = gf::core::landmark::nearestCenter(x / 16, z / 16);
    int  d  = static_cast<int>(static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>((nc.x - x) * (nc.x - x) + (nc.z - z) * (nc.z - z))))));
    int  b  = d * 16 % 512;
    if (b > 255) b = 511 - b;
    EXPECT_EQ(c::enchanted(0x00FFFF, x, z) & 0xFF, 255 - b);
}

TEST(BiomeColors, DarkForestCenterAndSpooky) {
    int grassLight = 0, grassDark = 0;
    for (int x = -2000; x < 2000; x += 37) {
        for (int z = -2000; z < 2000; z += 41) {
            int g = c::darkForestCenterGrass(x, z);
            EXPECT_TRUE(g == 0x667540 || g == 0x554114);
            (g == 0x667540 ? grassLight : grassDark)++;
            // (n + 1) / 2 is never below -0.1: TF's foliage is always the second colour
            EXPECT_EQ(c::darkForestCenterFoliage(x, z), 0xE94E14);
            double n = c::spookyNoise(x, z);
            EXPECT_EQ(c::spookyGrass(x, z), c::blendColors(0xC43323, 0x5BC423, n > 0.6 ? n * 0.1 : n));
            EXPECT_EQ(c::spookyFoliage(x, z), c::blendColors(0xFF0101, 0x49FF01, n > 0.6 ? n * 0.2 : n));
        }
    }
    EXPECT_GT(grassLight, 0);
    EXPECT_GT(grassDark, 0);
}

TEST(BiomeColors, HandlersOnlyForTfBiomes) {
    EXPECT_FALSE(c::grass("twilightforest:forest", 0x123456, 0, 0).has_value());
    EXPECT_FALSE(c::foliage("minecraft:plains", 0x123456, 0, 0).has_value());
    EXPECT_TRUE(c::isPositional("twilightforest:spooky_forest"));
    EXPECT_FALSE(c::isPositional("twilightforest:swamp"));
    EXPECT_TRUE(c::hasModifier("twilightforest:swamp"));
}

TEST(BiomeColors, BlendAveragesChannels) {
    // ClientLevel#calculateBlockTint: per channel sum / 25, truncated
    int v = c::blend(0, 0, 2, [](int x, int) { return x >= 0 ? 0xFF0000 : 0x000000; });
    EXPECT_EQ(v, (255 * 15 / 25) << 16);
    EXPECT_EQ(c::blend(5, 5, 0, [](int x, int z) { return x + z; }), 10);
}

TEST(BiomeColors, DarkForestCenterStaticColoursAreTheCommonOnes) {
    // The client biome file's static colours of the dark forest centre (what vanilla clients draw):
    // the foliage TF always draws, and the grass of darkForestCenterGrass' more common branch
    // (temperature noise >= -0.2: #554114) over a large sample.
    int common = 0, n = 0;
    for (int z = -20000; z < 20000; z += 97) {
        for (int x = -20000; x < 20000; x += 89) {
            common += c::darkForestCenterGrass(x, z) == 0x554114;
            ++n;
            EXPECT_EQ(c::darkForestCenterFoliage(x, z), 0xE94E14);
        }
    }
    RecordProperty("dark_forest_center_grass_554114_permille", common * 1000 / n);
    EXPECT_GT(common * 2, n) << common << " of " << n;
}

TEST(BiomeColors, EveryTfBiomeHasAColourAtEveryColumn) {
    // Java's blend reads each column's own biome colour: every TF biome answers (a modifier or its
    // static colour); a vanilla biome does not
    char const* ids[] = {"clearing", "dark_forest", "dark_forest_center", "dense_forest", "dense_mushroom_forest", "enchanted_forest",
                         "final_plateau", "fire_swamp", "firefly_forest", "forest", "glacier", "highlands", "highlands_underground",
                         "lake", "mushroom_forest", "oak_savannah", "snowy_forest", "spooky_forest", "stream", "swamp", "thornlands",
                         "underground"};
    for (char const* id : ids) {
        std::string full = std::string("twilightforest:") + id;
        EXPECT_TRUE(c::grassAt(full, 100, -200).has_value()) << full;
        EXPECT_TRUE(c::foliageAt(full, 100, -200).has_value()) << full;
        EXPECT_TRUE(c::water(full).has_value()) << full;
        EXPECT_TRUE(c::waterFog(full).has_value()) << full;
        // a biome with a modifier has no static colour (the modifier gives every colour)
        EXPECT_EQ(c::staticGrass(full).has_value(), !c::hasModifier(full)) << full;
    }
    EXPECT_FALSE(c::grassAt("minecraft:plains", 0, 0).has_value());
    EXPECT_FALSE(c::water("minecraft:plains").has_value());
}

TEST(BiomeColors, StaticColoursAreTfJsonOrTheColourMap) {
    // TF's JSON colours
    EXPECT_EQ(c::grassAt("twilightforest:snowy_forest", 0, 0), 0xFFFFFF);
    EXPECT_EQ(c::foliageAt("twilightforest:snowy_forest", 0, 0), 0xFFFFFF);
    EXPECT_EQ(c::grassAt("twilightforest:fire_swamp", 0, 0), 0x572E23);
    EXPECT_EQ(c::foliageAt("twilightforest:fire_swamp", 0, 0), 0x64260F);
    // the colour map at (0.7, 0.8) is Java's forest colour within one step (#79C05A / #59AE30)
    EXPECT_EQ(c::staticFoliage("twilightforest:dense_forest"), c::kFoliageForestMap);
    EXPECT_EQ(*c::staticGrass("twilightforest:dense_forest") & 0xFF00FF, c::kGrassForestMap & 0xFF00FF);
    // modifiers win: the swamp's flat colours, the enchanted forest's bands on its JSON 0x00FFFF
    EXPECT_EQ(c::grassAt("twilightforest:swamp", 5, 5), 0x5C694E);
    EXPECT_EQ(c::foliageAt("twilightforest:dark_forest", 5, 5), 0x3B5E3F);
    auto center = gf::core::landmark::nearestCenter(0, 0);
    EXPECT_EQ(c::grassAt("twilightforest:enchanted_forest", center.x + 8, center.z), 0x00FF7F);
}

TEST(BiomeColors, WaterColoursAreTfJson) {
    EXPECT_EQ(c::water("twilightforest:fire_swamp"), 0x6C2C2C);
    EXPECT_EQ(c::water("twilightforest:swamp"), 0x95B55F);
    EXPECT_EQ(c::water("twilightforest:spooky_forest"), 0xBC8857);
    EXPECT_EQ(c::water("twilightforest:dense_forest"), 0x005522);
    EXPECT_EQ(c::water("twilightforest:forest"), 0x3F76E4);
    EXPECT_EQ(c::waterFog("twilightforest:forest"), 0x050533);
    EXPECT_EQ(c::waterFog("twilightforest:dense_forest"), 0x005522);
    EXPECT_TRUE(c::closerWaterFog("twilightforest:swamp"));
    EXPECT_FALSE(c::closerWaterFog("twilightforest:dense_forest"));
}
