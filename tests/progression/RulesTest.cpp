// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of TF's biome restrictions, enforcement timing, the locked biome toast countdown and the
// advancement tree layout (U14; TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <set>

#include "core/progression/Rules.h"

using namespace gf::core::progression;
using nlohmann::json;

TEST(ProgressionRules, RestrictionJson) {
    auto j = json::parse(R"({"advancements":["twilightforest:progress_lich"],"enforcement":"twilightforest:darkness","locked_biome_toast":{"count":1,"id":"twilightforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"twilightforest:knight_stronghold"})");
    auto r = Restriction::fromJson("twilightforest:dark_forest", j);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->enforcement, Enforcement::Darkness);
    EXPECT_EQ(r->structureKey, "twilightforest:knight_stronghold");
    EXPECT_EQ(r->toastIcon, "twilightforest:lich_tower_miniature_structure");
    EXPECT_FLOAT_EQ(r->multiplier, 0.0f);
    std::set<std::string> done;
    auto has = [&](std::string const& a) { return done.contains(a); };
    EXPECT_FALSE(r->liftedFor(has));
    done.insert("twilightforest:progress_lich");
    EXPECT_TRUE(r->liftedFor(has));
    EXPECT_FALSE(Restriction::fromJson("x", json::parse(R"({"enforcement":"twilightforest:fire","multiplier":1,"advancements":[]})")));
    EXPECT_FALSE(Restriction::fromJson("x", json::parse(R"({"enforcement":"twilightforest:fire","advancements":["a"]})")));
}

TEST(ProgressionRules, NestedGates) {
    // darkness, hunger, fire and frost: every 60 ticks (lcm of 20 and 60)
    int count = 0;
    for (int t = 1; t <= 600; ++t)
        if (enforcementDue(Enforcement::Darkness, t)) ++count;
    EXPECT_EQ(count, 10);
    EXPECT_TRUE(enforcementDue(Enforcement::Fire, 120));
    EXPECT_FALSE(enforcementDue(Enforcement::Fire, 100));
    // acid rain: inner 5, but only on the outer 20-tick ticks: every 20 ticks, not 5
    count = 0;
    for (int t = 1; t <= 600; ++t)
        if (enforcementDue(Enforcement::AcidRain, t)) ++count;
    EXPECT_EQ(count, 30);
    EXPECT_FALSE(enforcementDue(Enforcement::AcidRain, 25));
    EXPECT_FALSE(enforcementDue(Enforcement::Unknown, 60));
    EXPECT_EQ(hungerAmplifier(-1, 1.0f), 1);
    EXPECT_EQ(hungerAmplifier(2, 1.0f), 3);
    EXPECT_EQ(hungerAmplifier(-1, 1.5f), 1);
}

TEST(ProgressionRules, LockedToastCountdown) {
    LockedToastCountdown c;
    int shown = 0;
    for (int i = 0; i < 59; ++i) shown += c.step(true);
    EXPECT_EQ(shown, 0);
    EXPECT_TRUE(c.step(true)) << "the 60th qualifying step shows the toast";
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(c.step(true)) << "only once per visit";
    EXPECT_LT(c.timeUntilToast, 0) << "the counter keeps decrementing while the player stays";
    EXPECT_FALSE(c.step(false));
    EXPECT_EQ(c.timeUntilToast, 60);
    EXPECT_FALSE(c.shownToast);
    // leaving before the toast keeps the partial countdown
    for (int i = 0; i < 30; ++i) c.step(true);
    c.step(false);
    EXPECT_EQ(c.timeUntilToast, 30);
    for (int i = 0; i < 29; ++i) EXPECT_FALSE(c.step(true));
    EXPECT_TRUE(c.step(true));
}

TEST(ProgressionRules, TreeLayout) {
    std::vector<TreeNode> nodes{{"root", ""}, {"a", "root"}, {"a1", "a"}, {"a2", "a"}, {"b", "root"}};
    layoutTree(nodes);
    EXPECT_FLOAT_EQ(nodes[0].x, 0);
    EXPECT_FLOAT_EQ(nodes[1].x, 1);
    EXPECT_FLOAT_EQ(nodes[2].x, 2);
    EXPECT_FLOAT_EQ(nodes[2].y, 0);
    EXPECT_FLOAT_EQ(nodes[3].y, 1);
    EXPECT_FLOAT_EQ(nodes[1].y, 0.5f);
    EXPECT_FLOAT_EQ(nodes[4].y, 2);
    EXPECT_FLOAT_EQ(nodes[0].y, 1.25f);
}
