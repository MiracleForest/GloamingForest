// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of the uncrafting table's rules (U13): UncraftingMenu's costs, damaged parts, recipe
// filters, grid layout and the UncraftingGuiPacket counters (upstream commit 9e40a757,
// LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include "core/uncraftmaps/Uncrafting.h"

using namespace gf::core::uncraftmaps;

TEST(Uncrafting, FloorModAndRound) {
    EXPECT_EQ(floorMod(5, 3), 2);
    EXPECT_EQ(floorMod(-1, 3), 2);
    EXPECT_EQ(floorMod(-3, 3), 0);
    EXPECT_EQ(floorMod(7, 1), 0);
    EXPECT_EQ(floorMod(4, 0), 0);
    EXPECT_EQ(javaRound(2.5), 3);
    EXPECT_EQ(javaRound(-2.5), -2);
    EXPECT_EQ(javaRound(1.49), 1);
}

TEST(Uncrafting, RecipeFilterIsAnXorOfBothLists) {
    RecipeFilter f;
    f.recipeIds = {"gloamingforest:giant_log_to_oak_planks"};
    EXPECT_FALSE(recipeAllowed(f, "gloamingforest:giant_log_to_oak_planks"));
    EXPECT_TRUE(recipeAllowed(f, "minecraft:iron_pickaxe"));
    f.flipRecipeList = true; // a whitelist
    EXPECT_TRUE(recipeAllowed(f, "gloamingforest:giant_log_to_oak_planks"));
    EXPECT_FALSE(recipeAllowed(f, "minecraft:iron_pickaxe"));
    f = {};
    f.modIds = {"gloamingforest"};
    EXPECT_FALSE(recipeAllowed(f, "gloamingforest:naga_chestplate"));
    EXPECT_TRUE(recipeAllowed(f, "minecraft:iron_pickaxe"));
    EXPECT_FALSE(recipeAllowed(RecipeFilter{{}, false, {"minecraft"}, false}, "iron_pickaxe")); // no namespace: minecraft
    f.flipIdList = true;
    EXPECT_TRUE(recipeAllowed(f, "gloamingforest:naga_chestplate"));
    EXPECT_FALSE(recipeAllowed(f, "minecraft:iron_pickaxe"));
}

TEST(Uncrafting, DamagedPartsUseAtLeastFour) {
    // an iron pickaxe: 3 iron (damageable) + 2 sticks (#uncrafting_ignores_cost) -> 3 damageable parts
    EXPECT_EQ(damagedParts(3, 0, 250), 0);
    EXPECT_EQ(damagedParts(3, 1, 250), 1);   // ceil(4 * 0.004)
    EXPECT_EQ(damagedParts(3, 125, 250), 2); // ceil(4 * 0.5)
    EXPECT_EQ(damagedParts(3, 249, 250), 4);
    EXPECT_EQ(damagedParts(8, 125, 250), 4);
    EXPECT_EQ(damagedParts(8, 0, 0), 0);
}

TEST(Uncrafting, UncraftingCost) {
    UncraftingCostInput in;
    in.hasRecipe       = true;
    in.damageableParts = 3;
    EXPECT_EQ(uncraftingCost(in), 3); // the iron pickaxe
    in.multiplier = 1.5;
    EXPECT_EQ(uncraftingCost(in), 5); // round(4.5)
    in.multiplier    = 0;
    EXPECT_EQ(uncraftingCost(in), 0);
    in.multiplier    = 1;
    in.assemblyEmpty = false;
    EXPECT_EQ(uncraftingCost(in), 0);
    in.assemblyEmpty     = true;
    in.disableUncrafting = true;
    EXPECT_EQ(uncraftingCost(in), 0);
    in.uncraftingRecipe = true; // special recipes still work
    in.recipeCost       = 4;     // tipped arrows
    EXPECT_EQ(uncraftingCost(in), 4);
    in.recipeCost = 0; // the written book
    EXPECT_EQ(uncraftingCost(in), 0);
}

TEST(Uncrafting, EnchantmentWeights) {
    EXPECT_EQ(weightModifier(1), 8);
    EXPECT_EQ(weightModifier(2), 4);
    EXPECT_EQ(weightModifier(3), 2);
    EXPECT_EQ(weightModifier(5), 2);
    EXPECT_EQ(weightModifier(10), 1);
    EXPECT_EQ(vanillaEnchantmentWeight("minecraft:sharpness"), 10);
    EXPECT_EQ(vanillaEnchantmentWeight("minecraft:mending"), 2);
    EXPECT_EQ(vanillaEnchantmentWeight("minecraft:silk_touch"), 1);
    EXPECT_EQ(vanillaEnchantmentWeight("minecraft:unbreaking"), 5);
    EXPECT_EQ(vanillaEnchantmentWeight("gloamingforest:fire_react"), 0);
    // sharpness III (weight 10: x1) and unbreaking II (5: x2): 3 + 1 + 4 + 1
    EXPECT_EQ(totalEnchantmentCost({{10, 3}, {5, 2}}), 9);
    EXPECT_EQ(totalEnchantmentCost({{1, 0}}), 0);
}

TEST(Uncrafting, RecraftingCost) {
    RecraftingCostInput in;
    in.sameItem = true;
    EXPECT_EQ(recraftingCost(in), 1); // at least 1
    in.inputEnchantments  = {{10, 3}, {5, 2}};
    in.resultEnchantments = 2;
    in.damagedParts       = 1;
    EXPECT_EQ(recraftingCost(in), 9 + 2 * 2);
    in.sameItem       = false;
    in.filledAssembly = 5;
    EXPECT_EQ(recraftingCost(in), 5 + 9 + 4);
    in.multiplier = 0.5;
    EXPECT_EQ(recraftingCost(in), 9); // round(18 * 0.5)
    in.outputEnchantable = false;
    EXPECT_EQ(recraftingCost(in), 0);
    in.outputEnchantable = true;
    in.outputPresent     = false;
    EXPECT_EQ(recraftingCost(in), 0);
}

TEST(Uncrafting, GridLayout) {
    // a 1 x 2 shaped recipe (a stick) goes into slots 0 and 3
    EXPECT_EQ(gridLayout(true, 1, 2, 2), (std::vector<int>{0, 3}));
    // a 2 x 2 pattern
    EXPECT_EQ(gridLayout(true, 2, 2, 4), (std::vector<int>{0, 1, 3, 4}));
    // the pickaxe
    EXPECT_EQ(gridLayout(true, 3, 3, 9), (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8}));
    // shapeless: in order
    EXPECT_EQ(gridLayout(false, 0, 0, 3), (std::vector<int>{0, 1, 2}));
}

TEST(Uncrafting, GuiButtons) {
    Cycles c;
    EXPECT_TRUE(applyGuiButton(c, 0, false));
    EXPECT_TRUE(applyGuiButton(c, 0, false));
    EXPECT_TRUE(applyGuiButton(c, 1, false));
    EXPECT_EQ(c.unrecipe, 1);
    EXPECT_TRUE(applyGuiButton(c, 3, false));
    EXPECT_EQ(c.ingredients, -1);
    EXPECT_TRUE(applyGuiButton(c, 2, true)); // switching disabled: no change
    EXPECT_EQ(c.ingredients, -1);
    EXPECT_TRUE(applyGuiButton(c, 4, false));
    EXPECT_TRUE(applyGuiButton(c, 5, false));
    EXPECT_TRUE(applyGuiButton(c, 5, false));
    EXPECT_EQ(c.recipe, -1);
    EXPECT_FALSE(applyGuiButton(c, 6, false));
}
