// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/travellers_gear/TravellersArmorBeltItem.java
//   src/main/java/gloamingforest/network/SwapHotbarPacket.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The belt's (and swap-hotbar wings') hotbar swap: the client mod's swap key sends "tgear.swap_hotbar"
// (SwapHotbarPacket); vanilla players sneak-use a belt or wings in their hand (docs/units/tgear.md).
#include <array>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "core/tgear/Logic.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;

/// Item#canFitInsideContainerItems: shulker boxes and stacks carrying a container (belts, swap-hotbar
/// wings: TravellersArmorBeltItem's override) cannot.
bool canFitInsideContainers(ItemStack const& stack) {
    if (items::isEmpty(stack)) return true;
    auto id = items::idOf(stack);
    if (id.find("shulker_box") != std::string::npos) return false;
    return !hasContainer(stack);
}

} // namespace

bool hasSwapHotbar(ItemStack const& legs) {
    return (hasModifier(legs, ids::kSwapHotbar) || hasModifier(legs, ids::kSwapHotbarAbility)) && hasContainer(legs);
}

bool trySwapHotbar(Player& player) {
    ItemStack legs = player.getArmor(SharedTypes::Legacy::ArmorSlot::Legs);
    if (!hasSwapHotbar(legs)) return false;
    bool active = (isActive(player, ids::kSwapHotbar) || isActive(player, ids::kSwapHotbarAbility)) && hasContainer(legs);
    auto stored = container(legs);
    auto& inv   = player.getInventory();

    std::array<core::tgear::HotbarSlot, 9> hotbar{};
    std::array<bool, 9>                    beltEmpty{};
    for (int i = 0; i < 9; ++i) {
        auto const& s = inv.getItem(i);
        hotbar[i].empty   = items::isEmpty(s);
        hotbar[i].allowed = canFitInsideContainers(s)
                         && (hotbar[i].empty || !tags::has(tags::Registry::Item, "gloamingforest:travellers_belt_blacklisted", items::idOf(s)));
        beltEmpty[i] = items::isEmpty(stored[i]);
    }
    auto plan = core::tgear::planHotbarSwap(hotbar, beltEmpty, active);
    std::array<ItemStack, 9> next = stored;
    for (int i = 0; i < 9; ++i) {
        if (!plan.exchange[i]) continue;
        ItemStack fromHotbar = inv.getItem(i);
        inv.setItem(i, stored[i]);
        next[i] = fromHotbar;
    }
    setContainer(legs, next);
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Legs, legs);
    player.sendInventory(false);
    if (plan.changed) {
        fx::sound(player.getDimension(), "item.gloamingforest.travellers_gear.swap_hotbar", player.getFeetPos(), 1.f, 1.f);
        log().info("tgear: {} swapped the hotbar", player.getRealName());
    }
    return plan.changed;
}

} // namespace gf::feature::tgear
