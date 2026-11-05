// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFItems.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The drying rack's materials (TFItems): the dried slime drops are food whose effect always
// applies (FoodProperties.effect(..., 1.0F)): gelatinous_slime_drop (nutrition 1, saturation 0.2,
// Speed 600 ticks) and gelatinous_maze_slime_drop (nutrition 2, saturation 0.3, Resistance 600
// ticks); nutrition, saturation and the eating come from the BP items (minecraft:food, import
// overrides), the effect is applied when the eating finishes. Tannin has craftRemainder(GLASS_BOTTLE):
// crafting treated_leather (its only recipe) gives the bottle back into the tannin's grid slot.

#include <string>
#include <vector>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/effects/Effects.h"
#include "feature/item/Item.h"
#include "feature/recipes/Recipes.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::woodwork {

namespace {

constexpr char kTannin[]         = "twilightforest:tannin";
constexpr char kTreatedLeather[] = "twilightforest:treated_leather";

/// FoodProperties: the effect of each dried slime drop (MobEffectInstance(effect, 600), chance 1).
struct SlimeFood {
    char const* item;
    char const* effect;
};
constexpr SlimeFood kSlimeFoods[] = {
    {"twilightforest:gelatinous_slime_drop", "minecraft:speed"},           // MobEffects.MOVEMENT_SPEED
    {"twilightforest:gelatinous_maze_slime_drop", "minecraft:resistance"}, // MobEffects.DAMAGE_RESISTANCE
};
constexpr int kSlimeEffectTicks = 600;

/// The treated_leather recipe (tannin + #c:leathers, shapeless): exactly one tannin and one
/// leather in the grid.
bool treatedLeatherMatches(recipes::CraftingGrid const& grid) {
    int tannin = 0, leather = 0, other = 0;
    for (auto const* s : grid.slots) {
        if (!s || game::items::isEmpty(*s)) continue;
        auto id = game::items::idOf(*s);
        if (id == kTannin) ++tannin;
        else if (id == "minecraft:leather") ++leather;
        else ++other;
    }
    return tannin == 1 && leather == 1 && other == 0;
}

} // namespace

std::vector<ItemStack> craftRemainders(recipes::CraftingGrid const& grid) {
    // Item#getCraftingRemainingItem per slot: tannin leaves a glass bottle
    std::vector<ItemStack> out;
    out.reserve(grid.slots.size());
    for (auto const* s : grid.slots) {
        if (s && !game::items::isEmpty(*s) && game::items::idOf(*s) == kTannin) {
            auto bottle = game::items::make("minecraft:glass_bottle", 1);
            out.push_back(bottle ? *bottle : ItemStack{});
        } else {
            out.emplace_back();
        }
    }
    return out;
}

void installMaterials() {
    static bool once = false;
    if (once) return;
    once = true; // item and recipe registrations last for the process
    for (auto const& f : kSlimeFoods) {
        std::string effect = f.effect;
        std::string item   = f.item;
        item::registerItem(f.item, {.finishUsing = [effect, item](Player& player, ItemStack&) {
                               effects::apply(player, effect, kSlimeEffectTicks, 0);
                               log().info("woodwork: {} ate {} ({} for {} ticks)", player.getRealName(), item, effect, kSlimeEffectTicks);
                           }});
    }
    recipes::registerSpecialCrafting(
        kTreatedLeather,
        treatedLeatherMatches,
        [](recipes::CraftingGrid const&) {
            auto s = game::items::make(kTreatedLeather, 1);
            return s ? *s : ItemStack{};
        },
        craftRemainders
    );
}

} // namespace gf::feature::woodwork
