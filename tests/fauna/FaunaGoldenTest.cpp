// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of U16's mobs against F13's Java goldens (tests/golden/mobs.json): every attribute the
// golden row lists that Bedrock has a component for, read from the unit's behavior pack entities
// (assets/pack/bp/entities/fauna/<name>.json): health, movement speed, attack damage, follow range,
// step height, the collision box and the experience reward; and the client entities' spawn egg
// colours (TFEntities' egg colours). The live actors are checked by the selftest fauna.attributes.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <map>
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

double num(json const& v) { return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>(); }

char const* const kMobs[] = {"boar", "bighorn_sheep", "deer", "penguin", "tiny_bird", "squirrel", "dwarf_rabbit", "raven", "kobold", "mosquito_swarm"};

} // namespace

TEST(FaunaMobs, MatchJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    for (char const* name : kMobs) {
        SCOPED_TRACE(name);
        std::string const id  = std::string("gloamingforest:") + name;
        auto const&       row = golden.at("entities").at(id);
        auto const        bp  = load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/fauna" / (std::string(name) + ".json"));
        auto const&       c   = bp.at("minecraft:entity").at("components");
        EXPECT_EQ(bp.at("minecraft:entity").at("description").at("identifier"), id);
        EXPECT_DOUBLE_EQ(num(c.at("minecraft:health").at("max")), row.at("maxHealth").get<double>());
        EXPECT_DOUBLE_EQ(num(c.at("minecraft:health").at("value")), row.at("health").get<double>());
        EXPECT_NEAR(num(c.at("minecraft:movement").at("value")), attr(row, "minecraft:generic.movement_speed"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:follow_range").at("value")), attr(row, "minecraft:generic.follow_range"), 1e-6);
        if (row.at("attributes").contains("minecraft:generic.attack_damage"))
            EXPECT_NEAR(num(c.at("minecraft:attack").at("damage")), attr(row, "minecraft:generic.attack_damage"), 1e-6);
        else
            EXPECT_FALSE(c.contains("minecraft:attack"));
        double step = c.contains("minecraft:variable_max_auto_step") ? num(c.at("minecraft:variable_max_auto_step").at("base_value")) : 0.6;
        EXPECT_NEAR(step, attr(row, "minecraft:generic.step_height"), 1e-6);
        EXPECT_FALSE(c.contains("minecraft:knockback_resistance"));
        EXPECT_NEAR(attr(row, "minecraft:generic.knockback_resistance"), 0.0, 1e-9);
        EXPECT_NEAR(attr(row, "minecraft:generic.armor"), 0.0, 1e-9) << "an armor component would be needed";
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("width")), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("height")), row.at("height").get<double>(), 1e-6);
        // the experience reward: Animal 1 + nextInt(3), Monster 5, only when a player hit it last
        ASSERT_TRUE(c.contains("minecraft:experience_reward"));
        auto onDeath = c.at("minecraft:experience_reward").at("on_death").get<std::string>();
        int  lo = row.at("experienceMin").get<int>(), hi = row.at("experienceMax").get<int>();
        if (lo == hi) EXPECT_EQ(onDeath, "query.last_hit_by_player ? " + std::to_string(lo) + " : 0");
        else EXPECT_EQ(onDeath, "query.last_hit_by_player ? Math.Random(" + std::to_string(lo) + "," + std::to_string(hi) + ") : 0");
        // MobCategory: CREATURE never despawns, MONSTER does
        EXPECT_EQ(c.contains("minecraft:despawn"), row.at("category") == "monster");
    }
}

TEST(FaunaMobs, SpawnEggColours) {
    std::map<std::string, std::pair<std::string, std::string>> const eggs = {
        {"boar", {"#83653b", "#ffefca"}},     {"bighorn_sheep", {"#dbceaf", "#d7c771"}}, {"deer", {"#7b4d2e", "#4b241d"}},
        {"penguin", {"#12151b", "#f9edd2"}},  {"tiny_bird", {"#33aadd", "#1188ee"}},     {"squirrel", {"#904f12", "#eeeeee"}},
        {"dwarf_rabbit", {"#fefeee", "#ccaa99"}}, {"raven", {"#000011", "#222233"}},     {"kobold", {"#372096", "#895d1b"}},
        {"mosquito_swarm", {"#080904", "#2d2f21"}},
    };
    for (auto const& [name, colours] : eggs) {
        SCOPED_TRACE(name);
        auto const rp   = load(gf::tests::golden::repoDir() / "assets/pack/rp/entity/fauna" / (name + ".entity.json"));
        auto const& egg = rp.at("minecraft:client_entity").at("description").at("spawn_egg");
        EXPECT_EQ(egg.at("base_color"), colours.first);
        EXPECT_EQ(egg.at("overlay_color"), colours.second);
    }
}
