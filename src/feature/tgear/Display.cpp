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
#include <optional>
#include <random>
#include <unordered_map>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/network/Packet.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/world/level/PositionTrackingId.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/position_trackingdb/PositionTrackingDBServer.h"
#include "mc/world/level/position_trackingdb/TrackingRecord.h"
#include "mc/world/level/position_trackingdb/ResultCode.h"
#include "mc/world/level/Level.h"

#include "core/tgear/Logic.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"
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

void playInsert(Player& p) { fx::sound(p.getDimension(), "minecraft:item.bundle.insert", p.getFeetPos(), 0.8f, bundlePitch()); }
void playRemove(Player& p) { fx::sound(p.getDimension(), "minecraft:item.bundle.remove_one", p.getFeetPos(), 0.8f, bundlePitch()); }

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

namespace {
std::string& lastCycle() {
    static std::string s;
    return s;
}
} // namespace

std::string lastCycleSound() { return lastCycle(); }

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
    char const* sound = newIndex == -1 ? "item.twilightforest.travellers_gear.cycle_maps_empty" : "item.twilightforest.travellers_gear.cycle_maps";
    fx::soundTo(player, sound, 1.f, 1.f);
    lastCycle() = sound;
    log().info("tgear: {} cycled the item display map to {}", player.getRealName(), newIndex);
    return true;
}

namespace {

/// The lodestone a stored lodestone compass tracks ({x, y, z, dim}; null when it tracks none): Bedrock's
/// position tracking database (Java's LodestoneTracker target).
Json lodestoneOf(Player& player, ItemStack const& compass) {
    if (items::idOf(compass) != "minecraft:lodestone_compass") return nullptr;
    auto* db = player.getLevel().getPositionTrackerDBServer();
    if (!db) return nullptr;
    // LodestoneCompassItem::getPositionTrackingId is client-only: the handle is the stack's "trackingHandle"
    auto const* tag = items::userData(compass);
    if (!tag || !tag->contains("trackingHandle", Tag::Type::Int)) return nullptr;
    PositionTrackingId id;
    id.mRawId = static_cast<uint>(tag->at("trackingHandle").get<IntTag>().data);
    PositionTrackingDB::TrackingRecord* record = nullptr;
    if (db->findTracker(id, &record) != PositionTrackingDB::ResultCode::Ok || !record) return nullptr;
    BlockPos const& pos = record->mPosition;
    return Json{{"x", pos.x}, {"y", pos.y}, {"z", pos.z}, {"dim", static_cast<int>(record->mDimensionType.get())}};
}

} // namespace

namespace {
/// Per wearer: the stored map JSON and the stack built from it.
std::unordered_map<std::int64_t, std::pair<Json, ItemStack>>& mapCache() {
    static std::unordered_map<std::int64_t, std::pair<Json, ItemStack>> c; // server thread
    return c;
}
} // namespace

void tickDisplayMap(Player& player) {
    if (!isActive(player, ids::kItemDisplay)) return;
    ItemStack head = player.getArmor(SharedTypes::Legacy::ArmorSlot::Head);
    auto      d    = displayOf(head);
    if (!d) return;
    bool changed = false;
    // CompassDisplay's lodestone target, kept with the stored compass for the client's HUD
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i) {
        if (core::tgear::kDisplayLayout[i] != core::tgear::DisplayType::Compass || d->items[i].is_null()) continue;
        auto target = lodestoneOf(player, items::fromJson(d->items[i]));
        auto old    = d->items[i].contains("lodestone") ? d->items[i]["lodestone"] : Json();
        if (target == old) continue;
        if (target.is_null()) d->items[i].erase("lodestone");
        else d->items[i]["lodestone"] = target;
        changed = true;
    }
    int slot = d->chosenMapSlot;
    if (slot >= 0 && slot < core::tgear::kDisplaySlots && !d->items[slot].is_null()) {
        // the stored map's stack, kept between ticks while its JSON is the same (no per-tick rebuild)
        auto& cached = mapCache()[player.getOrCreateUniqueID().rawID];
        if (cached.first != d->items[slot]) cached = {d->items[slot], items::fromJson(d->items[slot])};
        ItemStack& map    = cached.second;
        auto       id     = items::idOf(map);
        ItemStack  before = map;
        if (id == "minecraft:filled_map") {
            // GogglesItem#inventoryTick / MapHooks (UpdateMapsInGoggles): the chosen map updates as if
            // held, with the wearer as its carrier; the goggles are no complex item, so send the update
            if (auto* item = map.mItem.get()) {
                auto const& mapItem = static_cast<MapItem const&>(*item);
                mapItem.inventoryTick(map, player.getLevel(), player, 0, true);
                if (auto packet = mapItem.getUpdatePacket(map, player.getLevel(), player)) packet->sendTo(player);
            }
        } else if (auto const* b = gf::item::internal::behaviourOf(id); b && b->inventoryTick) {
            // TF's own maps (magic, maze, ore: MapItem subclasses, another unit's registered items):
            // their inventory tick as the selected item, the wearer carrying it
            b->inventoryTick(map, player, -1, true);
        }
        // whatever the tick wrote to the stack (map data, TF's map state) is kept in the display
        if (!items::sameItemSameData(before, map)) {
            d->items[slot] = displayJsonOf(map);
            cached.first   = d->items[slot];
            changed        = true;
        }
    }
    if (changed) {
        setDisplay(head, *d);
        player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, head);
    }
}

namespace {

/// A slot by the client's collection name: the player's own inventory, armour and off-hand, or a slot
/// of the open screen's container (a chest, barrel, shulker box, ...: the server container manager's
/// model of that collection).
struct SlotRef {
    enum Kind { Inventory, Armor, Offhand, Model } kind;
    int                             index;
    std::shared_ptr<ContainerModel> model;
};
std::optional<SlotRef> slotRefOf(Player& player, std::string const& collection, int index) {
    if (collection == "hotbar_items" || collection == "inventory_items" || collection == "combined_hotbar_and_inventory_items")
        return index >= 0 && index < 36 ? std::optional<SlotRef>(SlotRef{SlotRef::Inventory, index, nullptr}) : std::nullopt;
    if (collection == "armor_items") return index >= 0 && index < 4 ? std::optional<SlotRef>(SlotRef{SlotRef::Armor, index, nullptr}) : std::nullopt;
    if (collection == "offhand_items") return SlotRef{SlotRef::Offhand, 0, nullptr};
    auto const& manager = player.mContainerManager.get();
    if (!manager) return std::nullopt;
    auto const& models = manager->mContainers.get();
    auto        it     = models.find(collection);
    if (it == models.end() || !it->second || index < 0 || index >= it->second->getContainerSize()) return std::nullopt;
    return SlotRef{SlotRef::Model, index, it->second};
}

ItemStack getSlot(Player& p, SlotRef const& r) {
    switch (r.kind) {
    case SlotRef::Inventory: return p.getInventory().getItem(r.index);
    case SlotRef::Armor: return p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.index));
    case SlotRef::Model: return r.model->getItemStack(r.index);
    default: return p.getOffhandSlot();
    }
}

void setSlot(Player& p, SlotRef const& r, ItemStack const& s) {
    switch (r.kind) {
    case SlotRef::Inventory: p.getInventory().setItem(r.index, s); break;
    case SlotRef::Armor: p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.index), s); break;
    case SlotRef::Model: r.model->setItem(r.index, s); break;
    default: p.setOffhandSlot(s); break;
    }
}
constexpr int kCursorSlot = 0; // PlayerUIContainer: the cursor

} // namespace

bool displaySlotClick(Player& player, std::string const& collection, int index) {
    auto ref = slotRefOf(player, collection, index);
    if (!ref) return false;
    auto&     ui     = player.mPlayerUIContainer.get();
    ItemStack slot   = getSlot(player, *ref);
    ItemStack cursor = ui.getItem(kCursorSlot);
    auto      isGoggles = [](ItemStack const& s) { return !items::isEmpty(s) && items::idOf(s) == ids::kGoggles && displayOf(s) && items::countOf(s) == 1; };
    bool      changed   = false;
    if (isGoggles(slot)) {
        // overrideOtherStackedOnMe: the goggles in the slot, the cursor's item goes in (the replaced one
        // comes back on the cursor), or an empty cursor takes the first stored item
        if (items::isEmpty(cursor)) {
            ItemStack out;
            if (displayRemoveFirst(slot, out)) {
                cursor = out;
                playRemove(player);
                changed = true;
            }
        } else {
            ItemStack replaced;
            if (displayInsert(slot, cursor, replaced)) {
                if (!items::isEmpty(replaced)) {
                    if (!items::isEmpty(cursor) && !player.add(cursor)) player.drop(cursor, false);
                    cursor = replaced;
                }
                playInsert(player);
                changed = true;
            }
        }
    } else if (isGoggles(cursor)) {
        // overrideStackedOnOther: the goggles on the cursor, an empty slot gets the first stored item,
        // else the slot's item goes in
        if (items::isEmpty(slot)) {
            ItemStack out;
            if (displayRemoveFirst(cursor, out)) {
                slot = out;
                playRemove(player);
                changed = true;
            }
        } else {
            ItemStack replaced;
            if (displayInsert(cursor, slot, replaced)) {
                if (!items::isEmpty(replaced)) {
                    if (!items::isEmpty(slot) && !player.add(slot)) player.drop(slot, false);
                    slot = replaced;
                }
                playInsert(player);
                changed = true;
            }
        }
    }
    if (!changed) return false;
    setSlot(player, *ref, slot);
    ui.setItem(kCursorSlot, cursor);
    player.sendInventory(true);
    log().info("tgear: {} used the item display on {} {}", player.getRealName(), collection, index);
    return true;
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
