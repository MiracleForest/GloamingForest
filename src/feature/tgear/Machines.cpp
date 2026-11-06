// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Travellers' Gear at the grindstone and the anvil (ext/tgear CraftHooks):
//   - removeModifiersFromTravellersGear (GrindstoneEvent.OnPlaceItem): one piece with insertable
//     modifiers comes out without them; two pieces, or a piece without any, are refused;
//   - extractItemsFromSwapHotbarModifier (GrindstoneEvent.OnTakeItem): the swap_hotbar container and the
//     item_display contents of the single piece are given to the player;
//   - cancelCombiningTravellersGear (AnvilUpdateEvent): two pieces are never combined; and the anvil
//     drops every book enchantment of a piece for a survival player (supportsEnchantment false,
//     isBookEnchantable true: Java's AnvilMenu then has no result);
//   - TravellersArmorBeltItem#canFitInsideContainerItems: a belt (with its container) stays out of
//     shulker boxes and bundles.
#include <optional>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/common/Interactions.h"
#include "feature/ext/tgear/CraftHooks.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace {
namespace items = game::items;

bool isTravellers(ItemStack const& s) { return !items::isEmpty(s) && isGear(items::idOf(s)); }

/// getUniqueTravellersGear: the one travellers piece of the two inputs that matches.
template <class Pred>
ItemStack const* uniqueGear(ItemStack const& top, ItemStack const& bottom, Pred&& pred) {
    std::vector<ItemStack const*> found;
    for (auto const* s : {&top, &bottom})
        if (isTravellers(*s) && pred(*s)) found.push_back(s);
    return found.size() == 1 ? found.front() : nullptr;
}

void give(Player& player, std::vector<ItemStack> stacks) {
    if (stacks.empty()) return;
    // after the item stack request (its own slot moves must find the inventory as the client saw it)
    auto id = player.getOrCreateUniqueID();
    interactions::nextTick([id, stacks = std::move(stacks)] {
        auto level = ll::service::getLevel();
        if (!level) return;
        auto* actor = level->fetchEntity(id, false);
        if (!actor || !actor->isPlayer()) return;
        auto& p = static_cast<Player&>(*actor);
        for (auto s : stacks) // ItemHandlerHelper.giveItemToPlayer: the inventory, else dropped
            if (!items::isEmpty(s) && !p.add(s)) p.drop(s, false);
        p.sendInventory(false);
    });
}

} // namespace

std::optional<ItemStack> grindstoneResult(ItemStack const& top, ItemStack const& bottom) {
    std::vector<ItemStack const*> gear;
    for (auto const* s : {&top, &bottom})
        if (isTravellers(*s)) gear.push_back(s);
    if (gear.empty()) return std::nullopt; // vanilla logic
    if (gear.size() > 1) return ItemStack::EMPTY_ITEM();
    auto modifiers = insertableModifiers(*gear.front());
    if (modifiers.empty()) return ItemStack::EMPTY_ITEM();
    ItemStack out = *gear.front();
    for (auto const& m : modifiers) removeModifier(out, m, nullptr);
    return out;
}

std::vector<ItemStack> grindstoneReturns(ItemStack const& top, ItemStack const& bottom) {
    std::vector<ItemStack> out;
    // swap_hotbar: the minecraft:container's non-empty stacks
    if (auto const* s = uniqueGear(top, bottom, [](ItemStack const& x) { return hasModifier(x, ids::kSwapHotbar); }))
        for (auto const& c : container(*s))
            if (!items::isEmpty(c)) out.push_back(c);
    // item_display: the stored items
    if (auto const* s = uniqueGear(top, bottom, [](ItemStack const& x) { return hasModifier(x, ids::kItemDisplay); }))
        if (auto d = displayOf(*s))
            for (auto const& j : d->items)
                if (!j.is_null()) out.push_back(items::fromJson(j));
    return out;
}

bool anvilRefuses(ItemStack const& input, ItemStack const& material, bool creative) {
    if (isTravellers(input) && isTravellers(material)) return true; // cancelCombiningTravellersGear
    // a piece and an enchanted book: no enchantment is supported, so a survival player gets no result
    if (isTravellers(input) && items::idOf(material) == "minecraft:enchanted_book" && !creative) return true;
    return false;
}

/// TravellersArmorBeltItem#canFitInsideContainerItems = !stack.has(CONTAINER): the class of both the belt and
/// the wings, so a belt or wings that carry the swap_hotbar container are refused.
bool storageRefuses(ItemStack const& item) {
    if (items::isEmpty(item)) return false;
    auto id = items::idOf(item);
    return (id == ids::kBelt || id == ids::kWings) && hasContainer(item);
}

void registerMachines() {
    // TravellersArmorBeltItem#canFitInsideContainerItems(stack): a belt or wings with a container never
    // go into a shulker box or a bundle
    gf::ext::tgear::onStorageRefuse([](ItemStackBase const& item) { return storageRefuses(static_cast<ItemStack const&>(item)); });
    gf::ext::tgear::onGrindResult([](ItemStack const& top, ItemStack const& bottom) { return grindstoneResult(top, bottom); });
    gf::ext::tgear::onGrindTaken([](Player& player, ItemStack const& top, ItemStack const& bottom) {
        auto back = grindstoneReturns(top, bottom);
        if (back.empty()) return;
        log().info("tgear: the grindstone gave {} {} stored stacks back", player.getRealName(), back.size());
        give(player, std::move(back));
    });
    gf::ext::tgear::onAnvilRefuse([](Player& player, ItemStack const& input, ItemStack const& material) {
        bool refused = anvilRefuses(input, material, player.canUseAbility(AbilitiesIndex::Instabuild));
        if (refused) log().info("tgear: the anvil refused {}'s travellers' gear", player.getRealName());
        return refused;
    });
}

} // namespace gf::feature::tgear
