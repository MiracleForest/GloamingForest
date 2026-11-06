// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of src/core/labyrinth (U23): the labyrinth's terraformer (LabyrinthStructure), the maze
// helpers of MinotaurMazeComponent, the mob rules, and the unit's BP entities against F13's Java
// goldens (tests/golden/mobs.json).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <string>

#include "../golden/GoldenRepo.h"
#include "core/labyrinth/Terraformer.h"

using namespace gf::core;
using namespace gf::core::labyrinth;
using nlohmann::json;

namespace {

// The start box of a labyrinth started at (7, 0, 7): the level 2 maze (-42, -24, -42 .. 70, -19, 70),
// the level 1 maze (-43, -14, -43 .. 69, -9, 69) and the mound (-2, 0, -2 .. 33, 12, 33).
BlockBox startBox() { return BlockBox{-43, -24, -43, 70, 12, 70}; }

json load(std::filesystem::path const& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << p.string();
    if (!in.good()) return json::object();
    return json::parse(in, nullptr, true, true);
}

double num(json const& v) { return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>(); }
double attr(json const& row, char const* key) { return row.at("attributes").at(key).at("value").get<double>(); }

} // namespace

TEST(Labyrinth, BoxCenter) {
    auto c = boxCenter(startBox());
    EXPECT_EQ(c.x, 14);
    EXPECT_EQ(c.y, -6);
    EXPECT_EQ(c.z, 14);
}

TEST(Labyrinth, DensityFunctions) {
    // HollowHillFunction: zero beyond the radius, clamped to [-1, 1] inside
    EXPECT_DOUBLE_EQ(hollowHill(0.0F, 0.0F, 0.0F, 10.0F, 1.0F, 50, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(hollowHill(0.0F, 0.0F, 0.0F, 10.0F, 1.0F, 0, 0, 0), 1.0);
    EXPECT_DOUBLE_EQ(hollowHill(0.0F, 0.0F, 0.0F, 10.0F, 1.0F, 0, 100, 0), -1.0);
    // FocusedDensityFunction: the near value at the center, the far one beyond the radius
    EXPECT_DOUBLE_EQ(focused(0.0F, 0.0F, 0.0F, 10.0F, -5.0F, 3.0F, 0, 0, 0), -5.0);
    EXPECT_DOUBLE_EQ(focused(0.0F, 0.0F, 0.0F, 10.0F, -5.0F, 3.0F, 40, 0, 0), 3.0);
    // AbsoluteDifferenceFunction.Min / Max, capped at max
    EXPECT_DOUBLE_EQ(absDiffMin(32.0, 0.5, 0.5, 10, 3), 2.5);
    EXPECT_DOUBLE_EQ(absDiffMax(32.0, 0.5, 0.5, 10, 3), 9.5);
    EXPECT_DOUBLE_EQ(absDiffMax(32.0, 0.5, 0.5, 100, 3), 32.0);
    // DensityFunctions.yClampedGradient
    EXPECT_DOUBLE_EQ(yClampedGradient(5, 6, -1.0, 1.0, 0), -1.0);
    EXPECT_DOUBLE_EQ(yClampedGradient(5, 6, -1.0, 1.0, 9), 1.0);
    EXPECT_DOUBLE_EQ(yClampedGradient(-4, 3, 26.0, -1.0, -4), 26.0);
}

TEST(Labyrinth, Terraformer) {
    auto box = startBox();
    // far from the mound: nothing
    EXPECT_DOUBLE_EQ(terraformer(box, 400, 5, 400), 0.0);
    EXPECT_DOUBLE_EQ(terraformer(box, 400, -10, 400), 0.0);
    // the mound's cap above the maze's ceiling (y >= 6): filled
    EXPECT_DOUBLE_EQ(terraformer(box, 15, 8, 16), 1.0);
    // above the mound: nothing
    EXPECT_DOUBLE_EQ(terraformer(box, 15, 30, 16), 0.0);
    // the hollow under the cap (3 .. 5): left to the terrain
    EXPECT_DOUBLE_EQ(terraformer(box, 15, 4, 16), 0.0);
    // the floor sloping up inwards
    EXPECT_DOUBLE_EQ(terraformer(box, 15, 2, 16), 1.0);
    // never negative: the interior is clamped to [0, 1]
    for (int x = -30; x <= 60; x += 3)
        for (int y = -10; y <= 20; y += 2)
            for (int z = -30; z <= 60; z += 3) ASSERT_GE(terraformer(box, x, y, z), 0.0) << x << "," << y << "," << z;
}

TEST(Labyrinth, MazeSeed) {
    // Java: minX * 90342903L + minY * 90342903L ^ minZ
    EXPECT_EQ(mazeSeed(BlockBox{-55, -14, -55, 0, 0, 0}), 6233660324LL);
    EXPECT_EQ(mazeSeed(BlockBox{100, -24, -300, 0, 0, 0}), -6866060416LL);
    EXPECT_EQ(mazeSeed(BlockBox{12345, 5, -7, 0, 0, 0}), -1115734852053LL);
}

TEST(Labyrinth, IsNearRoom) {
    int rc[14] = {11, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(isNearRoom(1, 1, rc, 7, 4)); // never on the origin
    EXPECT_TRUE(isNearRoom(5, 5, rc, 7, 7));
    EXPECT_FALSE(isNearRoom(4, 4, rc, 7, 7));
    EXPECT_FALSE(isNearRoom(5, 5, rc, 7, 4));
    EXPECT_TRUE(isNearRoom(8, 14, rc, 7, 4));
}

TEST(Labyrinth, PieceKinds) {
    EXPECT_STREQ(roomType(0, 1), "twilightforest:TFMMR");
    EXPECT_STREQ(roomType(1, 1), "twilightforest:TFMMRE");
    EXPECT_STREQ(roomType(1, 2), "twilightforest:TFMMRB");
    EXPECT_STREQ(roomType(2, 1), "twilightforest:TFMMRC");
    EXPECT_STREQ(roomType(3, 2), "twilightforest:TFMMMR");
    EXPECT_STREQ(roomType(4, 1), "twilightforest:TFMMRF");
    EXPECT_STREQ(roomType(4, 2), "twilightforest:TFMMRV");
    EXPECT_STREQ(roomType(5, 1), "twilightforest:TFMMRSC");
    EXPECT_STREQ(roomType(6, 2), "twilightforest:TFMMRSC");
    EXPECT_STREQ(deadEndType(0, false, 1), "twilightforest:TFMMDE");
    EXPECT_STREQ(deadEndType(1, false, 1), "twilightforest:TFMMDEC");
    EXPECT_STREQ(deadEndType(2, true, 1), "twilightforest:TFMMDETC");
    EXPECT_STREQ(deadEndType(2, false, 1), "twilightforest:TFMMDETrC");
    EXPECT_STREQ(deadEndType(5, false, 2), "twilightforest:TFMMDEFL");
    EXPECT_STREQ(deadEndType(7, false, 1), "twilightforest:TFMMDER");
    EXPECT_STREQ(deadEndType(7, false, 2), "twilightforest:TFMMDES");
    EXPECT_EQ(corridorType(0, 1), nullptr);
    EXPECT_EQ(corridorType(3, 1), nullptr); // painting
    EXPECT_STREQ(corridorType(2, 2), "twilightforest:TFMMCIF");
    EXPECT_STREQ(corridorType(4, 1), "twilightforest:TFMMCR");
    EXPECT_STREQ(corridorType(4, 2), "twilightforest:TFMMCS");
    EXPECT_EQ(kMazeRadius, 55);
    EXPECT_EQ(kMazeDiameter, 110);
}

TEST(Labyrinth, MobRules) {
    EXPECT_FLOAT_EQ(weaponModifier("minecraft:golden_axe"), 6.0F);
    EXPECT_FLOAT_EQ(weaponModifier("twilightforest:gold_minotaur_axe"), 6.0F);
    EXPECT_FLOAT_EQ(weaponModifier("twilightforest:diamond_minotaur_axe"), 9.0F);
    EXPECT_FLOAT_EQ(weaponModifier("minecraft:stick"), 0.0F);
    // the Minoshroom's slam with its diamond minotaur axe: (2 + 9) / 2
    EXPECT_FLOAT_EQ(slamDamage(2.0F + weaponModifier("twilightforest:diamond_minotaur_axe")), 5.5F);
    // Minotaur: (int)(nextInt(10) / (effective difficulty + 1)) == 0 -> gold minotaur axe
    EXPECT_STREQ(minotaurAxe(0, 0.0F), "twilightforest:gold_minotaur_axe");
    EXPECT_STREQ(minotaurAxe(1, 0.0F), "minecraft:golden_axe");
    EXPECT_STREQ(minotaurAxe(3, 3.0F), "twilightforest:gold_minotaur_axe");
    EXPECT_STREQ(minotaurAxe(4, 3.0F), "minecraft:golden_axe");
    // MazeSlime: three times the slime's health
    EXPECT_FLOAT_EQ(mazeSlimeHealth(1), 3.0F);
    EXPECT_FLOAT_EQ(mazeSlimeHealth(2), 12.0F);
    EXPECT_FLOAT_EQ(mazeSlimeHealth(4), 48.0F);
    EXPECT_EQ(mazeSlimeXp(4), 7);
    EXPECT_EQ(mazeSlimeSize(0, false), 1);
    EXPECT_EQ(mazeSlimeSize(0, true), 2);
    EXPECT_EQ(mazeSlimeSize(1, true), 4);
    EXPECT_EQ(mazeSlimeSize(2, true), 4);
}

TEST(Labyrinth, MobsMatchJavaGoldens) {
    auto const golden = load(gf::tests::golden::goldenDir() / "mobs.json");
    ASSERT_TRUE(golden.contains("entities"));
    for (char const* name : {"minotaur", "minoshroom"}) {
        SCOPED_TRACE(name);
        std::string const id  = std::string("twilightforest:") + name;
        auto const&       row = golden.at("entities").at(id);
        auto const        bp  = load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/labyrinth" / (std::string(name) + ".json"));
        auto const&       c   = bp.at("minecraft:entity").at("components");
        EXPECT_EQ(bp.at("minecraft:entity").at("description").at("identifier"), id);
        EXPECT_DOUBLE_EQ(num(c.at("minecraft:health").at("max")), row.at("maxHealth").get<double>());
        EXPECT_NEAR(num(c.at("minecraft:movement").at("value")), attr(row, "minecraft:generic.movement_speed"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:attack").at("damage")), attr(row, "minecraft:generic.attack_damage"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:follow_range").at("value")), attr(row, "minecraft:generic.follow_range"), 1e-6);
        double kb = c.contains("minecraft:knockback_resistance") ? num(c.at("minecraft:knockback_resistance").at("value")) : 0.0;
        EXPECT_NEAR(kb, attr(row, "minecraft:generic.knockback_resistance"), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("width")), row.at("width").get<double>(), 1e-6);
        EXPECT_NEAR(num(c.at("minecraft:collision_box").at("height")), row.at("height").get<double>(), 1e-6);
        ASSERT_EQ(row.at("experienceMin"), row.at("experienceMax"));
        EXPECT_EQ(c.at("minecraft:experience_reward").at("on_death").get<std::string>(),
                  "query.last_hit_by_player ? " + std::to_string(row.at("experienceMin").get<int>()) + " : 0");
    }
    // the maze slime's size groups follow Slime#setSize + MazeSlime's double health
    auto const  bp  = load(gf::tests::golden::repoDir() / "assets/pack/bp/entities/labyrinth/maze_slime.json");
    auto const& row = golden.at("entities").at("twilightforest:maze_slime");
    auto const& groups = bp.at("minecraft:entity").at("component_groups");
    for (int size : {1, 2, 4}) {
        SCOPED_TRACE(size);
        auto const& g = groups.at("gf:size_" + std::to_string(size));
        EXPECT_EQ(g.at("minecraft:variant").at("value").get<int>(), size);
        EXPECT_DOUBLE_EQ(num(g.at("minecraft:health").at("max")), mazeSlimeHealth(size));
        EXPECT_NEAR(num(g.at("minecraft:movement").at("value")), mazeSlimeSpeed(size), 1e-6);
        EXPECT_DOUBLE_EQ(num(g.at("minecraft:attack").at("damage")), size);
        EXPECT_NEAR(num(g.at("minecraft:collision_box").at("width")), row.at("width").get<double>() * size, 1e-6);
        EXPECT_EQ(g.at("minecraft:experience_reward").at("on_death").get<std::string>(), "query.last_hit_by_player ? " + std::to_string(mazeSlimeXp(size)) + " : 0");
    }
}
