// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of U17's mobs against F13's Java goldens (tests/golden/mobs.json): every attribute the
// golden row lists that Bedrock has a component for, read from the unit's behavior pack entities
// (assets/pack/bp/entities/hostiles/<name>.json): health, movement speed, attack damage, follow
// range, the collision box, the experience reward and fire immunity; the projectiles' boxes. The
// armour of the redcap sapper and the pinch beetle (2) is native (Module.cpp); the live actors are
// checked by the selftest hostiles.mob_goldens.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#include "../golden/GoldenRepo.h"

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

json load(fs::path const& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << p.string();
    if (!in.good()) return json::object();
    return json::parse(in, nullptr, true, true);
}

double attr(json const& row, char const* key) { return row.at("attributes").at(key).at("value").get<double>(); }

json bpOf(std::string const& name) { return load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/hostiles" / (name + ".json")); }

} // namespace

TEST(HostilesMobs, MatchJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    for (char const* name : {"hostile_wolf", "mist_wolf", "winter_wolf", "king_spider", "hedge_spider", "swarm_spider", "skeleton_druid", "redcap",
                             "redcap_sapper", "wraith", "fire_beetle", "slime_beetle", "pinch_beetle"}) {
        SCOPED_TRACE(name);
        std::string const id  = std::string("twilightforest:") + name;
        auto const&       row = golden.at("entities").at(id);
        auto const        bp  = bpOf(name);
        auto const&       c   = bp.at("minecraft:entity").at("components");
        EXPECT_EQ(bp.at("minecraft:entity").at("description").at("identifier"), id);
        EXPECT_DOUBLE_EQ(c.at("minecraft:health").at("max").get<double>(), row.at("maxHealth").get<double>());
        EXPECT_DOUBLE_EQ(c.at("minecraft:health").at("value").get<double>(), row.at("health").get<double>());
        EXPECT_NEAR(c.at("minecraft:movement").at("value").get<double>(), attr(row, "minecraft:generic.movement_speed"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:attack").at("damage").get<double>(), attr(row, "minecraft:generic.attack_damage"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:follow_range").at("value").get<double>(), attr(row, "minecraft:generic.follow_range"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:collision_box").at("width").get<double>(), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(c.at("minecraft:collision_box").at("height").get<double>(), row.at("height").get<double>(), 1e-6);
        EXPECT_EQ(c.contains("minecraft:fire_immune"), row.at("fireImmune").get<bool>());
        EXPECT_NEAR(attr(row, "minecraft:generic.knockback_resistance"), 0.0, 1e-9);
        // the experience reward: a constant (min == max over 256 draws), dropped only when a player
        // hit it last (LivingEntity#dropExperience)
        ASSERT_EQ(row.at("experienceMin"), row.at("experienceMax"));
        EXPECT_EQ(c.at("minecraft:experience_reward").at("on_death").get<std::string>(),
                  "query.last_hit_by_player ? " + std::to_string(row.at("experienceMin").get<int>()) + " : 0");
        double armor = attr(row, "minecraft:generic.armor");
        bool   armored = std::string(name) == "redcap_sapper" || std::string(name) == "pinch_beetle";
        EXPECT_NEAR(armor, armored ? 2.0 : 0.0, 1e-9) << "the native armour list (Module.cpp) is out of date";
    }
}

TEST(HostilesMobs, ProjectileBoxes) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    for (char const* name : {"nature_bolt", "slime_blob"}) {
        SCOPED_TRACE(name);
        auto const& row = golden.at("entities").at(std::string("twilightforest:") + name);
        auto const& c   = bpOf(name).at("minecraft:entity").at("components");
        EXPECT_NEAR(c.at("minecraft:collision_box").at("width").get<double>(), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(c.at("minecraft:collision_box").at("height").get<double>(), row.at("height").get<double>(), 1e-6);
    }
}

// SkeletonDruid's baby: half the size, +50 % speed, x 2.5 experience (int)
TEST(HostilesMobs, BabyDruid) {
    auto const& groups = bpOf("skeleton_druid").at("minecraft:entity").at("component_groups");
    auto const& baby   = groups.at("gf:baby");
    EXPECT_NEAR(baby.at("minecraft:collision_box").at("width").get<double>(), 0.3, 1e-6);
    EXPECT_NEAR(baby.at("minecraft:collision_box").at("height").get<double>(), 0.995, 1e-6);
    EXPECT_NEAR(baby.at("minecraft:movement").at("value").get<double>(), 0.25 * 1.5, 1e-6);
    EXPECT_EQ(baby.at("minecraft:experience_reward").at("on_death").get<std::string>(), "query.last_hit_by_player ? 12 : 0");
}
