// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/custom/TravellersModifiersManager.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorBeltItem.java
//   src/main/java/twilightforest/item/travellers_gear/modifiers/TravellersComponentModifier.java
//   src/main/java/twilightforest/item/travellers_gear/modifiers/TransferableComponentModifier.java
//   src/main/java/twilightforest/item/travellers_gear/modifiers/TravellersModifier.java
//   src/main/java/twilightforest/components/item/ItemDisplayContents.java
//   src/main/java/twilightforest/init/TFKeyBinds.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/tgear/Gear.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gear/Gear.h"
#include "feature/item/DataFiles.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace {

namespace data  = feature::item::data;
namespace items = game::items;

constexpr char kContainerKey[] = "tgear.container";
constexpr char kDisplayKey[]   = "tgear.display";
constexpr char kGlovesKey[]    = "tgear.gloves";

struct Registry {
    std::mutex                          mutex;
    bool                                loaded = false;
    std::map<std::string, ModifierInfo> entries;
    std::set<std::string>               warned;
};

Registry& registry() {
    static Registry r;
    return r;
}

void loadLocked(Registry& r) {
    r.entries.clear();
    for (auto const& f : data::list("twilight/travellers_modifiers")) {
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        ModifierInfo m;
        m.id    = f.id();
        m.type  = doc.value("type", std::string());
        m.group = doc.value("equipment_slots", std::string("armor"));
        if (auto it = doc.find("component"); it != doc.end()) {
            if (it->is_string()) m.component = it->get<std::string>();
            else if (it->is_object() && !it->empty()) {
                m.component = it->begin().key();
                m.value     = it->begin().value();
            }
        }
        m.ability        = m.type == "twilightforest:builtin" || doc.value("builtin_modifier", false);
        m.hasDescription = doc.contains("description") && doc["description"].is_array() && !doc["description"].empty();
        if (m.hasDescription) m.description = doc["description"];
        r.entries[m.id]  = std::move(m);
    }
    r.loaded = true;
}

[[maybe_unused]] bool const kReload = (data::onReload([] {
                                           auto&           r = registry();
                                           std::lock_guard lock(r.mutex);
                                           r.loaded = false;
                                           r.warned.clear();
                                       }),
                                       true);

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "twilightforest:" + id : id; }

} // namespace

std::vector<ModifierInfo> allModifiers() {
    auto&           r = registry();
    std::lock_guard lock(r.mutex);
    if (!r.loaded) loadLocked(r);
    std::vector<ModifierInfo> out;
    out.reserve(r.entries.size());
    for (auto const& [id, m] : r.entries) out.push_back(m);
    return out;
}

std::optional<ModifierInfo> modifier(std::string const& rawId) {
    auto            id = fullId(rawId);
    auto&           r  = registry();
    std::lock_guard lock(r.mutex);
    if (!r.loaded) loadLocked(r);
    if (auto it = r.entries.find(id); it != r.entries.end()) return it->second;
    if (r.warned.insert(id).second) log().warn("tgear: Travellers modifier {} is not present in the registry", id);
    return std::nullopt;
}

bool modifierExists(std::string const& id) {
    auto&           r = registry();
    std::lock_guard lock(r.mutex);
    if (!r.loaded) loadLocked(r);
    return r.entries.contains(id);
}

bool isGear(std::string const& itemId) {
    return std::any_of(kGearItems.begin(), kGearItems.end(), [&](char const* g) { return itemId == g; });
}

int modifierSlots(std::string const& itemId) {
    if (itemId == ids::kGoggles || itemId == ids::kVest || itemId == ids::kWings || itemId == ids::kBoots) return 3;
    return 0;
}

int armorSlotOf(std::string const& itemId) {
    if (itemId == ids::kGoggles) return 0;
    if (itemId == ids::kVest || itemId == ids::kGloves) return 1;
    if (itemId == ids::kWings || itemId == ids::kBelt) return 2;
    if (itemId == ids::kBoots) return 3;
    return -1;
}

bool groupCovers(std::string const& group, int slot) {
    if (slot < 0 || slot > 3) return false;
    if (group == "armor" || group == "any") return true;
    static constexpr char const* names[] = {"head", "chest", "legs", "feet"};
    return group == names[slot];
}

int maxDamageOf(ItemStack const& stack) {
    if (items::isEmpty(stack) || !stack.isDamageableItem()) return 0;
    auto* item = stack.mItem.get();
    return item ? item->getMaxDamage() : 0;
}

bool isBroken(ItemStack const& stack) {
    if (items::isEmpty(stack) || !isGear(items::idOf(stack)) || !stack.isDamageableItem()) return false;
    int max = maxDamageOf(stack);
    return max > 0 && max - 1 <= stack.getDamageValue();
}

bool hasModifier(ItemStack const& stack, std::string const& id) {
    if (items::isEmpty(stack) || !isGear(items::idOf(stack))) return false;
    return gear::modifiers(stack).contains(fullId(id));
}

namespace {
std::vector<std::string> modifiersWhere(ItemStack const& stack, bool ability) {
    std::vector<std::string> out;
    if (items::isEmpty(stack) || !isGear(items::idOf(stack))) return out;
    auto present = gear::modifiers(stack);
    for (auto const& m : allModifiers())
        if (m.ability == ability && present.contains(m.id)) out.push_back(m.id);
    return out;
}
} // namespace

std::vector<std::string> insertableModifiers(ItemStack const& stack) { return modifiersWhere(stack, false); }
std::vector<std::string> abilityModifiers(ItemStack const& stack) { return modifiersWhere(stack, true); }

bool isActiveOn(Actor const& player, ItemStack const& stack, std::string const& rawId) {
    auto id = fullId(rawId);
    if (!modifier(id) || !hasModifier(stack, id)) return false;
    if (player.isSpectator()) return false;
    return id == ids::kAutoRepair || !isBroken(stack);
}

bool isActive(Actor const& player, std::string const& rawId) {
    auto id = fullId(rawId);
    auto m  = modifier(id);
    if (!m) return false;
    int slot = -1;
    for (int a = 0; a < 4; ++a) {
        if (!groupCovers(m->group, a)) continue;
        if (slot >= 0) return false; // getStackForGroup: several armour slots -> EMPTY
        slot = a;
    }
    if (slot < 0) return false;
    auto const& stack = player.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(slot));
    return !items::isEmpty(stack) && isActiveOn(player, stack, id);
}

int armorPointsOf(std::string const& itemId) {
    // TFArmorMaterials.TRAVELLERS defense: helmet 2, chestplate 4, leggings 3, boots 2 (the BP items'
    // minecraft:wearable protection); the gloves and the belt are cosmetic (0)
    if (itemId == ids::kGoggles || itemId == ids::kBoots) return 2;
    if (itemId == ids::kVest) return 4;
    if (itemId == ids::kWings) return 3;
    return 0;
}

double numberOf(std::string const& id, double fallback) {
    auto m = modifier(id);
    return m && m->value.is_number() ? m->value.get<double>() : fallback;
}

bool addModifier(ItemStack& stack, std::string const& rawId) {
    auto id = fullId(rawId);
    auto m  = modifier(id);
    if (!m || m->ability) return false;
    if (!gear::add(stack, id)) return false;
    if (id == ids::kItemDisplay && !displayOf(stack)) setDisplay(stack, Display{});
    if (id == ids::kSwapHotbar && !items::getData(stack, kContainerKey).is_array()) setContainer(stack, {});
    return true;
}

bool removeModifier(ItemStack& stack, std::string const& rawId, std::vector<ItemStack>* stored) {
    auto id = fullId(rawId);
    if (!hasModifier(stack, id)) return false;
    if (id == ids::kItemDisplay) {
        if (auto d = displayOf(stack); d && stored)
            for (auto const& j : d->items)
                if (!j.is_null()) stored->push_back(items::fromJson(j));
        items::setData(stack, kDisplayKey, nullptr);
    }
    if (id == ids::kSwapHotbar) {
        if (stored)
            for (auto const& s : container(stack))
                if (!items::isEmpty(s)) stored->push_back(s);
        items::setData(stack, kContainerKey, nullptr);
    }
    return gear::remove(stack, id);
}

// ---- container -------------------------------------------------------------------------------------

bool hasContainer(ItemStack const& stack) {
    if (items::isEmpty(stack)) return false;
    if (items::idOf(stack) == ids::kBelt) return true;
    return items::getData(stack, kContainerKey).is_array();
}

std::array<ItemStack, 9> container(ItemStack const& stack) {
    std::array<ItemStack, 9> out;
    for (auto& s : out) s = ItemStack::EMPTY_ITEM();
    auto j = items::getData(stack, kContainerKey);
    if (!j.is_array()) return out;
    for (std::size_t i = 0; i < 9 && i < j.size(); ++i)
        if (!j[i].is_null()) out[i] = items::fromJson(j[i]);
    return out;
}

void setContainer(ItemStack& stack, std::array<ItemStack, 9> const& content) {
    Json j = Json::array();
    for (auto const& s : content) j.push_back(items::isEmpty(s) ? Json() : items::toJson(s));
    items::setData(stack, kContainerKey, j);
}

// ---- display ---------------------------------------------------------------------------------------

std::optional<Display> displayOf(ItemStack const& stack) {
    if (items::isEmpty(stack)) return std::nullopt;
    auto j = items::getData(stack, kDisplayKey);
    if (!j.is_object()) return std::nullopt;
    Display d;
    d.chosenMapSlot = j.value("chosen_map_slot", core::tgear::kFirstMapSlot);
    if (auto it = j.find("slots"); it != j.end() && it->is_array())
        for (auto const& s : *it) {
            int idx = s.value("slot", -1);
            if (idx >= 0 && idx < core::tgear::kDisplaySlots && s.contains("item")) d.items[idx] = s["item"];
        }
    return d;
}

void setDisplay(ItemStack& stack, Display const& d) {
    Json slots = Json::array();
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
        if (!d.items[i].is_null()) slots.push_back({{"slot", i}, {"item", d.items[i]}});
    items::setData(stack, kDisplayKey, Json{{"chosen_map_slot", d.chosenMapSlot}, {"slots", slots}});
}

core::tgear::DisplayType displayTypeOf(std::string const& itemId) {
    using core::tgear::DisplayType;
    // Java's MapItem: filled maps only (EmptyMapItem is no MapItem); Bedrock's lodestone compass is
    // Java's compass with a lodestone tracker
    if (itemId == "minecraft:filled_map" || itemId == "twilightforest:filled_magic_map" || itemId == "twilightforest:filled_maze_map" || itemId == "twilightforest:filled_ore_map")
        return DisplayType::Map;
    if (itemId == "minecraft:compass" || itemId == "minecraft:lodestone_compass") return DisplayType::Compass;
    if (itemId == "minecraft:clock") return DisplayType::Clock;
    if (itemId == "twilightforest:moon_dial") return DisplayType::MoonDial;
    return DisplayType::None;
}

namespace {
std::string identityOf(Json const& item) {
    if (!item.is_object()) return {};
    return item.value("id", std::string()) + "#" + std::to_string(item.value("aux", 0)) + "#" + item.value("tag", std::string());
}

/// The item JSON stored in the display: items::toJson plus the parts the rules and the HUD read
/// without parsing the SNBT (user data as "tag", the map id).
Json storedJson(ItemStack const& stack) {
    auto j = items::toJson(stack);
    if (j.is_null()) return j;
    j["count"] = 1;
    if (auto const* ud = items::userData(stack)) {
        j["tag"] = ud->toSnbt(SnbtFormat::Minimize, 0);
        if (ud->contains("map_uuid")) {
            try {
                j["map_id"] = static_cast<std::int64_t>((*ud)["map_uuid"]);
            } catch (...) {
            }
        }
    }
    return j;
}
} // namespace

core::tgear::DisplayItem displayItemOf(ItemStack const& stack) {
    if (items::isEmpty(stack)) return {};
    auto j = storedJson(stack);
    return {identityOf(j), displayTypeOf(items::idOf(stack))};
}

core::tgear::DisplayContents contentsOf(Display const& d) {
    core::tgear::DisplayContents c;
    c.chosenMapSlot = d.chosenMapSlot;
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
        if (!d.items[i].is_null()) c.items[i] = {identityOf(d.items[i]), displayTypeOf(d.items[i].value("id", std::string()))};
    return c;
}

std::optional<std::int64_t> mapIdOf(Json const& item) {
    if (!item.is_object()) return std::nullopt;
    if (auto it = item.find("map_id"); it != item.end() && it->is_number_integer()) return it->get<std::int64_t>();
    return std::nullopt;
}

/// The JSON to store for `stack` in the display (internal to the display code, declared here).
Json displayJsonOf(ItemStack const& stack) { return storedJson(stack); }

// ---- gloves ----------------------------------------------------------------------------------------

bool hasGloves(ItemStack const& stack) {
    if (items::isEmpty(stack)) return false;
    if (items::idOf(stack) == ids::kGloves) return true;
    return items::getData(stack, kGlovesKey).is_boolean() && items::getData(stack, kGlovesKey).get<bool>();
}

void setGloves(ItemStack& stack, bool gloves) { items::setData(stack, kGlovesKey, gloves ? Json(true) : Json()); }

// ---- names -----------------------------------------------------------------------------------------

std::string defaultKeyName(std::string const& keyName) {
    // TFKeyBinds: item_display_map_cycle C, red_thread_vision unbound, swap_hotbar V, zoom Z
    if (keyName == "key.twilightforest.item_display_map_cycle") return "C";
    if (keyName == "key.twilightforest.swap_hotbar") return "V";
    if (keyName == "key.twilightforest.zoom") return "Z";
    if (keyName == "key.twilightforest.red_thread_vision") return std::string(translate("key.keyboard.unknown", "Not Bound"));
    return "Nonexistent key";
}

std::string keybindName(std::string const& key) {
    if (key.rfind("key.twilightforest.", 0) == 0) return defaultKeyName(key);
    // Java's default bindings (KeyMapping's InputConstants names)
    static std::map<std::string, std::string> const kDefaults{
        {"key.left", "A"},          {"key.right", "D"},       {"key.forward", "W"},         {"key.back", "S"},
        {"key.jump", "Space"},      {"key.sneak", "Left Shift"}, {"key.sprint", "Left Control"}, {"key.use", "Right Button"},
        {"key.attack", "Left Button"}, {"key.inventory", "E"}, {"key.drop", "Q"},           {"key.swapOffhand", "F"},
    };
    if (auto it = kDefaults.find(key); it != kDefaults.end()) return it->second;
    return key;
}

std::vector<std::string> modifierDescription(std::string const& rawId, TranslateFn const& tr, std::string (*keyName)(std::string const&)) {
    std::vector<std::string> lines;
    auto                     m = modifier(rawId);
    if (!m || !m->description.is_array()) return lines;
    auto keyOf = [&](std::string const& k) { return keyName ? keyName(k) : keybindName(k); };
    std::function<std::string(Json const&)> render = [&](Json const& c) -> std::string {
        if (c.is_string()) return c.get<std::string>();
        if (!c.is_object()) return {};
        if (auto it = c.find("text"); it != c.end() && it->is_string()) return it->get<std::string>();
        if (auto it = c.find("keybind"); it != c.end() && it->is_string()) return keyOf(it->get<std::string>());
        if (auto it = c.find("translate"); it != c.end() && it->is_string()) {
            std::vector<std::string> args;
            if (auto w = c.find("with"); w != c.end() && w->is_array())
                for (auto const& a : *w) args.push_back(render(a));
            auto fallback = c.value("fallback", it->get<std::string>());
            return core::formatMessage(tr(it->get<std::string>(), fallback), args);
        }
        return {};
    };
    for (auto const& c : m->description) lines.push_back(render(c));
    return lines;
}

std::string modifierName(std::string const& rawId, std::string const& locale, std::string (*keyName)(std::string const&)) {
    auto id    = fullId(rawId);
    auto colon = id.find(':');
    auto key   = "travellers_gear.modifier." + id.substr(0, colon) + "." + id.substr(colon + 1);
    std::string text(translate(key, key, locale));
    return core::tgear::interpolate(text, [&](std::string const& k) { return keyName ? keyName(k) : defaultKeyName(k); });
}

} // namespace gf::feature::tgear
