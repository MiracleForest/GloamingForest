// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of TF's charm, casket, shield, flask, ore meter, scepter and moon dial rules (U9):
// CharmEvents, TFItemStackUtils, FortificationShieldAttachment, PotionFlaskComponent,
// OreScannerComponent, ScepterRepairRecipe, MoonDialItem (TeamTwilight/twilightforest @ 9e40a757,
// LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include "core/artifacts/Artifacts.h"

using namespace gf::core::artifacts;

namespace {
Slot s(char const* id, int n = 1) { return Slot{id, n}; }
constexpr char kDirt[]   = "minecraft:dirt";
constexpr char kSword[]  = "minecraft:iron_sword";
constexpr char kHelmet[] = "minecraft:iron_helmet";
} // namespace

TEST(Artifacts, KeepingThreeKeepsEverythingAndEveryCasket) {
    Inventory inv;
    inv.main[0]  = s(kSword);
    inv.main[20] = s(kDirt, 64);
    inv.main[5]  = s(kCasket, 3);
    inv.main[30] = s(kCasket, 1);
    inv.main[35] = s(kKeeping3, 2);
    inv.armor[3] = s(kHelmet);
    auto r = applyKeeping(inv);
    EXPECT_EQ(r.tier, 3);
    ASSERT_TRUE(r.consumedFrom);
    EXPECT_EQ(r.consumedFrom->area, SlotRef::Area::Main);
    EXPECT_EQ(r.consumedFrom->index, 35);
    EXPECT_TRUE(inv.empty()); // the remaining charm is kept too
    EXPECT_EQ(r.kept.main[5], s(kCasket, 3));
    EXPECT_EQ(r.kept.main[30], s(kCasket, 1));
    EXPECT_EQ(r.kept.main[35], s(kKeeping3, 1));
    EXPECT_EQ(r.kept.armor[3], s(kHelmet));
}

TEST(Artifacts, KeepingTwoKeepsTheHotbarAndLeavesOneCasket) {
    Inventory inv;
    inv.main[1]  = s(kCasket, 2);
    inv.main[2]  = s(kCasket, 4);
    inv.main[3]  = s(kKeeping2);
    inv.main[10] = s(kDirt, 5);
    inv.armor[0] = s(kCasket, 1);
    auto r = applyKeeping(inv);
    EXPECT_EQ(r.tier, 2);
    EXPECT_EQ(r.kept.main[1], s(kCasket, 1)); // the first casket stack: one stays for placing
    EXPECT_EQ(inv.main[1], s(kCasket, 1));
    EXPECT_EQ(r.kept.main[2], s(kCasket, 4)); // later stacks whole
    EXPECT_EQ(r.kept.armor[0], s(kCasket, 1)); // a casket was left in the main slots: armour whole
    EXPECT_EQ(inv.main[10], s(kDirt, 5));      // outside the hotbar
    EXPECT_TRUE(r.kept.main[10].empty());
    EXPECT_TRUE(inv.main[3].empty());           // the consumed charm
}

TEST(Artifacts, KeepingTwoSingleCasketIsNotKept) {
    Inventory inv;
    inv.main[0] = s(kCasket, 1);
    inv.main[1] = s(kKeeping2);
    inv.main[2] = s(kDirt);
    auto r      = applyKeeping(inv);
    EXPECT_EQ(r.tier, 2);
    EXPECT_TRUE(r.kept.main[0].empty());
    EXPECT_EQ(inv.main[0], s(kCasket, 1));
    EXPECT_EQ(r.kept.main[2], s(kDirt));
}

TEST(Artifacts, KeepingOneKeepsOnlyArmourAndOffhand) {
    Inventory inv;
    inv.selected = 0;
    inv.main[0]  = s(kSword);
    inv.main[4]  = s(kKeeping1);
    inv.armor[2] = s("minecraft:iron_chestplate");
    inv.offhand  = s("minecraft:shield");
    auto r       = applyKeeping(inv);
    EXPECT_EQ(r.tier, 1);
    EXPECT_EQ(inv.main[0], s(kSword)); // TF quirk: the selected stack is not kept
    EXPECT_EQ(r.kept.armor[2], s("minecraft:iron_chestplate"));
    EXPECT_EQ(r.kept.offhand, s("minecraft:shield"));
    EXPECT_TRUE(inv.main[4].empty());
}

TEST(Artifacts, KeepingOneNotConsumedWithoutArmourOrOffhand) {
    Inventory inv;
    inv.main[0] = s(kSword);
    inv.main[4] = s(kKeeping1);
    auto r      = applyKeeping(inv);
    EXPECT_EQ(r.tier, 0);
    EXPECT_EQ(inv.main[4], s(kKeeping1));
    EXPECT_FALSE(r.consumedFrom);
}

TEST(Artifacts, KeepingTriesTheTiersInOrderAndIgnoresTheCharmItself) {
    Inventory inv;
    inv.main[0] = s(kKeeping3); // only the charm: tier III does not apply, nothing is consumed
    auto r      = applyKeeping(inv);
    EXPECT_EQ(r.tier, 0);
    EXPECT_EQ(inv.main[0], s(kKeeping3));
    // charm II's own stacks do not count either, but charm III's do for tier II
    Inventory both;
    both.main[0] = s(kKeeping2);
    both.main[20] = s(kKeeping3);
    auto rb      = applyKeeping(both);
    EXPECT_EQ(rb.tier, 3); // charm II counts for tier III
    EXPECT_EQ(rb.kept.main[0], s(kKeeping2));
    // armour first when consuming
    Inventory inv2;
    inv2.armor[1] = s(kKeeping2);
    inv2.main[0]  = s(kKeeping2);
    inv2.main[2]  = s(kDirt);
    auto r2       = applyKeeping(inv2);
    EXPECT_EQ(r2.tier, 2);
    ASSERT_TRUE(r2.consumedFrom);
    EXPECT_EQ(r2.consumedFrom->area, SlotRef::Area::Armor);
}

TEST(Artifacts, KeepingOneNeedsAHotbarSelection) {
    Inventory inv;
    inv.selected = 12;
    inv.main[4]  = s(kKeeping1);
    inv.offhand  = s(kDirt);
    EXPECT_EQ(applyKeeping(inv).tier, 0);
}

TEST(Artifacts, CasketLayout) {
    Inventory inv;
    inv.armor[3] = s(kHelmet);
    inv.armor[0] = s("minecraft:iron_boots");
    inv.offhand  = s("minecraft:shield");
    inv.main[0]  = s(kSword);
    inv.main[9]  = s(kDirt, 3);
    auto l       = casketLayout(inv);
    ASSERT_EQ(l.size(), 45u);
    EXPECT_EQ(l[0], s(kHelmet));
    EXPECT_EQ(l[3], s("minecraft:iron_boots"));
    for (int i = 4; i < 8; ++i) EXPECT_TRUE(l[static_cast<std::size_t>(i)].empty());
    EXPECT_EQ(l[8], s("minecraft:shield"));
    EXPECT_EQ(l[9], s(kDirt, 3));
    EXPECT_EQ(l[36], s(kSword));
    EXPECT_TRUE(hasNonCasketItem(inv));
    Inventory caskets;
    caskets.main[3] = s(kCasket, 2);
    EXPECT_FALSE(hasNonCasketItem(caskets));
}

TEST(Artifacts, CasketPlacementAndDamage) {
    EXPECT_EQ(casketStartY(-70, -64, 384), -62);
    EXPECT_EQ(casketStartY(400, -64, 384), 383);
    EXPECT_EQ(casketStartY(384, -64, 384), 384);
    EXPECT_EQ(casketStartY(64, -64, 384), 64);
    EXPECT_EQ(rollCasketDamage(0, 0.15F).damage, 1);
    EXPECT_EQ(rollCasketDamage(0, 0.16F).damage, 0);
    EXPECT_TRUE(rollCasketDamage(2, 0.1F).skullChest);
    EXPECT_FALSE(rollCasketDamage(1, 0.1F).skullChest);
    EXPECT_STREQ(facingName(0), "south");
    EXPECT_STREQ(facingName(1), "west");
    EXPECT_STREQ(facingName(2), "north");
    EXPECT_EQ(casketName("AVeryLongPlayerName", "Keepsake Casket"), "AVeryLongPla's Keepsake Casket");
}

TEST(Artifacts, Shields) {
    Shields sh;
    sh.set(5, true);
    EXPECT_EQ(sh.left(), 5);
    int expired = 0;
    for (int t = 1; t <= 241; ++t) expired += sh.tick(false, false, t) ? 1 : 0;
    EXPECT_EQ(expired, 1); // 240 ticks of decay, then one expires
    EXPECT_EQ(sh.temporary, 4);
    sh.add(3, false);
    sh.breakOne();
    EXPECT_EQ(sh.temporary, 3);
    EXPECT_EQ(sh.permanent, 3);
    sh.set(500, false);
    EXPECT_EQ(sh.permanent, kShieldMax);
    // the crown slows the decay by a third
    Shields crown;
    crown.set(1, true);
    int t = 0;
    while (!crown.tick(false, true, ++t)) {}
    EXPECT_GT(t, 350);
    Shields inv;
    inv.set(1, true);
    for (int i = 0; i < 1000; ++i) EXPECT_FALSE(inv.tick(true, false, i));
}

TEST(Artifacts, PotionsAndFlasks) {
    ASSERT_NE(potionByAux(24), nullptr);
    EXPECT_EQ(potionByAux(24)->javaId, "minecraft:strong_harming");
    EXPECT_EQ(potionColor(0), 0x385DC6u);
    EXPECT_EQ(potionColor(14), 0x33EBFFu);
    Flask f;
    EXPECT_TRUE(f.canFill(14));
    f = f.addDose(14).addDose(14);
    EXPECT_FALSE(f.canFill(15));
    EXPECT_TRUE(f.canFill(14));
    f = f.addDose(14);
    EXPECT_FALSE(f.canFill(14));
    EXPECT_EQ(f.barWidth(), 13);
    f = f.removeDose();
    EXPECT_EQ(f.breakage, 1);
    EXPECT_EQ(f.barWidth(), 9);
    EXPECT_FALSE(f.canFill(14)); // 2 doses, breakage 1: no room
    f = f.removeDose().removeDose();
    EXPECT_TRUE(f.empty());
    EXPECT_EQ(f.breakage, 3);
    Flask g{-1, 0, 0, false};
    g = g.addDose(5).removeDose();
    EXPECT_EQ(g.breakage, 0);
    FlaskTracking t;
    EXPECT_EQ(t.track("minecraft:strong_harming", 100), std::make_pair(1, 0));
    EXPECT_EQ(t.track("minecraft:strong_harming", 150), std::make_pair(2, 2));
    EXPECT_EQ(t.track("minecraft:healing", 160), std::make_pair(1, 0));
}

TEST(Artifacts, OreScan) {
    auto s = OreScan::fromCenter(5, 20, 1, oreScanTicks(1));
    EXPECT_EQ(s.originX, -16);
    EXPECT_EQ(s.originZ, 0);
    EXPECT_EQ(s.xSpan, 47);
    EXPECT_EQ(s.duration, 75);
    EXPECT_EQ(s.centerChunk(), std::make_pair(0, 1));
    int calls = 0;
    while (!s.finished()) s.tick(-64, 4, [&](int, int y, int) {
        ++calls;
        return y == -64 ? std::string("minecraft:diamond_ore") : std::string("minecraft:stone");
    });
    EXPECT_EQ(calls, 47 * 47 * 4);
    EXPECT_EQ(s.counts["minecraft:diamond_ore"], 47 * 47);
    EXPECT_EQ(oreRatio(1, 3), "33.333");
    EXPECT_EQ(javaStringHash("abc"), 96354);
    EXPECT_NE(oreUniversalId({{"a", 1}}, 1, 2, 1, 100), 0);
}

TEST(Artifacts, ScepterRepair) {
    EXPECT_TRUE(scepterRepairMatches(9, 1, 9));
    EXPECT_FALSE(scepterRepairMatches(9, 2, 9)); // would overshoot by a whole ingredient
    EXPECT_TRUE(scepterRepairMatches(10, 2, 9));
    EXPECT_EQ(scepterRepairResult(10, 2, 9), 0);
    EXPECT_EQ(scepterRepairResult(50, 2, 9), 32);
    EXPECT_FALSE(scepterRepairMatches(0, 1, 9));
}

TEST(Artifacts, MoonDial) {
    EXPECT_EQ(moonPhaseKey(true, 0, false), "0");
    EXPECT_EQ(moonPhaseKey(true, 24000 * 9 + 5, false), "1");
    EXPECT_EQ(moonPhaseKey(false, 0, false), "unknown");
    EXPECT_EQ(moonPhaseKey(false, 0, true), "unknown_fools");
}
