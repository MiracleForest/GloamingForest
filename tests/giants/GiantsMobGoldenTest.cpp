// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of U29's giants against F13's Java goldens (tests/golden/mobs.json): every attribute the
// golden row lists that Bedrock has a component for, read from the unit's behavior pack entities
// (assets/pack/bp/entities/giants/<name>.json). The live actors are checked by giants.giant_mobs.
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

} // namespace

TEST(GiantsMobs, MatchJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    for (char const* name : {"giant_miner", "armored_giant"}) {
        SCOPED_TRACE(name);
        std::string const id  = std::string("gloamingforest:") + name;
        auto const&       row = golden.at("entities").at(id);
        auto const        bp  = load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/giants" / (std::string(name) + ".json"));
        auto const&       c   = bp.at("minecraft:entity").at("components");
        EXPECT_EQ(bp.at("minecraft:entity").at("description").at("identifier"), id);
        EXPECT_DOUBLE_EQ(c.at("minecraft:health").at("max").get<double>(), row.at("maxHealth").get<double>());
        EXPECT_DOUBLE_EQ(c.at("minecraft:health").at("value").get<double>(), row.at("health").get<double>());
        EXPECT_NEAR(c.at("minecraft:movement").at("value").get<double>(), attr(row, "minecraft:generic.movement_speed"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:attack").at("damage").get<double>(), attr(row, "minecraft:generic.attack_damage"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:follow_range").at("value").get<double>(), attr(row, "minecraft:generic.follow_range"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:variable_max_auto_step").at("base_value").get<double>(), attr(row, "minecraft:generic.step_height"), 1e-6);
        EXPECT_NEAR(c.at("minecraft:collision_box").at("width").get<double>(), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(c.at("minecraft:collision_box").at("height").get<double>(), row.at("height").get<double>(), 1e-6);
        EXPECT_NEAR(attr(row, "minecraft:generic.knockback_resistance"), 0.0, 1e-9);
        EXPECT_NEAR(attr(row, "minecraft:generic.armor"), 0.0, 1e-9) << "an armor component would be needed";
        // the experience reward: Monster's 5 (+ equipment), dropped only after a player's hit
        ASSERT_TRUE(c.contains("minecraft:experience_reward"));
        EXPECT_EQ(c.at("minecraft:experience_reward").at("on_death").get<std::string>(), "query.last_hit_by_player ? 5 : 0");
        // setDropChance(slot, 0) for every slot
        for (auto const& s : c.at("minecraft:equipment").at("slot_drop_chance")) EXPECT_EQ(s.at("drop_chance").get<double>(), 0.0);
    }
}
