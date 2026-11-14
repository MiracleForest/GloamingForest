// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/inventory/UncraftingMenu.java
//   src/main/java/gloamingforest/network/UncraftingGuiPacket.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/uncraftmaps/Uncrafting.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_map>

namespace gf::core::uncraftmaps {

int floorMod(int x, int n) {
    if (n == 0) return 0;
    int m = x % n;
    return (m != 0 && ((m < 0) != (n < 0))) ? m + n : m;
}

int javaRound(double x) {
    if (std::isnan(x)) return 0;
    double r = std::floor(x + 0.5);
    if (r >= static_cast<double>(INT_MAX)) return INT_MAX;
    if (r <= static_cast<double>(INT_MIN)) return INT_MIN;
    return static_cast<int>(r);
}

std::string namespaceOf(std::string_view id) {
    auto colon = id.find(':');
    return colon == std::string_view::npos ? std::string("minecraft") : std::string(id.substr(0, colon));
}

bool recipeAllowed(RecipeFilter const& f, std::string_view recipeId) {
    bool listed = std::find(f.recipeIds.begin(), f.recipeIds.end(), recipeId) != f.recipeIds.end();
    if (f.flipRecipeList != listed) return false;
    auto ns       = namespaceOf(recipeId);
    bool modListed = std::find(f.modIds.begin(), f.modIds.end(), ns) != f.modIds.end();
    return f.flipIdList == modListed;
}

int damagedParts(int damageableParts, int damage, int maxDamage) {
    if (maxDamage <= 0) return 0;
    int   totalMax4 = std::max(4, damageableParts);
    float fraction  = static_cast<float>(damage) / static_cast<float>(maxDamage);
    return static_cast<int>(std::ceil(static_cast<float>(totalMax4) * fraction));
}

int uncraftingCost(UncraftingCostInput const& in) {
    if ((!in.disableUncrafting || in.uncraftingRecipe) && in.assemblyEmpty) {
        if (in.uncraftingRecipe) return in.recipeCost;
        return javaRound(static_cast<double>(in.damageableParts) * in.multiplier);
    }
    return 0;
}

int weightModifier(int weight) {
    switch (weight) {
    case 1: return 8;
    case 2: return 4;
    case 3:
    case 4:
    case 5: return 2;
    default: return 1;
    }
}

int vanillaEnchantmentWeight(std::string_view id) {
    // data/minecraft/enchantment/*.json of Minecraft 1.21.1 ("weight")
    static std::unordered_map<std::string_view, int> const weights{
        {"minecraft:aqua_affinity", 2},      {"minecraft:bane_of_arthropods", 5}, {"minecraft:binding_curse", 1},
        {"minecraft:blast_protection", 2},   {"minecraft:breach", 2},             {"minecraft:channeling", 1},
        {"minecraft:density", 5},            {"minecraft:depth_strider", 2},      {"minecraft:efficiency", 10},
        {"minecraft:feather_falling", 5},    {"minecraft:fire_aspect", 2},        {"minecraft:fire_protection", 5},
        {"minecraft:flame", 2},              {"minecraft:fortune", 2},            {"minecraft:frost_walker", 2},
        {"minecraft:impaling", 2},           {"minecraft:infinity", 1},           {"minecraft:knockback", 5},
        {"minecraft:looting", 2},            {"minecraft:loyalty", 5},            {"minecraft:luck_of_the_sea", 2},
        {"minecraft:lure", 2},               {"minecraft:mending", 2},            {"minecraft:multishot", 2},
        {"minecraft:piercing", 10},          {"minecraft:power", 10},             {"minecraft:projectile_protection", 5},
        {"minecraft:protection", 10},        {"minecraft:punch", 2},              {"minecraft:quick_charge", 5},
        {"minecraft:respiration", 2},        {"minecraft:riptide", 2},            {"minecraft:sharpness", 10},
        {"minecraft:silk_touch", 1},         {"minecraft:smite", 5},              {"minecraft:soul_speed", 1},
        {"minecraft:sweeping_edge", 2},      {"minecraft:swift_sneak", 1},        {"minecraft:thorns", 1},
        {"minecraft:unbreaking", 5},         {"minecraft:vanishing_curse", 1},    {"minecraft:wind_burst", 2},
    };
    auto it = weights.find(id);
    return it == weights.end() ? 0 : it->second;
}

int totalEnchantmentCost(std::vector<EnchantmentLevel> const& enchantments) {
    int count = 0;
    for (auto const& e : enchantments) {
        if (e.level > 0) {
            count += weightModifier(e.weight) * e.level;
            count += 1;
        }
    }
    return count;
}

int recraftingCost(RecraftingCostInput const& in) {
    if (!in.inputPresent || !in.outputPresent) return 0;
    if (!in.outputEnchantable) return 0;
    int cost = 0;
    if (!in.sameItem) cost += in.filledAssembly;
    cost += totalEnchantmentCost(in.inputEnchantments);
    cost += (1 + in.damagedParts) * in.resultEnchantments;
    cost = std::max(1, cost);
    return javaRound(static_cast<double>(cost) * in.multiplier);
}

std::vector<int> gridLayout(bool shaped, int width, int height, int ingredientCount) {
    std::vector<int> out(static_cast<std::size_t>(std::max(0, ingredientCount)), -1);
    if (shaped) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int index = x + y * width;
                if (index >= ingredientCount) continue;
                if (x < 3 && y < 3) out[static_cast<std::size_t>(index)] = x + y * 3;
            }
    } else {
        for (int i = 0; i < 9 && i < ingredientCount; ++i) out[static_cast<std::size_t>(i)] = i;
    }
    return out;
}

bool applyGuiButton(Cycles& c, int type, bool ingredientSwitchingDisabled) {
    switch (type) {
    case 0: ++c.unrecipe; return true;
    case 1: --c.unrecipe; return true;
    case 2:
        if (!ingredientSwitchingDisabled) ++c.ingredients;
        return true;
    case 3:
        if (!ingredientSwitchingDisabled) --c.ingredients;
        return true;
    case 4: ++c.recipe; return true;
    case 5: --c.recipe; return true;
    default: return false;
    }
}

} // namespace gf::core::uncraftmaps
