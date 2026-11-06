// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of the imported harvest rules (unit F1 "import", prefix "import.", testPos dx 5100..5199):
// TF's tool tags (#minecraft:mineable/*, #minecraft:needs_*_tool, ...) as Bedrock block tags, the
// requires-correct-tool flag (Harvest.cpp), the digger of TF's tiered tools, and the mining speed and
// drops they give in game, measured against vanilla blocks and tools of the same Java values (the
// tool and tier rules are Bedrock's own for vanilla blocks: a TF block must behave as the vanilla
// block TF's values describe).
//
// F1 owns no source directory, so these tests live in src/feature/ext/import/ (new files only,
// rule R18); A8 may fold them into a unit directory.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"

#include "nlohmann/json.hpp"

#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace gf::feature::ext::harvest {
int requiresToolCount();
int applyNow();
} // namespace gf::feature::ext::harvest

namespace {

using gf::selftest::Context;
using JDoc = nlohmann::json;

/// data/gf/import/harvest.json: {"blocks": {id: {"tools": [...], "tier": "...", "requiresTool": true}}}.
JDoc const& harvestIndex() {
    static JDoc doc = [] {
        std::ifstream in(gf::Mod::getInstance().getSelf().getModDir() / "data" / "gf" / "import" / "harvest.json");
        if (!in) return JDoc();
        return JDoc::parse(in, nullptr, false);
    }();
    return doc;
}

// the Bedrock block tags of the tool kinds and tiers (tools/import/src/Harvest.cs)
std::map<std::string, std::string> const kToolTag{
    {"pickaxe", "minecraft:is_pickaxe_item_destructible"},
    {"axe", "minecraft:is_axe_item_destructible"},
    {"shovel", "minecraft:is_shovel_item_destructible"},
    {"hoe", "minecraft:is_hoe_item_destructible"},
    {"sword", "minecraft:is_sword_item_destructible"},
    {"shears", "minecraft:is_shears_item_destructible"},
    {"leaves", "minecraft:leaves"},
};
std::map<std::string, std::string> const kTierTag{
    {"stone", "minecraft:stone_tier_destructible"},
    {"iron", "minecraft:iron_tier_destructible"},
    {"diamond", "minecraft:diamond_tier_destructible"},
};

Block const* blockOf(std::string const& id) { return gf::game::blockWith(id); }

bool near(double a, double b, double rel = 1e-3) { return std::abs(a - b) <= rel * std::max(std::abs(a), std::abs(b)) + 1e-9; }

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

/// Mining measurements of one survival player with a tool in the main hand ("" = empty hand).
struct Miner {
    Player& player;
    explicit Miner(Player& p) : player(p) { player.setPlayerGameType(GameType::Survival); }
    void hold(std::string const& tool) {
        if (tool.empty()) {
            player.setCarriedItem(ItemStack::EMPTY_ITEM());
            return;
        }
        auto stack = gf::game::items::make(tool);
        player.setCarriedItem(stack ? *stack : ItemStack::EMPTY_ITEM());
    }
    /// The destroy progress per tick of `block` with `tool` in hand (the game's own formula).
    double progress(std::string const& tool, Block const& block) {
        hold(tool);
        return player.getDestroyProgress(block);
    }
    /// The tool can harvest the block (Bedrock's canDestroySpecial: Java's isCorrectToolForDrops).
    static bool harvests(std::string const& tool, Block const& block) {
        auto stack = gf::game::items::make(tool);
        if (!stack || !stack->mItem) return false;
        return stack->mItem->canDestroySpecial(block);
    }
    /// The tool's speed on the block (Java's Tool rule speed, 1 by default).
    static double speed(std::string const& tool, Block const& block) {
        auto stack = gf::game::items::make(tool);
        return stack && stack->mItem ? stack->mItem->getDestroySpeed(*stack, block) : 0.0;
    }
};

/// The game's stored hardness of a block permutation (Bedrock's own for vanilla blocks: obsidian 35, Java 50).
double hardnessOf(Block const& block) { return block.mDirectData->mDestroySpeed; }

/// The tags of a block type and of its default permutation, as the game holds them.
std::string tagsOf(Block const& block) {
    std::string s;
    for (auto const& t : *block.getBlockType().mTags) s += (s.empty() ? "" : ", ") + t.getString();
    std::string p;
    for (auto const& t : *block.mTags) p += (p.empty() ? "" : ", ") + t.getString();
    return "[" + s + "] / permutation [" + p + "]";
}

} // namespace

// Every TF block carries the Bedrock tool and tier tags of TF's tool tags, and the blocks TF registers with
// requiresCorrectToolForDrops() have the engine's flag (the vanilla blocks with it, such as stone and obsidian,
// show the flag is the game's own).
GF_REGISTER_SELFTEST("import.harvest_tags", [](Context& ctx) {
    auto const& idx = harvestIndex();
    ctx.require(idx.is_object() && idx.contains("blocks"), "data/gf/import/harvest.json is assembled and readable");
    gf::feature::ext::harvest::applyNow();
    // the game's own flag on vanilla blocks (minecraft:stone and obsidian drop only for a pickaxe, dirt for anything)
    for (auto const& [id, want] : std::vector<std::pair<char const*, bool>>{{"minecraft:stone", true}, {"minecraft:obsidian", true}, {"minecraft:dirt", false}}) {
        auto const* b = blockOf(id);
        ctx.require(b != nullptr, std::string(id) + " is registered");
        ctx.check(b->getBlockType().mRequiresCorrectToolForDrops == want, std::string(id) + ": requires a correct tool " + (want ? "true" : "false") + " (the game's vanilla data)");
    }
    // the tags the game gives vanilla blocks (the vanilla tools read them; the TF blocks get the same names)
    for (auto const* id : {"minecraft:stone", "minecraft:obsidian", "minecraft:oak_planks", "minecraft:oak_leaves", "minecraft:web", "minecraft:white_wool",
                           "minecraft:iron_ore", "minecraft:diamond_block", "minecraft:dirt", "twilightforest:twilight_oak_leaves", "twilightforest:castle_brick"})
        if (auto const* b = blockOf(id)) ctx.info(std::string(id) + " tags " + tagsOf(*b));
    int checked = 0, needTool = 0, bad = 0;
    for (auto const& [id, e] : idx["blocks"].items()) {
        auto const* b = blockOf(id);
        if (!b) {
            if (++bad <= 8) ctx.check(false, id + " is not a registered block");
            continue;
        }
        std::vector<std::string> tools;
        for (auto const& t : e.value("tools", JDoc::array())) tools.push_back(t.get<std::string>());
        for (auto const& [kind, tag] : kToolTag) {
            bool want = std::find(tools.begin(), tools.end(), kind) != tools.end();
            if (b->hasTag(HashedString(tag)) != want && ++bad <= 8) ctx.check(false, id + ": tag " + tag + (want ? " missing" : " present") + " in game");
        }
        std::string tier = e.value("tier", std::string{});
        for (auto const& [t, tag] : kTierTag)
            if (b->hasTag(HashedString(tag)) != (t == tier) && ++bad <= 8) ctx.check(false, id + ": tier tag " + tag + (t == tier ? " missing" : " present") + " in game");
        bool rt = e.value("requiresTool", false);
        if (rt) ++needTool;
        if (b->getBlockType().mRequiresCorrectToolForDrops != rt && ++bad <= 8)
            ctx.check(false, id + ": requires a correct tool " + (rt ? "true" : "false") + " in TF, the game's flag is the opposite");
        ++checked;
    }
    if (bad > 8) ctx.check(false, std::to_string(bad) + " harvest mismatches in total");
    ctx.check(needTool == gf::feature::ext::harvest::requiresToolCount(), "harvest.json lists " + std::to_string(needTool) + " blocks that need a correct tool");
    ctx.check(checked > 300, std::to_string(checked) + " TF blocks with a tool, tier or drop rule checked");
    ctx.info(std::to_string(checked) + " blocks checked, " + std::to_string(needTool) + " drop only for a correct tool");
});

// Mining speed and harvest, measured with a survival player in game: a TF block mines as the vanilla block of the
// same Java tool tag, tier and drop rule (the progress per tick normalised by the game's stored hardness of each,
// as Bedrock's own vanilla hardness can differ from Java's: obsidian 35), and TF's tools as the vanilla tools of the
// same speed and tier (Java's TFToolMaterials: knightmetal / steeleaf 8 diamond, fiery 9 netherite).
GF_REGISTER_SELFTEST("import.harvest_mining", [](Context& ctx) {
    gf::feature::ext::harvest::applyNow();
    Miner m(ctx.player());
    struct Pair {
        char const*              tf;
        char const*              vanilla;
        std::vector<char const*> tools; // "" = empty hand
    };
    // TF's tags (TFBlocks.java, the tag JSONs) against the vanilla block with the same ones (Minecraft 1.21.1)
    std::vector<Pair> const pairs{
        {"twilightforest:twilight_oak_planks", "minecraft:oak_planks", {"", "minecraft:iron_axe", "minecraft:iron_pickaxe"}},                            // axe, no tool needed
        {"twilightforest:underbrick", "minecraft:iron_ore", {"", "minecraft:wooden_pickaxe", "minecraft:stone_pickaxe", "minecraft:iron_pickaxe"}},     // pickaxe, needs stone
        {"twilightforest:knightmetal_block", "minecraft:diamond_block", {"", "minecraft:stone_pickaxe", "minecraft:iron_pickaxe"}},                      // pickaxe, needs iron
        {"twilightforest:castle_brick", "minecraft:obsidian", {"", "minecraft:iron_pickaxe", "minecraft:diamond_pickaxe"}},                             // pickaxe, needs diamond
        {"twilightforest:twilight_oak_leaves", "minecraft:oak_leaves", {"", "minecraft:iron_hoe"}},                                                    // hoe
        {"twilightforest:uberous_soil", "minecraft:dirt", {"", "minecraft:iron_shovel"}},                                                              // shovel
    };
    for (auto const& p : pairs) {
        auto const* tf = blockOf(p.tf);
        auto const* va = blockOf(p.vanilla);
        if (!tf || !va) {
            ctx.check(false, std::string(p.tf) + " / " + p.vanilla + " not registered");
            continue;
        }
        for (auto const* tool : p.tools) {
            double a = m.progress(tool, *tf) * hardnessOf(*tf), b = m.progress(tool, *va) * hardnessOf(*va);
            std::string with = std::string(*tool ? tool : "an empty hand");
            ctx.info(std::string(p.tf) + " with " + with + ": progress x hardness " + num(a) + ", " + p.vanilla + " " + num(b));
            ctx.check(near(a, b), std::string(p.tf) + " mines with " + with + " as " + p.vanilla + " (progress x hardness " + num(a) + ", vanilla " + num(b) + ")");
        }
    }    // TF's tools against vanilla tools of the same speed and tier
    struct Same {
        char const*              tf;
        char const*              vanilla;
        std::vector<char const*> blocks;
    };
    std::vector<Same> const same{
        {"twilightforest:knightmetal_pickaxe", "minecraft:diamond_pickaxe", {"minecraft:stone", "minecraft:obsidian", "twilightforest:castle_brick"}},
        {"twilightforest:steeleaf_pickaxe", "minecraft:diamond_pickaxe", {"minecraft:stone", "twilightforest:mazestone"}},
        {"twilightforest:fiery_pickaxe", "minecraft:netherite_pickaxe", {"minecraft:obsidian", "twilightforest:deadrock"}},
        {"twilightforest:steeleaf_axe", "minecraft:diamond_axe", {"minecraft:oak_planks", "twilightforest:twilight_oak_planks"}},
        {"twilightforest:steeleaf_shovel", "minecraft:diamond_shovel", {"minecraft:dirt", "twilightforest:uberous_soil"}},
        {"twilightforest:steeleaf_hoe", "minecraft:diamond_hoe", {"minecraft:oak_leaves", "twilightforest:twilight_oak_leaves"}},
        {"twilightforest:ironwood_sword", "minecraft:iron_sword", {"minecraft:web"}},
    };
    for (auto const& s : same)
        for (auto const* id : s.blocks) {
            auto const* b = blockOf(id);
            if (!b) {
                ctx.check(false, std::string(id) + " not registered");
                continue;
            }
            double a = m.progress(s.tf, *b), v = m.progress(s.vanilla, *b);
            ctx.info(std::string(s.tf) + " on " + id + ": progress " + num(a) + " (speed " + num(Miner::speed(s.tf, *b)) + ", harvests " + (Miner::harvests(s.tf, *b) ? "yes" : "no") + "), "
                     + s.vanilla + " " + num(v) + " (speed " + num(Miner::speed(s.vanilla, *b)) + ", harvests " + (Miner::harvests(s.vanilla, *b) ? "yes" : "no") + ")");
            ctx.check(near(a, v), std::string(s.tf) + " mines " + id + " as " + s.vanilla + " (progress " + num(a) + ", vanilla " + num(v) + ")");
        }
    // the vanilla shears on TF's leaves: Java's ShearsItem overrides the speed to 15 on #minecraft:leaves (TF's leaves are
    // members). Bedrock's shears read minecraft:is_shears_item_destructible on a JSON block at 5, so the leaves' own
    // destructible_by_mining item_specific_speeds (U3, tools/import/overrides/trees.json) give the shears Java's 15
    if (auto const* leaves = blockOf("twilightforest:twilight_oak_leaves")) {
        double hand = m.progress("", *leaves), shears = m.progress("minecraft:shears", *leaves);
        ctx.check(near(shears / hand, 15.0, 0.02), "vanilla shears on twilight_oak_leaves: " + num(shears / hand) + " x an empty hand (Java: 15 on #minecraft:leaves)");
    }
    // a sword's #minecraft:sword_efficient speed (Java 1.5; Bedrock's digger speed is an integer, the import writes 2) on
    // TF's and vanilla leaves
    for (auto const* id : {"minecraft:oak_leaves", "twilightforest:twilight_oak_leaves"})
        if (auto const* b = blockOf(id))
            ctx.check(near(Miner::speed("twilightforest:ironwood_sword", *b), 2.0), std::string("ironwood sword on ") + id + ": speed " + num(Miner::speed("twilightforest:ironwood_sword", *b)) + " (Java 1.5, written 2)");
    // a tool without a vanilla twin: ironwood (Java 6.5; Bedrock's digger speed is an integer, the import writes 7)
    // against iron (6) where both harvest
    if (auto const* stone = blockOf("minecraft:stone")) {
        double a = m.progress("twilightforest:ironwood_pickaxe", *stone), v = m.progress("minecraft:iron_pickaxe", *stone);
        ctx.check(near(a / v, 7.0 / 6.0), "ironwood pickaxe on stone: progress " + num(a) + " = 7 / 6 of the iron pickaxe's " + num(v));
    }
    // which tool harvests which block (Java's isCorrectToolForDrops: the mineable tag and the tier's incorrect blocks)
    struct HarvestCase {
        char const* tool;
        char const* block;
        bool        harvests;
    };
    std::vector<HarvestCase> const harvest{
        {"twilightforest:ironwood_pickaxe", "minecraft:iron_ore", true},           // iron tier
        {"twilightforest:ironwood_pickaxe", "minecraft:obsidian", false},
        {"twilightforest:ironwood_pickaxe", "twilightforest:knightmetal_block", true},
        {"twilightforest:ironwood_pickaxe", "twilightforest:castle_brick", false},
        {"twilightforest:giant_pickaxe", "twilightforest:underbrick", true},       // stone tier
        {"twilightforest:giant_pickaxe", "twilightforest:knightmetal_block", false},
        {"twilightforest:giant_pickaxe", "minecraft:diamond_block", false},
        {"twilightforest:knightmetal_pickaxe", "minecraft:obsidian", true},        // diamond tier
        {"twilightforest:knightmetal_pickaxe", "twilightforest:castle_brick", true},
        {"twilightforest:fiery_pickaxe", "twilightforest:deadrock", true},          // netherite tier
        {"minecraft:wooden_pickaxe", "twilightforest:underbrick", false},          // vanilla tools on TF blocks
        {"minecraft:stone_pickaxe", "twilightforest:underbrick", true},
        {"minecraft:stone_pickaxe", "twilightforest:knightmetal_block", false},
        {"minecraft:iron_pickaxe", "twilightforest:knightmetal_block", true},
        {"minecraft:iron_pickaxe", "twilightforest:castle_brick", false},
        {"minecraft:diamond_pickaxe", "twilightforest:castle_brick", true},
        {"minecraft:iron_pickaxe", "minecraft:obsidian", false},                   // the game's own rule, for reference
        {"minecraft:diamond_pickaxe", "minecraft:obsidian", true},
    };
    for (auto const& h : harvest) {
        auto const* b = blockOf(h.block);
        if (!b) {
            ctx.check(false, std::string(h.block) + " not registered");
            continue;
        }
        bool got = Miner::harvests(h.tool, *b);
        ctx.check(got == h.harvests, std::string(h.tool) + (h.harvests ? " harvests " : " does not harvest ") + h.block + " in TF (game: " + (got ? "harvests" : "does not") + ")");
    }
    m.hold("");
});

// Drops, measured by breaking the blocks as a survival player: a block that requires a correct tool drops only for
// one (TF's underbrick and knightmetal block, as vanilla iron ore and diamond block), any other block for any tool (TF's planks).
GF_REGISTER_SELFTEST("import.harvest_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    gf::feature::ext::harvest::applyNow();
    struct Case {
        char const* block;
        char const* tool;
        bool        drops;
    };
    // blocks soft enough to break by mining in a few seconds (castle brick and obsidian would take minutes): each TF case
    // has a vanilla control of the same rule (iron ore needs a stone pickaxe, diamond block an iron one)
    std::vector<Case> const cases{
        {"minecraft:iron_ore", "minecraft:wooden_pickaxe", false}, // the game's own rule: the harness works
        {"minecraft:iron_ore", "minecraft:stone_pickaxe", true},
        {"twilightforest:underbrick", "minecraft:wooden_pickaxe", false}, // needs a stone pickaxe
        {"twilightforest:underbrick", "minecraft:stone_pickaxe", true},
        {"twilightforest:underbrick", "twilightforest:giant_pickaxe", true}, // stone tier
        {"minecraft:diamond_block", "minecraft:stone_pickaxe", false},
        {"twilightforest:knightmetal_block", "minecraft:stone_pickaxe", false}, // needs an iron pickaxe
        {"twilightforest:knightmetal_block", "minecraft:iron_pickaxe", true},
        {"twilightforest:knightmetal_block", "twilightforest:ironwood_pickaxe", true}, // iron tier
        {"twilightforest:twilight_oak_planks", "", true},                        // no correct tool needed
    };    for (size_t i = 0; i < cases.size(); ++i) {
        auto const& c   = cases[i];
        auto        pos = ctx.testPos(5190 + static_cast<int>(i % 6), 1 + 2 * static_cast<int>(i / 6));
        ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
        ctx.require(ctx.setBlock(pos, c.block), std::string("place ") + c.block);
        BlockPos    bp = gf::game::blockPos(pos);
        std::string path;
        {
            // a survival player two blocks south of the block, looking at it, the tool in hand
            auto& sp = static_cast<SimulatedPlayer&>(ctx.player()); // the test player is a simulated one (selftest/SelfTest.h)
            sp.setPlayerGameType(GameType::Survival);
            sp.teleport(Vec3{bp.x + 0.5f, static_cast<float>(bp.y), bp.z + 2.5f}, sp.getDimensionId());
            sp.simulateLookAt(bp, sim::LookDuration::Instant);
            auto stack = *c.tool ? gf::game::items::make(c.tool) : std::nullopt;
            sp.simulateSetItem(stack ? *stack : ItemStack::EMPTY_ITEM(), true, 0);
        }
        co_await ctx.ticks(2); // a simulated player applies its look on its next tick
        // The simulated player breaks as a client does (its destroy, continued each tick, until the progress is complete):
        // the game then decides the drops from the tool and the block, as for any survival break. (Its game mode's
        // direct destroyBlock refuses a survival break without that progress, and Block::playerDestroy drops whatever
        // the tool, so neither tells the harvest rule.)
        int limit = 0;
        {
            auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
            double per = sp.getDestroyProgress(*gf::game::blockWith(c.block));
            limit      = per > 0 ? std::min(600, static_cast<int>(std::ceil(1.0 / per)) * 2 + 20) : 20;
            path       = std::string("simulateDestroyBlock ") + (sp.simulateDestroyBlock(bp, ScriptModuleMinecraft::ScriptFacing::South) ? "started" : "refused")
                 + ", progress " + num(per) + " per tick";
        }
        int took = 0;
        for (; took < limit; ++took) {
            co_await ctx.ticks(1);
            if (ctx.blockAt(pos) != c.block) break;
        }
        path += ", " + std::to_string(took + 1) + " ticks";
        static_cast<SimulatedPlayer&>(ctx.player()).simulateStopDestroyingBlock();
        co_await ctx.ticks(3);
        auto* region = ctx.region();
        ctx.require(region != nullptr, "region");
        int  dropped = 0;
        auto items   = region->getEntities(AABB(Vec3(bp.x - 1.5f, bp.y - 1.5f, bp.z - 1.5f), Vec3(bp.x + 2.5f, bp.y + 2.5f, bp.z + 4.5f)), 0.0f, ActorType::ItemEntity, false);
        for (auto* a : items) {
            auto& item = static_cast<ItemActor*>(a)->item();
            dropped += gf::game::items::countOf(item); // the block itself, or its loot (iron ore: raw iron)
            a->remove();
        }
        std::string with = *c.tool ? c.tool : "an empty hand";
        std::string now  = ctx.blockAt(pos);
        ctx.info(std::string(c.block) + " broken with " + with + " (" + path + "): now " + now + ", " + std::to_string(dropped) + " dropped");
        ctx.check(now == "minecraft:air", std::string(c.block) + " is broken with " + with + " (now " + now + ")");
        ctx.check((dropped > 0) == c.drops, std::string(c.block) + " broken with " + with + (c.drops ? " drops" : " drops nothing") + " (dropped " + std::to_string(dropped) + ")");
    }    static_cast<SimulatedPlayer&>(ctx.player()).simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
});
