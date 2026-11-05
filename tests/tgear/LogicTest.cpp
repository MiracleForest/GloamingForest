// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of src/core/tgear/Logic.h (translated from TF 4.8's ItemDisplayContents, TravellersArmorBeltItem,
// TravellersGearLogic, TooltipStringInterpolator, ClockDisplay and MoonDialDisplay; LGPL-2.1-or-later
// code, see the header of Logic.h).
#include <cmath>
#include <gtest/gtest.h>

#include "core/tgear/Logic.h"

using namespace gf::core::tgear;

namespace {
DisplayItem map(std::string id) { return {std::move(id), DisplayType::Map}; }
DisplayItem compass() { return {"minecraft:compass", DisplayType::Compass}; }
} // namespace

TEST(TgearDisplay, InsertFillsFirstEmptySlotOfItsType) {
    DisplayContents c;
    auto            r = trySwap(c, compass());
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.slot, 3);
    EXPECT_TRUE(r.replaced.empty());
    auto m = trySwap(c, map("map#1"));
    EXPECT_EQ(m.slot, 0);
    auto m2 = trySwap(c, map("map#2"));
    EXPECT_EQ(m2.slot, 1);
    EXPECT_EQ(c.chosenMapSlot, 0) << "a second map keeps the chosen slot";
}

TEST(TgearDisplay, SwapReplacesFirstSlotUnlessIdentical) {
    DisplayContents c;
    (void)trySwap(c, compass());
    auto same = trySwap(c, compass());
    EXPECT_FALSE(same.ok) << "isSameItemSameComponents refuses";
    auto other = trySwap(c, DisplayItem{"minecraft:compass{lodestone}", DisplayType::Compass});
    ASSERT_TRUE(other.ok);
    EXPECT_EQ(other.slot, 3);
    EXPECT_EQ(other.replaced.identity, "minecraft:compass");
    EXPECT_FALSE(trySwap(c, DisplayItem{"minecraft:stick", DisplayType::None}).ok);
}

TEST(TgearDisplay, FirstMapResetsChosenSlot) {
    DisplayContents c;
    c.chosenMapSlot = -1;
    (void)trySwap(c, map("a"));
    EXPECT_EQ(c.chosenMapSlot, 0);
    c.chosenMapSlot = -1;
    (void)trySwap(c, map("b")); // other maps exist: no reset
    EXPECT_EQ(c.chosenMapSlot, -1);
}

TEST(TgearDisplay, CycleAndRemove) {
    DisplayContents c;
    (void)trySwap(c, map("a"));
    (void)trySwap(c, map("b"));
    (void)trySwap(c, map("c"));
    EXPECT_EQ(cycleChosenMapSlot(c), 1);
    EXPECT_EQ(cycleChosenMapSlot(c), 2);
    EXPECT_EQ(cycleChosenMapSlot(c), -1);
    EXPECT_EQ(cycleChosenMapSlot(c), 0) << "from -1 the first map again";
    // removing the chosen map moves the choice on
    EXPECT_EQ(removeFirstFree(c), 0);
    EXPECT_EQ(c.chosenMapSlot, 1);
    EXPECT_EQ(removeFirstFree(c, [](DisplayItem const& i) { return i.identity == "c"; }), 2);
    EXPECT_EQ(removeFirstFree(c), 1);
    EXPECT_EQ(c.chosenMapSlot, -1);
    EXPECT_EQ(removeFirstFree(c), -1);
}

TEST(TgearBelt, SwapPlan) {
    std::array<HotbarSlot, 9> hotbar{};
    std::array<bool, 9>       belt{};
    belt.fill(true);
    hotbar[0] = {false, true};
    hotbar[1] = {false, false}; // blacklisted / a belt
    auto plan = planHotbarSwap(hotbar, belt, true);
    EXPECT_TRUE(plan.exchange[0]);
    EXPECT_FALSE(plan.exchange[1]);
    EXPECT_TRUE(plan.exchange[2]);
    EXPECT_TRUE(plan.changed);
    // inactive (broken): only into empty hotbar slots
    auto inactive = planHotbarSwap(hotbar, belt, false);
    EXPECT_FALSE(inactive.exchange[0]);
    EXPECT_TRUE(inactive.exchange[2]);
    EXPECT_FALSE(inactive.changed) << "both empty everywhere it exchanged";
}

TEST(TgearRepair, Chance) {
    EXPECT_DOUBLE_EQ(autoRepairChance(0.001, false, true, true), 0.001);
    EXPECT_DOUBLE_EQ(autoRepairChance(0.001, true, false, false), 0.001);
    EXPECT_NEAR(autoRepairChance(0.001, true, false, true), 1 - std::pow(0.999, 3), 1e-12);
    EXPECT_NEAR(autoRepairChance(0.001, true, true, false), 1 - std::pow(0.999, 1.5), 1e-12);
}

TEST(TgearHud, Clock) {
    EXPECT_EQ(clockText(0, true), "06:00");
    EXPECT_EQ(clockText(18000, true), "00:00");
    EXPECT_EQ(clockText(6000, false), "12:00 PM");
    EXPECT_EQ(clockText(18000, false), "12:00 AM");
    EXPECT_EQ(clockText(1000, true), "07:00");
    EXPECT_EQ(clockFrame(0), 0);
    EXPECT_EQ(clockFrame(501), 1);
    EXPECT_EQ(clockFrame(12500), 2);
    EXPECT_EQ(clockFrame(14000), 3);
    EXPECT_EQ(clockFrame(22000), 0);
    EXPECT_EQ(clockFrame(24000 + 600), 1);
    EXPECT_EQ(moonIconUv(5), (std::array<int, 2>{8, 8}));
}

TEST(TgearTooltip, Interpolate) {
    auto name = [](std::string const& k) { return k == "key.twilightforest.swap_hotbar" ? std::string("V") : std::string("Nonexistent key"); };
    EXPECT_EQ(interpolate("Swap Hotbar (keybind: ${tfkeybinds/key.twilightforest.swap_hotbar})", name), "Swap Hotbar (keybind: V)");
    EXPECT_EQ(interpolate("a ${tfkeybinds/x} b ${other/y} c", name), "a Nonexistent key b ${other/y} c");
    EXPECT_EQ(interpolate("plain", name), "plain");
}

TEST(TgearDodge, OnlyAProjectilesOwnImpactIsDodged) {
    for (auto const* t : {"minecraft:arrow", "minecraft:trident", "minecraft:thrown", "minecraft:mob_projectile", "minecraft:fireball", "minecraft:wither_skull"})
        EXPECT_TRUE(isProjectileImpactType(t)) << t;
    for (auto const* t : {"minecraft:explosion", "minecraft:player_explosion", "minecraft:fireworks", "minecraft:in_fire", "minecraft:magic", "minecraft:indirect_magic", ""})
        EXPECT_FALSE(isProjectileImpactType(t)) << t;
}
