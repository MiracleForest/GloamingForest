#pragma once

// Internals of the recipe runtime shared with F9's selftests.

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "feature/recipes/Recipes.h"

class ItemStack;
class Player;

namespace gf::recipes::internal {

/// Parses a Java ingredient ({"item"}, {"tag"}, arrays, NeoForge compound "children", "#tag").
[[nodiscard]] Ingredient parseIngredient(Json const& j);
/// Whether a stack id fits an ingredient (tags through gf::tags).
[[nodiscard]] bool matches(Ingredient const& ingredient, std::string const& itemId);

/// The crafting grid of a player's open crafting screen (2x2 inventory grid or 3x3 table) as the
/// crafting hook sees it.
struct PlayerGrid {
    std::vector<ItemStack> stacks;        ///< row-major copies
    CraftingGrid           grid;          ///< points into `stacks`
    int                    firstSlot = 0; ///< the PlayerUIContainer slot of the grid's first stack
};
[[nodiscard]] PlayerGrid gridOf(Player& player, bool workbench);

/// What the special crafting `recipeId` (or a layout of ext::addSpecialLayout) makes from `grid`
/// (nullopt: not special, or the matcher refused). The same path as the crafting hook.
[[nodiscard]] std::optional<ItemStack> assembleSpecial(std::string const& recipeId, CraftingGrid const& grid);
[[nodiscard]] bool                     isSpecial(std::string const& recipeId);
/// Whether any special recipe is registered.
[[nodiscard]] bool anySpecial();
/// The BP recipes of the game that give `resultItemId`, are special recipes or layouts of one
/// (ext/item/Recipes.h) and whose ingredients fit `grid` (the server checked the crafted recipe
/// against the grid), in id order. Only the game's own recipes count (never a data recipe of the
/// same id).
[[nodiscard]] std::vector<std::string> specialCandidates(std::string const& resultItemId, CraftingGrid const& grid);
/// The crafting hook's choice: `bpRecipeId` (the recipe the request crafted, "" when unknown) when
/// it is special, else the first of specialCandidates whose matcher accepts `grid`; "" for none.
/// `refused`: the named recipe is special but its matcher refused the grid.
[[nodiscard]] std::string resolveSpecial(std::string const& bpRecipeId, std::string const& resultItemId, CraftingGrid const& grid, bool* refused = nullptr);
/// Whether `grid` holds the ingredients of a recipe (shaped: the pattern anywhere in the grid,
/// mirrored too, nothing else; shapeless: exactly one stack per ingredient).
[[nodiscard]] bool fits(CraftingRecipe const& recipe, CraftingGrid const& grid);
/// The remainder of a special recipe for `grid` (empty stacks dropped).
[[nodiscard]] std::vector<ItemStack> remainderOf(std::string const& recipeId, CraftingGrid const& grid);
/// The remainder of each grid slot (getRemainingItems; empty stacks kept, one per slot).
[[nodiscard]] std::vector<ItemStack> remaindersBySlot(std::string const& recipeId, CraftingGrid const& grid);
/// Java's ResultSlot#onTake for the remainders (one per slot from `firstSlot` of the player's UI
/// container): into the emptied grid slot, onto the same item, else the inventory / the ground;
/// the client gets the grid slots.
void placeRemainders(Player& player, int firstSlot, std::vector<ItemStack> const& remainders);
/// How many special crafts a player made through the crafting hook this session (selftests).
[[nodiscard]] int specialCraftCount(std::int64_t playerUid);
/// How the hook found the special recipe of the player's last special craft: "request" (the
/// request's recipe net id) or "result" (the candidates of the crafted item); "" before any.
[[nodiscard]] std::string lastSpecialRoute(std::int64_t playerUid);
/// Removes a special crafting registration (selftests).
void unregisterSpecial(std::string const& recipeId);

/// Recipes loaded from the data tree / converted from the game.
[[nodiscard]] std::size_t dataRecipeCount();
[[nodiscard]] std::size_t nativeRecipeCount();
/// Converts the game's crafting table recipes again (after a world load).
void refreshNative();
/// The game's own recipe of an id (the BP / vanilla recipe, never the data's), nullptr: none.
[[nodiscard]] CraftingRecipe const* nativeById(std::string const& recipeId);

/// Selftests: a data recipe added to (Json() removes it from) the data tree, as if a file of that
/// id existed; the data is parsed again.
void setTestDataRecipe(std::string const& id, Json const& recipe);
/// Selftests: a stand-in for a BP recipe of the game (added to the game's recipes the runtime
/// indexes; `remove` takes it out again).
void setTestNativeRecipe(CraftingRecipe const& recipe, bool remove = false);
/// Selftests: removes a condition type registered with ext::registerCondition (the built-in one,
/// if any, comes back).
void unregisterCondition(std::string const& typeId);
/// Whether a NeoForge condition list holds (recipes' "neoforge:conditions").
[[nodiscard]] bool conditionsHold(Json const& conditions);
/// The recipe types the data had recipes of without a loader (each warned once).
[[nodiscard]] std::set<std::string> typesWithoutLoader();

} // namespace gf::recipes::internal
