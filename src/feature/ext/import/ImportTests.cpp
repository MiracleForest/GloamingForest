// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of the imported content skeleton (unit F1 "import", prefix "import.", testPos dx
// 5100..5199). They read data/gf/import/skeleton.json (written by tools/import/run.ps1) and check
// that the game loaded what the import generated: every TF block with its frozen states
// (docs/spec/blockstates.md), every item, block item and spawn egg, every entity definition, the
// imported lang and data, TF's own sound type in the assembled resource pack, and that every block can be placed.
//
// F1 owns no source directory, so these tests live in src/feature/ext/import/ (new files only,
// rule R18); A8 may fold them into a unit directory.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ll/api/service/Bedrock.h"
#include "mc/common/Brightness.h"
#include "mc/world/actor/ActorDefinitionGroup.h"
#include "mc/world/actor/ActorDefinitionPtr.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "nlohmann/json.hpp"

#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JDoc = nlohmann::json;

std::filesystem::path modDir() { return gf::Mod::getInstance().getSelf().getModDir(); }

/// data/gf/import/skeleton.json: {"blocks": {id: {state: [values]}}, "items": [...], "entities": [...],
/// "spawnEggs": [...], "data": {"twilightforest/<registry>": file count}}.
JDoc const& skeleton() {
    static JDoc doc = [] {
        std::ifstream in(modDir() / "data" / "gf" / "import" / "skeleton.json");
        if (!in) return JDoc();
        return JDoc::parse(in, nullptr, false);
    }();
    return doc;
}

void requireSkeleton(Context& ctx) {
    auto const& s = skeleton();
    ctx.require(s.is_object() && s.contains("blocks") && s.contains("items"), "data/gf/import/skeleton.json is assembled and readable");
}

/// Reports at most `limit` individual failures, then one summary line.
struct Failures {
    Context&    ctx;
    std::string what;
    int         count = 0;
    void        add(std::string const& message) {
        if (++count <= 8) ctx.check(false, message);
    }
    void done(int total) {
        if (count > 8) ctx.check(false, std::to_string(count) + " of " + std::to_string(total) + " " + what + " failed");
        else if (count == 0) ctx.info(std::to_string(total) + " " + what + " OK");
    }
};

std::string stateText(JDoc const& v) {
    if (v.is_string()) return v.get<std::string>();
    return v.dump();
}

/// The assembled behaviour-pack file of a block (what the game loaded), or null.
JDoc bpBlock(std::string const& id) {
    auto path = modDir() / "behavior_packs" / "GloamingForest_BP" / "blocks" / (id.substr(id.find(':') + 1) + ".json");
    std::ifstream in(path);
    if (!in) return JDoc();
    auto doc = JDoc::parse(in, nullptr, false);
    return doc.is_object() && doc.contains("minecraft:block") ? doc["minecraft:block"] : JDoc();
}

/// One permutation condition as the import writes visual ones: terms joined by "&&", each
/// q.block_state('s') == v, q.block_state('s') or !q.block_state('s'). nullopt: not of that form.
std::optional<bool> holds(std::string const& cond, std::map<std::string, std::string> const& state) {
    if (cond.find("||") != std::string::npos) return std::nullopt;
    static std::regex const term(R"(^\s*(!?)q\.block_state\('([^']+)'\)(?:\s*==\s*'?([^']*)'?)?\s*$)");
    size_t start = 0;
    while (start <= cond.size()) {
        size_t end = cond.find("&&", start);
        auto   t   = cond.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::smatch m;
        if (!std::regex_match(t, m, term)) return std::nullopt;
        auto it = state.find(m[2].str());
        if (it == state.end()) return false;
        bool ok = m[3].matched && m[3].length() > 0 ? it->second == m[3].str() : (it->second == "true") != (m[1].str() == "!");
        if (!ok) return false;
        if (end == std::string::npos) break;
        start = end + 2;
    }
    return true;
}

/// The rotation Bedrock applies to a block state: the root's minecraft:transformation, replaced by every
/// permutation whose condition holds that names one (a permutation without one keeps the earlier value).
std::string rotationOf(JDoc const& block, std::map<std::string, std::string> const& state) {
    // (value() / find() only: a missing key on a const json must not be indexed)
    auto text = [](JDoc const& comps) -> std::optional<std::string> {
        if (!comps.is_object()) return std::nullopt;
        auto t = comps.find("minecraft:transformation");
        if (t == comps.end() || !t->is_object()) return std::nullopt;
        auto r = t->value("rotation", JDoc::array({0, 0, 0}));
        if (!r.is_array() || r.size() != 3) return "?";
        return std::to_string(r[0].get<int>()) + "," + std::to_string(r[1].get<int>()) + "," + std::to_string(r[2].get<int>());
    };
    std::string rot = text(block.value("components", JDoc::object())).value_or("0,0,0");
    for (auto const& p : block.value("permutations", JDoc::array())) {
        auto h = holds(p.value("condition", ""), state);
        if (h && *h)
            if (auto r = text(p.value("components", JDoc::object()))) rot = *r;
    }
    return rot;
}

} // namespace

GF_REGISTER_SELFTEST("import.blocks", [](Context& ctx) {
    requireSkeleton(ctx);
    auto const& blocks = skeleton()["blocks"];
    Failures    fails{ctx, "blocks (registered with their frozen states and defaults)"};
    for (auto const& [id, states] : blocks.items()) {
        auto const* block = gf::game::blockWith(id);
        if (!block) {
            fails.add(id + " is not a registered block");
            continue;
        }
        auto got = gf::game::statesOf(*block);
        for (auto const& [name, values] : states.items()) {
            auto it = got.find(name);
            if (it == got.end()) {
                fails.add(id + " has no state " + name);
                break;
            }
            if (!values.empty() && it->second != stateText(values.front())) {
                fails.add(id + " default " + name + "=" + it->second + ", expected " + stateText(values.front()));
                break;
            }
        }
        int custom = 0;
        for (auto const& [name, _] : got)
            if (name.rfind("twilightforest:", 0) == 0) ++custom;
        if (custom != static_cast<int>(states.size())) fails.add(id + " has " + std::to_string(custom) + " states, expected " + std::to_string(states.size()));
    }
    ctx.check(blocks.size() == 523, "523 TF blocks in the skeleton (got " + std::to_string(blocks.size()) + ")");
    fails.done(static_cast<int>(blocks.size()));
});

GF_REGISTER_SELFTEST("import.items", [](Context& ctx) {
    requireSkeleton(ctx);
    auto const& s = skeleton();
    std::vector<std::string> ids;
    for (auto const& id : s["items"]) ids.push_back(id.get<std::string>());
    for (auto const& id : s["spawnEggs"]) ids.push_back(id.get<std::string>());
    for (auto const& [id, _] : s["blocks"].items()) ids.push_back(id); // every block has an item form
    Failures fails{ctx, "items (items, block items and spawn eggs)"};
    for (auto const& id : ids) {
        auto stack = gf::game::items::make(id);
        if (!stack || gf::game::items::idOf(*stack) != id) fails.add(id + " is not a registered item");
    }
    fails.done(static_cast<int>(ids.size()));
});

GF_REGISTER_SELFTEST("import.entities", [](Context& ctx) {
    requireSkeleton(ctx);
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level is loaded");
    auto* group = level->getEntityDefinitions();
    ctx.require(group != nullptr, "the level has entity definitions");
    auto const& entities = skeleton()["entities"];
    Failures    fails{ctx, "entity definitions"};
    for (auto const& e : entities) {
        auto id  = e.get<std::string>();
        auto def = group->tryGetDefinition(id);
        if (static_cast<ActorDefinition*>(def.mPtr) == nullptr) fails.add(id + " has no entity definition");
    }
    ctx.check(entities.size() == 80, "80 TF entities in the skeleton (got " + std::to_string(entities.size()) + ")");
    fails.done(static_cast<int>(entities.size()));
});

GF_REGISTER_SELFTEST("import.lang_and_data", [](Context& ctx) {
    requireSkeleton(ctx);
    // TF's en_us and a translation, through the mod's lang files (gf::tr)
    ctx.check(gf::translate("block.twilightforest.twilight_oak_planks", "?", "en_US") == "Twilight Oak Planks", "en_US TF block name");
    ctx.check(gf::translate("advancement.twilightforest.root", "?", "en_US") != "?", "en_US TF advancement title");
    ctx.check(gf::translate("item.twilightforest.naga_scale", "?", "de_DE") == "Nagaschuppe", "de_DE TF item name");
    // the imported data, file by file, as the import counted it (at least: W1 tag fragments may add tag files)
    namespace fs = std::filesystem;
    for (auto const& [dir, expected] : skeleton()["data"].items()) {
        auto root  = modDir() / "data" / fs::path(dir);
        int  count = 0;
        std::error_code ec;
        if (fs::exists(root, ec))
            for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (!it->is_directory()) ++count;
        ctx.check(count >= expected.get<int>(), "data/" + dir + ": " + std::to_string(count) + " files, expected at least " + std::to_string(expected.get<int>()));
    }
});

// The registered states of blocks whose properties come from TF's block classes only (createBlockStateDefinition,
// not the blockstate files), written down by hand from the Java sources: independent of the generator's output.
GF_REGISTER_SELFTEST("import.java_states", [](Context& ctx) {
    std::vector<std::pair<char const*, std::vector<char const*>>> const cases{
        {"twilightforest:naga_trophy", {"powered", "rotation"}},                       // TrophyBlock
        {"twilightforest:naga_wall_trophy", {"facing", "powered"}},                    // TrophyWallBlock
        {"twilightforest:zombie_skull_candle", {"candles", "lighting", "rotation"}},   // SkullCandleBlock < AbstractSkullCandleBlock
        {"twilightforest:keepsake_casket", {"damage", "facing", "multilogged"}},       // KeepsakeCasketBlock < SkullChestBlock
        {"twilightforest:candelabra", {"facing", "has_candle_1", "has_candle_2", "has_candle_3", "lighting", "on_wall"}},
        {"twilightforest:blue_force_field", {"down", "east", "north", "south", "up", "west"}},
        {"twilightforest:brazier", {"half", "light"}},
        {"twilightforest:fire_jet", {"state"}},
        {"twilightforest:locked_vanishing_block", {"active", "locked"}},               // < VanishingBlock
        {"twilightforest:firefly_particle_spawner", {"particle_radius"}},
        {"twilightforest:chiseled_canopy_bookshelf", {"facing", "slot_0_occupied", "slot_1_occupied", "slot_2_occupied", "slot_3_occupied", "slot_4_occupied", "slot_5_occupied", "spawner"}},
        {"twilightforest:twilight_oak_trapped_chest", {"facing", "type"}},             // TrappedChestBlock
        {"twilightforest:red_thread", {"down", "east", "north", "south", "up", "west"}}, // MultifaceBlock
        {"twilightforest:clover_patch", {"east", "north", "south", "west"}},           // PatchBlock
        {"twilightforest:ominous_candle", {"candles"}},
        {"twilightforest:firefly", {"facing"}},                                        // CritterBlock
    };
    for (auto const& [id, want] : cases) {
        auto const* block = gf::game::blockWith(id);
        if (!block) {
            ctx.check(false, std::string(id) + " is not a registered block");
            continue;
        }
        std::vector<std::string> got;
        for (auto const& [name, _] : gf::game::statesOf(*block))
            if (name.rfind("twilightforest:", 0) == 0) got.push_back(name.substr(15));
        std::vector<std::string> expected(want.begin(), want.end());
        auto join = [](std::vector<std::string> const& v) {
            std::string s;
            for (auto const& x : v) s += (s.empty() ? "" : ", ") + x;
            return s;
        };
        ctx.check(got == expected, std::string(id) + ": states [" + join(got) + "], TF declares [" + join(expected) + "]");
    }
});

// The rotation of every state of a rotated block, as Bedrock evaluates the assembled block file, against TF's
// blockstate files (Java x / y -> Bedrock [(360 - x) % 360, (360 - y) % 360, 0]): an unrotated state must not
// inherit the rotation of another one.
GF_REGISTER_SELFTEST("import.rotations", [](Context& ctx) {
    struct Case {
        char const*                        id;
        std::map<std::string, std::string> state;
        char const*                        rotation;
    };
    // twilightforest/blockstates/aurora_pillar.json and twilight_oak_stairs.json (TF 4.8)
    std::vector<Case> const cases{
        {"twilightforest:aurora_pillar", {{"twilightforest:axis", "y"}}, "0,0,0"},
        {"twilightforest:aurora_pillar", {{"twilightforest:axis", "x"}}, "270,270,0"},
        {"twilightforest:aurora_pillar", {{"twilightforest:axis", "z"}}, "270,0,0"},
        {"twilightforest:twilight_oak_stairs", {{"twilightforest:facing", "east"}, {"twilightforest:half", "bottom"}, {"twilightforest:shape", "straight"}}, "0,0,0"},
        {"twilightforest:twilight_oak_stairs", {{"twilightforest:facing", "south"}, {"twilightforest:half", "bottom"}, {"twilightforest:shape", "straight"}}, "0,270,0"},
        {"twilightforest:twilight_oak_stairs", {{"twilightforest:facing", "east"}, {"twilightforest:half", "bottom"}, {"twilightforest:shape", "inner_right"}}, "0,0,0"},
        {"twilightforest:twilight_oak_stairs", {{"twilightforest:facing", "north"}, {"twilightforest:half", "top"}, {"twilightforest:shape", "straight"}}, "180,90,0"},
    };
    for (auto const& c : cases) {
        auto block = bpBlock(c.id);
        ctx.require(block.is_object(), std::string(c.id) + ": assembled block file readable");
        // the game must accept the state too
        JDoc states = JDoc::object();
        for (auto const& [k, v] : c.state) states[k] = v;
        ctx.check(gf::game::blockWithStates(c.id, states) != nullptr, std::string(c.id) + " " + states.dump() + " is a registered state");
        auto got = rotationOf(block, c.state);
        ctx.check(got == c.rotation, std::string(c.id) + " " + states.dump() + ": rotation [" + got + "], TF gives [" + c.rotation + "]");
    }
});

// State-dependent light (TF's lightLevel lambdas and getLightEmission overrides, one Bedrock permutation per
// light level), read back from the game's block types.
GF_REGISTER_SELFTEST("import.light", [](Context& ctx) {
    struct Case {
        char const* id;
        JDoc        states;
        int         light;
    };
    std::vector<Case> const cases{
        {"twilightforest:fire_jet", {{"twilightforest:state", "idle"}}, 0},                     // FLAME ? 15 : 0
        {"twilightforest:fire_jet", {{"twilightforest:state", "flame"}}, 15},
        {"twilightforest:torchberry_plant", {{"twilightforest:has_torchberries", false}}, 1},   // berries ? 7 : 1
        {"twilightforest:torchberry_plant", {{"twilightforest:has_torchberries", true}}, 7},
        {"twilightforest:brazier", {{"twilightforest:half", "upper"}, {"twilightforest:light", "full"}}, 12}, // BrazierLight.FULL
        {"twilightforest:brazier", {{"twilightforest:half", "lower"}, {"twilightforest:light", "full"}}, 0},
        {"twilightforest:ominous_candle", {{"twilightforest:candles", 3}}, 6},                  // 2 * candles
        {"twilightforest:zombie_skull_candle", {{"twilightforest:lighting", "normal"}, {"twilightforest:candles", 4}}, 12}, // 3 * candles
        {"twilightforest:zombie_skull_candle", {{"twilightforest:lighting", "none"}, {"twilightforest:candles", 4}}, 0},
        {"twilightforest:keepsake_casket", {{"twilightforest:multilogged", "lava"}}, 15},
        {"twilightforest:keepsake_casket", {{"twilightforest:multilogged", "air"}}, 0},
        {"twilightforest:time_log_core", {{"twilightforest:active", true}}, 15},
        {"twilightforest:firefly", JDoc::object(), 15},                                         // constant
    };
    for (auto const& c : cases) {
        auto const* block = gf::game::blockWithStates(c.id, c.states);
        if (!block) {
            ctx.check(false, std::string(c.id) + " " + c.states.dump() + " is not a registered state");
            continue;
        }
        // the light emission of this permutation (its minecraft:light_emission component, per block state)
        int light = static_cast<int>(block->mDirectData->mLightEmission->mValue);
        ctx.check(light == c.light, std::string(c.id) + " " + c.states.dump() + ": light " + std::to_string(light) + ", TF gives " + std::to_string(c.light));
    }
});

// Hardness and explosion resistance TF gives (written down by hand from TFBlocks.java and the block classes'
// getExplosionResistance / getDestroyProgress overrides, independent of the generator), read back from the game's
// block permutations. The game's units are calibrated on vanilla blocks, which the game stores at Java's values
// (minecraft:oak_planks strength(2.0F, 3.0F), stone (1.5F, 6.0F), obsidian (50.0F, 1200.0F)): a TF block must hold
// the value of the vanilla block with the same Java value (twilight_oak_planks = oak_planks). Measured on 1.26.51: a
// JSON block's destroy speed is its seconds_to_destroy value (-1 for destructible_by_mining false), its explosion
// resistance a fifth of explosion_resistance (the import writes Java's x 5; FLT_MAX / 5 for false).
GF_REGISTER_SELFTEST("import.behaviour", [](Context& ctx) {
    constexpr double kUnbreakable    = -1; // destroyTime -1 / getDestroyProgress -1: minecraft:destructible_by_mining false
    constexpr double kIndestructible = -2; // resistance >= 3600000 or Float.MAX_VALUE: minecraft:destructible_by_explosion false
    struct Case {
        char const* id;
        JDoc        states;
        double      hardness;
        double      resistance;
    };
    std::vector<Case> const cases{
        {"twilight_oak_planks", JDoc::object(), 2.0, 3.0},                 // strength(2.0F, 3.0F): as minecraft:oak_planks
        {"castle_brick", JDoc::object(), 100.0, 50.0},                     // strength(100.0F, 50.0F)
        {"underbrick", JDoc::object(), 1.5, 6.0},                          // strength(1.5F, 6.0F): as minecraft:stone
        {"oak_drying_rack", JDoc::object(), 1.0, 1.5},                     // copyAndScaleProperties(Blocks.OAK_SLAB, 0.5F)
        {"crimson_drying_rack", JDoc::object(), 1.0, 1.5},                 // copyAndScaleProperties(Blocks.CRIMSON_SLAB, 0.5F)
        {"twilight_oak_drying_rack", JDoc::object(), 1.0, 1.5},            // copyAndScaleProperties(TWILIGHT_OAK_SLAB, 0.5F)
        {"brazier", JDoc::object(), 0.0, 0.0},                             // Properties.of(): no strength call
        {"raspberry_bush", JDoc::object(), 0.4, 0.0},                      // destroyTime(0.4F) only
        {"iron_oreberry", JDoc::object(), 0.4, 0.0},
        {"twilight_oak_chest", JDoc::object(), 2.5, 2.5},                  // ofFullCopy(TWILIGHT_OAK_PLANKS).strength(2.5F)
        {"giant_leaves", JDoc::object(), 12.8, 15.0},                      // strength(0.2F * 64.0F, 15.0F)
        {"twilight_portal", JDoc::object(), kUnbreakable, 0.0},            // strength(-1.0F): resistance max(0, -1)
        {"stronghold_shield", JDoc::object(), kUnbreakable, kIndestructible}, // strength(-1.0F, 6000000.0F)
        {"skull_chest", JDoc::object(), 3.0, kIndestructible},             // SkullChestBlock: Float.MAX_VALUE
        {"vanishing_block", {{"twilightforest:active", false}}, 10.0, 6000.0}, // VanishingBlock: !ACTIVE ? 6000F : super
        {"vanishing_block", {{"twilightforest:active", true}}, 10.0, 35.0},
        {"locked_vanishing_block", JDoc::object(), kUnbreakable, kIndestructible}, // default LOCKED (true): 6000000.0F
        {"locked_vanishing_block", {{"twilightforest:locked", false}, {"twilightforest:active", false}}, kUnbreakable, 6000.0},
        {"locked_vanishing_block", {{"twilightforest:locked", false}, {"twilightforest:active", true}}, kUnbreakable, 2000.0},
        {"trophy_pedestal", {{"twilightforest:active", false}}, kUnbreakable, 2000.0}, // getDestroyProgress: ACTIVE ? super : -1
        {"trophy_pedestal", {{"twilightforest:active", true}}, 2.0, 2000.0},
    };
    auto stored = [](Block const* block, double& h, double& r) {
        if (!block) return false;
        h = block->mDirectData->mDestroySpeed;
        r = block->mDirectData->mExplosionResistance;
        return true;
    };
    auto valuesOf = [&](Case const& c, double& h, double& r) {
        return stored(gf::game::blockWithStates(std::string("twilightforest:") + c.id, c.states), h, r);
    };
    auto near = [](double a, double b) { return std::abs(a - b) <= 1e-4 * std::max(1.0, std::abs(b)); };
    // the calibration: vanilla blocks of known Java values (game units per Java unit, 1 when the game stores Java's)
    double h0 = 0, r0 = 0;
    ctx.require(stored(gf::game::blockWith("minecraft:oak_planks"), h0, r0) && h0 > 0 && r0 > 0, "minecraft:oak_planks is registered with a hardness and a resistance");
    double const hs = h0 / 2.0, rs = r0 / 3.0;
    ctx.info("calibration (minecraft:oak_planks, Java 2.0 / 3.0): destroy speed " + std::to_string(h0) + ", explosion resistance " + std::to_string(r0));
    // (Bedrock's obsidian is its own: hardness 35 where Java has 50, so only its resistance calibrates)
    for (auto const& [id, jh, jr] : std::vector<std::tuple<char const*, double, double>>{{"minecraft:stone", 1.5, 6.0}, {"minecraft:obsidian", -1.0, 1200.0}}) {
        double h = 0, r = 0;
        ctx.require(stored(gf::game::blockWith(id), h, r), std::string(id) + " is registered");
        ctx.check((jh < 0 || near(h, jh * hs)) && near(r, jr * rs), std::string(id) + ": destroy speed " + std::to_string(h) + ", explosion resistance " + std::to_string(r) + " (Java " + (jh < 0 ? std::string("resistance") : std::to_string(jh) + " /") + " " + std::to_string(jr) + ": the calibration holds)");
    }
    // a TF block holds what the vanilla block of the same Java values holds
    {
        double th = 0, tr = 0, vh = 0, vr = 0;
        ctx.check(stored(gf::game::blockWith("twilightforest:twilight_oak_planks"), th, tr) && stored(gf::game::blockWith("minecraft:oak_planks"), vh, vr) && near(th, vh) && near(tr, vr),
                  "twilight_oak_planks (strength(2.0F, 3.0F)) holds destroy speed " + std::to_string(th) + " / explosion resistance " + std::to_string(tr) + ", minecraft:oak_planks "
                      + std::to_string(vh) + " / " + std::to_string(vr));
        ctx.check(stored(gf::game::blockWith("twilightforest:underbrick"), th, tr) && stored(gf::game::blockWith("minecraft:stone"), vh, vr) && near(th, vh) && near(tr, vr),
                  "underbrick (strength(1.5F, 6.0F)) holds destroy speed " + std::to_string(th) + " / explosion resistance " + std::to_string(tr) + ", minecraft:stone "
                      + std::to_string(vh) + " / " + std::to_string(vr));
    }
    for (auto const& c : cases) {
        std::string what = std::string(c.id) + " " + c.states.dump();
        double      h = 0, r = 0;
        if (!valuesOf(c, h, r)) {
            ctx.check(false, what + " is not a registered state");
            continue;
        }
        ctx.info(what + ": destroy speed " + std::to_string(h) + ", explosion resistance " + std::to_string(r));
        if (c.hardness == kUnbreakable) ctx.check(h < 0, what + ": unbreakable in TF, destroy speed " + std::to_string(h));
        else ctx.check(near(h, c.hardness * hs), what + ": destroy speed " + std::to_string(h) + ", TF gives " + std::to_string(c.hardness * hs));
        if (c.resistance == kIndestructible)
            ctx.check(r >= 3600000.0 * rs, what + ": indestructible by explosions in TF, resistance " + std::to_string(r));
        else ctx.check(near(r, c.resistance * rs), what + ": explosion resistance " + std::to_string(r) + ", TF gives " + std::to_string(c.resistance * rs));
    }
});

// TF's own sound type (TFSoundTypes.JAR = DeferredSoundType(1.0F, 1.0F, JAR_BREAK, JAR_STEP, JAR_PLACE, JAR_HIT, JAR_FALL),
// TFSounds "block.twilightforest.jar.<event>") in the assembled resource pack the game plays from, written down by hand
// from TF 4.8 and Minecraft 1.21.1, independent of the generator: a SoundType of volume v and pitch p plays break
// (LevelRenderer.levelEvent 2001) and place (BlockItem.place) at (v + 1) / 2, pitch p x 0.8, hit
// (MultiPlayerGameMode.continueDestroyBlock) at (v + 1) / 8, pitch p x 0.5, step (Entity.playStepSound) at v x 0.15,
// pitch p, fall (LivingEntity.playBlockFallSound, with fall damage) at v x 0.5, pitch p x 0.75; every event is one of
// F3's sound definitions, and TF's four jar blocks use the type (RP blocks.json).
GF_REGISTER_SELFTEST("import.sound_types", [](Context& ctx) {
    namespace fs = std::filesystem;
    auto rp   = modDir() / "resource_packs" / "GloamingForest_RP";
    auto read = [](fs::path const& p) {
        std::ifstream in(p);
        return in ? JDoc::parse(in, nullptr, false) : JDoc();
    };
    auto sounds = read(rp / "sounds.json");
    auto blocks = read(rp / "blocks.json");
    auto defs   = read(rp / "sounds" / "sound_definitions.json");
    ctx.require(sounds.is_object(), "resource_packs/GloamingForest_RP/sounds.json is assembled and readable");
    ctx.require(blocks.is_object(), "resource_packs/GloamingForest_RP/blocks.json is assembled and readable");
    ctx.require(defs.is_object() && defs.contains("sound_definitions"), "resource_packs/GloamingForest_RP/sounds/sound_definitions.json is assembled and readable");
    // (find() only: a missing key on a json must not be created or indexed)
    auto at = [](JDoc const& doc, std::vector<char const*> const& path) -> JDoc const* {
        JDoc const* cur = &doc;
        for (auto const* k : path) {
            if (!cur->is_object()) return nullptr;
            auto it = cur->find(k);
            if (it == cur->end()) return nullptr;
            cur = &*it;
        }
        return cur;
    };
    auto num  = [](JDoc const& o, char const* k) { return o.is_object() && o.contains(k) && o[k].is_number() ? o[k].get<double>() : 1.0; };
    auto near = [](double a, double b) { return std::abs(a - b) <= 1e-4; };
    struct Event {
        bool        interactive;
        char const* event;
        char const* sound;
        double      volume;
        double      pitch;
    };
    std::vector<Event> const events{
        {false, "break", "block.twilightforest.jar.break", 1.0, 0.8},
        {false, "place", "block.twilightforest.jar.place", 1.0, 0.8},
        {false, "hit", "block.twilightforest.jar.hit", 0.25, 0.5},
        {true, "step", "block.twilightforest.jar.step", 0.15, 1.0},
        {true, "fall", "block.twilightforest.jar.fall", 0.5, 0.75},
    };
    JDoc const* type        = at(sounds, {"block_sounds", "twilightforest.jar"});
    JDoc const* interactive = at(sounds, {"interactive_sounds", "block_sounds", "twilightforest.jar"});
    ctx.require(type && type->is_object(), "sounds.json block_sounds defines twilightforest.jar");
    ctx.require(interactive && interactive->is_object(), "sounds.json interactive_sounds.block_sounds defines twilightforest.jar");
    for (auto const* t : {type, interactive})
        ctx.check(near(num(*t, "volume"), 1.0) && near(num(*t, "pitch"), 1.0), "twilightforest.jar " + t->value("volume", JDoc()).dump() + " / " + t->value("pitch", JDoc()).dump() + ": volume and pitch 1 (the events carry Java's)");
    for (auto const& e : events) {
        std::string what = std::string(e.interactive ? "interactive_sounds" : "block_sounds") + " twilightforest.jar " + e.event;
        JDoc const* ev   = at(e.interactive ? *interactive : *type, {"events", e.event});
        if (!ev || !ev->is_object()) {
            ctx.check(false, what + " is not defined (TF plays " + e.sound + ")");
            continue;
        }
        // (a sound that is not a string is a readable failure, not a json type_error)
        auto sound = ev->find("sound");
        bool named = sound != ev->end() && sound->is_string() && sound->get<std::string>() == e.sound;
        ctx.check(
            named && near(num(*ev, "volume"), e.volume) && near(num(*ev, "pitch"), e.pitch),
            what + " " + ev->dump() + ", TF plays " + e.sound + " at " + std::to_string(e.volume) + " / " + std::to_string(e.pitch)
        );
        ctx.check(defs["sound_definitions"].contains(e.sound), what + ": " + e.sound + " is a sound definition (F3)");
    }
    // Bedrock's jump and land: TF plays no block sound for a jump, nor for a landing without fall damage. The import's
    // fragment is the type's only source (the families kit no longer writes a copy of it) and declares both silent
    // (""), so no engine fallback plays a sound TF does not.
    for (auto const* x : {"jump", "land"}) {
        JDoc const* ev     = at(*interactive, {"events", x});
        bool        silent = ev && ev->is_string() && ev->get<std::string>().empty();
        ctx.check(silent, std::string("interactive_sounds twilightforest.jar ") + x + " " + (ev ? ev->dump() : "(none)") + ": TF plays nothing (\"\")");
    }
    for (auto const* id : {"twilightforest:mason_jar", "twilightforest:firefly_jar", "twilightforest:firefly_particle_spawner", "twilightforest:cicada_jar"}) {
        JDoc const* b = at(blocks, {id});
        JDoc const* snd = b ? at(*b, {"sound"}) : nullptr;
        std::string s   = !b || !b->is_object() ? std::string("(no entry)")
                        : !snd                  ? std::string("(none)")
                        : snd->is_string()      ? snd->get<std::string>()
                                                : snd->dump();
        ctx.check(s == "twilightforest.jar", std::string(id) + ": RP blocks.json sound " + s + ", TF gives TFSoundTypes.JAR -> twilightforest.jar");
    }
});

// TF's booleans that default to true (Java's StateDefinition.any(): trophies' powered, HugeMushroomBlock faces, rope y,
// terrorcotta rotated, a locked vanishing block) are declared [true, false]: the game's default permutation has them
// true, the explicit true state is that permutation and the false one another (Bedrock takes the first listed value as
// the default and reads a boolean by its value, not its position).
GF_REGISTER_SELFTEST("import.bool_defaults", [](Context& ctx) {
    std::vector<std::pair<char const*, char const*>> const cases{
        {"twilightforest:naga_trophy", "twilightforest:powered"},
        {"twilightforest:naga_wall_trophy", "twilightforest:powered"},
        {"twilightforest:huge_mushgloom", "twilightforest:up"},
        {"twilightforest:huge_mushgloom_stem", "twilightforest:north"},
        {"twilightforest:rope", "twilightforest:y"},
        {"twilightforest:terrorcotta_lines", "twilightforest:rotated"},
        {"twilightforest:locked_vanishing_block", "twilightforest:locked"},
    };
    for (auto const& [id, state] : cases) {
        auto const* def = gf::game::blockWith(id);
        if (!def) {
            ctx.check(false, std::string(id) + " is not a registered block");
            continue;
        }
        auto got = gf::game::statesOf(*def);
        auto it  = got.find(state);
        ctx.check(it != got.end() && it->second == "true", std::string(id) + ": default " + state + "=" + (it != got.end() ? it->second : "?") + ", TF's default is true");
        auto const* on  = gf::game::blockWithStates(id, JDoc{{state, true}});
        auto const* off = gf::game::blockWithStates(id, JDoc{{state, false}});
        ctx.check(on == def, std::string(id) + ": " + state + "=true is the default permutation");
        ctx.check(off != nullptr && off != def && gf::game::statesOf(*off)[state] == "false", std::string(id) + ": " + state + "=false is another permutation");
    }
});

namespace {

/// What a TF block needs around it to stay where it is placed: what the canSurvive (and the updateShape or
/// neighborChanged that asks it) of its Java classes requires, written down by hand from Minecraft 1.21.1 and TF 4.8.
/// Every block stands on a floor block, minecraft:dirt unless a rule says otherwise: sturdy, solid and in
/// #minecraft:dirt, it holds StandingSignBlock (isSolid below), PressurePlateBlock, DoorBlock's lower half,
/// BushBlock.mayPlaceOn (saplings, TFPlantBlock), BerryBushBlock (#twilightforest:tf_berry_bushes_survive),
/// CritterBlock and ThornRoseBlock (default facing up: resting on the block below), MossPatchBlock, FallenLeavesBlock,
/// MushgloomBlock, OminousFireBlock, Experiment115Block, CarpetBlock and CandelabraBlock (default on_wall false).
struct Support {
    std::string                      floor = "minecraft:dirt";
    std::string                      ceiling;   ///< the block above it ("": none)
    std::vector<std::pair<int, int>> sides;     ///< grass blocks beside it (dx, dz), at its height
    JDoc                             states = JDoc::object(); ///< the state it is placed in (its default otherwise)
    std::optional<JDoc>              upper;     ///< its upper half, one above
    bool                             poke = false; ///< a block set above it afterwards: Java's neighbour check runs in place
};

/// The support of a block from its Java class chain (data/gf/import/skeleton.json "classes": TF's classes, then the
/// vanilla class reached); the first class of the chain with a rule decides.
Support supportOf(JDoc const& chain) {
    Support s;
    for (auto const& c : chain) {
        auto const cls = c.is_string() ? c.get<std::string>() : std::string();
        if (cls == "WallSignBlock" || cls == "LadderBlock") {
            // WallSignBlock.canSurvive: the block behind it (facing.getOpposite()) isSolid; LadderBlock.canSurvive: that
            // block's face is sturdy. Facing north: the block south of it.
            s.states = {{"twilightforest:facing", "north"}};
            s.sides  = {{0, 1}};
        } else if (cls == "ButtonBlock") {
            // FaceAttachedHorizontalDirectionalBlock.canSurvive (TF's default face wall, facing north): the block south of
            // it has a sturdy north face
            s.states = {{"twilightforest:face", "wall"}, {"twilightforest:facing", "north"}};
            s.sides  = {{0, 1}};
        } else if (cls == "WallHangingSignBlock") {
            // WallHangingSignBlock.canPlace: a sturdy block on its clockwise (east of a sign facing north) or
            // counter-clockwise side
            s.states = {{"twilightforest:facing", "north"}};
            s.sides  = {{1, 0}};
        } else if (cls == "CeilingHangingSignBlock") {
            s.ceiling = "minecraft:dirt"; // CeilingHangingSignBlock.canSurvive: the block above has a sturdy centre
        } else if (cls == "TorchberryPlantBlock" || cls == "RootStrandBlock") {
            s.ceiling = "minecraft:dirt"; // TFPlantBlock.canPlaceRootAt: the block above is #twilightforest:plants_hang_on
        } else if (cls == "TrollRootBlock") {
            s.ceiling = "minecraft:stone"; // canPlaceRootBelow: the block above is #minecraft:base_stone_overworld
        } else if (cls == "RopeBlock") {
            // RopeBlock (default y true, x and z false): onPlace schedules a tick, and tick() destroys a rope whose
            // checkConnection(UP) fails; canSurvive / canConnectTo(UP): the block above is leaves, a rope along y, or has
            // a sturdy centre on its down face
            s.ceiling = "minecraft:dirt";
        } else if (cls == "DoorBlock" || cls == "BrazierBlock") {
            // DoorBlock / BrazierBlock.updateShape: the lower half goes when the block above is not its upper half
            s.states = {{"twilightforest:half", "lower"}};
            s.upper  = JDoc{{"twilightforest:half", "upper"}};
        } else if (cls == "DarkTowerBerryBushBlock") {
            // canBePlacedAt: #twilightforest:dark_tower_berry_bushes_survive (#c:netherracks, ...); shouldDie on #minecraft:nylium
            s.floor = "minecraft:netherrack";
        } else if (cls == "OreBerryBlock") {
            s.floor = "minecraft:stone"; // canBePlacedAt: #twilightforest:oreberry_bushes_survive (#c:stones, ...)
        } else if (cls == "WaterlilyBlock") {
            s.floor = "minecraft:ice"; // WaterlilyBlock.mayPlaceOn: water or an IceBlock below, no fluid above
        } else if (cls == "MultifaceBlock") {
            s.states = {{"twilightforest:down", true}}; // MultifaceBlock.updateShape: a block without a face goes
        } else if (cls == "LeavesBlock") {
            s.states = {{"twilightforest:persistent", true}}; // LeavesBlock.randomTick: decays unless persistent (player placed)
        } else if (cls == "TFPortalBlock") {
            // TFPortalBlock.neighborChanged: a sturdy floor and #twilightforest:portal/edge (grass, dirt) on all four sides,
            // else water again; poked to run that check in its frame
            s.sides = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            s.poke  = true;
        } else {
            continue;
        }
        return s;
    }
    return s;
}

} // namespace

// Every block placed and read back. Each block gets its own cell, one air gap away from the next, with the support its
// Java classes need (supportOf: a floor, the block it hangs on or from, its upper half, the portal's frame), so that a
// block a unit gives TF's survival rules (F11's signs, F4's portal, ...) stays: the supports go first, the blocks and
// then their upper halves, and the blocks are removed before their supports.
GF_REGISTER_SELFTEST("import.place_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    requireSkeleton(ctx);
    auto const& skel = skeleton();
    ctx.require(skel.contains("classes") && skel["classes"].is_object(), "data/gf/import/skeleton.json lists the Java classes of the blocks");
    // cells at x 1, 3, .., 97 (dx 5101..5197) and dz 1, 3 of this unit's row (dx 5100..5199, dz 0..4), in layers of 4
    // (floor, block, the block above it, air) from one above the surface
    constexpr int kPerRow = 49, kPerLayer = 2 * kPerRow, kLayerHeight = 4;
    for (int x = 0; x < 100; x += 16) ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5100 + x)), "test row loaded");
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5199)), "test row loaded");
    struct Cell {
        std::string         id;
        gf::core::GlobalPos pos;
        Support             support;
    };
    std::vector<Cell>                                        cells;
    std::vector<std::pair<gf::core::GlobalPos, std::string>> supports;
    std::set<std::tuple<int, int, int>>                      taken;
    auto addSupport = [&](gf::core::GlobalPos const& p, std::string const& id) {
        if (taken.insert(std::make_tuple(p.x, p.y, p.z)).second) supports.emplace_back(p, id); // a grass block between two cells serves both
    };
    Failures fails{ctx, "blocks placed with their support and read back"};
    for (auto const& [id, states] : skel["blocks"].items()) {
        int  i     = static_cast<int>(cells.size());
        int  j     = i % kPerLayer;
        auto pos   = ctx.testPos(5101 + 2 * (j % kPerRow), 1 + 2 * (j / kPerRow));
        pos.y     += 2 + (i / kPerLayer) * kLayerHeight;
        auto s     = supportOf(skel["classes"].value(id, JDoc::array()));
        for (auto const& [name, _] : s.states.items())
            if (!states.contains(name)) fails.add(id + " has no state " + name + " (its support rule places it in one)");
        // one block one above: a ceiling support, or the upper half / poke block (the removal orders them apart)
        if (!s.ceiling.empty() && (s.upper || s.poke)) fails.add(id + ": its support rule sets both a ceiling and a block above it");
        addSupport(pos.offset(0, -1, 0), s.floor);
        if (!s.ceiling.empty()) addSupport(pos.offset(0, 1, 0), s.ceiling);
        for (auto const& [dx, dz] : s.sides) addSupport(pos.offset(dx, 0, dz), "minecraft:grass_block");
        cells.push_back({id, pos, std::move(s)});
    }
    // (setBlock reports false when the block is already there.) From here on nothing aborts the test, so the removal
    // below always runs in its order.
    for (auto const& [p, id] : supports)
        if (!ctx.setBlock(p, id) && ctx.blockAt(p) != id) fails.add("cannot place the support " + id);
    for (auto const& c : cells)
        if (!ctx.setBlock(c.pos, c.id, c.support.states)) fails.add("cannot place " + c.id + " " + c.support.states.dump());
    for (auto const& c : cells)
        if (c.support.upper && !ctx.setBlock(c.pos.offset(0, 1, 0), c.id, *c.support.upper)) fails.add("cannot place the upper half of " + c.id);
    co_await ctx.ticks(1);
    for (auto const& c : cells)
        if (c.support.poke && !ctx.setBlock(c.pos.offset(0, 1, 0), "minecraft:glass")) fails.add("cannot set a block above " + c.id);
    co_await ctx.ticks(3);
    for (auto const& c : cells) {
        if (auto got = ctx.blockAt(c.pos); got != c.id) fails.add(c.id + " " + c.support.states.dump() + " reads back as " + got);
        if (!c.support.upper) continue;
        if (auto got = ctx.blockAt(c.pos.offset(0, 1, 0)); got != c.id) fails.add(c.id + "'s upper half reads back as " + got);
    }
    fails.done(static_cast<int>(cells.size()));
    // the blocks go before what holds them (a support removed first breaks its block, with drops): the upper halves
    // (held by their lower halves) and the blocks set above a cell, then the cells, then the supports
    if (auto* region = ctx.region()) {
        for (auto const& c : cells)
            if (c.support.upper || c.support.poke) gf::game::clearBlock(*region, gf::game::blockPos(c.pos.offset(0, 1, 0)));
        for (auto const& c : cells) gf::game::clearBlock(*region, gf::game::blockPos(c.pos));
        for (auto const& [p, id] : supports) gf::game::clearBlock(*region, gf::game::blockPos(p));
    }
});
