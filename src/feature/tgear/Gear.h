// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/custom/TravellersModifiersManager.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersArmorBeltItem.java
//   src/main/java/gloamingforest/item/travellers_gear/modifiers/*.java
//   src/main/java/gloamingforest/components/item/ItemDisplayContents.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11's model of Travellers' Gear on top of the gear contract (feature/gear/Gear.h, F9), which keeps
// the modifier ids of a stack in its user data. This adds what upstream's TravellersModifiersManager knows:
// the registry entries (data/gloamingforest/gloaming/travellers_modifiers, type, slot group, ability,
// component default, description), the six items and their insertable slot counts, the broken state,
// upstream's isModifierActive, and the stack data the modifiers carry:
//   - "gf:tgear.container": the belt's / swap-hotbar wings' 9 stored stacks (minecraft:container);
//   - "gf:tgear.display": the goggles' item display (gloamingforest:item_display), {chosen_map_slot,
//     slots: [{slot, item}]} as upstream's codec;
//   - "gf:tgear.gloves": the vest's gloamingforest:has_travellers_gloves.
// Server thread unless noted; the client reads the same data from its local player's stacks.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/tgear/Logic.h"

class Actor;
class ItemStack;
class Player;

namespace gf::feature::tgear {

using Json = nlohmann::json;

namespace ids {
inline constexpr char kGoggles[] = "gloamingforest:travellers_goggles";
inline constexpr char kVest[]    = "gloamingforest:travellers_vest";
inline constexpr char kGloves[]  = "gloamingforest:travellers_gloves";
inline constexpr char kWings[]   = "gloamingforest:travellers_wings";
inline constexpr char kBelt[]    = "gloamingforest:travellers_belt";
inline constexpr char kBoots[]   = "gloamingforest:travellers_boots";
inline constexpr char kRedThread[] = "gloamingforest:red_thread";

inline constexpr char kAutoRepair[]      = "gloamingforest:auto_repair";
inline constexpr char kZoom[]            = "gloamingforest:zoom";
inline constexpr char kRedThreadVision[] = "gloamingforest:red_thread_vision";
inline constexpr char kAllNight[]        = "gloamingforest:all_night_goggles";
inline constexpr char kItemDisplay[]     = "gloamingforest:item_display";
inline constexpr char kStealth[]         = "gloamingforest:stealth";
inline constexpr char kArrowMagnetism[]  = "gloamingforest:arrow_magnetism";
inline constexpr char kEfficientEater[]  = "gloamingforest:efficient_eater";
inline constexpr char kPerfectDodge[]    = "gloamingforest:perfect_dodge";
inline constexpr char kHaste[]           = "gloamingforest:haste";
inline constexpr char kSwapHotbarAbility[] = "gloamingforest:swap_hotbar_ability";
inline constexpr char kSwapHotbar[]      = "gloamingforest:swap_hotbar";
} // namespace ids

/// The six TravellersModifiable items.
inline constexpr std::array<char const*, 6> kGearItems{ids::kGoggles, ids::kVest, ids::kGloves, ids::kWings, ids::kBelt, ids::kBoots};

/// One entry of the gloamingforest:travellers_modifiers registry.
struct ModifierInfo {
    std::string id;        ///< "gloamingforest:auto_repair"
    std::string type;      ///< "gloamingforest:component", "builtin", "attribute", "transferable_component"
    std::string group;     ///< equipment_slots: "head", "chest", "legs", "feet", "armor"
    std::string component; ///< the data component the modifier sets
    Json        value;     ///< the component's default value (null for unit / builtin)
    bool        ability        = false; ///< builtin, or attribute with builtin_modifier
    bool        hasDescription = false;
    Json        description;  ///< the "description" array of text components (TravellersModifier#getDescription)
};

/// Every registry entry (upstream's 24, data-driven), sorted by id as Java's registry lists them.
[[nodiscard]] std::vector<ModifierInfo> allModifiers();
/// One entry; nullopt (one warning per id, TravellersModifiersManager#getCachedModifier) when missing.
[[nodiscard]] std::optional<ModifierInfo> modifier(std::string const& id);
/// Entries known without a warning (command validation).
[[nodiscard]] bool modifierExists(std::string const& id);

[[nodiscard]] bool isGear(std::string const& itemId);
/// TravellersModifiable#getModifierSlots: goggles, vest, wings, boots 3; gloves and belt 0.
[[nodiscard]] int modifierSlots(std::string const& itemId);
/// The armour slot (0 head .. 3 feet) of a gear item, -1 for other items (getEquipmentSlotForItem).
[[nodiscard]] int armorSlotOf(std::string const& itemId);
/// Whether an equipment slot group covers armour slot `slot`.
[[nodiscard]] bool groupCovers(std::string const& group, int slot);

/// TravellersArmorItem#isTravellersArmorAndBroken: damageable and at max damage - 1.
[[nodiscard]] bool isBroken(ItemStack const& stack);
[[nodiscard]] int  maxDamageOf(ItemStack const& stack);

[[nodiscard]] bool hasModifier(ItemStack const& stack, std::string const& id);
/// findAllInsertableModifiers / findAllAbilityModifiers (registry order).
[[nodiscard]] std::vector<std::string> insertableModifiers(ItemStack const& stack);
[[nodiscard]] std::vector<std::string> abilityModifiers(ItemStack const& stack);

/// TravellersModifier#isActive on a stack: the stack has it, the wearer is no spectator and the stack
/// is not broken (auto_repair is ALWAYS_ACTIVE). The wearer is any LivingEntity (a player, a mob, an
/// armour stand).
[[nodiscard]] bool isActiveOn(Actor const& wearer, ItemStack const& stack, std::string const& id);
/// isModifierActive(LivingEntity, key): the stack of the group's single armour slot.
[[nodiscard]] bool isActive(Actor const& wearer, std::string const& id);
/// The armour points of a gear item (its BP wearable protection; upstream's GloamingArmorMaterials.TRAVELLERS).
[[nodiscard]] int armorPointsOf(std::string const& itemId);
/// The modifier's component default as a number (probabilities, amplifiers), `fallback` without one.
[[nodiscard]] double numberOf(std::string const& id, double fallback);

/// InsertableTravellersModifier#addModifier with the default component value (transferable
/// swap_hotbar: the marker and an empty 9-slot container). False when it does not apply.
bool addModifier(ItemStack& stack, std::string const& id);
/// InsertableTravellersModifier#removeModifier; the stacks stored by swap_hotbar / item_display are
/// appended to `stored` (the grindstone gives them back, the command deletes them).
bool removeModifier(ItemStack& stack, std::string const& id, std::vector<ItemStack>* stored = nullptr);

// ---- the belt's container ------------------------------------------------------------------------

/// Whether the stack carries a container (the belt always does: its default component).
[[nodiscard]] bool hasContainer(ItemStack const& stack);
[[nodiscard]] std::array<ItemStack, 9> container(ItemStack const& stack);
void setContainer(ItemStack& stack, std::array<ItemStack, 9> const& items);

// ---- the goggles' item display ---------------------------------------------------------------------

struct Display {
    std::array<Json, core::tgear::kDisplaySlots> items{}; ///< items::toJson of each stored stack (null: empty)
    int                                          chosenMapSlot = core::tgear::kFirstMapSlot;
};
[[nodiscard]] std::optional<Display> displayOf(ItemStack const& stack);
void setDisplay(ItemStack& stack, Display const& display);
/// The display kind of an item id (any map, compass, clock, gloamingforest:moon_dial).
[[nodiscard]] core::tgear::DisplayType displayTypeOf(std::string const& itemId);
/// The rules' view of the stored items (identity = id + aux + data).
[[nodiscard]] core::tgear::DisplayContents contentsOf(Display const& display);
[[nodiscard]] core::tgear::DisplayItem displayItemOf(ItemStack const& stack);
/// The JSON stored for `stack` in the display (items::toJson with count 1, "tag": its user data,
/// "map_id": a map's id).
[[nodiscard]] Json displayJsonOf(ItemStack const& stack);
/// The map id ("map_uuid") of a stored map item JSON (nullopt: none).
[[nodiscard]] std::optional<std::int64_t> mapIdOf(Json const& item);

// ---- vest / gloves -----------------------------------------------------------------------------------

[[nodiscard]] bool hasGloves(ItemStack const& stack);
void setGloves(ItemStack& stack, bool gloves);

// ---- names -------------------------------------------------------------------------------------------

/// upstream's default binding names of its keys (GloamingKeyBinds): the server (BDS) has no player bindings.
[[nodiscard]] std::string defaultKeyName(std::string const& keyName);
/// TravellersModifiersManager#getModifierTooltipComponent: the modifier's name in `locale` ("" =
/// the server's) with ${gloamingkeybinds/...} replaced by `keyName`.
[[nodiscard]] std::string modifierName(std::string const& id, std::string const& locale = {}, std::string (*keyName)(std::string const&) = nullptr);
/// Translates a key (with its English fallback) for a description line.
using TranslateFn = std::function<std::string(std::string const& key, std::string const& fallback)>;
/// The display name of a keybind component's key ("key.left" -> "A"): Java's default bindings, upstream's
/// own keys by defaultKeyName.
[[nodiscard]] std::string keybindName(std::string const& key);
/// TravellersModifier#getDescription rendered: one line per description component (a "translate" with its
/// "with" arguments: keybind, translate and text components; a "text"), through `tr`.
[[nodiscard]] std::vector<std::string> modifierDescription(std::string const& id, TranslateFn const& tr, std::string (*keyName)(std::string const&) = nullptr);

} // namespace gf::feature::tgear
