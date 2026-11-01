// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of the imported content skeleton (unit F1 "import", prefix "import.", testPos dx
// 5100..5199). They read data/gf/import/skeleton.json (written by tools/import/run.ps1) and check
// that the game loaded what the import generated: every TF block with its frozen states
// (docs/spec/blockstates.md), every item, block item and spawn egg, every entity definition, the
// imported lang and data, and that every block can be placed.
//
// F1 owns no source directory, so these tests live in src/feature/ext/import/ (new files only,
// rule R18); A8 may fold them into a unit directory.

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
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
            for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_directory()) {
                    if (it->path().filename() == "gf_known") it.disable_recursion_pending();
                    continue;
                }
                ++count;
            }
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
        int light = static_cast<int>(block->getBlockType().getLightEmission(*block).mValue);
        ctx.check(light == c.light, std::string(c.id) + " " + c.states.dump() + ": light " + std::to_string(light) + ", TF gives " + std::to_string(c.light));
    }
});

GF_REGISTER_SELFTEST("import.place_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    requireSkeleton(ctx);
    std::vector<std::string> ids;
    for (auto const& [id, _] : skeleton()["blocks"].items()) ids.push_back(id);
    // 100 x 5 positions per layer in this unit's row (dx 5100..5199, dz 0..4), two layers up
    for (int x = 0; x < 100; x += 16) ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5100 + x)), "test row loaded");
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5199)), "test row loaded");
    std::vector<gf::core::GlobalPos> placed(ids.size());
    Failures                         fails{ctx, "blocks placed"};
    for (size_t i = 0; i < ids.size(); ++i) {
        auto pos = ctx.testPos(5100 + static_cast<int>(i % 100), static_cast<int>((i / 100) % 5));
        pos.y += 1 + static_cast<int>(i / 500);
        placed[i] = pos;
        if (!ctx.setBlock(pos, ids[i])) fails.add("cannot place " + ids[i]);
    }
    co_await ctx.ticks(1);
    for (size_t i = 0; i < ids.size(); ++i) {
        auto got = ctx.blockAt(placed[i]);
        if (got != ids[i]) fails.add(ids[i] + " reads back as " + got);
    }
    fails.done(static_cast<int>(ids.size()));
});
