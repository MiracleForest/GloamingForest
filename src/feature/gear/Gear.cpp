// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/custom/TravellersModifiersManager.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorBeltItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Travellers' Gear part of contract "data" (F9): the query side of TF's travellers_modifiers
// registry (data/twilightforest/twilight/travellers_modifiers/*.json, 24 entries: type component /
// attribute / builtin / transferable_component, equipment_slots head / chest / legs / feet / armor).
// A gear stack keeps its added modifiers in its user data ("gf:gear" {id: level}); the built-in
// ones come with the item as on Java (TravellersArmorItem's default components: the goggles' zoom,
// the vest's swift swim, the wings' high jump, the boots' high step, the belt's hotbar swap) and
// are only listed as removed ("-<id>") when taken off. A modifier counts for a player in the armour
// slot its equipment_slots group names (Java's getStackForGroup).
#include "feature/gear/Gear.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gear/GearInternal.h"
#include "feature/item/DataFiles.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::gear {

namespace {

namespace data = feature::item::data;
using Json     = nlohmann::json;

constexpr char kDataKey[] = "gear"; ///< "gf:gear" in the user data

struct Modifier {
    std::string type;   ///< "twilightforest:component", "twilightforest:builtin", ...
    std::string group;  ///< "head", "chest", "legs", "feet", "armor"
    std::string component;
    bool        builtin = false;
};

struct Store {
    std::mutex                      mutex;
    std::map<std::string, Modifier> modifiers;
};

Store& store() {
    static Store s;
    return s;
}

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "twilightforest:" + id : id; }

/// The built-in modifiers of TF's gear items (their default data components).
std::vector<std::string> const& builtinsOf(std::string const& itemId) {
    static std::map<std::string, std::vector<std::string>> const table{
        {"twilightforest:travellers_goggles", {"twilightforest:zoom"}},
        {"twilightforest:travellers_vest", {"twilightforest:swift_swim"}},
        {"twilightforest:travellers_wings", {"twilightforest:high_jump"}},
        {"twilightforest:travellers_boots", {"twilightforest:step_up"}},
        {"twilightforest:travellers_belt", {"twilightforest:swap_hotbar_ability"}},
    };
    static std::vector<std::string> const none;
    auto                                  it = table.find(itemId);
    return it == table.end() ? none : it->second;
}

void load() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.modifiers.clear();
    for (auto const& f : data::list("twilight/travellers_modifiers")) {
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        Modifier m;
        m.type  = doc.value("type", std::string());
        m.group = doc.value("equipment_slots", std::string("armor"));
        if (auto it = doc.find("component"); it != doc.end()) {
            if (it->is_string()) m.component = it->get<std::string>();
            else if (it->is_object() && !it->empty()) m.component = it->begin().key();
        }
        m.builtin              = m.type == "twilightforest:builtin" || doc.value("builtin_modifier", false);
        s.modifiers[f.id()]    = std::move(m);
    }
    log().info("gear: {} travellers modifiers loaded", s.modifiers.size());
}

[[maybe_unused]] bool const kRegistered = (data::onReload(&load), true);

/// Whether a modifier group covers an armour slot (0 head .. 3 feet).
bool groupCovers(std::string const& group, int slot) {
    if (group == "armor" || group == "any") return slot >= 0 && slot <= 3;
    static constexpr char const* names[] = {"head", "chest", "legs", "feet"};
    return slot >= 0 && slot <= 3 && group == names[slot];
}

int armorSlotOf(ItemStack const& stack) {
    if (game::items::isEmpty(stack) || !(stack.isArmorItem() || stack.isHumanoidWearableItem())) return -1;
    auto s = stack.getArmorSlot();
    if (s < SharedTypes::Legacy::ArmorSlot::Head || s > SharedTypes::Legacy::ArmorSlot::Feet) return -1;
    return static_cast<int>(s);
}

std::optional<Modifier> modifierOf(std::string const& id) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.modifiers.find(fullId(id));
    if (it == s.modifiers.end()) return std::nullopt;
    return it->second;
}

} // namespace

std::map<std::string, int> modifiers(ItemStack const& stack) {
    std::map<std::string, int> out;
    if (game::items::isEmpty(stack)) return out;
    auto stored = game::items::getData(stack, kDataKey);
    for (auto const& b : builtinsOf(game::items::idOf(stack)))
        if (!(stored.is_object() && stored.contains("-" + b))) out[b] = 1;
    if (stored.is_object())
        for (auto& [k, v] : stored.items())
            if (!k.starts_with('-') && v.is_number_integer() && v.get<int>() > 0) out[k] = v.get<int>();
    return out;
}

// TravellersModifiersManager#isModifierActive: an unknown modifier is never active; the stack is
// getStackForGroup's, the single armour slot of the modifier's equipment_slots group. A group of
// several armour slots ("armor", "any") matches more than one slot, for which Java returns EMPTY:
// such a modifier is active only through the stack-taking overload (gear::modifiers of that stack).
int level(Player const& player, std::string const& modifierId) {
    auto id  = fullId(modifierId);
    auto mod = modifierOf(id);
    if (!mod) return 0;
    int slot = -1;
    for (int a = 0; a < 4; ++a) {
        if (!groupCovers(mod->group, a)) continue;
        if (slot >= 0) return 0; // getStackForGroup: a second armour slot -> EMPTY
        slot = a;
    }
    if (slot < 0) return 0;
    auto const& stack = player.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(slot));
    if (game::items::isEmpty(stack)) return 0;
    auto m  = modifiers(stack);
    auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
}

bool add(ItemStack& stack, std::string const& modifierId, int lvl) {
    if (game::items::isEmpty(stack) || lvl <= 0) return false;
    auto id  = fullId(modifierId);
    auto mod = modifierOf(id);
    if (!mod) return false;
    int slot = armorSlotOf(stack);
    if (slot < 0 || !groupCovers(mod->group, slot)) return false;
    auto stored = game::items::getData(stack, kDataKey);
    if (!stored.is_object()) stored = Json::object();
    stored.erase("-" + id);
    auto const& builtins = builtinsOf(game::items::idOf(stack));
    bool        builtin  = std::find(builtins.begin(), builtins.end(), id) != builtins.end();
    if (builtin && lvl == 1) stored.erase(id); // the item has it already
    else stored[id] = std::max(lvl, stored.value(id, 0));
    game::items::setData(stack, kDataKey, stored.empty() ? Json() : stored);
    return true;
}

namespace internal {

void registerModifier(std::string const& id, std::string const& group, bool builtin) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.modifiers[fullId(id)] = Modifier{"twilightforest:component", group, {}, builtin};
}

void unregisterModifier(std::string const& id) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.modifiers.erase(fullId(id));
}

std::size_t modifierCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.modifiers.size();
}

} // namespace internal

bool remove(ItemStack& stack, std::string const& modifierId) {
    if (game::items::isEmpty(stack)) return false;
    auto id = fullId(modifierId);
    if (!modifiers(stack).contains(id)) return false;
    auto stored = game::items::getData(stack, kDataKey);
    if (!stored.is_object()) stored = Json::object();
    stored.erase(id);
    auto const& builtins = builtinsOf(game::items::idOf(stack));
    if (std::find(builtins.begin(), builtins.end(), id) != builtins.end()) stored["-" + id] = 1;
    game::items::setData(stack, kDataKey, stored.empty() ? Json() : stored);
    return true;
}

} // namespace gf::gear
