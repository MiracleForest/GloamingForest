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
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"
#include "mc/world/actor/ActorDefinitionGroup.h"
#include "mc/world/actor/ActorDefinitionPtr.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "nlohmann/json.hpp"

#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using Json = nlohmann::json;

std::filesystem::path modDir() { return gf::Mod::getInstance().getSelf().getModDir(); }

/// data/gf/import/skeleton.json: {"blocks": {id: {state: [values]}}, "items": [...], "entities": [...],
/// "spawnEggs": [...], "data": {"twilightforest/<registry>": file count}}.
Json const& skeleton() {
    static Json doc = [] {
        std::ifstream in(modDir() / "data" / "gf" / "import" / "skeleton.json");
        if (!in) return Json();
        return Json::parse(in, nullptr, false);
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

std::string stateText(Json const& v) {
    if (v.is_string()) return v.get<std::string>();
    return v.dump();
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
    // the imported data, file by file, as the import counted it
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
        ctx.check(count == expected.get<int>(), "data/" + dir + ": " + std::to_string(count) + " files, expected " + std::to_string(expected.get<int>()));
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
