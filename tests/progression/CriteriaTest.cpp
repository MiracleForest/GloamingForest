// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of the criteria engine's predicates (U14) with the conditions of upstream's generated
// advancements (upstream commit 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <set>

#include "core/progression/Criteria.h"

using namespace gf::core::progression;
using nlohmann::json;

namespace {
Env testEnv() {
    Env env;
    env.itemTag = [](std::string const& tag, std::string const& id) {
        return tag == "c:foods/jerky" && (id == "gloamingforest:chicken_jerky" || id == "gloamingforest:experiment_115");
    };
    env.entityTag = [](std::string const& tag, std::string const& id) { return tag == "gloamingforest:bosses" && id == "gloamingforest:naga"; };
    return env;
}
} // namespace

TEST(ProgressionCriteria, Ints) {
    EXPECT_TRUE(matchInts(nullptr, 5));
    EXPECT_TRUE(matchInts(3, 3));
    EXPECT_FALSE(matchInts(3, 4));
    EXPECT_TRUE(matchInts(json{{"max", 6}}, 6));
    EXPECT_FALSE(matchInts(json{{"max", 6}}, 7));
    EXPECT_TRUE(matchInts(json{{"min", 115}}, 115));
    EXPECT_FALSE(matchInts(json{{"min", 115}}, 114));
}

TEST(ProgressionCriteria, ItemPredicates) {
    auto env = testEnv();
    EXPECT_TRUE(matchItem(json{{"items", "gloamingforest:naga_scale"}}, {"gloamingforest:naga_scale"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "gloamingforest:naga_scale"}}, {"gloamingforest:naga_trophy"}, env));
    EXPECT_TRUE(matchItem(json{{"items", json::array({"gloamingforest:raw_meef", "gloamingforest:cooked_meef"})}}, {"gloamingforest:cooked_meef"}, env));
    EXPECT_TRUE(matchItem(json{{"items", "#c:foods/jerky"}}, {"gloamingforest:chicken_jerky"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "#c:foods/jerky"}}, {"gloamingforest:raw_meef"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "minecraft:air"}}, {"minecraft:air"}, env));
}

TEST(ProgressionCriteria, ItemColorPredicate) {
    auto env  = testEnv();
    auto pred = json::parse(R"({"items":"gloamingforest:arctic_boots","predicates":{"gloamingforest:color":{}}})");
    ItemView plain{"gloamingforest:arctic_boots"};
    ItemView dyed{"gloamingforest:arctic_boots", 1, 0x3355AA};
    EXPECT_FALSE(matchItem(pred, plain, env)) << "an undyed armor piece has no dyed_color";
    EXPECT_TRUE(matchItem(pred, dyed, env)) << "color -1 accepts any dyed colour";
    EXPECT_TRUE(matchItemColor(json{{"color", 0x3355AA}}, dyed));
    EXPECT_FALSE(matchItemColor(json{{"color", 0x112233}}, dyed));
    EXPECT_FALSE(matchItemColor(json{{"color", 0x112233}}, plain));
}

TEST(ProgressionCriteria, InventoryNeedsEveryPredicate) {
    auto env  = testEnv();
    auto cond = json::parse(R"({"items":[{"items":"gloamingforest:lifedrain_scepter"},{"items":"gloamingforest:gloaming_scepter"},{"items":"gloamingforest:zombie_scepter"},{"items":"gloamingforest:fortification_scepter"}]})");
    std::vector<ItemView> three{{"gloamingforest:lifedrain_scepter"}, {"gloamingforest:gloaming_scepter"}, {"gloamingforest:zombie_scepter"}};
    EXPECT_FALSE(matchInventory(cond, three, env));
    three.push_back({"gloamingforest:fortification_scepter"});
    EXPECT_TRUE(matchInventory(cond, three, env));
    EXPECT_FALSE(matchInventory(json::object(), three, env));
}

TEST(ProgressionCriteria, KilledEntityPredicates) {
    auto env  = testEnv();
    auto hill = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"structures":"gloamingforest:small_hollow_hill"},"type":"gloamingforest:redcap"}}])");
    EntityView redcap;
    redcap.type = "gloamingforest:redcap";
    EXPECT_FALSE(matchConditions(hill, Context{&redcap}, env)) << "outside the hollow hill";
    redcap.structures = {"gloamingforest:small_hollow_hill"};
    EXPECT_TRUE(matchConditions(hill, Context{&redcap}, env));
    redcap.type = "gloamingforest:redcap_sapper";
    EXPECT_FALSE(matchConditions(hill, Context{&redcap}, env));

    auto hunter = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"dimension":"gloamingforest:gloaming_forest"},"type":"gloamingforest:deer"}}])");
    EntityView deer;
    deer.type      = "gloamingforest:deer";
    deer.dimension = "minecraft:overworld";
    EXPECT_FALSE(matchConditions(hunter, Context{&deer}, env));
    deer.dimension = "gloamingforest:gloaming_forest";
    EXPECT_TRUE(matchConditions(hunter, Context{&deer}, env));
}

TEST(ProgressionCriteria, ChickenJerky) {
    auto env  = testEnv();
    auto pred = json::parse(R"({"flags":{"is_baby":true},"type":"minecraft:zombie","vehicle":{"type":"minecraft:chicken"}})");
    EntityView jockey;
    jockey.type   = "minecraft:zombie";
    jockey.isBaby = true;
    EXPECT_FALSE(matchEntity(pred, jockey, env)) << "a baby zombie that rides nothing";
    jockey.vehicle       = std::make_shared<EntityView>();
    jockey.vehicle->type = "minecraft:chicken";
    EXPECT_TRUE(matchEntity(pred, jockey, env));
    jockey.isBaby = false;
    EXPECT_FALSE(matchEntity(pred, jockey, env));

    auto blow = json::parse(R"({"equipment":{"mainhand":{"items":"gloamingforest:chicken_jerky"}}})");
    EntityView player;
    player.type = "minecraft:player";
    EXPECT_FALSE(matchEntity(blow, player, env));
    player.mainhand = ItemView{"gloamingforest:chicken_jerky"};
    EXPECT_TRUE(matchEntity(blow, player, env));
}

TEST(ProgressionCriteria, PlayerAdvancementsAndStats) {
    auto env  = testEnv();
    auto tick = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_naga":true}}}}])");
    EntityView p;
    p.type = "minecraft:player";
    EXPECT_FALSE(matchConditions(tick, Context{&p}, env));
    p.advancements.insert("gloamingforest:progress_naga");
    EXPECT_TRUE(matchConditions(tick, Context{&p}, env));

    auto e115 = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"stats":[{"stat":"gloamingforest:e115_slices_eaten","type":"minecraft:custom","value":{"min":115}}],"type":"minecraft:player"}}}])");
    p.stats["gloamingforest:e115_slices_eaten"] = 114;
    EXPECT_FALSE(matchConditions(e115, Context{&p}, env));
    p.stats["gloamingforest:e115_slices_eaten"] = 115;
    EXPECT_TRUE(matchConditions(e115, Context{&p}, env));

    auto plateau = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"biomes":"gloamingforest:final_plateau"}}}])");
    p.biome = "gloamingforest:highlands";
    EXPECT_FALSE(matchConditions(plateau, Context{&p}, env));
    p.biome = "gloamingforest:final_plateau";
    EXPECT_TRUE(matchConditions(plateau, Context{&p}, env));
}

TEST(ProgressionCriteria, ItemUsedOnBlock) {
    auto env  = testEnv();
    auto cond = json::parse(R"([{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"gloamingforest:experiment_115","state":{"regenerate":"true"}}}},{"condition":"minecraft:match_tool","predicate":{"items":"minecraft:redstone"}}])");
    BlockView cake{"gloamingforest:experiment_115", json{{"regenerate", false}}};
    ItemView  redstone{"minecraft:redstone"};
    EXPECT_FALSE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env));
    cake.states["regenerate"] = true;
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env));
    cake.states["regenerate"] = 1;
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env)) << "a 0/1 integer state";
    BlockView bedrock{"gloamingforest:experiment_115", json{{"gloamingforest:regenerate", true}}};
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &bedrock, &redstone}, env)) << "Bedrock's namespaced state name";
    ItemView beans{"gloamingforest:magic_beans"};
    EXPECT_FALSE(matchConditions(cond, Context{nullptr, &cake, &beans}, env));
}

TEST(ProgressionCriteria, TfTriggers) {
    auto env = testEnv();
    auto fight = json::parse(R"({"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:naga"}}]})");
    EXPECT_TRUE(matchTfTrigger("gloamingforest:hurt_boss", fight, json{{"entity", "gloamingforest:naga"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:hurt_boss", fight, json{{"entity", "gloamingforest:lich"}}, env));

    auto bug = json{{"bug", "gloamingforest:cicada"}};
    EXPECT_TRUE(matchTfTrigger("gloamingforest:kill_bug", bug, json{{"bug", "gloamingforest:cicada"}}, env));
    EXPECT_TRUE(matchTfTrigger("gloamingforest:kill_bug", bug, json{{"block", "gloamingforest:cicada[facing=up]"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:kill_bug", bug, json{{"block", "gloamingforest:firefly"}}, env));

    auto flask = json::parse(R"({"doses":3,"potion":"minecraft:strong_harming","seconds":{"max":6}})");
    EXPECT_TRUE(matchTfTrigger("gloamingforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 6}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 7}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:drink_from_flask", flask, json{{"doses", 2}, {"seconds", 1}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 1}, {"potion", "minecraft:harming"}}, env));

    auto uncraft = json::parse(R"({"item":{"items":"gloamingforest:uncrafting_table"}})");
    EXPECT_TRUE(matchTfTrigger("gloamingforest:uncraft_item", uncraft, json{{"item", "gloamingforest:uncrafting_table"}}, env));
    EXPECT_TRUE(matchTfTrigger("gloamingforest:uncraft_item", uncraft, json{{"item", {{"id", "gloamingforest:uncrafting_table"}, {"count", 1}}}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:uncraft_item", uncraft, json{{"item", "minecraft:stick"}}, env));

    EXPECT_TRUE(matchTfTrigger("gloamingforest:activate_ghast_trap", json::object(), json::object(), env));
}

TEST(ProgressionCriteria, RecipeLayouts) {
    // Java recipes: the split belt, an unsplit map, and a Java recipe whose own id ends in _2
    std::set<std::string> java{"gloamingforest:equipment/travellers_belt", "gloamingforest:maze_map", "gloamingforest:stone_2",
                               "gloamingforest:stone", "gloamingforest:thing"};
    std::set<std::string> bp{"gloamingforest:equipment/travellers_belt_0", "gloamingforest:equipment/travellers_belt_3", "gloamingforest:maze_map",
                             "gloamingforest:maze_map_1", "gloamingforest:stone", "gloamingforest:stone_2", "gloamingforest:thing_0",
                             "gloamingforest:thing_1"};
    RecipeIndex idx{[&](std::string const& id) { return java.contains(id); }, [&](std::string const& id) { return bp.contains(id); }};
    EXPECT_TRUE(sameRecipe("gloamingforest:equipment/travellers_belt", "gloamingforest:equipment/travellers_belt", idx));
    EXPECT_TRUE(sameRecipe("gloamingforest:equipment/travellers_belt", "gloamingforest:equipment/travellers_belt_3", idx));
    EXPECT_TRUE(sameRecipe("gloamingforest:equipment/travellers_belt", "gloamingforest:equipment/travellers_belt_0", idx));
    EXPECT_FALSE(sameRecipe("gloamingforest:equipment/travellers_belt", "gloamingforest:equipment/travellers_belt_x", idx));
    EXPECT_FALSE(sameRecipe("gloamingforest:maze_map", "gloamingforest:maze_map_focus", idx));
    // not split (a BP recipe of the Java id exists): an unrelated _1 recipe is not a layout
    EXPECT_FALSE(sameRecipe("gloamingforest:maze_map", "gloamingforest:maze_map_1", idx));
    // a Java recipe of its own that ends in _<digits> is not a layout of its prefix
    EXPECT_FALSE(sameRecipe("gloamingforest:stone", "gloamingforest:stone_2", idx));
    EXPECT_TRUE(sameRecipe("gloamingforest:thing", "gloamingforest:thing_1", idx));
    // an unknown Java recipe has no layouts; without an index only the exact id counts
    EXPECT_FALSE(sameRecipe("gloamingforest:other", "gloamingforest:other_0", idx));
    EXPECT_FALSE(sameRecipe("gloamingforest:thing", "gloamingforest:thing_1", RecipeIndex{}));
    EXPECT_TRUE(sameRecipe("gloamingforest:thing", "gloamingforest:thing", RecipeIndex{}));
    EXPECT_EQ(idIn(json{{"item", {{"id", "gloamingforest:naga_scale"}}}}, {"item"}), "gloamingforest:naga_scale");
    EXPECT_EQ(idIn(json{{"bug", "cicada"}}, {"bug"}), "minecraft:cicada");
}

TEST(ProgressionCriteria, ModifierAndStructureConditions) {
    Env env;
    // AddModifierTrigger: no modifier condition accepts any; a named one only itself
    EXPECT_TRUE(matchTfTrigger("gloamingforest:add_modifier", json::object(), json{{"modifier", "gloamingforest:water_walk"}}, env));
    EXPECT_TRUE(matchTfTrigger("gloamingforest:add_modifier", json{{"modifier", "gloamingforest:water_walk"}}, json{{"modifier", "gloamingforest:water_walk"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:add_modifier", json{{"modifier", "gloamingforest:water_walk"}}, json{{"modifier", "gloamingforest:step_up"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:add_modifier", json{{"modifier", "gloamingforest:water_walk"}}, json::object(), env));
    // StructureClearedTrigger: the structure is required and compared
    EXPECT_TRUE(matchTfTrigger("gloamingforest:structure_cleared", json{{"structure", "gloamingforest:lich_tower"}}, json{{"structure", "gloamingforest:lich_tower"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:structure_cleared", json{{"structure", "gloamingforest:lich_tower"}}, json{{"structure", "gloamingforest:labyrinth"}}, env));
    EXPECT_FALSE(matchTfTrigger("gloamingforest:structure_cleared", json::object(), json{{"structure", "gloamingforest:lich_tower"}}, env));
}
