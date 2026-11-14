// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/inventory/UncraftingMenu.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent rules of the uncrafting table (U13): the recipe filters of the config, the
// costs, the damaged-part count and the enchantment weights. The menu (feature/uncraftmaps) feeds
// them what it reads from the game.

#include <string>
#include <string_view>
#include <vector>

namespace gf::core::uncraftmaps {

/// Java's Math.floorMod for ints.
[[nodiscard]] int floorMod(int x, int n);
/// Java's (int) Math.round(double): floor(x + 0.5), saturated to int.
[[nodiscard]] int javaRound(double x);

/// The config's two id lists (GloamingConfig): the recipe id list (disableUncraftingRecipes, a blacklist,
/// a whitelist with flipRecipeList) and the namespace list (blacklistedUncraftingModIds, inverted by
/// flipIdList). getRecipesFor keeps a recipe when
/// `flipRecipe == recipeIds.contains(id)` and `flipIds == modIds.contains(namespace)`.
struct RecipeFilter {
    std::vector<std::string> recipeIds;
    bool                     flipRecipeList = false;
    std::vector<std::string> modIds;
    bool                     flipIdList = false;
};
[[nodiscard]] bool recipeAllowed(RecipeFilter const& filter, std::string_view recipeId);
/// The namespace of an id ("minecraft" for an id without one, as ResourceLocation).
[[nodiscard]] std::string namespaceOf(std::string_view id);

/// UncraftingMenu#countDamagedParts: ceil(max(4, damageableParts) * damage / maxDamage).
[[nodiscard]] int damagedParts(int damageableParts, int damage, int maxDamage);

/// UncraftingMenu#calculateUncraftingCost: 0 unless the assembly grid is empty and uncrafting is
/// allowed (not disableUncrafting, or an uncrafting recipe); an uncrafting recipe costs its `cost`,
/// a reversed crafting recipe round(damageableParts * multiplier).
struct UncraftingCostInput {
    bool   assemblyEmpty      = true;
    bool   uncraftingRecipe   = false; ///< storedGhostRecipe instanceof UncraftingRecipe
    bool   hasRecipe          = false; ///< storedGhostRecipe != null
    int    recipeCost         = 0;     ///< UncraftingRecipe#getCost (-1 when the JSON has none)
    int    damageableParts    = 0;     ///< countDamageableParts(uncraftingMatrix)
    double multiplier         = 1.0;   ///< uncraftingXpCostMultiplier
    bool   disableUncrafting  = false; ///< disableUncraftingOnly
};
[[nodiscard]] int uncraftingCost(UncraftingCostInput const& in);

/// UncraftingMenu#getWeightModifier: weight 1 -> 8, 2 -> 4, 3..5 -> 2, else 1.
[[nodiscard]] int weightModifier(int weight);
/// Java 1.21.1's weight of a vanilla enchantment ("minecraft:sharpness" -> 10); 0 when unknown.
[[nodiscard]] int vanillaEnchantmentWeight(std::string_view id);

struct EnchantmentLevel {
    int weight = 1;
    int level  = 1;
};
/// UncraftingMenu#countTotalEnchantmentCost: sum over levels > 0 of weightModifier * level + 1.
[[nodiscard]] int totalEnchantmentCost(std::vector<EnchantmentLevel> const& enchantments);

/// UncraftingMenu#calculateRecraftingCost (input and output present and the output enchantable):
/// max(1, (different item ? filled assembly slots : 0) + enchantment cost + (1 + damaged parts) *
/// result enchantment count), times the multiplier, rounded.
struct RecraftingCostInput {
    bool                          inputPresent      = true;
    bool                          outputPresent     = true;
    bool                          outputEnchantable = true;
    bool                          sameItem          = false;
    int                           filledAssembly    = 0;
    std::vector<EnchantmentLevel> inputEnchantments;
    int                           damagedParts      = 0;
    int                           resultEnchantments = 0;
    double                        multiplier        = 1.0; ///< repairingXpCostMultiplier
};
[[nodiscard]] int recraftingCost(RecraftingCostInput const& in);

/// The uncrafting grid layout of a recipe: ingredient index -> grid slot (0..8, -1 when outside),
/// as UncraftingMenu#slotsChanged lays a shaped recipe (width x height) or a shapeless list out.
[[nodiscard]] std::vector<int> gridLayout(bool shaped, int width, int height, int ingredientCount);

/// The cycle button of UncraftingGuiPacket: 0/1 next/previous uncraft recipe, 2/3 next/previous
/// ingredient (not while ingredient switching is disabled), 4/5 next/previous recraft recipe.
/// Applies it to the three counters (false: unknown type). Types below 4 recompute the uncrafting
/// side (slotsChanged(tinkerInput)), 4 and 5 the assembly side.
struct Cycles {
    int unrecipe    = 0;
    int ingredients = 0;
    int recipe      = 0;
};
bool applyGuiButton(Cycles& cycles, int type, bool ingredientSwitchingDisabled);

} // namespace gf::core::uncraftmaps
