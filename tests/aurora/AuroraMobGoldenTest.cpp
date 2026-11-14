// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of U28's mobs against F13's Java goldens (tests/golden/mobs.json): every attribute the
// golden row lists that Bedrock has a component for, read from the unit's behavior pack entities
// (assets/pack/bp/entities/aurora/<name>.json): health, movement and flying speed, attack damage,
// follow range, knockback resistance, the collision box and the experience reward. The live
// actors are checked by the selftest aurora.mob_attributes; the eye height is a Bedrock limit
// (0.9 x the collision height, docs/units/aurora.md).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
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

double num(json const& v) { return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>(); }

} // namespace

TEST(AuroraMobs, MatchJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    for (char const* name : {"snow_queen", "snow_guardian", "stable_ice_core", "unstable_ice_core", "ice_crystal"}) {
        SCOPED_TRACE(name);
        std::string const id  = std::string("gloamingforest:") + name;
        auto const&       row = golden.at("entities").at(id);
        auto const        bp  = load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/aurora" / (std::string(name) + ".json"));
        auto const&       c   = bp.at("minecraft:entity").at("components");
        EXPECT_EQ(bp.at("minecraft:entity").at("description").at("identifier"), id);
        EXPECT_DOUBLE_EQ(num(c.at("minecraft:health").at("max")), row.at("maxHealth").get<double>());
        EXPECT_DOUBLE_EQ(num(c.at("minecraft:health").at("value")), row.at("health").get<double>());
        EXPECT_NEAR(num(c.at("minecraft:movement").at("value")), attr(row, "minecraft:generic.movement_speed"), 1e-6);
        if (row.at("attributes").contains("minecraft:generic.flying_speed"))
            EXPECT_NEAR(num(c.at("minecraft:flying_speed").at("value")), attr(row, "minecraft:generic.flying_speed"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:attack").at("damage")), attr(row, "minecraft:generic.attack_damage"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:follow_range").at("value")), attr(row, "minecraft:generic.follow_range"), 1e-6);
        double kb = c.contains("minecraft:knockback_resistance") ? num(c.at("minecraft:knockback_resistance").at("value")) : 0.0;
        EXPECT_NEAR(kb, attr(row, "minecraft:generic.knockback_resistance"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("width")), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("height")), row.at("height").get<double>(), 1e-6);
        // the experience reward: a constant (the golden's min == max over 256 draws), dropped only
        // when a player hit it last (LivingEntity#dropExperience: lastHurtByPlayerTime > 0)
        ASSERT_EQ(row.at("experienceMin"), row.at("experienceMax"));
        ASSERT_TRUE(c.contains("minecraft:experience_reward"));
        EXPECT_EQ(c.at("minecraft:experience_reward").at("on_death").get<std::string>(),
                  "query.last_hit_by_player ? " + std::to_string(row.at("experienceMin").get<int>()) + " : 0");
        EXPECT_NEAR(attr(row, "minecraft:generic.armor"), 0.0, 1e-9) << "an armor component would be needed";
    }
}
