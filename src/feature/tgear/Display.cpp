// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersGogglesItem.java
//   src/main/java/twilightforest/components/item/ItemDisplayContents.java
//   src/main/java/twilightforest/network/CycleMapSlotPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The goggles' item display on the server: the bundle-like insert / remove rules (core/tgear/Logic.h),
// the map cycle (the client mod's key sends "tgear.cycle_map_slot"), the worn goggles ticking their
// active map as if held and sending its update (GogglesItem#inventoryTick), and the vanilla fallback:
// Bedrock's inventory has no secondary-click-on-item interaction, so sneak-using the goggles opens a
// form that stores the held off-hand item / a hotbar item, takes stored items out and cycles the map
// (docs/units/tgear.md).
#include <memory>
#include <random>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/network/Packet.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/Level.h"

#include "core/tgear/Logic.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "feature/ui/Ui.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;

float bundlePitch() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return 0.8f + std::uniform_real_distribution<float>(0.f, 1.f)(rng) * 0.4f;
}

void playInsert(Player& p) { fx::sound(p.getDimension(), "minecraft:item.bundle.insert", p.getPosition(), 0.8f, bundlePitch()); }
void playRemove(Player& p) { fx::sound(p.getDimension(), "minecraft:item.bundle.remove_one", p.getPosition(), 0.8f, bundlePitch()); }

} // namespace

bool displayInsert(ItemStack& goggles, ItemStack& source, ItemStack& replaced) {
    replaced = ItemStack::EMPTY_ITEM();
    auto d = displayOf(goggles);
    if (!d || items::isEmpty(source) || items::countOf(goggles) != 1) return false;
    // canFitInsideContainerItems: the display only takes maps, compasses, clocks and moon dials anyway
    auto contents = contentsOf(*d);
    auto item     = displayItemOf(source);
    auto r        = core::tgear::trySwap(contents, item);
    if (!r.ok) return false;
    if (!d->items[r.slot].is_null()) replaced = items::fromJson(d->items[r.slot]);
    d->items[r.slot] = displayJsonOf(items::withCount(source, 1)); // split(1)
    d->chosenMapSlot = contents.chosenMapSlot;
    source           = items::withCount(source, items::countOf(source) - 1);
    setDisplay(goggles, *d);
    return true;
}

bool displayRemoveFirst(ItemStack& goggles, ItemStack& out) {
    auto d = displayOf(goggles);
    if (!d) return false;
    auto contents = contentsOf(*d);
    int  slot     = core::tgear::removeFirstFree(contents);
    if (slot < 0) return false;
    out              = items::fromJson(d->items[slot]);
    d->items[slot]   = nullptr;
    d->chosenMapSlot = contents.chosenMapSlot;
    setDisplay(goggles, *d);
    return true;
}

bool cycleMapSlot(Player& player) {
    ItemStack head = player.getArmor(SharedTypes::Legacy::ArmorSlot::Head);
    auto      d    = displayOf(head);
    if (!d || !isActive(player, ids::kItemDisplay)) return false;
    auto contents = contentsOf(*d);
    int  oldIndex = contents.chosenMapSlot;
    int  newIndex = core::tgear::cycleChosenMapSlot(contents);
    if (oldIndex == newIndex) return false;
    d->chosenMapSlot = newIndex;
    setDisplay(head, *d);
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, head);
    player.sendInventory(false);
    fx::soundTo(player, newIndex == -1 ? "item.twilightforest.travellers_gear.cycle_maps_empty" : "item.twilightforest.travellers_gear.cycle_maps", 1.f, 1.f);
    log().info("tgear: {} cycled the item display map to {}", player.getRealName(), newIndex);
    return true;
}

void tickDisplayMap(Player& player) {
    if (!isActive(player, ids::kItemDisplay)) return;
    ItemStack head = player.getArmor(SharedTypes::Legacy::ArmorSlot::Head);
    auto      d    = displayOf(head);
    if (!d) return;
    int slot = d->chosenMapSlot;
    if (slot < 0 || slot >= core::tgear::kDisplaySlots || d->items[slot].is_null()) return;
    ItemStack map = items::fromJson(d->items[slot]);
    // Bedrock's MapItem is the vanilla filled map only (TF's magic / maze / ore maps are U13's items)
    if (items::idOf(map) != "minecraft:filled_map") return;
    auto* item = map.mItem.get();
    if (!item) return;
    auto const& mapItem = static_cast<MapItem const&>(*item);
    // mark as selected so the map updates; the goggles are no complex item, so send the update here
    mapItem.inventoryTick(map, player.getLevel(), player, 0, true);
    if (auto packet = mapItem.getUpdatePacket(map, player.getLevel(), player)) packet->sendTo(player);
}

void openDisplayForm(Player& player) {
    auto goggles = player.getSelectedItem();
    auto d       = displayOf(goggles);
    if (!d) return;
    std::vector<ui::FormButton> buttons;
    // store the off-hand item, else each display item in the hotbar
    auto storeFrom = [](int invSlot) {
        return [invSlot](Player& p) {
            ItemStack g = p.getSelectedItem();
            ItemStack src = invSlot < 0 ? p.getOffhandSlot() : p.getInventory().getItem(invSlot);
            ItemStack replaced;
            if (!displayInsert(g, src, replaced)) return;
            p.setSelectedItem(g);
            // trySwap: the slot gets the replaced item and the rest of the stack goes to the player,
            // else the slot keeps the rest
            ItemStack rest   = src;
            ItemStack inSlot = items::isEmpty(replaced) ? src : replaced;
            if (invSlot < 0) p.setOffhandSlot(inSlot);
            else p.getInventory().setItem(invSlot, inSlot);
            if (!items::isEmpty(replaced) && !items::isEmpty(rest) && !p.add(rest)) p.drop(rest, false);
            p.sendInventory(false);
            playInsert(p);
        };
    };
    if (displayTypeOf(items::idOf(player.getOffhandSlot())) != core::tgear::DisplayType::None)
        buttons.push_back({tr("gf.tgear.form.store", "Store {}", items::displayName(player.getOffhandSlot())), "", storeFrom(-1)});
    for (int i = 0; i < 9; ++i) {
        auto const& s = player.getInventory().getItem(i);
        if (!items::isEmpty(s) && displayTypeOf(items::idOf(s)) != core::tgear::DisplayType::None)
            buttons.push_back({tr("gf.tgear.form.store", "Store {}", items::displayName(s)), "", storeFrom(i)});
    }
    bool any = false;
    for (auto const& j : d->items) any = any || !j.is_null();
    if (any)
        buttons.push_back({tr("gf.tgear.form.take", "Take out the first item"), "", [](Player& p) {
                               ItemStack g = p.getSelectedItem();
                               ItemStack out;
                               if (!displayRemoveFirst(g, out)) return;
                               p.setSelectedItem(g);
                               if (!p.add(out)) p.drop(out, false);
                               p.sendInventory(false);
                               playRemove(p);
                           }});
    int maps = 0;
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
        if (core::tgear::kDisplayLayout[i] == core::tgear::DisplayType::Map && !d->items[i].is_null()) ++maps;
    if (maps > 0)
        buttons.push_back({tr("gf.tgear.form.cycle", "Cycle the shown map"), "", [](Player& p) {
                               ItemStack g  = p.getSelectedItem();
                               auto      dd = displayOf(g);
                               if (!dd) return;
                               auto c   = contentsOf(*dd);
                               int  old = c.chosenMapSlot;
                               int  now = core::tgear::cycleChosenMapSlot(c);
                               if (old == now) return;
                               dd->chosenMapSlot = now;
                               setDisplay(g, *dd);
                               p.setSelectedItem(g);
                               p.sendInventory(false);
                               fx::soundTo(p, now == -1 ? "item.twilightforest.travellers_gear.cycle_maps_empty" : "item.twilightforest.travellers_gear.cycle_maps", 1.f, 1.f);
                           }});
    std::string body;
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
        if (!d->items[i].is_null()) body += "- " + items::displayName(items::fromJson(d->items[i])) + (i == d->chosenMapSlot ? " *" : "") + "\n";
    if (body.empty()) body = tr("travellers_gear.modifier.twilightforest.item_display.description", "Right-click items on the Goggles to add displays");
    ui::form(player, modifierName(ids::kItemDisplay), body, std::move(buttons));
}

} // namespace gf::feature::tgear
