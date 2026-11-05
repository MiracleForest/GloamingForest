// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/recipe/travellers/TravellersGearModifierRecipe.java
//   src/main/java/twilightforest/item/recipe/travellers/TravellersGearModifierShapedRecipe.java
//   src/main/java/twilightforest/item/recipe/travellers/TravellersGearModifierShapelessRecipe.java
//   src/main/java/twilightforest/item/recipe/travellers/TravellersVestGlovesMergeRecipe.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java (fireCraftingModifierTrigger)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's three recipe serializers as special crafting (feature/recipes/Recipes.h, ext/item/Recipes.h):
// every data recipe of the travellers modifier types (24, in the imported data) is a special recipe
// of the same id whose shape is the BP recipe of that id (assets/pack/bp/recipes/tgear, one more
// BP recipe "<id>_<n>" per potion of a potion ingredient: Bedrock recipes take one item + data value
// per key). The matcher is TravellersGearModifierRecipe#matches (a free insertable slot, not the
// modifier yet, at most one ingredient carrying the transferable container), the assembler
// applyModifier (transfer, else add). The vest / gloves merge is the BP shapeless recipe of the vest
// and the gloves.
//
// Java's ItemCraftedEvent fires add_modifier per new modifier with the player; Bedrock's crafting hook
// gives the assembler no player, so the result carries the new ids ("gf:tgear.crafted") and the
// first inventory change that sees the stack fires the triggers and drops the mark.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/ext/item/Recipes.h"
#include "feature/progression/Progression.h"
#include "feature/recipes/Recipes.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;
constexpr char kCraftedKey[] = "tgear.crafted";

/// getModifiableArmor: the first stack that is TravellersModifiable with modifier slots.
ItemStack const* modifiableArmor(recipes::CraftingGrid const& grid) {
    for (auto const* s : grid.slots)
        if (s && !items::isEmpty(*s) && modifierSlots(items::idOf(*s)) > 0) return s;
    return nullptr;
}

/// TransferableComponentModifier#findDataComponentProviders: grid stacks carrying the transferable
/// component (a container); 0 for other modifiers.
int dataComponentProviders(recipes::CraftingGrid const& grid, std::string const& modifierId) {
    auto m = modifier(modifierId);
    if (!m || m->type != "twilightforest:transferable_component") return 0;
    int n = 0;
    for (auto const* s : grid.slots)
        if (s && !items::isEmpty(*s) && hasContainer(*s)) ++n;
    return n;
}

ItemStack const* containerProvider(recipes::CraftingGrid const& grid) {
    for (auto const* s : grid.slots)
        if (s && !items::isEmpty(*s) && hasContainer(*s)) return s;
    return nullptr;
}

} // namespace

bool modifierRecipeMatches(std::string const& modifierId, recipes::CraftingGrid const& grid) {
    auto const* stack = modifiableArmor(grid);
    if (!stack) return false;
    int slots = modifierSlots(items::idOf(*stack));
    return static_cast<int>(insertableModifiers(*stack).size()) < slots && !hasModifier(*stack, modifierId)
        && dataComponentProviders(grid, modifierId) <= 1;
}

std::optional<ItemStack> modifierRecipeAssemble(std::string const& modifierId, recipes::CraftingGrid const& grid) {
    auto const* gear = modifiableArmor(grid);
    if (!gear) return std::nullopt;
    ItemStack out = items::withCount(*gear, 1);
    auto      m   = modifier(modifierId);
    if (!m) return std::nullopt;
    bool ok = false;
    if (m->type == "twilightforest:transferable_component") {
        // TransferableComponentModifier#transfer: the marker and the provider's container
        if (auto const* provider = containerProvider(grid)) {
            auto content = container(*provider);
            ok           = addModifier(out, modifierId);
            if (ok) setContainer(out, content);
        }
    } else {
        ok = addModifier(out, modifierId);
    }
    if (!ok) return std::nullopt;
    // the add_modifier trigger of fireCraftingModifierTrigger: the modifiers the gear did not have
    auto before = insertableModifiers(*gear);
    Json fresh  = Json::array();
    for (auto const& id : insertableModifiers(out))
        if (std::find(before.begin(), before.end(), id) == before.end()) fresh.push_back(id);
    if (!fresh.empty()) items::setData(out, kCraftedKey, fresh);
    return out;
}

bool mergeRecipeMatches(recipes::CraftingGrid const& grid) {
    ItemStack const* vest   = nullptr;
    ItemStack const* gloves = nullptr;
    int              n      = 0;
    for (auto const* s : grid.slots) {
        if (!s || items::isEmpty(*s)) continue;
        ++n;
        auto id = items::idOf(*s);
        if (id == ids::kVest && !vest) vest = s;
        else if (id == ids::kGloves && !gloves) gloves = s;
    }
    return n == 2 && vest && gloves && !hasGloves(*vest);
}

std::optional<ItemStack> mergeRecipeAssemble(recipes::CraftingGrid const& grid) {
    if (!mergeRecipeMatches(grid)) return std::nullopt;
    for (auto const* s : grid.slots)
        if (s && !items::isEmpty(*s) && items::idOf(*s) == ids::kVest) {
            ItemStack out = items::withCount(*s, 1);
            setGloves(out, true);
            return out;
        }
    return std::nullopt;
}

void registerRecipes() {
    int n = 0;
    for (auto const* type : {"twilightforest:travellers_gear_modifier_shaped_recipe", "twilightforest:travellers_gear_modifier_shapeless_recipe"}) {
        for (auto const* r : recipes::ofType(type)) {
            auto modifierId = r->data.value("modifier_key", std::string());
            if (modifierId.empty()) continue;
            recipes::registerSpecialCrafting(
                r->id,
                [modifierId](recipes::CraftingGrid const& g) { return modifierRecipeMatches(modifierId, g); },
                [modifierId](recipes::CraftingGrid const& g) {
                    auto out = modifierRecipeAssemble(modifierId, g);
                    return out ? *out : ItemStack::EMPTY_ITEM();
                }
            );
            // a potion ingredient: one BP recipe per potion (assets/pack/bp/recipes/tgear)
            for (int v = 1; v <= 3; ++v) recipes::ext::addSpecialLayout(r->id, r->id + "_" + std::to_string(v));
            if (r->id.starts_with("twilightforest:")) ++n;
        }
    }
    recipes::registerSpecialCrafting(
        "twilightforest:travellers_vest_gloves_merge_recipe",
        [](recipes::CraftingGrid const& g) { return mergeRecipeMatches(g); },
        [](recipes::CraftingGrid const& g) {
            auto out = mergeRecipeAssemble(g);
            return out ? *out : ItemStack::EMPTY_ITEM();
        }
    );
    log().info("tgear: {} travellers modifier recipes registered", n);
}

void fireCraftedTriggers(Player& player) {
    auto& inv     = player.getInventory();
    int   size    = inv.getContainerSize();
    bool  changed = false;
    for (int i = 0; i < size; ++i) {
        auto const& s = inv.getItem(i);
        if (items::isEmpty(s) || !isGear(items::idOf(s))) continue;
        auto fresh = items::getData(s, kCraftedKey);
        if (!fresh.is_array()) continue;
        ItemStack copy = s;
        items::setData(copy, kCraftedKey, nullptr);
        inv.setItem(i, copy);
        changed = true;
        for (auto const& id : fresh)
            if (id.is_string()) progression::trigger(player, "twilightforest:add_modifier", {{"modifier", id.get<std::string>()}});
    }
    if (changed) player.sendInventory(false);
}

} // namespace gf::feature::tgear
