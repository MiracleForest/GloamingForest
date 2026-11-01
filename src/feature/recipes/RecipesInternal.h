#pragma once

// Internals of the recipe runtime shared with F9's selftests.

#include <optional>
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
/// The remainder of a special recipe for `grid` (empty stacks dropped).
[[nodiscard]] std::vector<ItemStack> remainderOf(std::string const& recipeId, CraftingGrid const& grid);
/// The remainder of each grid slot (getRemainingItems; empty stacks kept, one per slot).
[[nodiscard]] std::vector<ItemStack> remaindersBySlot(std::string const& recipeId, CraftingGrid const& grid);
/// Java's ResultSlot#onTake for the remainders (one per slot from `firstSlot` of the player's UI
/// container): into the emptied grid slot, onto the same item, else the inventory / the ground;
/// the client gets the grid slots.
void placeRemainders(Player& player, int firstSlot, std::vector<ItemStack> const& remainders);
/// Removes a special crafting registration (selftests).
void unregisterSpecial(std::string const& recipeId);

/// Recipes loaded from the data tree / converted from the game.
[[nodiscard]] std::size_t dataRecipeCount();
[[nodiscard]] std::size_t nativeRecipeCount();
/// Converts the game's crafting table recipes again (after a world load).
void refreshNative();

} // namespace gf::recipes::internal
