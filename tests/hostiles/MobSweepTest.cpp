// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of the spawn sweep's table (src/core/hostiles/MobSweep.h) against F13's Java goldens: one row
// per entity type of tests/golden/mobs.json with its attributes, box, fire immunity and summonability,
// and the loot table flag from tests/golden/loot.json; the owners from docs/spec/owners.tsv.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "../golden/GoldenRepo.h"
#include "core/hostiles/MobSweep.h"

namespace {

using nlohmann::json;
namespace fs = std::filesystem;
using gf::core::hostiles::kMobSweep;

json load(fs::path const& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << p.string();
    if (!in.good()) return json::object();
    return json::parse(in, nullptr, true, true);
}

double attr(json const& row, char const* key) {
    auto const& a = row.at("attributes");
    auto        k = std::string("minecraft:generic.") + key;
    return a.contains(k) ? a.at(k).at("value").get<double>() : 0.0;
}

} // namespace

TEST(MobSweep, EveryEntityTypeOnce) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    std::set<std::string> ids;
    for (auto const& row : kMobSweep) EXPECT_TRUE(ids.insert(row.id).second) << row.id;
    std::set<std::string> goldenIds;
    for (auto const& [id, _] : golden.at("entities").items()) goldenIds.insert(id);
    EXPECT_EQ(ids, goldenIds);
}

TEST(MobSweep, MatchesJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    auto const loot   = load(gf::tests::golden::goldenDir() / "loot.json");
    for (auto const& row : kMobSweep) {
        SCOPED_TRACE(row.id);
        ASSERT_TRUE(golden.at("entities").contains(row.id));
        auto const& g      = golden.at("entities").at(row.id);
        bool const  living = g.value("living", false);
        EXPECT_EQ(row.living, living);
        EXPECT_NEAR(row.width, g.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(row.height, g.at("height").get<double>(), 1e-6);
        EXPECT_EQ(row.fireImmune, g.at("fireImmune").get<bool>());
        EXPECT_EQ(row.canSummon, g.at("canSummon").get<bool>());
        std::string const path = std::string(row.id).substr(std::string(row.id).find(':') + 1);
        EXPECT_EQ(row.lootTable, loot.at("tables").contains("twilightforest:entities/" + path));
        if (!living) {
            EXPECT_FALSE(row.peacefulDespawn);
            EXPECT_FALSE(row.hurtDeathSounds);
            continue;
        }
        EXPECT_NEAR(row.maxHealth, g.at("maxHealth").get<double>(), 1e-6);
        EXPECT_NEAR(row.movementSpeed, attr(g, "movement_speed"), 1e-6);
        EXPECT_NEAR(row.attackDamage, attr(g, "attack_damage"), 1e-6);
        EXPECT_NEAR(row.followRange, attr(g, "follow_range"), 1e-6);
        EXPECT_NEAR(row.knockbackResistance, attr(g, "knockback_resistance"), 1e-6);
        EXPECT_NEAR(row.armor, attr(g, "armor"), 1e-6);
        EXPECT_EQ(row.experienceMin, g.at("experienceMin").get<int>());
        EXPECT_EQ(row.experienceMax, g.at("experienceMax").get<int>());
        // Monster#shouldDespawnInPeaceful: every monster but the loyal zombie (a TamableAnimal)
        EXPECT_EQ(row.peacefulDespawn, g.at("category") == "monster" && path != "loyal_zombie");
    }
}

TEST(MobSweep, OwnersFromOwnersTsv) {
    std::ifstream                      in(gf::tests::golden::repoDir() / "docs/spec/owners.tsv");
    std::map<std::string, std::string> owners;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        std::string        kind, id, owner;
        if (std::getline(ss, kind, '\t') && kind == "entity" && std::getline(ss, id, '\t') && std::getline(ss, owner, '\t')) owners[id] = owner;
    }
    for (auto const& row : kMobSweep) EXPECT_EQ(owners[row.id], row.owner) << row.id;
}
