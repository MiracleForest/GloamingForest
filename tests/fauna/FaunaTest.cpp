// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of src/core/fauna (U16): dyes and fleece colours, the bighorn loot tables, the variant
// picks, the bird flap and flight steering, the kobold and mosquito difficulty numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>

#include "core/fauna/Fauna.h"
#include "core/jrand/JRand.h"

using namespace gf::core::fauna;
using gf::core::jrand::LegacyRandom;

TEST(FaunaDyes, NamesAndItems) {
    EXPECT_EQ(dyeName(0), "white");
    EXPECT_EQ(dyeName(12), "brown");
    EXPECT_EQ(dyeName(15), "black");
    EXPECT_EQ(dyeName(16), "");
    EXPECT_EQ(dyeByName("light_blue"), 3);
    EXPECT_EQ(dyeOfItem("minecraft:red_dye"), 14);
    EXPECT_EQ(dyeOfItem("light_gray_dye"), 8);
    EXPECT_EQ(dyeOfItem("minecraft:ink_sac"), std::nullopt);
    EXPECT_EQ(dyeOfItem("minecraft:bone_meal"), std::nullopt);
    EXPECT_EQ(woolOf(10), "minecraft:purple_wool");
}

TEST(FaunaDyes, VanillaMixes) {
    EXPECT_EQ(mixDyes(14, 4), 1);  // red + yellow = orange
    EXPECT_EQ(mixDyes(4, 14), 1);
    EXPECT_EQ(mixDyes(11, 13), 9); // blue + green = cyan
    EXPECT_EQ(mixDyes(15, 0), 7);  // black + white = gray
    EXPECT_EQ(mixDyes(7, 0), 8);   // gray + white = light gray
    EXPECT_EQ(mixDyes(10, 6), 2);  // purple + pink = magenta
    EXPECT_EQ(mixDyes(11, 14), 10); // blue + red = purple
    EXPECT_EQ(mixDyes(14, 0), 6);  // red + white = pink
    EXPECT_EQ(mixDyes(13, 0), 5);  // green + white = lime
    EXPECT_EQ(mixDyes(11, 0), 3);  // blue + white = light blue
    EXPECT_EQ(mixDyes(0, 0), std::nullopt);
    EXPECT_EQ(mixDyes(12, 15), std::nullopt);
    LegacyRandom r(42);
    for (int i = 0; i < 50; ++i) {
        int c = offspringColor(12, 15, r);
        EXPECT_TRUE(c == 12 || c == 15);
    }
    EXPECT_EQ(offspringColor(14, 4, r), 1);
}

TEST(FaunaBighorn, FleeceColourIsBrownAboutHalfTheTime) {
    LegacyRandom r(1234);
    std::map<int, int> counts;
    int const          n = 32000;
    for (int i = 0; i < n; ++i) counts[randomFleeceColor(r)]++;
    // brown 1/2 + 1/32 = 17/32
    EXPECT_NEAR(counts[kBrown] / double(n), 17.0 / 32.0, 0.02);
    EXPECT_NEAR(counts[0] / double(n), 1.0 / 32.0, 0.01);
    EXPECT_EQ(counts.size(), 16u);
}

TEST(FaunaBighorn, LootTablesAndShearing) {
    EXPECT_EQ(bighornLootTable(true, 14), "gloamingforest:entities/bighorn_sheep");
    EXPECT_EQ(bighornLootTable(false, 0), "gloamingforest:entities/bighorn_sheep/white");
    EXPECT_EQ(bighornLootTable(false, 3), "gloamingforest:entities/bighorn_sheep/light_blue");
    EXPECT_EQ(bighornLootTable(false, 99), "gloamingforest:entities/bighorn_sheep/white");
    LegacyRandom  r(7);
    std::set<int> seen;
    for (int i = 0; i < 200; ++i) seen.insert(shearCount(r));
    EXPECT_EQ(seen, (std::set<int>{1, 2, 3}));
}

TEST(FaunaVariants, BiomeFilterAndFallback) {
    std::vector<Variant> v = {
        {"gloamingforest:blue", "a", true, {}},
        {"gloamingforest:brown", "b", false, {"gloamingforest:glacier"}},
        {"gloamingforest:gold", "c", true, {}},
    };
    LegacyRandom  r(99);
    std::set<int> forest, glacier;
    for (int i = 0; i < 200; ++i) {
        forest.insert(pickVariant(v, "gloamingforest:forest", 0, r));
        glacier.insert(pickVariant(v, "gloamingforest:glacier", 0, r));
    }
    EXPECT_EQ(forest, (std::set<int>{0, 2}));
    EXPECT_EQ(glacier, (std::set<int>{0, 1, 2}));
    std::vector<Variant> onlyFiltered = {{"x:a", "", false, {"x:b"}}};
    EXPECT_EQ(pickVariant(onlyFiltered, "x:c", 3, r), 3);
    EXPECT_EQ(pickCommonVariant(onlyFiltered, 5, r), 5);
    EXPECT_EQ(variantIndex(v, "gloamingforest:gold"), 2);
    EXPECT_EQ(variantIndex(v, "gloamingforest:red"), std::nullopt);
}

TEST(FaunaVariants, RabbitOffspringMostlyFromParents) {
    std::vector<Variant> v = {{"gloamingforest:brown", "", true, {}}, {"gloamingforest:dutch", "", true, {}}, {"gloamingforest:white", "", true, {}}};
    LegacyRandom         r(5);
    int                  fromParents = 0, n = 20000;
    for (int i = 0; i < n; ++i) {
        int c = rabbitOffspringVariant(v, 0, 1, 0, r);
        if (c == 0 || c == 1) fromParents++;
    }
    // 19/20 a parent's, 1/20 any of three (two of them a parent's)
    EXPECT_NEAR(fromParents / double(n), 19.0 / 20.0 + (1.0 / 20.0) * (2.0 / 3.0), 0.01);
}

TEST(FaunaBirds, FlapFollowsBirdAiStep) {
    BirdFlap f;
    f.step(true);
    EXPECT_FLOAT_EQ(f.flapIntensity, 0.0F);
    EXPECT_NEAR(f.flapSpeed, 0.9F, 1e-6);
    EXPECT_NEAR(f.flapLength, 1.8F, 1e-6);
    f.step(false); // airborne: intensity + 1.2 clamped, speed reset to 1 then * 0.9
    EXPECT_FLOAT_EQ(f.flapIntensity, 1.0F);
    EXPECT_NEAR(f.flapSpeed, 0.9F, 1e-6);
    EXPECT_NEAR(f.flapLength, 3.6F, 1e-5);
    EXPECT_NEAR(f.bob(), (std::sin(3.6F) + 1.0F) * 1.0F, 1e-5);
    f.step(true);
    EXPECT_NEAR(f.flapIntensity, 0.7F, 1e-6);
}

TEST(FaunaBirds, FlightTargetAndSteering) {
    LegacyRandom r(3);
    for (int i = 0; i < 100; ++i) {
        auto t = nextFlightTarget(10.5, 64.0, -3.5, 50, r);
        EXPECT_GE(t[0], 10 - 6);
        EXPECT_LE(t[0], 10 + 6);
        EXPECT_GE(t[1], 62);
        EXPECT_LE(t[1], 67);
        auto t2 = nextFlightTarget(10.5, 64.0, -3.5, 150, r);
        EXPECT_GE(t2[1], 60);
        EXPECT_LE(t2[1], 65);
    }
    auto m = steerTowards({0, 0, 0}, {0, 64, 0}, {5, 70, -5});
    EXPECT_NEAR(m[0], 0.05, 1e-6);
    EXPECT_NEAR(m[1], 0.07, 1e-6);
    EXPECT_NEAR(m[2], -0.05, 1e-6);
    EXPECT_NEAR(yawOf(0.0, 1.0), 0.0F, 1e-4);
    EXPECT_NEAR(yawOf(-1.0, 0.0), 90.0F, 1e-4);
    EXPECT_NEAR(wrapDegrees(270.0F), -90.0F, 1e-4);
    EXPECT_NEAR(wrapDegrees(-190.0F), 170.0F, 1e-4);
}

TEST(FaunaMonsters, DifficultyNumbers) {
    EXPECT_EQ(koboldEatDelay(1), 400);
    EXPECT_EQ(koboldEatDelay(2), 200);
    EXPECT_EQ(koboldEatDelay(3), 100);
    EXPECT_EQ(koboldEatDelay(0), 200);
    EXPECT_EQ(mosquitoHungerSeconds(1), 7);
    EXPECT_EQ(mosquitoHungerSeconds(2), 15);
    EXPECT_EQ(mosquitoHungerSeconds(3), 30);
    EXPECT_EQ(mosquitoHungerSeconds(0), 15);
    LegacyRandom r(11);
    for (int i = 0; i < 100; ++i) {
        float p = parrotPitch(r);
        EXPECT_GE(p, 0.8F);
        EXPECT_LE(p, 1.2F);
        float b = voicePitch(r, true);
        EXPECT_GE(b, 1.3F);
        EXPECT_LE(b, 1.7F);
    }
}
