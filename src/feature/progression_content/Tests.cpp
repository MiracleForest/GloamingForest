// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// U14 selftests (progression.*, dx 1400-1499): upstream's 45 advancements as loaded, the restriction
// registry, the recipe unlocks, the criteria engine on every trigger kind (inventory, kills of
// real entities by id, consumption, item used on a block, crafting, location / tick and upstream's 13
// triggers), the scripted playthrough that lifts the biome locks, the enforcements and their
// timing, the gamerule, the toasts and the advancement screen (native data for companions, the
// form for vanilla players), and in the client build the client mod's toasts.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"

#include "core/progression/Rules.h"
#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/entity/Entity.h"
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
#include "client/Client.h"
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

/// Takes every upstream advancement back (simulated players may keep a UUID between tests).
void resetProgress(Player& p) {
    for (auto const& a : pc::advancements()) pr::revoke(p, a.id);
}

/// The test player's advancements show nothing while the scope lives (no toast, chat line to the
/// server's real players, experience or fanfare): for tests that do not check the presentation.
struct QuietScope {
    std::int64_t id;
    explicit QuietScope(Player& p) : id(p.getOrCreateUniqueID().rawID) { pc::setQuietFor(id, true); }
    ~QuietScope() { pc::setQuietFor(id, false); }
    QuietScope(QuietScope const&)            = delete;
    QuietScope& operator=(QuietScope const&) = delete;
};

/// The chat announcements go to a list instead of every player while the scope lives.
struct AnnounceCapture {
    std::shared_ptr<std::vector<std::string>> seen = std::make_shared<std::vector<std::string>>();
    AnnounceCapture() {
        pc::setAnnounceSink([seen = seen](Player&, pc::Advancement const& a) { seen->push_back(a.id); });
    }
    ~AnnounceCapture() { pc::setAnnounceSink({}); }
    AnnounceCapture(AnnounceCapture const&)            = delete;
    AnnounceCapture& operator=(AnnounceCapture const&) = delete;
};

// ---- data -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.advancements", [](Context& ctx) {
    auto const& all = pc::advancements();
    ctx.require(all.size() == 45, "upstream's 45 advancements are loaded (" + std::to_string(all.size()) + ")");
    std::map<std::string, std::string> parents{
        {"progress_naga", "root"}, {"hill1", "root"}, {"hill2", "hill1"}, {"hill3", "hill2"}, {"hedge", "root"}, {"gloaming_hunter", "root"},
        {"magic_map_focus", "gloaming_hunter"}, {"magic_map", "magic_map_focus"}, {"maze_map", "magic_map"}, {"ore_map", "maze_map"},
        {"gloaming_dinner", "root"}, {"kill_cicada", "root"}, {"quest_ram", "root"}, {"arborist", "root"}, {"chicken_jerky", "root"},
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
        auto const* a = pc::find("gloamingforest:" + id);
        if (!a) {
            ctx.check(false, id + " is loaded");
            continue;
        }
        ctx.check(a->parent == "gloamingforest:" + parent, id + "'s parent is " + parent + " (" + a->parent + ")");
    }
    auto const* root = pc::find("gloamingforest:root");
    ctx.require(root && root->parent.empty(), "root has no parent");
    ctx.check(root->background == "gloamingforest:textures/block/mazestone_large_brick.png", "the tab background is mazestone_large_brick");
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
                          "minecraft:recipe_crafted", "minecraft:location", "minecraft:tick", "gloamingforest:make_tf_portal", "gloamingforest:hurt_boss",
                          "gloamingforest:kill_bug", "gloamingforest:broke_glass_sword", "gloamingforest:drink_from_flask",
                          "gloamingforest:consume_hydra_chop_on_low_hunger", "gloamingforest:add_modifier", "gloamingforest:uncraft_item",
                          "gloamingforest:complete_quest_ram", "gloamingforest:placed_on_trophy_pedestal", "gloamingforest:kill_all_phantoms",
                          "gloamingforest:activate_ghast_trap"})
        ctx.check(byTrigger.contains(t), std::string("a criterion listens to ") + t);
    ctx.check(byTrigger["minecraft:player_killed_entity"] == 69 && byTrigger["minecraft:tick"] == 13 && byTrigger["gloamingforest:hurt_boss"] == 6,
              "criteria per trigger: 69 kills, 13 ticks, 6 boss fights");
    ctx.check(pc::find("gloamingforest:arborist")->experience == 1000 && pc::find("gloamingforest:gloaming_dinner")->experience == 75, "experience rewards");
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
        auto it = all.find(std::string("gloamingforest:") + r.biome);
        if (it == all.end()) {
            ctx.check(false, std::string(r.biome) + " is restricted");
            continue;
        }
        auto const& x = it->second;
        ctx.check(x.structureKey == std::string("gloamingforest:") + r.structure && x.enforcement == r.e && x.multiplier == r.m
                      && x.toastIcon == std::string("gloamingforest:") + r.icon && x.advancements == std::vector<std::string>{std::string("gloamingforest:") + r.adv},
                  std::string(r.biome) + ": the record of upstream's Restrictions");
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
    ctx.check(rewards.size() >= 485, "at least upstream's 485 recipe unlocks");
    std::vector<std::string> missing;
    // a rewarded id names a Java recipe by definition (RecipeHolder ids)
    cp::RecipeIndex layouts{[&](std::string const& id) { return data.contains(id) || rewards.contains(id); }, [&](std::string const& id) { return bp.contains(id); }};
    for (auto const& r : rewards) {
        bool ok = bp.contains(r) || data.contains(r);
        if (!ok)
            for (auto const& b : bp)
                if (cp::sameRecipe(r, b, layouts)) {
                    ok = true;
                    break;
                }
        if (!ok) missing.push_back(r);
    }
    std::string list;
    for (std::size_t i = 0; i < missing.size() && i < 10; ++i) list += " " + missing[i];
    ctx.check(missing.empty(), std::to_string(missing.size()) + " rewarded recipes resolve to no recipe:" + list);
});

// The challenge toast's sound (for Mojang's ui.toast.challenge_complete): U14's original sound is in
// the assembled resource pack (category ui, its .ogg present), so fx::soundTo and the client's
// playUI find it.
GF_REGISTER_SELFTEST("progression.challenge_sound", [](Context& ctx) {
    std::filesystem::path mod;
    try {
        mod = gf::Mod::getInstance().getSelf().getModDir();
    } catch (...) {}
    ctx.require(!mod.empty(), "the mod directory");
    auto          rp = mod / "resource_packs" / "GloamingForest_RP";
    std::ifstream in(rp / "sounds" / "sound_definitions.json");
    auto          j    = Json::parse(in, nullptr, false, true);
    auto const&   defs = j.is_object() && j.contains("sound_definitions") ? j["sound_definitions"] : j;
    ctx.require(defs.is_object() && defs.contains(cp::kChallengeSound), std::string("the resource pack defines ") + cp::kChallengeSound);
    auto const& def = defs[cp::kChallengeSound];
    ctx.check(def.value("category", std::string{}) == "ui", "its category is ui (Java's forUI)");
    auto name = def.value("sounds", Json::array()).empty() ? std::string{} : def["sounds"][0].value("name", std::string{});
    std::error_code ec;
    ctx.check(!name.empty() && std::filesystem::is_regular_file(rp / (name + ".ogg"), ec), "its file " + name + ".ogg is in the pack");
});

// ---- criteria ----------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.inventory_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto  id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    // lich_scepters needs all four scepters at once
    for (char const* s : {"gloamingforest:lifedrain_scepter", "gloamingforest:gloaming_scepter", "gloamingforest:zombie_scepter"}) ctx.give(s, 1);
    co_await ctx.ticks(3);
    ctx.check(!pr::has(*playerById(id), "gloamingforest:lich_scepters"), "three scepters are not enough for lich_scepters");
    ctx.check(pr::done(*playerById(id), "gloamingforest:progress_lich", "lifedrain_scepter"), "progress_lich's lifedrain_scepter criterion");
    ctx.give("gloamingforest:fortification_scepter", 1);
    co_await ctx.ticks(3);
    ctx.check(pr::has(*playerById(id), "gloamingforest:lich_scepters"), "the four scepters earn lich_scepters");
    // arctic_armor_dyed: dyed pieces only (the gloamingforest:color item sub-predicate)
    auto& p = *playerById(id);
    for (char const* piece : {"gloamingforest:arctic_helmet", "gloamingforest:arctic_chestplate", "gloamingforest:arctic_leggings"}) {
        auto stack = gf::game::items::make(piece, 1);
        if (!stack) continue;
        gf::game::items::setData(*stack, "dyed_color", 0x3355AA);
        ctx.give(p, *stack);
    }
    ctx.give("gloamingforest:arctic_boots", 1); // undyed
    co_await ctx.ticks(3);
    ctx.check(pr::done(*playerById(id), "gloamingforest:arctic_armor_dyed", "helmet"), "a dyed hood meets the helmet criterion");
    ctx.check(!pr::done(*playerById(id), "gloamingforest:arctic_armor_dyed", "boots"), "undyed boots do not");
    {
        auto stack = gf::game::items::make("gloamingforest:arctic_boots", 1);
        ctx.require(stack.has_value(), "arctic boots exist");
        gf::game::items::setData(*stack, "dyed_color", 0x112233);
        ctx.give(*playerById(id), *stack);
    }
    co_await ctx.ticks(3);
    ctx.check(pr::has(*playerById(id), "gloamingforest:arctic_armor_dyed"), "four dyed pieces earn arctic_armor_dyed");
    resetProgress(*playerById(id));
});

GF_REGISTER_SELFTEST("progression.kill_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
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
    // a deer killed in the overworld is not a gloaming hunt
    ctx.require(killNear("gloamingforest:deer"), "a deer spawns in the overworld");
    co_await ctx.ticks(5);
    ctx.check(!pr::done(*playerById(id), "gloamingforest:gloaming_hunter", "deer"), "the overworld deer does not count for gloaming_hunter");
    ctx.require(co_await ctx.inGloaming(), "the Gloaming Forest is available");
    id = ctx.player().getOrCreateUniqueID().rawID;
    co_await ctx.ticks(5);
    ctx.require(killNear("gloamingforest:deer"), "a deer spawns in the Gloaming Forest");
    co_await ctx.ticks(5);
    ctx.check(pr::done(*playerById(id), "gloamingforest:gloaming_hunter", "deer"), "killing a deer in the Gloaming Forest meets gloaming_hunter's deer");
    ctx.check(pr::has(*playerById(id), "gloamingforest:gloaming_hunter"), "one hunt earns gloaming_hunter (any of its criteria)");
    resetProgress(*playerById(id));
});

// EntityEvents#resetFlaskLogic: earning an advancement with a drink_from_flask criterion hands the
// reset of the flask dose tracking to its owner (U9) as the trigger
// gloamingforest:flask_doses_reset {"advancement": id}; other advancements do not. U14 never
// writes U9's tracking (R2).
std::vector<std::string>& flaskResets() {
    static std::vector<std::string> seen;
    static bool                     listening = false;
    if (!listening) {
        listening = true;
        pr::onTrigger("gloamingforest:flask_doses_reset", [](Player& p, Json const& data) {
            if (p.isSimulated()) flaskResets().push_back(data.value("advancement", std::string{}));
        });
    }
    return seen;
}

GF_REGISTER_SELFTEST("progression.flask_reset", [](Context& ctx) {
    auto&      p = ctx.player();
    QuietScope quiet{p};
    resetProgress(p);
    auto& seen = flaskResets();
    seen.clear();
    auto const before = gf::entity::data(p, "gloamingforest:flask_doses"); // whatever its owner keeps there
    pr::grant(p, "gloamingforest:hedge");
    ctx.check(seen.empty(), "an advancement without a flask criterion hands over no reset");
    pr::grant(p, "gloamingforest:full_mettle_alchemist");
    ctx.check(seen == std::vector<std::string>{"gloamingforest:full_mettle_alchemist"},
              "full_mettle_alchemist hands the reset to the tracking's owner (gloamingforest:flask_doses_reset)");
    ctx.check(gf::entity::data(p, "gloamingforest:flask_doses") == before, "U14 leaves U9's tracking data alone");
    seen.clear();
    resetProgress(p);
});

// PlayerAdvancements#award announces only while the announceAdvancements gamerule is true.
GF_REGISTER_SELFTEST("progression.announce_gamerule", [](Context& ctx) {
    auto&           p = ctx.player();
    AnnounceCapture capture;
    resetProgress(p);
    pr::trigger(p, "gloamingforest:make_tf_portal"); // the root (announced: true)
    // the world's rule is put back however the test ends
    struct RuleRestore {
        bool was = gf::gamerules::getBool("announceAdvancements");
        ~RuleRestore() { gf::gamerules::set("announceAdvancements", was); }
    } restore;
    ctx.require(gf::gamerules::set("announceAdvancements", true), "announceAdvancements is a registered bool gamerule");
    capture.seen->clear();
    pr::grant(p, "gloamingforest:hedge");
    ctx.check(*capture.seen == std::vector<std::string>{"gloamingforest:hedge"}, "announceAdvancements true: hedge is announced");
    gf::gamerules::set("announceAdvancements", false);
    pr::grant(p, "gloamingforest:hill1");
    ctx.check(capture.seen->size() == 1 && pr::has(p, "gloamingforest:hill1"), "announceAdvancements false: hill1 is earned without an announcement");
    resetProgress(p);
});
// The demo's restore is quiet: no toast, chat line or experience for re-granted progress.
GF_REGISTER_SELFTEST("progression.quiet_grants", [](Context& ctx) {
    AnnounceCapture capture; // the chat lines stay out of real players' chat
    auto& p = ctx.player();
    resetProgress(p);
    pr::trigger(p, "gloamingforest:make_tf_portal"); // the root
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    pc::setQuietGrants(true);
    pr::grant(p, "gloamingforest:hedge");
    pc::setQuietGrants(false);
    auto quiet = gf::net::takeTestMessages(p);
    ctx.check(pr::has(p, "gloamingforest:hedge"), "a quiet grant still completes the advancement");
    ctx.check(std::none_of(quiet.begin(), quiet.end(), [](auto const& m) { return m.channel == "progression.toast"; }), "a quiet grant sends no toast");
    resetProgress(p);
    pr::trigger(p, "gloamingforest:make_tf_portal");
    (void)gf::net::takeTestMessages(p);
    pr::grant(p, "gloamingforest:hedge");
    auto loud = gf::net::takeTestMessages(p);
    ctx.check(std::any_of(loud.begin(), loud.end(), [](auto const& m) { return m.channel == "progression.toast"; }), "the same grant shows its toast otherwise");
    gf::net::setTestCompanion(p, false);
    resetProgress(p);
});

GF_REGISTER_SELFTEST("progression.trigger_criteria", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto  id = ctx.player().getOrCreateUniqueID().rawID;
    auto& p  = ctx.player();
    resetProgress(p);
    // the root: gloamingforest:make_tf_portal (F4)
    pr::trigger(p, "gloamingforest:make_tf_portal");
    ctx.check(pr::has(p, "gloamingforest:root"), "make_tf_portal earns the root");
    // upstream's triggers fired by their owners by string
    pr::trigger(p, "gloamingforest:kill_bug", {{"bug", "gloamingforest:firefly"}});
    ctx.check(!pr::has(p, "gloamingforest:kill_cicada"), "a firefly is not a cicada");
    pr::trigger(p, "gloamingforest:kill_bug", {{"block", "gloamingforest:cicada"}});
    ctx.check(pr::has(p, "gloamingforest:kill_cicada"), "kill_bug with a cicada earns kill_cicada");
    pr::trigger(p, "gloamingforest:drink_from_flask", {{"doses", 3}, {"seconds", 9}, {"potion", "minecraft:strong_harming"}});
    ctx.check(!pr::has(p, "gloamingforest:full_mettle_alchemist"), "three doses in 9 s are too slow");
    pr::trigger(p, "gloamingforest:drink_from_flask", {{"doses", 3}, {"seconds", 5}, {"potion", "minecraft:strong_harming"}});
    ctx.check(pr::has(p, "gloamingforest:full_mettle_alchemist"), "three strong harming doses in 5 s earn full_mettle_alchemist");
    pr::trigger(p, "gloamingforest:uncraft_item", {{"item", "gloamingforest:uncrafting_table"}});
    ctx.check(pr::has(p, "gloamingforest:uncraft_uncrafting_table"), "uncrafting the table earns uncraft_uncrafting_table");
    for (auto [trigger, adv] : std::initializer_list<std::pair<char const*, char const*>>{
             {"gloamingforest:broke_glass_sword", "gloamingforest:break_glass_sword"},
             {"gloamingforest:consume_hydra_chop_on_low_hunger", "gloamingforest:hydra_chop"},
             {"gloamingforest:add_modifier", "gloamingforest:modify_travellers_gear"},
             {"gloamingforest:complete_quest_ram", "gloamingforest:quest_ram"},
             {"gloamingforest:activate_ghast_trap", "gloamingforest:ghast_trap"},
         }) {
        pr::trigger(p, trigger);
        ctx.check(pr::has(p, adv), std::string(trigger) + " earns " + adv);
    }
    pr::trigger(p, "gloamingforest:placed_on_trophy_pedestal");
    ctx.check(pr::done(p, "gloamingforest:progress_trophy_pedestal", "trophy_pedestal") && !pr::has(p, "gloamingforest:progress_trophy_pedestal"),
              "placed_on_trophy_pedestal meets one of progress_trophy_pedestal's two groups");
    pr::trigger(p, "gloamingforest:kill_all_phantoms");
    ctx.check(pr::done(p, "gloamingforest:progress_knights", "all_knights"), "kill_all_phantoms meets progress_knights' all_knights");
    // the consume_item trigger F10 fires after Player#completeUsingItem
    pr::trigger(p, "minecraft:consume_item", {{"item", "gloamingforest:experiment_115"}});
    ctx.check(pr::has(p, "gloamingforest:experiment_115"), "eating experiment 115 earns experiment_115");
    ctx.check(pr::done(p, "gloamingforest:gloaming_dinner", "experiment_115") && !pr::has(p, "gloamingforest:gloaming_dinner"),
              "and one of gloaming_dinner's 11 dishes");
    // recipe_crafted: every BP layout of a Java recipe counts
    pr::trigger(p, "minecraft:recipe_crafted", {{"recipe_id", "gloamingforest:equipment/travellers_belt_3"}});
    ctx.check(pr::has(p, "gloamingforest:craft_travellers_gear"), "crafting a travellers' belt layout earns craft_travellers_gear");
    // item_used_on_block: the block at the position after the use, the stack used
    auto pos = ctx.testPos(1400, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "the test position is loaded");
    ctx.require(ctx.setBlock(pos, "gloamingforest:uberous_soil"), "uberous soil placed");
    {
        auto& q = *playerById(id);
        pr::trigger(q, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "minecraft:stick"}});
        ctx.check(!pr::done(q, "gloamingforest:beanstalk", "use_beans"), "a stick on uberous soil is not the beans");
        pr::trigger(q, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "gloamingforest:magic_beans"}});
        ctx.check(pr::done(q, "gloamingforest:beanstalk", "use_beans"), "magic beans on uberous soil meet beanstalk's use_beans");
        // the boss fight criterion (F8's hurt_boss, also the group advancement path)
        pr::trigger(q, "gloamingforest:hurt_boss", {{"entity", "gloamingforest:hydra"}});
        ctx.check(pr::done(q, "gloamingforest:progress_hydra", "was_in_fight"), "hurt_boss with the hydra meets progress_hydra's was_in_fight");
        resetProgress(q);
    }
});

// Grants follow the tick criteria at once: progress_naga makes progress_lich's "kill_naga" true.
GF_REGISTER_SELFTEST("progression.playthrough", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    {
        auto& p = ctx.player();
        ctx.check(pc::restrictionFor(p, "gloamingforest:dark_forest") != nullptr, "the dark forest starts locked");
        ctx.check(pc::restrictionFor(p, "gloamingforest:fire_swamp") != nullptr, "the fire swamp starts locked");
    }
    // the naga's scale: progress_naga (any of naga, trophy, scale, was_in_fight)
    ctx.give("gloamingforest:naga_scale", 1);
    co_await ctx.ticks(3);
    {
        auto& p = *playerById(id);
        ctx.check(pr::has(p, "gloamingforest:progress_naga"), "a naga scale earns progress_naga");
        ctx.check(pr::done(p, "gloamingforest:progress_lich", "kill_naga"), "progress_lich's previous-progression criterion follows at once");
        ctx.check(!pr::has(p, "gloamingforest:progress_lich"), "progress_lich still needs the lich");
        // the lich fight (F8's hurt_boss for every participant)
        pr::trigger(p, "gloamingforest:hurt_boss", {{"entity", "gloamingforest:lich"}});
        ctx.check(pr::has(p, "gloamingforest:progress_lich"), "the lich fight earns progress_lich");
        for (char const* b : {"dark_forest", "snowy_forest", "swamp"})
            ctx.check(pc::restrictionFor(p, std::string("gloamingforest:") + b) == nullptr, std::string(b) + " is unlocked after progress_lich");
        ctx.check(pc::restrictionFor(p, "gloamingforest:fire_swamp") != nullptr, "the fire swamp still needs progress_labyrinth");
        ctx.check(pr::done(p, "gloamingforest:progress_labyrinth", "kill_lich") && pr::done(p, "gloamingforest:progress_yeti", "previous_progression")
                      && pr::done(p, "gloamingforest:progress_merge", "hydra") == false,
                  "progress_lich's children see it at once");
        pr::trigger(p, "minecraft:consume_item", {{"item", "gloamingforest:meef_stroganoff"}});
        ctx.check(pr::has(p, "gloamingforest:progress_labyrinth"), "meef stroganoff after the lich earns progress_labyrinth");
        ctx.check(pc::restrictionFor(p, "gloamingforest:fire_swamp") == nullptr, "the fire swamp is unlocked");
        ctx.check(pc::restrictionFor(p, "gloamingforest:dark_forest_center") != nullptr, "the dark forest's centre still needs progress_knights");
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
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    int n = co_await stayIn(ctx, id, "gloamingforest:dark_forest", cp::Enforcement::Darkness, 125);
    ctx.check(n == 2 || n == 3, "darkness every 60 ticks: " + std::to_string(n) + " times in 125 ticks");
    ctx.check(gf::effects::has(*playerById(id), "minecraft:darkness"), "the player has Darkness");
    ctx.check(gf::effects::amplifier(*playerById(id), "minecraft:darkness") == 0, "Darkness I (multiplier 0)");
    pr::grant(*playerById(id), "gloamingforest:progress_lich");
    n = co_await stayIn(ctx, id, "gloamingforest:dark_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "no darkness once progress_lich is earned (" + std::to_string(n) + ")");
    gf::effects::remove(*playerById(id), "minecraft:darkness");
    resetProgress(*playerById(id));
});

GF_REGISTER_SELFTEST("progression.enforce_acid_rain", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    // the inner 5-tick gate inside the outer 20-tick one: every 20 ticks, not 5
    int n = co_await stayIn(ctx, id, "gloamingforest:highlands", cp::Enforcement::AcidRain, 61);
    ctx.check(n == 3 || n == 4, "acid rain every 20 ticks: " + std::to_string(n) + " times in 61 ticks");
});

GF_REGISTER_SELFTEST("progression.enforce_fire_hunger_frost", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    int n = co_await stayIn(ctx, id, "gloamingforest:fire_swamp", cp::Enforcement::Fire, 62);
    ctx.check(n == 1 || n == 2, "fire every 60 ticks (" + std::to_string(n) + ")");
    ctx.check(playerById(id)->isOnFire(), "the fire swamp sets the player on fire");
    playerById(id)->stopFire();
    n = co_await stayIn(ctx, id, "gloamingforest:swamp", cp::Enforcement::Hunger, 125);
    ctx.check(n == 2 || n == 3, "hunger every 60 ticks (" + std::to_string(n) + ")");
    ctx.check(gf::effects::amplifier(*playerById(id), "minecraft:hunger") >= 2, "Hunger stacks: the amplifier grows by the multiplier on each application");
    gf::effects::remove(*playerById(id), "minecraft:hunger");
    n = co_await stayIn(ctx, id, "gloamingforest:glacier", cp::Enforcement::Frost, 62);
    ctx.check(n == 1 || n == 2, "frost every 60 ticks (" + std::to_string(n) + ")");
    gf::effects::remove(*playerById(id), "gloamingforest:frosted");
});

GF_REGISTER_SELFTEST("progression.enforce_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    bool was = gf::gamerules::getBool("tfEnforcedProgression");
    gf::gamerules::set("tfEnforcedProgression", false);
    int n = co_await stayIn(ctx, id, "gloamingforest:dark_forest", cp::Enforcement::Darkness, 65);
    gf::gamerules::set("tfEnforcedProgression", was);
    ctx.check(n == 0, "tfEnforcedProgression false: no enforcement (" + std::to_string(n) + ")");
    playerById(id)->setPlayerGameType(GameType::Creative);
    n = co_await stayIn(ctx, id, "gloamingforest:dark_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "creative players are not enforced (" + std::to_string(n) + ")");
    playerById(id)->setPlayerGameType(GameType::Survival);
    n = co_await stayIn(ctx, id, "gloamingforest:enchanted_forest", cp::Enforcement::Darkness, 65);
    ctx.check(n == 0, "an unrestricted biome is not enforced (" + std::to_string(n) + ")");
});

// ---- presentation ------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("progression.toasts_and_screen", [](Context& ctx) {
    AnnounceCapture capture; // the chat lines stay out of real players' chat
    auto& p = ctx.player();
    resetProgress(p);
    pr::trigger(p, "gloamingforest:make_tf_portal"); // the root
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    pr::grant(p, "gloamingforest:hedge");
    auto msgs  = gf::net::takeTestMessages(p);
    auto toast = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "progression.toast"; });
    ctx.require(toast != msgs.end(), "a companion gets progression.toast when it earns an advancement");
    ctx.check(toast->payload.value("advancement", "") == "gloamingforest:hedge" && toast->payload.value("frame", "") == "task"
                  && toast->payload.value("title", "") == "advancement.gloamingforest.hedge" && toast->payload.value("icon", "") == "gloamingforest:hedge",
              "the toast names the advancement, its frame, title and icon");
    // the screen: native data for companions
    ctx.check(pc::openAdvancements(p), "the advancement screen opens");
    msgs      = gf::net::takeTestMessages(p);
    auto open = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "screen.open"; });
    ctx.require(open != msgs.end(), "the companion gets screen.open");
    auto data = open->payload.value("data", Json::object());
    ctx.check(open->payload.value("type", "") == "progression.advancements", "of type progression.advancements");
    ctx.check(data["tab"].value("background", "") == "textures/gloamingforest/block/mazestone_large_brick", "upstream's tab background");
    std::set<std::string> shown;
    for (auto const& n : data["nodes"]) shown.insert(n.value("id", ""));
    ctx.check(shown.contains("gloamingforest:hedge") && shown.contains("gloamingforest:root"), "the earned hedge and its parent are shown");
    ctx.check(shown.contains("gloamingforest:progress_naga") && shown.contains("gloamingforest:progress_lich"),
              "unearned nodes up to two levels below an earned one are shown");
    ctx.check(!shown.contains("gloamingforest:kill_cicada"), "a hidden advancement is not shown before it is earned");
    ctx.check(!shown.contains("gloamingforest:progress_labyrinth"), "an advancement three levels below anything earned is not shown");
    gf::net::setTestCompanion(p, false);
    // vanilla players: the form (and the game's toast)
    ctx.check(pc::openAdvancements(p), "a vanilla player gets the form");
    pr::grant(p, "gloamingforest:hill1");
    ctx.check(pr::has(p, "gloamingforest:hill1"), "a vanilla player earns an advancement with the game's toast");
    resetProgress(p);
});

// The vanilla fallback of LockedBiomeToastHandler: the server's own countdown per player.
GF_REGISTER_SELFTEST("progression.vanilla_locked_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    ctx.player().setPlayerGameType(GameType::Survival);
    resetProgress(ctx.player());
    pc::resetEnforcementCounts(ctx.player());
    int before = pc::vanillaLockedToasts(ctx.player());
    pc::setBiomeOverride(ctx.player(), "gloamingforest:swamp");
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
        ctx.check(!title.empty() && title.find("advancement.gloamingforest") == std::string::npos, a.id + "'s title is translated (" + title + ")");
        ctx.check(desc.find("%s") == std::string::npos && desc.find("%1$s") == std::string::npos && desc.find("{}") == std::string::npos,
                  a.id + "'s description has its arguments (" + desc + ")");
    }
    ctx.check(withArgs >= 20, std::to_string(withArgs) + " descriptions take arguments");
    auto hill1 = pc::textFor(p, pc::find("gloamingforest:hill1")->description);
    ctx.check(hill1.find("Redcap") != std::string::npos, "hill1 names the Redcap: " + hill1);
    auto merge = pc::textFor(p, pc::find("gloamingforest:progress_merge")->description);
    ctx.check(merge.find("Hydra") != std::string::npos && merge.find("Ur-Ghast") != std::string::npos && merge.find("Snow Queen") != std::string::npos,
              "progress_merge names its three bosses: " + merge);
});

// TreeNodePosition over the whole tree (Java's server layout), sent unchanged to the screen.
GF_REGISTER_SELFTEST("progression.tree_layout", [](Context& ctx) {
    QuietScope quiet{ctx.player()};
    std::set<std::pair<float, float>> cells;
    for (auto const& a : pc::advancements()) {
        ctx.check(a.placed, a.id + " is laid out");
        ctx.check(cells.insert({a.x, a.y}).second, a.id + " has a cell of its own");
    }
    ctx.check(pc::find("gloamingforest:root")->x == 0 && pc::find("gloamingforest:progression_end")->x == 7, "the root in column 0, progression_end in column 7");
    // hill2 sits right of hill1 on its row; experiment_115's two children around it
    auto const* h1 = pc::find("gloamingforest:hill1");
    auto const* h2 = pc::find("gloamingforest:hill2");
    ctx.check(h2->x == h1->x + 1 && h2->y == h1->y, "a single child stays on its parent's row");
    auto const* e  = pc::find("gloamingforest:experiment_115");
    auto const* e1 = pc::find("gloamingforest:experiment_115_115");
    auto const* e2 = pc::find("gloamingforest:experiment_115_self_replenishing");
    ctx.check(e->y == (e1->y + e2->y) / 2, "a parent sits halfway between its first and last child");
    // the screen sends the server's positions
    auto& p = ctx.player();
    resetProgress(p);
    pr::trigger(p, "gloamingforest:make_tf_portal");
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
        auto const* a = pc::find(std::string("gloamingforest:") + adv);
        ctx.require(a != nullptr, std::string(adv) + " is loaded");
        auto item = pc::iconItem(*a);
        ctx.check(item.is_object() && item.value("id", "") == a->icon, std::string(adv) + "'s icon item is " + a->icon);
        auto stack = gf::game::items::fromJson(item);
        auto got   = gf::game::items::getData(stack, component);
        ctx.check(got == want, std::string(adv) + "'s icon carries " + component + " = " + want.dump() + " (" + got.dump() + ")");
    };
    check("experiment_115_115", "gloamingforest:e115_variant", "think");
    check("experiment_115_self_replenishing", "gloamingforest:e115_variant", "full");
    // upstream's flask component as U9's flask data (Bedrock's strong harming potion: aux 24)
    check("full_mettle_alchemist", "flask_contents", Json{{"potion", 24}, {"doses", 4}, {"breakage", 0}, {"breakable", false}});
    check("modify_travellers_gear", "gloamingforest:water_walk", Json::object());
});

// chicken_jerky: a baby zombie riding a chicken, killed by a blow whose direct entity holds the
// jerky. The vehicle is read before die() dismounts the rider; a kill whose direct entity is not
// the player (a projectile) does not count.
GF_REGISTER_SELFTEST("progression.chicken_jockey", [](Context& ctx) -> ll::coro::CoroTask<> {
    QuietScope quiet{ctx.player()};
    auto id = ctx.player().getOrCreateUniqueID().rawID;
    resetProgress(ctx.player());
    auto base = ctx.testPos(1404, 3);
    ctx.require(co_await ctx.ensureLoaded(base), "the test position is loaded");
    // hostile mobs cannot be summoned in peaceful: easy while the test runs
    struct EasyScope {
        SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
        EasyScope() {
            if (auto level = ll::service::getLevel()) {
                previous = level->getDifficulty();
                if (previous == SharedTypes::Legacy::Difficulty::Peaceful) level->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
            }
        }
        ~EasyScope() {
            if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
        }
    } easy;
    // a baby zombie (spawn event minecraft:as_baby) riding a chicken, both summoned by name
    auto jockey = [&](std::string const& name) -> bool {
        auto at = std::to_string(base.x) + " " + std::to_string(base.y + 1) + " " + std::to_string(base.z);
        auto c1 = gf::selftest::runServerCommand("summon minecraft:chicken " + at + " 0 0 minecraft:spawn_adult " + name + "_mount", base.dim);
        auto c2 = gf::selftest::runServerCommand("summon minecraft:zombie " + at + " 0 0 minecraft:as_baby " + name, base.dim);
        auto c3 = gf::selftest::runServerCommand("ride @e[name=" + name + "] start_riding @e[name=" + name + "_mount] teleport_rider", base.dim);
        if (!c1 || !c2 || !c3) ctx.info("jockey commands: " + c1.messages + " / " + c2.messages + " / " + c3.messages);
        return c1 && c2 && c3;
    };    auto jerky = gf::game::items::make("gloamingforest:chicken_jerky", 1);
    ctx.require(jerky.has_value(), "chicken jerky exists");
    playerById(id)->setSelectedItem(*jerky);
    // a blow whose direct entity is another entity (a projectile's place): not the jerky's holder
    {
        ctx.require(jockey("gf_jockey_a"), "a jockey spawns");
        co_await ctx.ticks(2);
        Actor* rider = nullptr;
        auto   level = ll::service::getLevel();
        for (auto* a : level->getRuntimeActorList())
            if (a && a->getNameTag() == "gf_jockey_a") rider = a;
        ctx.require(rider && rider->isBaby() && rider->getVehicle(), "the baby zombie rides the chicken");
        auto* vehicle = rider->getVehicle();
        gf::combat::hurt(*rider, 1000.0f, "minecraft:arrow", playerById(id), vehicle);
        co_await ctx.ticks(2);
        ctx.check(!pr::has(*playerById(id), "gloamingforest:chicken_jerky"), "a kill whose direct entity is not the player does not count");
    }
    {
        ctx.require(jockey("gf_jockey_b"), "a second jockey spawns");
        co_await ctx.ticks(2);
        Actor* rider = nullptr;
        for (auto* a : ll::service::getLevel()->getRuntimeActorList())
            if (a && a->getNameTag() == "gf_jockey_b") rider = a;
        ctx.require(rider && rider->isBaby() && rider->getVehicle(), "the second baby zombie rides its chicken");
        gf::combat::hurt(*rider, 1000.0f, "minecraft:player_attack", playerById(id), playerById(id));
        co_await ctx.ticks(2);
        ctx.check(pr::has(*playerById(id), "gloamingforest:chicken_jerky"), "killing the jockey's rider with jerky in hand earns chicken_jerky");
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

/// Puts the saved progress back quietly: the advancements earned again show no toast, chat line
/// or message and award no experience (setQuietGrants).
bool restoreSaved(Player& p) {
    auto it = savedProgress().find(p.getOrCreateUniqueID().rawID);
    if (it == savedProgress().end()) return false;
    pc::setQuietGrants(true);
    try {
        resetProgress(p);
        for (auto const& a : pc::advancements()) {
            if (it->second.done.contains(a.id)) pr::grant(p, a.id);
            else if (auto c = it->second.criteria.find(a.id); c != it->second.criteria.end())
                for (auto const& name : c->second) pr::grant(p, a.id, name);
        }
    } catch (...) {
        pc::setQuietGrants(false);
        throw;
    }
    pc::setQuietGrants(false);
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
        for (char const* a : {"root", "progress_naga", "hedge", "hill1", "gloaming_hunter", "progress_lich", "naga_armors"})
            pr::grant(p, std::string("gloamingforest:") + a);
        ++n;
    });
    ctx.info("granted the demo advancements to " + std::to_string(n) + " opted-in player(s)");
});

GF_REGISTER_SELFTEST("progression.demo_toast", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        saveOnce(p);
        pr::revoke(p, "gloamingforest:progress_labyrinth");
        pr::grant(p, "gloamingforest:progress_labyrinth");
        ++n;
    });
    ctx.info("progress_labyrinth earned by " + std::to_string(n) + " opted-in player(s)");
});

// A challenge with a title wider than 125 pixels and a variant icon: experiment_115_115 ("Eating
// 115 Everyday, 115 Years, Forever", the "think" Experiment 115).
GF_REGISTER_SELFTEST("progression.demo_challenge_toast", [](Context& ctx) {
    int n = 0;
    forOptedIn([&](Player& p) {
        saveOnce(p);
        pr::revoke(p, "gloamingforest:experiment_115_115");
        pr::grant(p, "gloamingforest:experiment_115_115");
        ++n;
    });
    ctx.info("experiment_115_115 earned by " + std::to_string(n) + " opted-in player(s)");
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
        pr::revoke(p, "gloamingforest:progress_lich");
        pc::resetEnforcementCounts(p);
        auto type = p.getPlayerGameType();
        p.setPlayerGameType(GameType::Survival);
        pc::setBiomeOverride(p, "gloamingforest:swamp", 400, type);
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
    bool hadHedge = pr::has(*playerById(who), "gloamingforest:hedge");
    pr::revoke(*playerById(who), "gloamingforest:hedge");
    pr::grant(*playerById(who), "gloamingforest:hedge");
    std::optional<pcl::ShownToast> shown;
    for (int i = 0; i < 60; ++i) {
        co_await ctx.ticks(1);
        shown = pcl::lastToast();
        if (shown && shown->what == "gloamingforest:hedge") break;
    }
    ctx.require(shown && shown->what == "gloamingforest:hedge", "the client mod showed the hedge's toast");
    ctx.check(shown->body == "advancement.gloamingforest.hedge" && shown->icon == "gloamingforest:hedge" && shown->ms == 5000,
              "the advancement's title and icon, 5 s");
    auto info = pcl::toastOf("gloamingforest:hedge");
    ctx.check(info && info->kind == "advancement" && info->titleColor == 0xFFFFFF00u, "task: AdvancementToast's -256 (yellow) toast name");
    // a challenge: -30465 (pink); the icon keeps its components
    pcl::showAdvancementToast({{"advancement", "gloamingforest:full_mettle_alchemist"},
                               {"frame", "challenge"},
                               {"title", "advancement.gloamingforest.full_mettle_alchemist"},
                               {"icon", "gloamingforest:brittle_potion_flask"}});
    auto challenge = pcl::toastOf("gloamingforest:full_mettle_alchemist");
    ctx.check(challenge && challenge->titleColor == 0xFFFF88FFu && challenge->title == gf::client::ctr("advancements.toast.challenge", "Challenge Complete!"),
              "challenge: AdvancementToast's -30465 (pink) \"Challenge Complete!\"");
    pcl::cancelToasts("gloamingforest:full_mettle_alchemist");
    if (!hadHedge)
        if (auto* p = playerById(who)) pr::revoke(*p, "gloamingforest:hedge");
});

// LockedBiomeToastHandler: one toast after the 60-step countdown (300 ticks) in a locked biome.
GF_REGISTER_SELFTEST("progression.client_locked_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto who = localCompanion();
    if (who == 0) {
        ctx.info("no opted-in companion player in this world: skipped");
        co_return;
    }
    auto  type = playerById(who)->getPlayerGameType();
    bool  had  = pr::has(*playerById(who), "gloamingforest:progress_lich");
    if (had) pr::revoke(*playerById(who), "gloamingforest:progress_lich");
    playerById(who)->setPlayerGameType(GameType::Survival);
    pcl::setClientBiomeOverride("gloamingforest:dark_forest");
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
    ctx.check(shown && shown->icon == "gloamingforest:lich_tower_miniature_structure" && shown->title == "misc.gloamingforest.biome_locked",
              "the dark forest's icon and upstream's text");
    pcl::setClientBiomeOverride("minecraft:plains");
    co_await ctx.ticks(10);
    auto [time, shownFlag] = pcl::countdown();
    ctx.check(time == 60 && !shownFlag, "leaving resets the countdown after a shown toast");
    pcl::setClientBiomeOverride("");
    if (auto* p = playerById(who)) {
        p->setPlayerGameType(type);
        if (had) {
            pc::setQuietGrants(true); // put back without a toast or chat line
            pr::grant(*p, "gloamingforest:progress_lich");
            pc::setQuietGrants(false);
        }
    }
});

#endif

} // namespace
