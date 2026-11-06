// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of U19's pure parts (src/core/questcamps/Questcamps.h), translated from QuestRam,
// QuestRamModel, TemplateFeature and GraveyardFeature (TeamTwilight/twilightforest @ 9e40a757,
// LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include "core/questcamps/Questcamps.h"

using namespace gf::core::questcamps;

TEST(QuestcampsDyes, NamesAndIds) {
    EXPECT_EQ(dyeId("white"), 0);
    EXPECT_EQ(dyeId("light_gray"), 8);
    EXPECT_EQ(dyeId("black"), 15);
    EXPECT_EQ(dyeId("rainbow"), -1);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(dyeId(kDyeNames[static_cast<std::size_t>(i)]), i);
}

TEST(QuestcampsDyes, SheepColours) {
    // Sheep.createSheepColor: white is fixed, the others are the diffuse colour * 0.75 (floored)
    EXPECT_EQ(sheepColor(0), 0xE6E6E6u);
    EXPECT_EQ(textureDiffuse(14), 0xB02E26u);
    EXPECT_EQ(sheepColor(14), (0xB0u * 3 / 4) << 16 | (0x2Eu * 3 / 4) << 8 | (0x26u * 3 / 4));
    EXPECT_EQ(sheepColor(15), (0x1Du * 3 / 4) << 16 | (0x1Du * 3 / 4) << 8 | (0x21u * 3 / 4));
}

TEST(QuestcampsRam, Flags) {
    EXPECT_FALSE(colorPresent(0, 3));
    EXPECT_TRUE(colorPresent(1 << 3, 3));
    EXPECT_EQ(countColors(0xFFFF), 16);
    EXPECT_TRUE(complete(0xFFFF));
    EXPECT_FALSE(complete(0x7FFF));
}

TEST(QuestcampsRam, ModelPose) {
    auto empty = modelPose(0);
    EXPECT_EQ(empty.count, 0);
    EXPECT_EQ(empty.headZ, -11);
    for (bool v : empty.visible) EXPECT_FALSE(v);
    // white and black: white comes first in colorOrder, black (15) fourth
    auto two = modelPose(1 << 0 | 1 << 15);
    EXPECT_EQ(two.count, 2);
    EXPECT_EQ(two.headZ, -13);
    EXPECT_EQ(two.backTorsoZ, 2);
    EXPECT_EQ(two.backLegZ, 11);
    EXPECT_EQ(two.frontLegZ, -13);
    EXPECT_TRUE(two.visible[0] && two.visible[15]);
    EXPECT_EQ(two.segmentZ[0], -2);
    EXPECT_EQ(two.segmentZ[15], 0);
    auto all = modelPose(0xFFFF);
    EXPECT_EQ(all.segmentZ[kColorOrder.back()], 30 - 16);
    EXPECT_EQ(all.segmentZ[kColorOrder.front()], -16);
}

TEST(QuestcampsFeatures, AverageGround) {
    auto flat = averageGround({64, 64, 64, 64}, false);
    ASSERT_TRUE(flat);
    EXPECT_EQ(flat->baseY, 64);
    EXPECT_EQ(flat->maxY, 64);
    auto mixed = averageGround({64, 65, 65, 65}, false); // mean 64.75
    ASSERT_TRUE(mixed);
    EXPECT_EQ(mixed->baseY, 65);
    EXPECT_EQ(mixed->maxY, 65);
    // a population standard deviation above 2 aborts
    EXPECT_FALSE(averageGround({60, 70}, false));
    EXPECT_TRUE(averageGround({60, 64}, false)); // deviation exactly 2
    EXPECT_EQ(averageGround({10, 11}, true)->baseY, 11); // Math.round(10.5)
}

TEST(QuestcampsFeatures, Rotation) {
    EXPECT_EQ(rotatedSize({17, 5, 14}, Rot::Cw90), (Vec3i{14, 5, 17}));
    EXPECT_EQ(rotatedSize({17, 5, 14}, Rot::Cw180), (Vec3i{17, 5, 14}));
    EXPECT_EQ(rotate({1, 2, 3}, Rot::Cw90), (Vec3i{-3, 2, 1}));
    EXPECT_EQ(rotate({1, 2, 3}, Rot::Cw180), (Vec3i{-1, 2, -3}));
    EXPECT_EQ(rotate({1, 2, 3}, Rot::Ccw90), (Vec3i{3, 2, -1}));
}

TEST(QuestcampsFeatures, GraveGrid) {
    // the base 17 x 5 x 14, graves 4 x 4 x 1, no rotation: graves 5 apart in x, 2 apart in z
    auto g = graveGrid({17, 5, 14}, {4, 4, 1}, Rot::None, Mir::None);
    EXPECT_EQ(g.innerSize, (Vec3i{12, 0, 9}));
    EXPECT_EQ(g.fixed, (Vec3i{3, 1, 2}));
    std::vector<std::pair<int, int>> want = {{0, 0}, {0, 2}, {0, 6}, {0, 8}, {5, 0}, {5, 2}, {5, 6}, {5, 8}};
    EXPECT_EQ(g.cells, want);
    // a quarter turn swaps the steps (2 in x, 5 in z) and the sizes
    auto r = graveGrid(rotatedSize({17, 5, 14}, Rot::Cw90), rotatedSize({4, 4, 1}, Rot::Cw90), Rot::Cw90, Mir::None);
    EXPECT_EQ(r.innerSize, (Vec3i{9, 0, 12}));
    for (auto const& [x, z] : r.cells) {
        EXPECT_EQ(x % 2, 0);
        EXPECT_EQ(z % 5, 0);
        EXPECT_NE(x, 4);
        EXPECT_NE(z, 6);
    }
    EXPECT_EQ(r.cells.size(), 8u);
    // FRONT_BACK shifts the grid by the grave's length; CW_180 by the grave size
    auto fb = graveGrid({17, 5, 14}, {4, 4, 1}, Rot::None, Mir::FrontBack);
    EXPECT_EQ(fb.fixed, (Vec3i{3 + 3, 1, 2}));
    auto half = graveGrid({17, 5, 14}, {4, 4, 1}, Rot::Cw180, Mir::None);
    EXPECT_EQ(half.fixed, (Vec3i{3 + 3, 1, 2}));
}
