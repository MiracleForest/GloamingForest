// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the criteria engine's predicates (U14) with the conditions of TF's generated
// advancements (TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include "core/progression/Criteria.h"

using namespace gf::core::progression;
using nlohmann::json;

namespace {
Env testEnv() {
    Env env;
    env.itemTag = [](std::string const& tag, std::string const& id) {
        return tag == "c:foods/jerky" && (id == "twilightforest:chicken_jerky" || id == "twilightforest:experiment_115");
    };
    env.entityTag = [](std::string const& tag, std::string const& id) { return tag == "twilightforest:bosses" && id == "twilightforest:naga"; };
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
    EXPECT_TRUE(matchItem(json{{"items", "twilightforest:naga_scale"}}, {"twilightforest:naga_scale"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "twilightforest:naga_scale"}}, {"twilightforest:naga_trophy"}, env));
    EXPECT_TRUE(matchItem(json{{"items", json::array({"twilightforest:raw_meef", "twilightforest:cooked_meef"})}}, {"twilightforest:cooked_meef"}, env));
    EXPECT_TRUE(matchItem(json{{"items", "#c:foods/jerky"}}, {"twilightforest:chicken_jerky"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "#c:foods/jerky"}}, {"twilightforest:raw_meef"}, env));
    EXPECT_FALSE(matchItem(json{{"items", "minecraft:air"}}, {"minecraft:air"}, env));
}

TEST(ProgressionCriteria, ItemColorPredicate) {
    auto env  = testEnv();
    auto pred = json::parse(R"({"items":"twilightforest:arctic_boots","predicates":{"twilightforest:color":{}}})");
    ItemView plain{"twilightforest:arctic_boots"};
    ItemView dyed{"twilightforest:arctic_boots", 1, 0x3355AA};
    EXPECT_FALSE(matchItem(pred, plain, env)) << "an undyed armor piece has no dyed_color";
    EXPECT_TRUE(matchItem(pred, dyed, env)) << "color -1 accepts any dyed colour";
    EXPECT_TRUE(matchItemColor(json{{"color", 0x3355AA}}, dyed));
    EXPECT_FALSE(matchItemColor(json{{"color", 0x112233}}, dyed));
    EXPECT_FALSE(matchItemColor(json{{"color", 0x112233}}, plain));
}

TEST(ProgressionCriteria, InventoryNeedsEveryPredicate) {
    auto env  = testEnv();
    auto cond = json::parse(R"({"items":[{"items":"twilightforest:lifedrain_scepter"},{"items":"twilightforest:twilight_scepter"},{"items":"twilightforest:zombie_scepter"},{"items":"twilightforest:fortification_scepter"}]})");
    std::vector<ItemView> three{{"twilightforest:lifedrain_scepter"}, {"twilightforest:twilight_scepter"}, {"twilightforest:zombie_scepter"}};
    EXPECT_FALSE(matchInventory(cond, three, env));
    three.push_back({"twilightforest:fortification_scepter"});
    EXPECT_TRUE(matchInventory(cond, three, env));
    EXPECT_FALSE(matchInventory(json::object(), three, env));
}

TEST(ProgressionCriteria, KilledEntityPredicates) {
    auto env  = testEnv();
    auto hill = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"structures":"twilightforest:small_hollow_hill"},"type":"twilightforest:redcap"}}])");
    EntityView redcap;
    redcap.type = "twilightforest:redcap";
    EXPECT_FALSE(matchConditions(hill, Context{&redcap}, env)) << "outside the hollow hill";
    redcap.structures = {"twilightforest:small_hollow_hill"};
    EXPECT_TRUE(matchConditions(hill, Context{&redcap}, env));
    redcap.type = "twilightforest:redcap_sapper";
    EXPECT_FALSE(matchConditions(hill, Context{&redcap}, env));

    auto hunter = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"dimension":"twilightforest:twilight_forest"},"type":"twilightforest:deer"}}])");
    EntityView deer;
    deer.type      = "twilightforest:deer";
    deer.dimension = "minecraft:overworld";
    EXPECT_FALSE(matchConditions(hunter, Context{&deer}, env));
    deer.dimension = "twilightforest:twilight_forest";
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

    auto blow = json::parse(R"({"equipment":{"mainhand":{"items":"twilightforest:chicken_jerky"}}})");
    EntityView player;
    player.type = "minecraft:player";
    EXPECT_FALSE(matchEntity(blow, player, env));
    player.mainhand = ItemView{"twilightforest:chicken_jerky"};
    EXPECT_TRUE(matchEntity(blow, player, env));
}

TEST(ProgressionCriteria, PlayerAdvancementsAndStats) {
    auto env  = testEnv();
    auto tick = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_naga":true}}}}])");
    EntityView p;
    p.type = "minecraft:player";
    EXPECT_FALSE(matchConditions(tick, Context{&p}, env));
    p.advancements.insert("twilightforest:progress_naga");
    EXPECT_TRUE(matchConditions(tick, Context{&p}, env));

    auto e115 = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"stats":[{"stat":"twilightforest:e115_slices_eaten","type":"minecraft:custom","value":{"min":115}}],"type":"minecraft:player"}}}])");
    p.stats["twilightforest:e115_slices_eaten"] = 114;
    EXPECT_FALSE(matchConditions(e115, Context{&p}, env));
    p.stats["twilightforest:e115_slices_eaten"] = 115;
    EXPECT_TRUE(matchConditions(e115, Context{&p}, env));

    auto plateau = json::parse(R"([{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"biomes":"twilightforest:final_plateau"}}}])");
    p.biome = "twilightforest:highlands";
    EXPECT_FALSE(matchConditions(plateau, Context{&p}, env));
    p.biome = "twilightforest:final_plateau";
    EXPECT_TRUE(matchConditions(plateau, Context{&p}, env));
}

TEST(ProgressionCriteria, ItemUsedOnBlock) {
    auto env  = testEnv();
    auto cond = json::parse(R"([{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"twilightforest:experiment_115","state":{"regenerate":"true"}}}},{"condition":"minecraft:match_tool","predicate":{"items":"minecraft:redstone"}}])");
    BlockView cake{"twilightforest:experiment_115", json{{"regenerate", false}}};
    ItemView  redstone{"minecraft:redstone"};
    EXPECT_FALSE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env));
    cake.states["regenerate"] = true;
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env));
    cake.states["regenerate"] = 1;
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &cake, &redstone}, env)) << "a 0/1 integer state";
    BlockView bedrock{"twilightforest:experiment_115", json{{"twilightforest:regenerate", true}}};
    EXPECT_TRUE(matchConditions(cond, Context{nullptr, &bedrock, &redstone}, env)) << "Bedrock's namespaced state name";
    ItemView beans{"twilightforest:magic_beans"};
    EXPECT_FALSE(matchConditions(cond, Context{nullptr, &cake, &beans}, env));
}

TEST(ProgressionCriteria, TfTriggers) {
    auto env = testEnv();
    auto fight = json::parse(R"({"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:naga"}}]})");
    EXPECT_TRUE(matchTfTrigger("twilightforest:hurt_boss", fight, json{{"entity", "twilightforest:naga"}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:hurt_boss", fight, json{{"entity", "twilightforest:lich"}}, env));

    auto bug = json{{"bug", "twilightforest:cicada"}};
    EXPECT_TRUE(matchTfTrigger("twilightforest:kill_bug", bug, json{{"bug", "twilightforest:cicada"}}, env));
    EXPECT_TRUE(matchTfTrigger("twilightforest:kill_bug", bug, json{{"block", "twilightforest:cicada[facing=up]"}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:kill_bug", bug, json{{"block", "twilightforest:firefly"}}, env));

    auto flask = json::parse(R"({"doses":3,"potion":"minecraft:strong_harming","seconds":{"max":6}})");
    EXPECT_TRUE(matchTfTrigger("twilightforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 6}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 7}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:drink_from_flask", flask, json{{"doses", 2}, {"seconds", 1}, {"potion", "minecraft:strong_harming"}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:drink_from_flask", flask, json{{"doses", 3}, {"seconds", 1}, {"potion", "minecraft:harming"}}, env));

    auto uncraft = json::parse(R"({"item":{"items":"twilightforest:uncrafting_table"}})");
    EXPECT_TRUE(matchTfTrigger("twilightforest:uncraft_item", uncraft, json{{"item", "twilightforest:uncrafting_table"}}, env));
    EXPECT_TRUE(matchTfTrigger("twilightforest:uncraft_item", uncraft, json{{"item", {{"id", "twilightforest:uncrafting_table"}, {"count", 1}}}}, env));
    EXPECT_FALSE(matchTfTrigger("twilightforest:uncraft_item", uncraft, json{{"item", "minecraft:stick"}}, env));

    EXPECT_TRUE(matchTfTrigger("twilightforest:activate_ghast_trap", json::object(), json::object(), env));
}

TEST(ProgressionCriteria, RecipeLayouts) {
    EXPECT_TRUE(sameRecipe("twilightforest:equipment/travellers_belt", "twilightforest:equipment/travellers_belt"));
    EXPECT_TRUE(sameRecipe("twilightforest:equipment/travellers_belt", "twilightforest:equipment/travellers_belt_3"));
    EXPECT_FALSE(sameRecipe("twilightforest:equipment/travellers_belt", "twilightforest:equipment/travellers_belt_x"));
    EXPECT_FALSE(sameRecipe("twilightforest:maze_map", "twilightforest:maze_map_focus"));
    EXPECT_EQ(idIn(json{{"item", {{"id", "twilightforest:naga_scale"}}}}, {"item"}), "twilightforest:naga_scale");
    EXPECT_EQ(idIn(json{{"bug", "cicada"}}, {"bug"}), "minecraft:cicada");
}
