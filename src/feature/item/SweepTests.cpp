// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// The sweep selftests of F9 (prefix "item.sweep_", testPos dx 5960..5969): every registered
// gloamingforest block is placed and broken and its drops are compared with what upstream 4.8 drops, and
// every recipe of upstream's data pack crafts its output in the game, both against F13's oracle goldens
// (tests/golden/loot.json and recipes.json, compacted into sweep/Golden.inc by
// sweep/gen_golden.py).
//
// - item.sweep_block_drops: each block's default state is placed and broken by a survival player
//   (Block::playerDestroy: the game's break with its drops, the loot contract's Java tables for upstream
//   blocks) with an empty hand, shears and a silk touch diamond pickaxe, the oracle's three block
//   contexts. A single break is one roll of the table: every item it drops must be one the oracle
//   saw, in the per-roll count range it saw, and every item the oracle saw in every roll must be
//   there. A block without a table in the oracle drops nothing (Java: no loot table). The harvest
//   rule (the correct tool) is not the loot table's and is checked by import.harvest_*.
// - item.sweep_recipes: every crafting recipe is put into a crafting grid from its first usable
//   ingredient items and one of the game's crafting table recipes of its result must match it
//   (the game's own Recipe::matches) with the oracle's count; stonecutting recipes need a game
//   stonecutter recipe taking the input and giving the result and count; cooking recipes need the
//   game's furnace / blast furnace / smoker / campfire result; upstream's custom types (drying,
//   uncrafting) and code recipes (special crafting) need their loader or registration.
//
// Genuine gaps are reviewed exceptions below (block / recipe id -> reason); each is reported as an
// info line, and an exception that no longer fails is reported too, so the list stays honest.

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "nlohmann/json.hpp"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/utility/NonOwnerPointer.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include "feature/catalog/Variants.h"
#include "feature/catalog/CatalogInternal.h"
#include "feature/item/Item.h"
#include "feature/loot/LootInternal.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tags/JavaIds.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

#include "feature/item/sweep/Golden.inc"

using gf::selftest::Context;
namespace items = gf::game::items;

constexpr int  kDx      = 5960;
constexpr char kPrefix[] = "gloamingforest:";

// ---- reviewed exceptions -----------------------------------------------------------------------------

/// Blocks whose drops differ from upstream's for a reason reviewed here: "<block id>" (every tool) or
/// "<block id>@<tool>" (0 hand, 1 shears, 2 silk touch) -> the reason.
std::map<std::string, std::string> const& blockExceptions() {
    // Not a gap: the oracle rolled the table without a block entity. upstream's AbstractSkullCandleBlock#getDrops
    // adds the candles of its block entity (candles x the candle colour) to the head, and with silk touch
    // replaces the head by the skull candle itself, which is what the port drops.
    static constexpr char kOracleCandle[] =
        "the oracle's context has no block entity: upstream's getDrops adds the candles (silk touch: the skull candle itself), as the port does";
    static std::map<std::string, std::string> const m{
        {"gloamingforest:creeper_skull_candle", kOracleCandle},
        {"gloamingforest:creeper_wall_skull_candle", kOracleCandle},
        {"gloamingforest:piglin_skull_candle", kOracleCandle},
        {"gloamingforest:piglin_wall_skull_candle", kOracleCandle},
        {"gloamingforest:player_skull_candle", kOracleCandle},
        {"gloamingforest:player_wall_skull_candle", kOracleCandle},
        {"gloamingforest:skeleton_skull_candle", kOracleCandle},
        {"gloamingforest:skeleton_wall_skull_candle", kOracleCandle},
        {"gloamingforest:wither_skeleton_skull_candle", kOracleCandle},
        {"gloamingforest:wither_skeleton_wall_skull_candle", kOracleCandle},
        {"gloamingforest:zombie_skull_candle", kOracleCandle},
        {"gloamingforest:zombie_wall_skull_candle", kOracleCandle},
    };
    return m;
}

/// Recipes the game cannot craft as upstream does, reviewed: recipe id -> the reason.
std::map<std::string, std::string> const& recipeExceptions() {
    static std::map<std::string, std::string> const m{
        // F1's recipe import keeps Java's minecraft:stone_slab (Bedrock: minecraft:normal_stone_slab, tags/JavaIds.cpp);
        // the BP recipes nagastone_spiral_{0..7} ask for Bedrock's minecraft:stone_slab, which is not Java's stone slab
        {"gloamingforest:nagastone/nagastone_spiral",
         "F1's recipe import does not rename Java's minecraft:stone_slab to Bedrock's minecraft:normal_stone_slab: the game's recipe wants another slab"},
    };
    return m;
}

// ---- helpers -------------------------------------------------------------------------------------------

/// The Bedrock ids of a Java item id of upstream's data (renamed vanilla items: tags/JavaIds.h).
std::vector<std::string> bedrockItems(std::string const& javaId) { return gf::tags::javaids::bedrock("item", javaId); }

/// The first alternative of an ingredient ("a|b|c") that is an item of the game.
std::optional<ItemStack> firstUsable(std::string_view alternatives) {
    std::size_t start = 0;
    while (start <= alternatives.size()) {
        auto end = alternatives.find('|', start);
        if (end == std::string_view::npos) end = alternatives.size();
        std::string javaId(alternatives.substr(start, end - start));
        if (!javaId.empty())
            for (auto const& id : bedrockItems(javaId))
                if (auto s = items::make(id); s && items::idOf(*s) == id) return s;
        start = end + 1;
    }
    return std::nullopt;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t start = 0;
    while (true) {
        auto end = s.find(sep, start);
        out.emplace_back(s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

/// The tool of an oracle block context: 0 an empty hand, 1 shears, 2 a silk touch diamond pickaxe.
ItemStack toolStack(int tool) {
    if (tool == 1) return items::make("minecraft:shears").value_or(ItemStack::EMPTY_ITEM());
    if (tool == 2) {
        auto pick  = items::make("minecraft:diamond_pickaxe").value_or(ItemStack::EMPTY_ITEM());
        auto& names = Enchant::mEnchantNameToType();
        if (auto it = names.find(HashedString("silk_touch")); it != names.end()) {
            EnchantmentInstance inst;
            inst.mEnchantType = it->second;
            inst.mLevel       = 1;
            EnchantUtils::applyEnchant(pick, inst, true);
        }
        return pick;
    }
    return ItemStack::EMPTY_ITEM();
}
char const* toolName(int tool) { return tool == 0 ? "an empty hand" : tool == 1 ? "shears" : "a silk touch pickaxe"; }

struct Expected {
    std::string              javaId;
    std::vector<std::string> ids; ///< its Bedrock ids (a renamed or split Java id has more than one)
    int                      present = 0, min = 0, max = 0;
};

/// The oracle's rows by table (indexed once).
std::map<std::string, std::vector<GoldenDrop const*>> const& dropsByTable() {
    static std::map<std::string, std::vector<GoldenDrop const*>> const m = [] {
        std::map<std::string, std::vector<GoldenDrop const*>> out;
        for (auto const& d : kGoldenDrops) out[d.table].push_back(&d);
        return out;
    }();
    return m;
}

/// The oracle's statistics of `table` for `tool`, one per Java item.
std::vector<Expected> expectedOf(std::string const& table, int tool) {
    std::vector<Expected> out;
    auto                  it = dropsByTable().find(table);
    if (it == dropsByTable().end()) return out;
    for (auto const* d : it->second)
        if (d->tool == tool) out.push_back({d->item, bedrockItems(d->item), d->present, d->min, d->max});
    return out;
}

std::set<std::string> const& goldenTables() {
    static std::set<std::string> const s(std::begin(kGoldenBlockTables), std::end(kGoldenBlockTables));
    return s;
}

/// Compares one break with the oracle; returns the differences ("" when it agrees).
std::string compare(std::string const& table, int tool, std::map<std::string, int> const& got) {
    std::string diff;
    auto        add = [&](std::string const& s) { diff += (diff.empty() ? "" : "; ") + s; };
    if (!goldenTables().contains(table)) {
        for (auto const& [id, n] : got) add("dropped " + std::to_string(n) + " " + id + " (upstream has no loot table: nothing)");
        return diff;
    }
    auto want = expectedOf(table, tool);
    for (auto const& [id, n] : got) {
        auto it = std::find_if(want.begin(), want.end(), [&](Expected const& e) { return std::find(e.ids.begin(), e.ids.end(), id) != e.ids.end(); });
        if (it == want.end()) add("dropped " + std::to_string(n) + " " + id + " (upstream never drops it)");
        else if (n < it->min || n > it->max) add("dropped " + std::to_string(n) + " " + id + " (upstream drops " + std::to_string(it->min) + ".." + std::to_string(it->max) + ")");
    }
    for (auto const& e : want) {
        bool any = std::any_of(e.ids.begin(), e.ids.end(), [&](std::string const& id) { return got.contains(id); });
        if (e.present == kGoldenRolls && !any) add("no " + e.javaId + " (upstream always drops " + std::to_string(e.min) + ".." + std::to_string(e.max) + ")");
    }
    return diff;
}

/// The block in Java's default state (the oracle's block_state) where it is not Bedrock's default permutation.
Block const* javaDefault(std::string const& id) {
    nlohmann::json states = nlohmann::json::object();
    for (auto const& d : kJavaDefaults) {
        if (id != d.block) continue;
        std::string v(d.value);
        if (v == "true" || v == "false") states[d.state] = v == "true";
        else if (!v.empty() && std::all_of(v.begin(), v.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) states[d.state] = std::stoi(v);
        else states[d.state] = v;
    }
    if (states.empty()) return gf::game::blockWith(id);
    return gf::game::blockWithStates(id, states); // nullptr: Java's default state is not a state of the block (reported)
}

/// The owner of a block (docs/spec/blockstates.md), "?" when unknown.
std::string ownerOf(std::string const& id) {
    for (auto const& o : kBlockOwners)
        if (id == o.block) return o.owner;
    return "?";
}

/// Every registered block type of the gloamingforest namespace, sorted.
std::vector<std::string> tfBlocks() {
    std::vector<std::string> out;
    auto                     level = ll::service::getLevel();
    if (!level) return out;
    auto registry = level->getBlockTypeRegistry();
    registry.get().get()->forEachBlockType([&](BlockType const& type) {
        auto const& name = type.getTypeName();
        if (name.rfind(kPrefix, 0) == 0) out.push_back(name);
        return true;
    });
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// Reports a difference: a check failure, or an info line when it is a reviewed exception.
struct Reporter {
    Context&                                  ctx;
    std::map<std::string, std::string> const& exceptions;
    std::set<std::string>                     used;
    int                                       failed = 0, excused = 0;
    void report(std::string const& id, std::string const& keyWithTool, std::string const& message) {
        for (auto const& key : {keyWithTool, id}) {
            if (key.empty()) continue;
            if (auto it = exceptions.find(key); it != exceptions.end()) {
                used.insert(key);
                ++excused;
                ctx.info("reviewed exception " + key + ": " + message + " -- " + it->second);
                return;
            }
        }
        ++failed;
        ctx.check(false, message);
    }
    /// Exceptions that did not fail any more (stale: the gap is closed, drop the entry).
    void stale(std::set<std::string> const& covered) {
        for (auto const& [key, _] : exceptions)
            if (!used.contains(key) && covered.contains(key.substr(0, key.find('@'))))
                ctx.info("reviewed exception " + key + " no longer differs: remove it");
    }
};

} // namespace

// Every registered gloamingforest block, placed and broken with an empty hand, shears and a silk touch pickaxe:
// its drops against upstream's loot table (F13's oracle).
GF_REGISTER_SELFTEST("item.sweep_block_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto blocks = tfBlocks();
    ctx.require(blocks.size() >= 500, "the gloamingforest blocks are registered (got " + std::to_string(blocks.size()) + ")");
    auto pos = ctx.testPos(kDx, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test position loaded");
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    Reporter                rep{ctx, blockExceptions()};
    std::set<std::string>   covered(blocks.begin(), blocks.end());
    std::set<std::string>   tables;
    int                     breaks = 0, unplaceable = 0, withTable = 0;
    BlockPos                bp     = gf::game::blockPos(pos);
    std::string             before = ctx.blockAt(pos);
    GameType                gameType = ctx.player().getPlayerGameType();
    ctx.require(Enchant::mEnchantNameToType().find(HashedString("silk_touch")) != Enchant::mEnchantNameToType().end(),
                "the silk touch pickaxe is enchanted");
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        auto const& id    = blocks[i];
        std::string table = "gloamingforest:blocks/" + id.substr(15);
        if (goldenTables().contains(table)) ++withTable;
        tables.insert(table);
        Block const* block = javaDefault(id);
        for (int tool = 0; tool < 3; ++tool) {
            auto* region = ctx.region();
            ctx.require(region != nullptr, "region");
            if (!block || !gf::game::setBlock(*region, bp, *block) || gf::game::blockIdAt(*region, bp) != id) {
                ++unplaceable;
                rep.report(id, id + "@" + std::to_string(tool),
                           id + " cannot be placed in Java's default state for " + toolName(tool) + (block ? " (now " + gf::game::blockIdAt(*region, bp) + ")" : " (no such state)"));
                continue;
            }
            auto& player = ctx.player();
            player.setPlayerGameType(GameType::Survival);
            player.setCarriedItem(toolStack(tool));
            std::map<std::string, int> got;
            {
                gf::loot::DropCapture capture;
                region->getBlock(bp).playerDestroy(player, bp);
                for (auto raw : capture.ids()) {
                    ActorUniqueID uid;
                    uid.rawID = raw;
                    auto* a   = level->fetchEntity(uid, false);
                    if (!a || !a->isType(ActorType::ItemEntity)) continue;
                    auto const& stack = static_cast<ItemActor*>(a)->item();
                    if (!items::isEmpty(stack)) got[items::idOf(stack)] += items::countOf(stack);
                    a->remove();
                }
            }
            gf::game::clearBlock(*region, bp);
            ++breaks;
            if (auto diff = compare(table, tool, got); !diff.empty())
                rep.report(id, id + "@" + std::to_string(tool), id + " (" + ownerOf(id) + ") broken with " + std::string(toolName(tool)) + ": " + diff);
        }
        if (i % 20 == 19) co_await ctx.ticks(1);
    }
    ctx.player().setCarriedItem(ItemStack::EMPTY_ITEM());
    ctx.player().setPlayerGameType(gameType);
    if (!before.empty() && before != "minecraft:air") ctx.setBlock(pos, before);
    // upstream's block tables whose block is not registered (the oracle's tables of blocks; "_berries" and similar
    // tables of other loot uses are no blocks)
    int orphan = 0;
    for (auto const* t : kGoldenBlockTables) {
        std::string table(t);
        if (tables.contains(table)) continue;
        std::string id = std::string(kPrefix) + table.substr(22);
        if (gf::game::blockWith(id) == nullptr && id.find("_berries") == std::string::npos && ++orphan <= 20) ctx.info("upstream block table without a registered block: " + table);
    }
    rep.stale(covered);
    ctx.info(std::to_string(blocks.size()) + " blocks (" + std::to_string(withTable) + " with an upstream table), " + std::to_string(breaks) + " breaks, "
             + std::to_string(unplaceable) + " not placeable, " + std::to_string(rep.failed) + " differences, " + std::to_string(rep.excused) + " reviewed exceptions");
});

// Every recipe of upstream's data pack crafts its output in the game.
GF_REGISTER_SELFTEST("item.sweep_recipes", [](Context& ctx) {
    namespace rc = gf::recipes;
    auto level   = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    auto&                    recipes = level->getRecipes();
    auto const&              all     = recipes.mRecipes.get();
    CraftingContext          craftCtx(Bedrock::NotNullNonOwnerPtr<ILevel>(Bedrock::NonOwnerPointer<ILevel>(static_cast<ILevel&>(*level))));
    Reporter                 rep{ctx, recipeExceptions()};
    std::set<std::string>    covered;
    std::map<std::string, int> counted;

    auto tagged = [&](char const* tag) -> std::map<std::string, std::shared_ptr<Recipe>> const* {
        auto it = all.find(HashedString(tag));
        return it == all.end() ? nullptr : &it->second;
    };
    auto resultIs = [](Recipe const& r, std::vector<std::string> const& ids, int count) {
        auto const& res = r.getResultItems();
        if (res.empty() || res.front().isNull()) return false;
        auto id = res.front().getTypeName();
        return std::find(ids.begin(), ids.end(), id) != ids.end() && (count <= 0 || res.front().mCount == count);
    };

    for (auto const& g : kGoldenRecipes) {
        std::string id(g.id), type(g.type), serializer(g.serializer);
        covered.insert(id);
        ++counted[type];
        auto results = g.result[0] ? bedrockItems(g.result) : std::vector<std::string>{};
        auto ings    = split(g.ingredients, ';');
        if (type == "minecraft:crafting" && !g.special) {
            bool shaped = serializer == "minecraft:crafting_shaped";
            int  w = shaped ? g.width : 3;
            if (w <= 0 || w > 3) {
                rep.report(id, "", id + ": the golden shaped recipe has width " + std::to_string(w));
                continue;
            }
            CraftingContainer grid(3, 3);
            std::string       missing;
            for (std::size_t i = 0; i < ings.size(); ++i) {
                if (ings[i].empty()) continue;
                auto stack = firstUsable(ings[i]);
                if (!stack) {
                    missing = ings[i];
                    break;
                }
                int slot = shaped ? static_cast<int>(i / w) * 3 + static_cast<int>(i % w) : static_cast<int>(i);
                if (slot < 9) grid.setItem(slot, *stack);
            }
            if (!missing.empty()) {
                rep.report(id, "", id + ": no game item for the ingredient " + missing);
                continue;
            }
            bool matched = false, sameResult = false;
            if (auto* table = tagged("crafting_table"))
                for (auto const& [rid, r] : *table) {
                    if (!r || r->isMultiRecipe() || !resultIs(*r, results, 0)) continue;
                    sameResult = true;
                    if (r->matches(grid, craftCtx) && resultIs(*r, results, g.count)) {
                        matched = true;
                        break;
                    }
                }
            if (!matched)
                rep.report(id, "", id + ": no crafting table recipe of the game makes " + std::to_string(g.count) + " " + g.result + " from upstream's ingredients"
                                       + (sameResult ? " (recipes of that result exist but do not match the grid or the count)" : " (no recipe of that result)"));
        } else if (type == "minecraft:crafting" || type == "minecraft:smithing") {
            // code recipes: special crafting (or a BP recipe of the id with a special layout) or a data recipe a loader read
            if (!rc::internal::isSpecial(id) && !rc::internal::nativeById(id) && !rc::byId(id))
                rep.report(id, "", id + ": the code recipe (" + serializer + ") is neither registered as special crafting nor loaded");
        } else if (type == "minecraft:stonecutting") {
            auto input = ings.empty() ? std::nullopt : firstUsable(ings[0]);
            bool ok    = false;
            if (input)
                if (auto* table = tagged("stonecutter"))
                    for (auto const& [rid, r] : *table) {
                        if (!r || !resultIs(*r, results, g.count)) continue;
                        for (auto const& ri : r->mMyIngredients.get())
                            if (r->itemValidForRecipe(ri, *input)) ok = true;
                        if (ok) break;
                    }
            if (!ok) rep.report(id, "", id + ": no stonecutter recipe of the game makes " + std::to_string(g.count) + " " + g.result + " from " + (ings.empty() ? "?" : ings[0]));
        } else if (type == "minecraft:smelting" || type == "minecraft:blasting" || type == "minecraft:smoking" || type == "minecraft:campfire_cooking") {
            char const* tag = type == "minecraft:smelting" ? "furnace" : type == "minecraft:blasting" ? "blast_furnace" : type == "minecraft:smoking" ? "smoker" : "campfire";
            auto input      = ings.empty() ? std::nullopt : firstUsable(ings[0]);
            bool ok         = false;
            std::string got = "nothing";
            if (input) {
                auto out = recipes.getFurnaceRecipeResult(*input, HashedString(tag));
                if (!out.isNull()) {
                    got = std::to_string(out.mCount) + " " + out.getTypeName();
                    ok  = std::find(results.begin(), results.end(), out.getTypeName()) != results.end() && out.mCount == g.count;
                }
            }
            if (!ok) rep.report(id, "", id + ": the game's " + tag + " makes " + got + " from " + (ings.empty() ? "?" : ings[0]) + ", upstream " + std::to_string(g.count) + " " + g.result);
        } else {
            // upstream's custom types: their owner's loader read the data recipe
            auto const* r = rc::byId(id);
            if (!r) rep.report(id, "", id + ": the " + type + " recipe is not loaded");
            else if (r->type != type) rep.report(id, "", id + ": loaded as " + r->type + ", upstream " + type);
            else if (!results.empty() && std::find(results.begin(), results.end(), r->resultItem) == results.end())
                rep.report(id, "", id + ": makes " + r->resultItem + ", upstream " + g.result);
        }
    }
    rep.stale(covered);
    std::string summary;
    for (auto const& [type, n] : counted) summary += (summary.empty() ? "" : ", ") + type + " " + std::to_string(n);
    ctx.info(std::to_string(std::size(kGoldenRecipes)) + " recipes (" + summary + "): " + std::to_string(rep.failed) + " differences, " + std::to_string(rep.excused)
             + " reviewed exceptions");
});

// gf::catalog::addVariant: a second creative entry of an item, with data, right after the item's entry
// (TFCreativeTabs#createGlassSwordAndLoreVer); added once however often it is applied, removed again here.
GF_REGISTER_SELFTEST("item.catalog_variant", [](Context& ctx) {
    namespace cat              = gf::catalog;
    constexpr char kItem[]     = "gloamingforest:naga_scale";
    constexpr char kKey[]      = "gf_selftest";
    auto           before      = cat::internal::entriesInGroupOrder(kItem);
    ctx.require(!before.empty(), std::string(kItem) + " has a creative entry");
    cat::addVariant(kItem, kKey, [](ItemStack& s) { gf::item::setData(s, "gf_selftest_variant", true); }); // applied at once: a world is ready
    int again = cat::internal::applyVariants();
    auto after = cat::internal::entriesInGroupOrder(kItem);
    bool removed = cat::internal::removeVariant(kItem, kKey);
    auto restored = cat::internal::entriesInGroupOrder(kItem);
    ctx.check(again == 0, "the variant entry is not added twice (applied again: " + std::to_string(again) + " added)");
    ctx.check(after.size() == before.size() + 1, "one more entry of the item (" + std::to_string(before.size()) + " -> " + std::to_string(after.size()) + ")");
    bool next = false;
    for (std::size_t i = 0; i + 1 < after.size(); ++i)
        if (after[i].first == before.front().first && after[i + 1].second) next = true;
    ctx.check(next, "the variant (with data) follows the item's own entry in its group");
    ctx.check(removed && restored.size() == before.size(), "the test variant is removed again");
});
