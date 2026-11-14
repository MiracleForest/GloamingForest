// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of src/core/provisions/Provisions.h (translated from upstream 4.8's GloamingItems, Experiment115Block,
// FrostedEffect, EntityEvents, GloamingEnchantments, GloamingJukeboxSongs and ChainBlockItem; LGPL-2.1-or-later
// code, see the header of Provisions.h).
#include <gtest/gtest.h>

#include "core/provisions/Provisions.h"

using namespace gf::core::provisions;

TEST(ProvisionsFood, TableHasGloamingValues) {
    EXPECT_EQ(foods().size(), 23u);
    auto const* chop = food("gloamingforest:hydra_chop");
    ASSERT_NE(chop, nullptr);
    EXPECT_EQ(chop->nutrition, 18);
    EXPECT_FLOAT_EQ(chop->saturation, 2.0f);
    EXPECT_EQ(chop->effects[0].effect, "minecraft:regeneration");
    EXPECT_EQ(chop->effects[0].ticks, 100);
    auto const* strog = food("gloamingforest:meef_stroganoff");
    ASSERT_NE(strog, nullptr);
    EXPECT_TRUE(strog->alwaysEdible);
    EXPECT_TRUE(strog->bowl);
    EXPECT_EQ(strog->effects[0].ticks, 3600);
    EXPECT_EQ(strog->effects[1].effect, "minecraft:strength");
    EXPECT_EQ(strog->effects[1].ticks, 2400);
    EXPECT_TRUE(food("gloamingforest:moss_soup")->bowl);
    EXPECT_FLOAT_EQ(food("gloamingforest:chicken_jerky")->saturation, 1.075f);
    EXPECT_EQ(food("gloamingforest:tropical_fish_jerky")->nutrition, 2);
    EXPECT_EQ(food("gloamingforest:stale_bread"), nullptr);
    EXPECT_FLOAT_EQ(saturationOf(6, 0.6f), 7.2f);
}

TEST(ProvisionsE115, UseOrder) {
    E115Use e115{.holdsE115 = true, .emptyHands = false};
    EXPECT_EQ(e115Action({0, false}, e115), E115Action::Fail);
    EXPECT_EQ(e115Action({3, false}, e115), E115Action::AddSlice);
    EXPECT_EQ(*e115After({3, false}, E115Action::AddSlice), (E115{2, false}));

    E115Use red{.holdsRedstone = true, .emptyHands = false};
    EXPECT_EQ(e115Action({0, false}, red), E115Action::Regenerate);
    EXPECT_EQ(e115Action({1, false}, red), E115Action::Eat); // not full: falls through to eating
    EXPECT_EQ(e115Action({0, true}, red), E115Action::Eat);
    red.canEat = false;
    EXPECT_EQ(e115Action({1, false}, red), E115Action::Pass);

    E115Use sneak{.sneaking = true};
    EXPECT_EQ(e115Action({7, false}, sneak), E115Action::TakeSlice);
    EXPECT_FALSE(e115After({7, false}, E115Action::TakeSlice).has_value());
    EXPECT_EQ(e115Action({2, true}, sneak), E115Action::Eat);
    sneak.emptyHands = false;
    EXPECT_EQ(e115Action({2, false}, sneak), E115Action::Pass);

    E115Use hand{};
    EXPECT_EQ(e115Action({4, false}, hand), E115Action::Eat);
    EXPECT_EQ(*e115After({4, false}, E115Action::Eat), (E115{5, false}));
    EXPECT_FALSE(e115After({7, true}, E115Action::Eat).has_value());
    EXPECT_EQ(e115EatenCount({4, false}), 4);
}

TEST(ProvisionsE115, Redstone) {
    EXPECT_EQ(e115Comparator({0, false}), 8);
    EXPECT_EQ(e115Comparator({7, false}), 1);
    EXPECT_EQ(e115Comparator({0, true}), 15);
    EXPECT_EQ(e115Comparator({7, true}), 8);
    EXPECT_EQ(e115Signal({0, false}), 0);
    EXPECT_EQ(e115Signal({0, true}), 15);
    EXPECT_EQ(e115Signal({7, true}), 1);
    EXPECT_EQ(*e115RandomTick({5, true}), (E115{4, true}));
    EXPECT_FALSE(e115RandomTick({5, false}).has_value());
    EXPECT_FALSE(e115RandomTick({0, true}).has_value());
}

TEST(ProvisionsFrosted, Numbers) {
    EXPECT_DOUBLE_EQ(frostedSpeedFactor(0), 0.85);
    EXPECT_DOUBLE_EQ(frostedSpeedFactor(2), 0.55);
    EXPECT_EQ(frostedFrozenTick(0, 0, true), 1);
    EXPECT_EQ(frostedFrozenTick(10, 2, true), 13);
    EXPECT_EQ(frostedFrozenTick(139, 3, true), 140);
    EXPECT_EQ(frostedFrozenTick(10, 2, false), 8);
    EXPECT_EQ(thawTick(1), 0);
    EXPECT_FLOAT_EQ(frostedFreezeDamage(1.0f, 3), 2.0f);
    EXPECT_FLOAT_EQ(frostedFreezeDamage(5.0f, 1), 5.0f);
    EXPECT_EQ(frostedAfterFire(0), -1);
    EXPECT_FLOAT_EQ(freezeDamage(true), 5.0f);
}

TEST(ProvisionsEnchant, LevelBasedValues) {
    EXPECT_FLOAT_EQ(reactChance(1), 0.15f);
    EXPECT_FLOAT_EQ(reactChance(3), 0.45f);
    EXPECT_FLOAT_EQ(fireReactSeconds(1), 2.0f);
    EXPECT_FLOAT_EQ(fireReactSeconds(3), 8.0f);
    EXPECT_EQ(chillAuraAmplifier(1), 0);
    EXPECT_EQ(chillAuraAmplifier(3), 2);
    EXPECT_FLOAT_EQ(destructionDamage(2), -3.0f);
    EXPECT_EQ(destructionIncorrectTag(1), "minecraft:incorrect_for_wooden_tool");
    EXPECT_EQ(destructionIncorrectTag(2), "minecraft:incorrect_for_stone_tool");
    EXPECT_EQ(destructionIncorrectTag(3), "minecraft:incorrect_for_iron_tool");
}

TEST(ProvisionsMusic, Songs) {
    int total = 0;
    for (auto const& s : songs()) {
        total += s.seconds;
        EXPECT_EQ(s.comparator, 15);
    }
    EXPECT_EQ(total, 123 + 195 + 192 + 215 + 173 + 196 + 207 + 201 + 169);
    EXPECT_EQ(songs()[4].key, "warfarer");
    EXPECT_EQ(songs()[4].track, "wayfarer");
}
