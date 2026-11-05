// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U14 selftests (progression.*, dx 1400-1499): TF's 45 advancements as loaded, the restriction
// registry, the recipe unlocks, the criteria engine on every trigger kind (inventory, kills of
// real entities by id, consumption, item used on a block, crafting, location / tick and TF's 13
// triggers), the scripted playthrough that lifts the biome locks, the enforcements and their
// timing, the gamerule, the toasts and the advancement screen (native data for companions, the
// form for vanilla players), and in the client build the client mod's toasts.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"

#include "core/progression/Rules.h"
#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/gamerules/GameRules.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/progression_content/Content.h"
#include "feature/progression_content/Restrictions.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

#ifdef GF_CLIENT
#include "client/progression/Client.h"
#include "client/ui/Ui.h"
#include "feature/ext/ui/Client.h"
#endif

namespace {

namespace pc = gf::feature::progression_content;
namespace pr = gf::progression;
namespace cp = gf::core::progression;
using gf::selftest::Context;
using Json = nlohmann::json;

Player* playerById(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->getPlayer(id);
}

/// Takes every TF advancement back (simulated players may keep a UUID between tests).
void resetProgress(Player& p) {
    for (auto const& a : pc::advancements()) pr::revoke(p, a.id);
}

// ---- data -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.advancements", [](Context& ctx) {
    auto const& all = pc::advancements();
    ctx.require(all.size() == 45, "TF's 45 advancements are loaded (" + std::to_string(all.size()) + ")");
    std::map<std::string, std::string> parents{
        {"progress_naga", "root"}, {"hill1", "root"}, {"hill2", "hill1"}, {"hill3", "hill2"}, {"hedge", "root"}, {"twilight_hunter", "root"},
        {"magic_map_focus", "twilight_hunter"}, {"magic_map", "magic_map_focus"}, {"maze_map", "magic_map"}, {"ore_map", "maze_map"},
        {"twilight_dinner", "root"}, {"kill_cicada", "root"}, {"quest_ram", "root"}, {"arborist", "root"}, {"chicken_jerky", "root"},
        {"craft_travellers_gear", "root"}, {"modify_travellers_gear", "craft_travellers_gear"}, {"uncraft_uncrafting_table", "root"},
        {"progress_lich", "progress_naga"}, {"naga_armors", "progress_naga"}, {"progress_labyrinth", "progress_lich"},
        {"progress_hydra", "progress_labyrinth"}, {"fiery_set", "progress_hydra"}, {"hydra_chop", "progress_hydra"},
        {"mazebreaker", "progress_labyrinth"}, {"progress_trophy_pedestal", "progress_lich"}, {"progress_knights", "progress_trophy_pedestal"},
        {"ghast_trap", "progress_knights"}, {"progress_ur_ghast", "ghast_trap"}, {"experiment_115", "progress_knights"},
        {"experiment_115_115", "experiment_115"}, {"experiment_115_self_replenishing", "experiment_115"}, {"progress_yeti", "progress_lich"},
        {"progress_glacier", "progress_yeti"}, {"arctic_armor_dyed", "progress_yeti"}, {"break_glass_sword", "progress_yeti"},
        {"progress_merge", "progress_lich"}, {"troll", "progress_merge"}, {"beanstalk", "progress_merge"}, {"giants", "beanstalk"},
        {"progress_troll", "giants"}, {"progression_end", "progress_troll"}, {"lich_scepters", "progress_lich"},
        {"full_mettle_alchemist", "progress_lich"},
    };
    for (auto const& [id, parent] : parents) {
        auto const* a = pc::find("twilightforest:" + id);
        if (!a) {
            ctx.check(false, id + " is loaded");
            continue;
        }
        ctx.check(a->parent == "twilightforest:" + parent, id + "'s parent is " + parent + " (" + a->parent + ")");
    }
    auto const* root = pc::find("twilightforest:root");
    ctx.require(root && root->parent.empty(), "root has no parent");
    ctx.check(root->background == "twilightforest:textures/block/mazestone_large_brick.png", "the tab background is mazestone_large_brick");
    std::set<std::string> goals, challenges, hidden;
    for (auto const& a : all) {
        auto id = a.id.substr(15);
        if (a.frame == pc::Frame::Goal) goals.insert(id);
        if (a.frame == pc::Frame::Challenge) challenges.insert(id);
        if (a.hidden) hidden.insert(id);
    }
    ctx.check(goals.size() == 16, "16 goal frames (11 progress_*, beanstalk, giants, maze_map, quest_ram, progression_end: " + std::to_string(goals.size()) + ")");
    ctx.check(challenges.size() == 12, "12 challenge frames (" + std::to_string(challenges.size()) + ")");
    ctx.check(hidden == std::set<std::string>{"break_glass_sword", "chicken_jerky", "experiment_115_115", "experiment_115_self_replenishing", "full_mettle_alchemist",
                                              "kill_cicada", "lich_scepters", "ore_map", "uncraft_uncrafting_table"},
              "the 9 hidden advancements");
    auto byTrigger = pc::criteriaByTrigger();
    for (char const* t : {"minecraft:inventory_changed", "minecraft:player_killed_entity", "minecraft:consume_item", "minecraft:item_used_on_block",
                          "minecraft:recipe_crafted", "minecraft:location", "minecraft:tick", "twilightforest:make_tf_portal", "twilightforest:hurt_boss",
                          "twilightforest:kill_bug", "twilightforest:broke_glass_sword", "twilightforest:drink_from_flask",
                          "twilightforest:consume_hydra_chop_on_low_hunger", "twilightforest:add_modifier", "twilightforest:uncraft_item",
                          "twilightforest:complete_quest_ram", "twilightforest:placed_on_trophy_pedestal", "twilightforest:kill_all_phantoms",
                          "twilightforest:activate_ghast_trap"})
        ctx.check(byTrigger.contains(t), std::string("a criterion listens to ") + t);
    ctx.check(byTrigger["minecraft:player_killed_entity"] == 69 && byTrigger["minecraft:tick"] == 13 && byTrigger["twilightforest:hurt_boss"] == 6,
              "criteria per trigger: 69 kills, 13 ticks, 6 boss fights");
    ctx.check(pc::find("twilightforest:arborist")->experience == 1000 && pc::find("twilightforest:twilight_dinner")->experience == 75, "experience rewards");
});

GF_REGISTER_SELFTEST("progression.restrictions", [](Context& ctx) {
    auto const& all = pc::restrictions();
    ctx.require(all.size() == 9, "the 9 biome restrictions (" + std::to_string(all.size()) + ")");
    struct Row {
        char const* biome;
        char const* structure;
        cp::Enforcement e;
        float       m;
        char const* icon;
        char const* adv;
    };
    for (auto const& r : std::initializer_list<Row>{
             {"dark_forest", "knight_stronghold", cp::Enforcement::Darkness, 0, "lich_tower_miniature_structure", "progress_lich"},
             {"dark_forest_center", "dark_tower", cp::Enforcement::Darkness, 0, "knight_phantom_trophy", "progress_knights"},
             {"final_plateau", "final_castle", cp::Enforcement::AcidRain, 1.5f, "lamp_of_cinders", "progress_troll"},
             {"fire_swamp", "hydra_lair", cp::Enforcement::Fire, 8, "meef_stroganoff", "progress_labyrinth"},
             {"glacier", "aurora_palace", cp::Enforcement::Frost, 1, "alpha_yeti_fur", "progress_yeti"},
             {"highlands", "troll_cave", cp::Enforcement::AcidRain, 0.5f, "uberous_soil", "progress_merge"},
             {"snowy_forest", "yeti_cave", cp::Enforcement::Frost, 0, "lich_tower_miniature_structure", "progress_lich"},
             {"swamp", "labyrinth", cp::Enforcement::Hunger, 1, "lich_tower_miniature_structure", "progress_lich"},
             {"thornlands", "final_castle", cp::Enforcement::AcidRain, 1, "lamp_of_cinders", "progress_troll"},
         }) {
        auto it = all.find(std::string("twilightforest:") + r.biome);
        if (it == all.end()) {
            ctx.check(false, std::string(r.biome) + " is restricted");
            continue;
        }
        auto const& x = it->second;
        ctx.check(x.structureKey == std::string("twilightforest:") + r.structure && x.enforcement == r.e && x.multiplier == r.m
                      && x.toastIcon == std::string("twilightforest:") + r.icon && x.advancements == std::vector<std::string>{std::string("twilightforest:") + r.adv},
                  std::string(r.biome) + ": the record of TF's Restrictions");
    }
});

// The recipe unlocks (data/<ns>/advancement/recipes/**, generated by F1 into the BP recipes'
// "unlock"): every rewarded recipe is a BP recipe (or a numbered layout of one) or a Java data
// recipe of a custom type.
GF_REGISTER_SELFTEST("progression.recipe_unlocks", [](Context& ctx) {
    std::filesystem::path mod;
    try {
        mod = gf::Mod::getInstance().getSelf().getModDir();
    } catch (...) {}
    ctx.require(!mod.empty(), "the mod directory");
    std::error_code       ec;
    std::set<std::string> rewards, bp, data;
    int                   files = 0;
    for (auto const& ns : std::filesystem::directory_iterator(mod / "data", ec)) {
        auto root = ns.path() / "advancement" / "recipes";
        if (std::filesystem::is_directory(root, ec))
            for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
                if (f.path().extension() != ".json") continue;
                ++files;
                std::ifstream in(f.path());
                auto          j = Json::parse(in, nullptr, false);
                if (j.is_discarded()) continue;
                for (auto const& r : j.value("rewards", Json::object()).value("recipes", Json::array()))
                    if (r.is_string()) rewards.insert(r.get<std::string>());
            }
        auto recipes = ns.path() / "recipe";
        if (std::filesystem::is_directory(recipes, ec))
            for (auto const& f : std::filesystem::recursive_directory_iterator(recipes, ec))
                if (f.path().extension() == ".json")
                    data.insert(ns.path().filename().string() + ":" + std::filesystem::relative(f.path(), recipes, ec).replace_extension().generic_string());
    }
    auto bpDir = mod / "behavior_packs" / "GloamingForest_BP" / "recipes";
    for (auto const& f : std::filesystem::recursive_directory_iterator(bpDir, ec)) {
        if (f.path().extension() != ".json") continue;
        std::ifstream in(f.path());
        auto          j = Json::parse(in, nullptr, false);
        if (!j.is_object()) continue;
        for (auto const& [k, v] : j.items())
            if (v.is_object() && v.contains("description")) bp.insert(v["description"].value("identifier", std::string{}));
    }
    ctx.info(std::to_string(files) + " recipe unlock advancements, " + std::to_string(rewards.size()) + " rewarded recipes, " + std::to_string(bp.size())
             + " BP recipes");
    ctx.check(rewards.size() >= 485, "at least TF's 485 recipe unlocks");
    std::vector<std::string> missing;
    for (auto const& r : rewards) {
        bool ok = bp.contains(r) || data.contains(r);
        if (!ok)
            for (auto const& b : bp)
                if (cp::sameRecipe(r, b)) {
                    ok = true;
                    break;
                }
        if (!ok) missing.push_back(r);
    }
    std::string list;
    for (std::size_t i = 0; i < missing.size() && i < 10; ++i) list += " " + missing[i];
    ctx.check(missing.empty(), std::to_string(missing.size()) + " rewarded recipes resolve to no recipe:" + list);
});

// ---- criteria ----------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.inventory_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    // lich_scepters needs all four scepters at once
    for (char const* s : {"twilightforest:lifedrain_scepter", "twilightforest:twilight_scepter", "twilightforest:zombie_scepter"}) ctx.give(s, 1);
    co_await ctx.ticks(3);
    ctx.check(!pr::has(*playerById(id), "twilightforest:lich_scepters"), "three scepters are not enough for lich_scepters");
    ctx.check(pr::done(*playerById(id), "twilightforest:progress_lich", "lifedrain_scepter"), "progress_lich's lifedrain_scepter criterion");
    ctx.give("twilightforest:fortification_scepter", 1);
    co_await ctx.ticks(3);
    ctx.check(pr::has(*playerById(id), "twilightforest:lich_scepters"), "the four scepters earn lich_scepters");
    // arctic_armor_dyed: dyed pieces only (the twilightforest:color item sub-predicate)
    auto& p = *playerById(id);
    for (char const* piece : {"twilightforest:arctic_helmet", "twilightforest:arctic_chestplate", "twilightforest:arctic_leggings"}) {
        auto stack = gf::game::items::make(piece, 1);
        if (!stack) continue;
        gf::game::items::setData(*stack, "dyed_color", 0x3355AA);
        ctx.give(p, *stack);
    }
    ctx.give("twilightforest:arctic_boots", 1); // undyed
    co_await ctx.ticks(3);
    ctx.check(pr::done(*playerById(id), "twilightforest:arctic_armor_dyed", "helmet"), "a dyed hood meets the helmet criterion");
    ctx.check(!pr::done(*playerById(id), "twilightforest:arctic_armor_dyed", "boots"), "undyed boots do not");
    {
        auto stack = gf::game::items::make("twilightforest:arctic_boots", 1);
        ctx.require(stack.has_value(), "arctic boots exist");
        gf::game::items::setData(*stack, "dyed_color", 0x112233);
        ctx.give(*playerById(id), *stack);
    }
    co_await ctx.ticks(3);
    ctx.check(pr::has(*playerById(id), "twilightforest:arctic_armor_dyed"), "four dyed pieces earn arctic_armor_dyed");
    resetProgress(*playerById(id));
});

GF_REGISTER_SELFTEST("progression.kill_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    auto killNear = [&](std::string const& type) -> bool {
        auto* p = playerById(id);
        if (!p) return false;
        auto pos = p->getFeetPos();
        pos.x += 2;
        auto* mob = gf::spawn::spawnMob(p->getDimensionBlockSource(), type, pos, gf::spawn::SpawnReason::MobSummoned);
        if (!mob) return false;
        gf::combat::hurt(*mob, 10000.0f, "minecraft:player_attack", p, p);
        return true;
    };
    // a deer killed in the overworld is not a twilight hunt
    ctx.require(killNear("twilightforest:deer"), "a deer spawns in the overworld");
    co_await ctx.ticks(5);
    ctx.check(!pr::done(*playerById(id), "twilightforest:twilight_hunter", "deer"), "the overworld deer does not count for twilight_hunter");
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    id = ctx.player().getOrCreateUniqueID().rawID;
    co_await ctx.ticks(5);
    ctx.require(killNear("twilightforest:deer"), "a deer spawns in the Twilight Forest");
    co_await ctx.ticks(5);
    ctx.check(pr::done(*playerById(id), "twilightforest:twilight_hunter", "deer"), "killing a deer in the Twilight Forest meets twilight_hunter's deer");
    ctx.check(pr::has(*playerById(id), "twilightforest:twilight_hunter"), "one hunt earns twilight_hunter (any of its criteria)");
    resetProgress(*playerById(id));
});

GF_REGISTER_SELFTEST("progression.trigger_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  id = ctx.player().getOrCreateUniqueID().rawID;
    auto& p  = ctx.player();
    resetProgress(p);
    // the root: twilightforest:make_tf_portal (F4)
    pr::trigger(p, "twilightforest:make_tf_portal");
    ctx.check(pr::has(p, "twilightforest:root"), "make_tf_portal earns the root");
    // TF's triggers fired by their owners by string
    pr::trigger(p, "twilightforest:kill_bug", {{"bug", "twilightforest:firefly"}});
    ctx.check(!pr::has(p, "twilightforest:kill_cicada"), "a firefly is not a cicada");
    pr::trigger(p, "twilightforest:kill_bug", {{"block", "twilightforest:cicada"}});
    ctx.check(pr::has(p, "twilightforest:kill_cicada"), "kill_bug with a cicada earns kill_cicada");
    pr::trigger(p, "twilightforest:drink_from_flask", {{"doses", 3}, {"seconds", 9}, {"potion", "minecraft:strong_harming"}});
    ctx.check(!pr::has(p, "twilightforest:full_mettle_alchemist"), "three doses in 9 s are too slow");
    pr::trigger(p, "twilightforest:drink_from_flask", {{"doses", 3}, {"seconds", 5}, {"potion", "minecraft:strong_harming"}});
    ctx.check(pr::has(p, "twilightforest:full_mettle_alchemist"), "three strong harming doses in 5 s earn full_mettle_alchemist");
    pr::trigger(p, "twilightforest:uncraft_item", {{"item", "twilightforest:uncrafting_table"}});
    ctx.check(pr::has(p, "twilightforest:uncraft_uncrafting_table"), "uncrafting the table earns uncraft_uncrafting_table");
    for (auto [trigger, adv] : std::initializer_list<std::pair<char const*, char const*>>{
             {"twilightforest:broke_glass_sword", "twilightforest:break_glass_sword"},
             {"twilightforest:consume_hydra_chop_on_low_hunger", "twilightforest:hydra_chop"},
             {"twilightforest:add_modifier", "twilightforest:modify_travellers_gear"},
             {"twilightforest:complete_quest_ram", "twilightforest:quest_ram"},
             {"twilightforest:activate_ghast_trap", "twilightforest:ghast_trap"},
         }) {
        pr::trigger(p, trigger);
        ctx.check(pr::has(p, adv), std::string(trigger) + " earns " + adv);
    }
    pr::trigger(p, "twilightforest:placed_on_trophy_pedestal");
    ctx.check(pr::done(p, "twilightforest:progress_trophy_pedestal", "trophy_pedestal") && !pr::has(p, "twilightforest:progress_trophy_pedestal"),
              "placed_on_trophy_pedestal meets one of progress_trophy_pedestal's two groups");
    pr::trigger(p, "twilightforest:kill_all_phantoms");
    ctx.check(pr::done(p, "twilightforest:progress_knights", "all_knights"), "kill_all_phantoms meets progress_knights' all_knights");
    // the consume_item trigger F10 fires after Player#completeUsingItem
    pr::trigger(p, "minecraft:consume_item", {{"item", "twilightforest:experiment_115"}});
    ctx.check(pr::has(p, "twilightforest:experiment_115"), "eating experiment 115 earns experiment_115");
    ctx.check(pr::done(p, "twilightforest:twilight_dinner", "experiment_115") && !pr::has(p, "twilightforest:twilight_dinner"),
              "and one of twilight_dinner's 11 dishes");
    // recipe_crafted: every BP layout of a Java recipe counts
    pr::trigger(p, "minecraft:recipe_crafted", {{"recipe_id", "twilightforest:equipment/travellers_belt_3"}});
    ctx.check(pr::has(p, "twilightforest:craft_travellers_gear"), "crafting a travellers' belt layout earns craft_travellers_gear");
    // item_used_on_block: the block at the position after the use, the stack used
    auto pos = ctx.testPos(1400, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "the test position is loaded");
    ctx.require(ctx.setBlock(pos, "twilightforest:uberous_soil"), "uberous soil placed");
    {
        auto& q = *playerById(id);
        pr::trigger(q, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "minecraft:stick"}});
        ctx.check(!pr::done(q, "twilightforest:beanstalk", "use_beans"), "a stick on uberous soil is not the beans");
        pr::trigger(q, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "twilightforest:magic_beans"}});
        ctx.check(pr::done(q, "twilightforest:beanstalk", "use_beans"), "magic beans on uberous soil meet beanstalk's use_beans");
        // the boss fight criterion (F8's hurt_boss, also the group advancement path)
        pr::trigger(q, "twilightforest:hurt_boss", {{"entity", "twilightforest:hydra"}});
        ctx.check(pr::done(q, "twilightforest:progress_hydra", "was_in_fight"), "hurt_boss with the hydra meets progress_hydra's was_in_fight");
        resetProgress(q);
    }
});

// Grants follow the tick criteria at once: progress_naga makes progress_lich's "kill_naga" true.
GF_REGISTER_SELFTEST("progression.playthrough", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    {
        auto& p = ctx.player();
        ctx.check(pc::restrictionFor(p, "twilightforest:dark_forest") != nullptr, "the dark forest starts locked");
        ctx.check(pc::restrictionFor(p, "twilightforest:fire_swamp") != nullptr, "the fire swamp starts locked");
    }
    // the naga's scale: progress_naga (any of naga, trophy, scale, was_in_fight)
    ctx.give("twilightforest:naga_scale", 1);
    co_await ctx.ticks(3);
    {
        auto& p = *playerById(id);
        ctx.check(pr::has(p, "twilightforest:progress_naga"), "a naga scale earns progress_naga");
        ctx.check(pr::done(p, "twilightforest:progress_lich", "kill_naga"), "progress_lich's previous-progression criterion follows at once");
        ctx.check(!pr::has(p, "twilightforest:progress_lich"), "progress_lich still needs the lich");
        // the lich fight (F8's hurt_boss for every participant)
        pr::trigger(p, "twilightforest:hurt_boss", {{"entity", "twilightforest:lich"}});
        ctx.check(pr::has(p, "twilightforest:progress_lich"), "the lich fight earns progress_lich");
        for (char const* b : {"dark_forest", "snowy_forest", "swamp"})
            ctx.check(pc::restrictionFor(p, std::string("twilightforest:") + b) == nullptr, std::string(b) + " is unlocked after progress_lich");
        ctx.check(pc::restrictionFor(p, "twilightforest:fire_swamp") != nullptr, "the fire swamp still needs progress_labyrinth");
        ctx.check(pr::done(p, "twilightforest:progress_labyrinth", "kill_lich") && pr::done(p, "twilightforest:progress_yeti", "previous_progression")
                      && pr::done(p, "twilightforest:progress_merge", "hydra") == false,
                  "progress_lich's children see it at once");
        pr::trigger(p, "minecraft:consume_item", {{"item", "twilightforest:meef_stroganoff"}});
        ctx.check(pr::has(p, "twilightforest:progress_labyrinth"), "meef stroganoff after the lich earns progress_labyrinth");
        ctx.check(pc::restrictionFor(p, "twilightforest:fire_swamp") == nullptr, "the fire swamp is unlocked");
        ctx.check(pc::restrictionFor(p, "twilightforest:dark_forest_center") != nullptr, "the dark forest's centre still needs progress_knights");
        resetProgress(p);
    }
});

// ---- enforcement -------------------------------------------------------------------------------

/// Puts the test player into `biome` (as the enforcement reads it) for `ticks` ticks and returns
/// how often `e` acted.
ll::coro::CoroTask<int> stayIn(Context& ctx, std::int64_t id, std::string biome, cp::Enforcement e, int ticks) {
    auto* p = playerById(id);
    if (!p) co_return -1;
    pc::resetEnforcementCounts(*p);
    pc::setBiomeOverride(*p, biome);
    co_await ctx.ticks(ticks);
    p = playerById(id);
    if (!p) co_return -1;
    int n = pc::enforcementCount(*p, e);
    pc::setBiomeOverride(*p, "");
    co_return n;
}

GF_REGISTER_SELFTEST("progression.enforce_darkness", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    int n = co_await stayIn(ctx, id, "twilightforest:dark_forest", cp::Enforcement::Darkness, 125);
    ctx.check(n == 2 || n == 3, "darkness every 60 ticks: " + std::to_string(n) + " times in 125 ticks");
    ctx.check(gf::effects::has(*playerById(id), "minecraft:darkness"), "the player has Darkness");
    ctx.check(gf::effects::amplifier(*playerById(id), "minecraft:darkness") == 0, "Darkness I (multiplier 0)");
    pr::grant(*playerById(id), "twilightforest:progress_lich");
    n = co_await stayIn(ctx, id, "twilightforest:dark_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "no darkness once progress_lich is earned (" + std::to_string(n) + ")");
    gf::effects::remove(*playerById(id), "minecraft:darkness");
    resetProgress(*playerById(id));
});

GF_REGISTER_SELFTEST("progression.enforce_acid_rain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    // the inner 5-tick gate inside the outer 20-tick one: every 20 ticks, not 5
    int n = co_await stayIn(ctx, id, "twilightforest:highlands", cp::Enforcement::AcidRain, 61);
    ctx.check(n == 3 || n == 4, "acid rain every 20 ticks: " + std::to_string(n) + " times in 61 ticks");
});

GF_REGISTER_SELFTEST("progression.enforce_fire_hunger_frost", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    int n = co_await stayIn(ctx, id, "twilightforest:fire_swamp", cp::Enforcement::Fire, 62);
    ctx.check(n == 1 || n == 2, "fire every 60 ticks (" + std::to_string(n) + ")");
    ctx.check(playerById(id)->isOnFire(), "the fire swamp sets the player on fire");
    playerById(id)->stopFire();
    n = co_await stayIn(ctx, id, "twilightforest:swamp", cp::Enforcement::Hunger, 125);
    ctx.check(n == 2 || n == 3, "hunger every 60 ticks (" + std::to_string(n) + ")");
    ctx.check(gf::effects::amplifier(*playerById(id), "minecraft:hunger") >= 2, "Hunger stacks: the amplifier grows by the multiplier on each application");
    gf::effects::remove(*playerById(id), "minecraft:hunger");
    n = co_await stayIn(ctx, id, "twilightforest:glacier", cp::Enforcement::Frost, 62);
    ctx.check(n == 1 || n == 2, "frost every 60 ticks (" + std::to_string(n) + ")");
    gf::effects::remove(*playerById(id), "twilightforest:frosted");
});

GF_REGISTER_SELFTEST("progression.enforce_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    bool was = gf::gamerules::getBool("tfEnforcedProgression");
    gf::gamerules::set("tfEnforcedProgression", false);
    int n = co_await stayIn(ctx, id, "twilightforest:dark_forest", cp::Enforcement::Darkness, 65);
    gf::gamerules::set("tfEnforcedProgression", was);
    ctx.check(n == 0, "tfEnforcedProgression false: no enforcement (" + std::to_string(n) + ")");
    playerById(id)->setPlayerGameType(GameType::Creative);
    n = co_await stayIn(ctx, id, "twilightforest:dark_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "creative players are not enforced (" + std::to_string(n) + ")");
    playerById(id)->setPlayerGameType(GameType::Survival);
    n = co_await stayIn(ctx, id, "twilightforest:enchanted_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "an unrestricted biome is not enforced (" + std::to_string(n) + ")");
});

// ---- presentation ------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.toasts_and_screen", [](Context& ctx) {
    auto& p = ctx.player();
    resetProgress(p);
    pr::trigger(p, "twilightforest:make_tf_portal"); // the root
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    pr::grant(p, "twilightforest:hedge");
    auto msgs  = gf::net::takeTestMessages(p);
    auto toast = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "progression.toast"; });
    ctx.require(toast != msgs.end(), "a companion gets progression.toast when it earns an advancement");
    ctx.check(toast->payload.value("advancement", "") == "twilightforest:hedge" && toast->payload.value("frame", "") == "task"
                  && toast->payload.value("title", "") == "advancement.twilightforest.hedge" && toast->payload.value("icon", "") == "twilightforest:hedge",
              "the toast names the advancement, its frame, title and icon");
    // the screen: native data for companions
    ctx.check(pc::openAdvancements(p), "the advancement screen opens");
    msgs      = gf::net::takeTestMessages(p);
    auto open = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "screen.open"; });
    ctx.require(open != msgs.end(), "the companion gets screen.open");
    auto data = open->payload.value("data", Json::object());
    ctx.check(open->payload.value("type", "") == "progression.advancements", "of type progression.advancements");
    ctx.check(data["tab"].value("background", "") == "textures/twilightforest/block/mazestone_large_brick", "TF's tab background");
    std::set<std::string> shown;
    for (auto const& n : data["nodes"]) shown.insert(n.value("id", ""));
    ctx.check(shown.contains("twilightforest:hedge") && shown.contains("twilightforest:root"), "the earned hedge and its parent are shown");
    ctx.check(shown.contains("twilightforest:progress_naga") && shown.contains("twilightforest:progress_lich"),
              "unearned nodes up to two levels below an earned one are shown");
    ctx.check(!shown.contains("twilightforest:kill_cicada"), "a hidden advancement is not shown before it is earned");
    ctx.check(!shown.contains("twilightforest:progress_labyrinth"), "an advancement three levels below anything earned is not shown");
    gf::net::setTestCompanion(p, false);
    // vanilla players: the form (and the game's toast)
    ctx.check(pc::openAdvancements(p), "a vanilla player gets the form");
    pr::grant(p, "twilightforest:hill1");
    ctx.check(pr::has(p, "twilightforest:hill1"), "a vanilla player earns an advancement with the game's toast");
    resetProgress(p);
});

// The vanilla fallback of LockedBiomeToastHandler: the server's own countdown per player.
GF_REGISTER_SELFTEST("progression.vanilla_locked_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    pc::resetEnforcementCounts(ctx.player());
    int before = pc::vanillaLockedToasts(ctx.player());
    pc::setBiomeOverride(ctx.player(), "twilightforest:swamp");
    co_await ctx.ticks(250);
    ctx.check(pc::vanillaLockedToasts(*playerById(id)) == before, "no toast before the 60-step countdown (300 ticks)");
    co_await ctx.ticks(70);
    ctx.check(pc::vanillaLockedToasts(*playerById(id)) == before + 1, "one toast after the countdown");
    co_await ctx.ticks(40);
    ctx.check(pc::vanillaLockedToasts(*playerById(id)) == before + 1, "only once per visit");
    pc::setBiomeOverride(*playerById(id), "");
    gf::effects::remove(*playerById(id), "minecraft:hunger");
});

// The display texts with their arguments (hill1: "Defeat a %s ..." with the Redcap's name).
GF_REGISTER_SELFTEST("progression.descriptions", [](Context& ctx) {
    auto&    p     = ctx.player();
    int      withArgs = 0;
    for (auto const& a : pc::advancements()) {
        if (!a.hasDisplay) continue;
        if (a.description.is_object() && a.description.contains("with")) ++withArgs;
        auto title = pc::textFor(p, a.title);
        auto desc  = pc::textFor(p, a.description);
        ctx.check(!title.empty() && title.find("advancement.twilightforest") == std::string::npos, a.id + "'s title is translated (" + title + ")");
        ctx.check(desc.find("%s") == std::string::npos && desc.find("%1$s") == std::string::npos && desc.find("{}") == std::string::npos,
                  a.id + "'s description has its arguments (" + desc + ")");
    }
    ctx.check(withArgs >= 20, std::to_string(withArgs) + " descriptions take arguments");
    auto hill1 = pc::textFor(p, pc::find("twilightforest:hill1")->description);
    ctx.check(hill1.find("Redcap") != std::string::npos, "hill1 names the Redcap: " + hill1);
    auto merge = pc::textFor(p, pc::find("twilightforest:progress_merge")->description);
    ctx.check(merge.find("Hydra") != std::string::npos && merge.find("Ur-Ghast") != std::string::npos && merge.find("Snow Queen") != std::string::npos,
              "progress_merge names its three bosses: " + merge);
});

// TreeNodePosition over the whole tree (Java's server layout), sent unchanged to the screen.
GF_REGISTER_SELFTEST("progression.tree_layout", [](Context& ctx) {
    std::set<std::pair<float, float>> cells;
    for (auto const& a : pc::advancements()) {
        ctx.check(a.placed, a.id + " is laid out");
        ctx.check(cells.insert({a.x, a.y}).second, a.id + " has a cell of its own");
    }
    ctx.check(pc::find("twilightforest:root")->x == 0 && pc::find("twilightforest:progression_end")->x == 7, "the root in column 0, progression_end in column 7");
    // hill2 sits right of hill1 on its row; experiment_115's two children around it
    auto const* h1 = pc::find("twilightforest:hill1");
    auto const* h2 = pc::find("twilightforest:hill2");
    ctx.check(h2->x == h1->x + 1 && h2->y == h1->y, "a single child stays on its parent's row");
    auto const* e  = pc::find("twilightforest:experiment_115");
    auto const* e1 = pc::find("twilightforest:experiment_115_115");
    auto const* e2 = pc::find("twilightforest:experiment_115_self_replenishing");
    ctx.check(e->y == (e1->y + e2->y) / 2, "a parent sits halfway between its first and last child");
    // the screen sends the server's positions
    auto& p = ctx.player();
    resetProgress(p);
    pr::trigger(p, "twilightforest:make_tf_portal");
    auto data = pc::screenData(p);
    int  same = 0;
    for (auto const& n : data["nodes"]) {
        auto const* a = pc::find(n.value("id", ""));
        if (a && n.value("x", -1.0f) == a->x && n.value("y", -1.0f) == a->y) ++same;
        ctx.check(!n.value("title", Json()).is_string() || !n.value("title", Json()).get<std::string>().empty(), "a node has a title component");
    }
    ctx.check(same == static_cast<int>(data["nodes"].size()) && same > 0, "the screen nodes keep the server's layout (" + std::to_string(same) + ")");
    resetProgress(p);
});

// The icon's components travel as item data (experiment_115_115's "think" variant, the flask).
GF_REGISTER_SELFTEST("progression.icon_variants", [](Context& ctx) {
    auto check = [&](char const* adv, char const* component, Json const& want) {
        auto const* a = pc::find(std::string("twilightforest:") + adv);
        ctx.require(a != nullptr, std::string(adv) + " is loaded");
        auto item = pc::iconItem(*a);
        ctx.check(item.is_object() && item.value("id", "") == a->icon, std::string(adv) + "'s icon item is " + a->icon);
        auto stack = gf::game::items::fromJson(item);
        auto got   = gf::game::items::getData(stack, component);
        ctx.check(got == want, std::string(adv) + "'s icon carries " + component + " = " + want.dump() + " (" + got.dump() + ")");
    };
    check("experiment_115_115", "twilightforest:e115_variant", "think");
    check("experiment_115_self_replenishing", "twilightforest:e115_variant", "full");
    check("full_mettle_alchemist", "twilightforest:flask_contents", Json{{"breakable", false}, {"doses", 4}, {"potion", {{"potion", "minecraft:strong_harming"}}}});
    check("modify_travellers_gear", "twilightforest:water_walk", Json::object());
});

// chicken_jerky: a baby zombie riding a chicken, killed by a blow whose direct entity holds the
// jerky. The vehicle is read before die() dismounts the rider; a kill whose direct entity is not
// the player (a projectile) does not count.
GF_REGISTER_SELFTEST("progression.chicken_jockey", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    auto base = ctx.testPos(1404, 3);
    ctx.require(co_await ctx.ensureLoaded(base), "the test position is loaded");
    auto jockey = [&](std::string const& name) -> Actor* {
        auto* p = playerById(id);
        if (!p) return nullptr;
        Vec3 pos{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 1.0f, static_cast<float>(base.z) + 0.5f};
        auto& region  = p->getDimensionBlockSource();
        auto* chicken = gf::spawn::spawnMob(region, "minecraft:chicken", pos, gf::spawn::SpawnReason::MobSummoned);
        auto* zombie  = gf::spawn::spawnMob(region, "minecraft:zombie", pos, gf::spawn::SpawnReason::MobSummoned);
        if (!chicken || !zombie) return nullptr;
        zombie->setNameTag(name);
        (void)gf::selftest::runServerCommand("event entity @e[name=" + name + "] minecraft:as_baby", base.dim);
        zombie->startRiding(*chicken, true);
        return zombie;
    };
    auto jerky = gf::game::items::make("twilightforest:chicken_jerky", 1);
    ctx.require(jerky.has_value(), "chicken jerky exists");
    playerById(id)->setSelectedItem(*jerky);
    // a blow whose direct entity is another entity (a projectile's place): not the jerky's holder
    {
        auto* z = jockey("gf_jockey_a");
        ctx.require(z != nullptr, "a jockey spawns");
        co_await ctx.ticks(2);
        z = nullptr;
        Actor* rider = nullptr;
        auto   level = ll::service::getLevel();
        for (auto* a : level->getRuntimeActorList())
            if (a && a->getNameTag() == "gf_jockey_a") rider = a;
        ctx.require(rider && rider->isBaby() && rider->getVehicle(), "the baby zombie rides the chicken");
        auto* vehicle = rider->getVehicle();
        gf::combat::hurt(*rider, 1000.0f, "minecraft:arrow", playerById(id), vehicle);
        co_await ctx.ticks(2);
        ctx.check(!pr::has(*playerById(id), "twilightforest:chicken_jerky"), "a kill whose direct entity is not the player does not count");
    }
    {
        auto* z = jockey("gf_jockey_b");
        ctx.require(z != nullptr, "a second jockey spawns");
        co_await ctx.ticks(2);
        Actor* rider = nullptr;
        for (auto* a : ll::service::getLevel()->getRuntimeActorList())
            if (a && a->getNameTag() == "gf_jockey_b") rider = a;
        ctx.require(rider && rider->isBaby() && rider->getVehicle(), "the second baby zombie rides its chicken");
        gf::combat::hurt(*rider, 1000.0f, "minecraft:player_attack", playerById(id), playerById(id));
        co_await ctx.ticks(2);
        ctx.check(pr::has(*playerById(id), "twilightforest:chicken_jerky"), "killing the jockey's rider with jerky in hand earns chicken_jerky");
    }
    (void)gf::selftest::runServerCommand("kill @e[type=minecraft:chicken,x=" + std::to_string(base.x) + ",y=" + std::to_string(base.y) + ",z=" + std::to_string(base.z) + ",r=6]", base.dim);
    (void)gf::selftest::runServerCommand("kill @e[type=minecraft:item,x=" + std::to_string(base.x) + ",y=" + std::to_string(base.y) + ",z=" + std::to_string(base.z) + ",r=8]", base.dim);
    playerById(id)->setSelectedItem(gf::game::items::empty());
    resetProgress(*playerById(id));
});
// ---- e2e demos: what the client scenario photographs -------------------------------------------
// They act only on real players who opted in with the scoreboard tag `gf_e2e_progression` (the
// client scenario adds it with /tag and removes it at the end), so a glob or full run on a played
// world never touches anyone's progression. The first demo saves each opted-in player's exact
// progress (every earned advancement and every met criterion) and demo_reset puts it back.

constexpr char kDemoTag[] = "gf_e2e_progression";

struct SavedProgress {
    std::set<std::string>                           done;     ///< completed advancements
    std::map<std::string, std::vector<std::string>> criteria; ///< met criteria of the others
};
std::map<std::int64_t, SavedProgress>& savedProgress() {
    static std::map<std::int64_t, SavedProgress> s;
    return s;
}

SavedProgress snapshotOf(Player const& p) {
    SavedProgress s;
    for (auto const& a : pc::advancements()) {
        if (pr::has(p, a.id)) {
            s.done.insert(a.id);
            continue;
        }
        for (auto const& c : a.criteria)
            if (pr::done(p, a.id, c.name)) s.criteria[a.id].push_back(c.name);
    }
    return s;
}

/// Saves the player's progress once per demo run.
void saveOnce(Player& p) {
    auto id = p.getOrCreateUniqueID().rawID;
    if (!savedProgress().contains(id)) savedProgress()[id] = snapshotOf(p);
}

/// Puts the saved progress back (the advancements earned again show their toasts once more).
bool restoreSaved(Player& p) {
    auto it = savedProgress().find(p.getOrCreateUniqueID().rawID);
    if (it == savedProgress().end()) return false;
    resetProgress(p);
    for (auto const& a : pc::advancements()) {
        if (it->second.done.contains(a.id)) pr::grant(p, a.id);
        else if (auto c = it->second.criteria.find(a.id); c != it->second.criteria.end())
            for (auto const& name : c->second) pr::grant(p, a.id, name);
    }
    savedProgress().erase(it);
    return true;
}

void forOptedIn(std::function<void(Player&)> const& fn) {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<std::int64_t> ids;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulated() && !p.isClientSide() && p.hasTag(kDemoTag)) ids.push_back(p.getOrCreateUniqueID().rawID);
        return true;
    });
    for (auto id : ids)
        if (auto* p = playerById(id)) fn(*p);
}

GF_REGISTER_SELFTEST("progression.demo_grant", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        saveOnce(p);
        resetProgress(p);
        for (char const* a : {"root", "progress_naga", "hedge", "hill1", "twilight_hunter", "progress_lich", "naga_armors"})
            pr::grant(p, std::string("twilightforest:") + a);
        ++n;
    });
    ctx.info("granted the demo advancements to " + std::to_string(n) + " opted-in player(s)");
});

GF_REGISTER_SELFTEST("progression.demo_toast", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        saveOnce(p);
        pr::revoke(p, "twilightforest:progress_labyrinth");
        pr::grant(p, "twilightforest:progress_labyrinth");
        ++n;
    });
    ctx.info("progress_labyrinth earned by " + std::to_string(n) + " opted-in player(s)");
});

GF_REGISTER_SELFTEST("progression.demo_screen", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        if (pc::openAdvancements(p)) ++n;
    });
    if (n == 0) ctx.info("no opted-in player in this world: nothing opened");
});

GF_REGISTER_SELFTEST("progression.demo_reset", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        pc::setBiomeOverride(p, "");
        if (restoreSaved(p)) ++n;
    });
    ctx.info("restored the progress of " + std::to_string(n) + " opted-in player(s)");
});

// A vanilla player in a locked biome (the swamp: Hunger, harmless to the picture) for 400 ticks
// in survival: the server's toast after the countdown (photographed); afterwards the override
// ends and the game type is put back by itself.
GF_REGISTER_SELFTEST("progression.demo_vanilla_locked", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        if (gf::net::isCompanion(p)) return;
        saveOnce(p);
        pr::revoke(p, "twilightforest:progress_lich");
        pc::resetEnforcementCounts(p);
        auto type = p.getPlayerGameType();
        p.setPlayerGameType(GameType::Survival);
        pc::setBiomeOverride(p, "twilightforest:swamp", 400, type);
        ++n;
    });
    ctx.info("opted-in vanilla players put into a locked biome: " + std::to_string(n));
});
#ifdef GF_CLIENT

std::int64_t localCompanion() {
    std::int64_t id    = 0;
    auto         level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&id](Player& p) {
        if (p.isSimulated() || !gf::net::isCompanion(p) || !p.hasTag(kDemoTag)) return true;
        id = p.getOrCreateUniqueID().rawID;
        return false;
    });
    return id;
}

namespace pcl = gf::client::progression;

// The client mod's advancement toast for a real grant (Java's AdvancementToast).
GF_REGISTER_SELFTEST("progression.client_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto who = localCompanion();
    if (who == 0) {
        ctx.info("no opted-in companion player in this world: skipped");
        co_return;
    }
    bool hadHedge = pr::has(*playerById(who), "twilightforest:hedge");
    pr::revoke(*playerById(who), "twilightforest:hedge");
    pr::grant(*playerById(who), "twilightforest:hedge");
    std::optional<pcl::ShownToast> shown;
    for (int i = 0; i < 60; ++i) {
        co_await ctx.ticks(1);
        shown = pcl::lastToast();
        if (shown && shown->what == "twilightforest:hedge") break;
    }
    ctx.require(shown && shown->what == "twilightforest:hedge", "the client mod showed the hedge's toast");
    ctx.check(shown->body == "advancement.twilightforest.hedge" && shown->icon == "twilightforest:hedge" && shown->ms == 5000,
              "the advancement's title and icon, 5 s");
    auto style = gf::client::toast::styleOfToast(shown->title);
    ctx.check(style && style->titleColor == 0xFF55FF55u && style->bodyColor == 0xFFFFFFFFu, "task: green title, white advancement title");
    if (!hadHedge)
        if (auto* p = playerById(who)) pr::revoke(*p, "twilightforest:hedge");
});

// LockedBiomeToastHandler: one toast after the 60-step countdown (300 ticks) in a locked biome.
GF_REGISTER_SELFTEST("progression.client_locked_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto who = localCompanion();
    if (who == 0) {
        ctx.info("no opted-in companion player in this world: skipped");
        co_return;
    }
    auto  type = playerById(who)->getPlayerGameType();
    bool  had  = pr::has(*playerById(who), "twilightforest:progress_lich");
    if (had) pr::revoke(*playerById(who), "twilightforest:progress_lich");
    playerById(who)->setPlayerGameType(GameType::Survival);
    pcl::setClientBiomeOverride("twilightforest:dark_forest");
    pcl::resetCountdown();
    int before = pcl::lockedToastCount();
    int waited = 0;
    while (pcl::lockedToastCount() == before && waited < 420) {
        co_await ctx.ticks(5);
        waited += 5;
    }
    ctx.check(pcl::lockedToastCount() == before + 1, "the locked biome toast came after " + std::to_string(waited) + " ticks");
    ctx.check(waited >= 280, "not before the 60-step countdown (" + std::to_string(waited) + " ticks)");
    co_await ctx.ticks(40);
    ctx.check(pcl::lockedToastCount() == before + 1, "only once while the player stays");
    auto shown = pcl::lastToast();
    ctx.check(shown && shown->icon == "twilightforest:lich_tower_miniature_structure" && shown->title == "misc.twilightforest.biome_locked",
              "the dark forest's icon and TF's text");
    pcl::setClientBiomeOverride("minecraft:plains");
    co_await ctx.ticks(10);
    auto [time, shownFlag] = pcl::countdown();
    ctx.check(time == 60 && !shownFlag, "leaving resets the countdown after a shown toast");
    pcl::setClientBiomeOverride("");
    if (auto* p = playerById(who)) {
        p->setPlayerGameType(type);
        if (had) pr::grant(*p, "twilightforest:progress_lich");
    }
});

#endif

} // namespace
