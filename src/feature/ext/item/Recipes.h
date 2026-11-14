#pragma once

// Extension of contract "recipes" by F9 (rule R18; to be folded into feature/recipes/Recipes.h by A8).
//
// How a special (code-driven) crafting recipe reaches Bedrock's crafting table. The crafting screen
// of every client (with or without the client mod) only offers a craft that matches a BP recipe of
// the "crafting_table" tag: the client sends the BP recipe's net id with the grid, the server checks
// the grid against that recipe's ingredients (shaped: pattern and items; shapeless: exactly these
// items, one per ingredient), and only then F9's crafting hook asks the special recipe's matcher
// and assembler (CraftHandlerCrafting -> ItemStackRequestActionCraftHandler::_initCraftResults) and
// replaces the BP recipe's static result. So a special recipe is declared as BP recipe(s) whose
// identifier names it:
//
// - one layout: a BP recipe with the special recipe's id ("gloamingforest:casket_repair" as a
//   shapeless recipe of the casket and the materials);
// - a variable layout (upstream's map cloning: one filled map plus 1..8 empty maps; the scepter repairs:
//   any number of repair items): one BP recipe per layout, each with its own identifier
//   ("gloamingforest:magic_map_cloning_2" ... "_9"), mapped to the special recipe here. Bedrock's
//   shapeless match takes exactly the listed items, so each count is its own recipe; the result
//   item of each BP recipe is what clients preview (the assembler's stack replaces it).
//
// The BP recipe is the one the client crafted (the request's recipe net id), never the Java data
// recipe of the same id: upstream's data has code-only recipes with the special ids
// (gloamingforest:casket_repair_recipe, ...); they stay in the data index (byId / ofType) and do not
// hide the BP recipe from the crafting hook. When the request does not name its recipe, every BP
// recipe giving the crafted item that is a special recipe (or a layout of one) and whose ingredients
// fit the grid is a candidate, in id order, and the first whose matcher accepts the grid crafts (as
// Java's RecipeManager takes the first recipe whose matches() is true).
//
// The assembler sees the whole grid (CraftingGrid, the player's 2x2 or 3x3 UI container slots), so
// one matcher serves every layout. The remainder (getRemainingItems) goes back into the grid slot
// the craft emptied, as Java's ResultSlot#onTake.
//
//     gf::recipes::registerSpecialCrafting("gloamingforest:magic_map_cloning", match, assemble);
//     for (int n = 2; n <= 9; ++n)
//         gf::recipes::ext::addSpecialLayout("gloamingforest:magic_map_cloning", "gloamingforest:magic_map_cloning_" + std::to_string(n));
//
// Recipe conditions: a data recipe with "neoforge:conditions" loads only when every condition
// holds, as on NeoForge (a failing one is skipped without a warning). Built in: neoforge:true,
// false, not, and, or, mod_loaded (minecraft, neoforge and gloamingforest are loaded; jeed and
// every other mod are not), item_exists (vanilla, upstream and fixture ids), tag_empty, and upstream's
// gloamingforest:uncrafting_table_enabled (upstream's default, enabled, until its owner registers the
// test of GloamingConfig.disableEntireTable). A registration re-evaluates the data at the next query.
//
//     gf::recipes::ext::registerCondition("gloamingforest:uncrafting_table_enabled",
//                                         [](nlohmann::json const&) { return !config().disableEntireTable; });
//
// Java ids: every data recipe (upstream's data and the units' bundles data/gf/recipes/<unit>.json) is
// Java data and names vanilla items by their Java id (minecraft:cobweb, lily_pad, slime_block,
// item_frame, dead_bush, chain, stone_slab, tipped_arrow). The built-in loaders hold the game's
// Bedrock ids in `ingredients` (every Bedrock id of a renamed item: minecraft:web) and `resultItem`
// (the main one: minecraft:deadbush, minecraft:iron_chain), so producing(), ofType() and matching
// work with Bedrock stacks; `data` keeps the file's JSON (Java ids) as it is. An owner's loader
// (registerType) parses the JSON itself: it uses parseIngredient / itemId below to get the same ids.
// Bedrock spells a few Java items as another item with a data value: minecraft:tipped_arrow is
// minecraft:arrow (the potion is the aux value; upstream's uncrafting recipe "tipped_arrow" so lists
// minecraft:arrow as its input, and its owner checks the stack's potion), and NeoForge's
// "components" ingredients (upstream's potions) keep their components in `data` only.
//
//     auto r = gf::recipes::CraftingRecipe{};
//     r.ingredients.push_back(gf::recipes::ext::parseIngredient(json["input"]));
//     r.resultItem = gf::recipes::ext::itemId(json["result"]);

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "feature/recipes/Recipes.h"

namespace gf::recipes::ext {

/// Crafts with BP recipe `bpRecipeId` run special recipe `specialId` (registerSpecialCrafting).
void addSpecialLayout(std::string const& specialId, std::string const& bpRecipeId);

/// The test of a recipe condition type: gets the condition object ({"type": ..., ...}).
using ConditionFn = std::function<bool(nlohmann::json const& condition)>;
/// Registers (or replaces) a recipe condition type; recipes are evaluated again at the next query.
void registerCondition(std::string const& typeId, ConditionFn test);

/// A Java ingredient of the data ({"item"}, {"tag"}, lists, "#tag", NeoForge's compound "children",
/// "items" and "ingredient") with the game's item ids, as the built-in loaders parse it.
[[nodiscard]] Ingredient parseIngredient(nlohmann::json const& ingredient);
/// A Java item id or result ("minecraft:dead_bush", {"id": ...}, {"item": ...}) as the game's item
/// id ("minecraft:deadbush"); "" for anything else.
[[nodiscard]] std::string itemId(nlohmann::json const& result);

} // namespace gf::recipes::ext
