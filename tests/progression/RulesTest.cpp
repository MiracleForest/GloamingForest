// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of upstream's biome restrictions, enforcement timing, the locked biome toast countdown and the
// advancement tree layout (U14; upstream commit 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>

#include "core/Format.h"
#include "core/progression/Rules.h"

using namespace gf::core::progression;
using nlohmann::json;

TEST(ProgressionRules, RestrictionJson) {
    auto j = json::parse(R"({"advancements":["gloamingforest:progress_lich"],"enforcement":"gloamingforest:darkness","locked_biome_toast":{"count":1,"id":"gloamingforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"gloamingforest:knight_stronghold"})");
    auto r = Restriction::fromJson("gloamingforest:dark_forest", j);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->enforcement, Enforcement::Darkness);
    EXPECT_EQ(r->structureKey, "gloamingforest:knight_stronghold");
    EXPECT_EQ(r->toastIcon, "gloamingforest:lich_tower_miniature_structure");
    EXPECT_FLOAT_EQ(r->multiplier, 0.0f);
    std::set<std::string> done;
    auto has = [&](std::string const& a) { return done.contains(a); };
    EXPECT_FALSE(r->liftedFor(has));
    done.insert("gloamingforest:progress_lich");
    EXPECT_TRUE(r->liftedFor(has));
    EXPECT_FALSE(Restriction::fromJson("x", json::parse(R"({"enforcement":"gloamingforest:fire","multiplier":1,"advancements":[]})")));
    EXPECT_FALSE(Restriction::fromJson("x", json::parse(R"({"enforcement":"gloamingforest:fire","advancements":["a"]})")));
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

namespace {
struct GoldenNode {
    char const* id;
    char const* parent;
    bool        display;
};
struct GoldenPos {
    char const* id;
    float       x, y;
};
struct GoldenTree {
    char const*             name;
    std::vector<GoldenNode> nodes;
    std::vector<GoldenPos>  positions;
};
// Minecraft 1.21.1's TreeNodePosition output (tests/progression/oracle/TreeNodePositionOracle.java)
std::vector<GoldenTree> const kGolden{
#include "TreeLayoutGolden.inc"
};
} // namespace

TEST(ProgressionRules, TreeLayoutMatchesJava) {
    ASSERT_GE(kGolden.size(), 41u);
    for (auto const& g : kGolden) {
        std::vector<TreeNode> nodes;
        for (auto const& n : g.nodes) nodes.push_back({n.id, n.parent, n.display});
        layoutTree(nodes);
        std::size_t placed = 0;
        for (auto const& n : nodes) placed += n.placed ? 1 : 0;
        EXPECT_EQ(placed, g.positions.size()) << g.name << ": every displayed node is placed";
        for (auto const& p : g.positions) {
            auto it = std::find_if(nodes.begin(), nodes.end(), [&](TreeNode const& n) { return n.id == p.id; });
            ASSERT_NE(it, nodes.end()) << g.name << " " << p.id;
            EXPECT_TRUE(it->placed) << g.name << " " << p.id;
            // exact: the same single-precision arithmetic as Java
            EXPECT_EQ(it->x, p.x) << g.name << " " << p.id;
            EXPECT_EQ(it->y, p.y) << g.name << " " << p.id;
        }
    }
}

TEST(ProgressionRules, TreeLayoutGloamingTree) {
    auto const& upsrc = kGolden.front();
    ASSERT_STREQ(upsrc.name, "gloamingforest");
    EXPECT_EQ(upsrc.nodes.size(), 45u);
    std::vector<TreeNode> nodes;
    for (auto const& n : upsrc.nodes) nodes.push_back({n.id, n.parent, n.display});
    layoutTree(nodes);
    auto at = [&](char const* id) {
        return *std::find_if(nodes.begin(), nodes.end(), [&](TreeNode const& n) { return n.id == std::string("gloamingforest:") + id; });
    };
    EXPECT_EQ(at("root").x, 0);
    EXPECT_EQ(at("progression_end").x, 7) << "root > progress_naga > progress_lich > progress_merge > beanstalk > giants > progress_troll > end";
    float minY = 1e9f;
    for (auto const& n : nodes) minY = std::min(minY, n.y);
    EXPECT_EQ(minY, 0.0f) << "thirdWalk moves the topmost node to row 0";
}

TEST(ProgressionRules, TreeLayoutSkipsNodesWithoutDisplay) {
    // a display-less node's children hang from its displayed parent, one column further
    std::vector<TreeNode> nodes{{"r", ""}, {"hidden", "r", false}, {"a", "hidden"}, {"b", "hidden"}, {"c", "r"}};
    layoutTree(nodes);
    EXPECT_FALSE(nodes[1].placed);
    EXPECT_EQ(nodes[2].x, 1);
    EXPECT_EQ(nodes[3].x, 1);
    EXPECT_EQ(nodes[2].y, 0);
    EXPECT_EQ(nodes[3].y, 1);
    EXPECT_EQ(nodes[4].y, 2);
    EXPECT_EQ(nodes[0].y, 1);
    // a root without a display is not positioned at all
    std::vector<TreeNode> bare{{"r", "", false}, {"a", "r"}};
    layoutTree(bare);
    EXPECT_FALSE(bare[0].placed);
    EXPECT_FALSE(bare[1].placed);
}

TEST(ProgressionRules, RenderComponent) {
    std::map<std::string, std::string> lang{
        {"advancement.gloamingforest.hill1.desc", "Defeat a %s in a Small Hollow Hill"},
        {"entity.gloamingforest.redcap", "Redcap"},
        {"advancement.gloamingforest.progress_merge.desc", "Defeat the %s, the %s and the %s"},
        {"entity.gloamingforest.hydra", "Hydra"},
        {"entity.gloamingforest.ur_ghast", "Ur-Ghast"},
        {"pos", "%2$s before %1$s"},
    };
    TranslateFn tr = [&](std::string const& key, std::string const& fallback, std::vector<std::string> const& args) {
        auto it = lang.find(key);
        return gf::core::formatMessage(it == lang.end() ? fallback : it->second, args);
    };
    auto hill1 = json::parse(R"({"translate":"advancement.gloamingforest.hill1.desc","with":[{"translate":"entity.gloamingforest.redcap"}]})");
    EXPECT_EQ(renderComponent(hill1, tr), "Defeat a Redcap in a Small Hollow Hill");
    auto merge = json::parse(
        R"({"translate":"advancement.gloamingforest.progress_merge.desc","with":[{"translate":"entity.gloamingforest.hydra"},{"translate":"entity.gloamingforest.ur_ghast"},{"translate":"entity.gloamingforest.snow_queen","fallback":"Snow Queen"}]})");
    EXPECT_EQ(renderComponent(merge, tr), "Defeat the Hydra, the Ur-Ghast and the Snow Queen");
    EXPECT_EQ(renderComponent(json::parse(R"({"translate":"pos","with":["a",{"text":"b"}]})"), tr), "b before a");
    EXPECT_EQ(renderComponent(json(""), tr), "");
    EXPECT_EQ(renderComponent(json::parse(R"({"text":"x","extra":[{"text":"y"},"z"]})"), tr), "xyz");
    EXPECT_EQ(renderComponent(json::parse(R"({"translate":"missing.key"})"), tr), "missing.key");
}
