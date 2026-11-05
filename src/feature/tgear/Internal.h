// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U11's internal interfaces between its server files (and its selftests). Not a contract.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/recipes/Recipes.h"

class Actor;
class ItemStack;
class Player;

namespace gf::feature::tgear {

// ---- Recipes.cpp -----------------------------------------------------------------------------------
[[nodiscard]] bool                     modifierRecipeMatches(std::string const& modifierId, recipes::CraftingGrid const& grid);
[[nodiscard]] std::optional<ItemStack> modifierRecipeAssemble(std::string const& modifierId, recipes::CraftingGrid const& grid);
[[nodiscard]] bool                     mergeRecipeMatches(recipes::CraftingGrid const& grid);
[[nodiscard]] std::optional<ItemStack> mergeRecipeAssemble(recipes::CraftingGrid const& grid);
void                                   registerRecipes();
/// Fires twilightforest:add_modifier for the crafted marks in the player's inventory.
void fireCraftedTriggers(Player& player);

// ---- Belt.cpp ----------------------------------------------------------------------------------------
/// TravellersArmorBeltItem#travellersTrySwapHotbar (+ the client's hasSwapHotbar check); true when
/// anything changed.
bool trySwapHotbar(Player& player);
[[nodiscard]] bool hasSwapHotbar(ItemStack const& legs);

// ---- Display.cpp -------------------------------------------------------------------------------------
/// CycleMapSlotPacket#handle: false when nothing changed.
bool cycleMapSlot(Player& player);
/// Bundle-like insert of `source` (one item) into the goggles stack; `source` keeps the rest, the
/// replaced stored item (if any) goes to `replaced`. False when refused.
bool displayInsert(ItemStack& goggles, ItemStack& source, ItemStack& replaced);
/// Removes the first stored item into `out`; false when the display is empty.
bool displayRemoveFirst(ItemStack& goggles, ItemStack& out);
/// Ticks the active map of worn goggles (GogglesItem#inventoryTick).
void tickDisplayMap(Player& player);
/// The vanilla fallback screen of the item display (sneak-use the goggles).
void openDisplayForm(Player& player);

// ---- Effects.cpp -------------------------------------------------------------------------------------
/// The game time of the player's last armour damage (TFDataAttachments.LAST_DAMAGE_ARMOR_TIME).
[[nodiscard]] std::int64_t lastArmorDamageTime(Player const& player);
void                       setLastArmorDamageTime(Player const& player, std::int64_t time);
/// Seeds the modifier rolls (selftests: perfect dodge, auto repair).
void seedRandom(std::uint64_t seed);
/// TravellersGearLogic#travellersGearAutoRepair for one player at `gameTime`; the pieces repaired.
int autoRepair(Player& player, std::int64_t gameTime);
/// One perfect dodge roll for a projectile hitting `victim` (combat handler; selftests).
bool rollPerfectDodge(Actor& victim, Actor* projectile);
/// Zoom state of a player (is_using_goggles_zoom_modifier).
[[nodiscard]] bool usingZoom(Player const& player);
void               setZoom(Player& player, bool zoom);

// ---- Command.cpp -------------------------------------------------------------------------------------
struct CommandOutcome {
    bool        ok = false;
    std::string message; ///< the success or error text
};
/// TravellersGearCommand for `player` (nullptr: not a player) and `modifierId` as typed.
CommandOutcome runCommand(Player* player, bool add, std::string const& modifierId);

} // namespace gf::feature::tgear
