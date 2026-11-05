// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U11's internal interfaces between its server files (and its selftests). Not a contract.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "feature/recipes/Recipes.h"

class Actor;
class BlockPos;
class BlockSource;
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
/// The sound event of the last map cycle (selftests).
[[nodiscard]] std::string lastCycleSound();
/// Ticks the active map of worn goggles (GogglesItem#inventoryTick).
void tickDisplayMap(Player& player);
/// The client mod's secondary click on slot `index` of `collection` (the player's inventory, armour or
/// off-hand) with the cursor (PlayerUIContainer slot 0): TravellersGogglesItem's bundle-style insert /
/// remove. False when nothing changed.
bool displaySlotClick(Player& player, std::string const& collection, int index);
/// The vanilla fallback screen of the item display (sneak-use the goggles).
void openDisplayForm(Player& player);

// ---- Module.cpp ----------------------------------------------------------------------------------------
/// The game time of the wearer's last armour damage (TFDataAttachments.LAST_DAMAGE_ARMOR_TIME).
[[nodiscard]] std::int64_t lastArmorDamageTime(Actor const& wearer);
void                       setLastArmorDamageTime(Actor const& wearer, std::int64_t time);
/// Seeds the modifier rolls (selftests: perfect dodge, auto repair).
void seedRandom(std::uint64_t seed);
/// Replaces the perfect dodge probability (selftests; nullopt: the modifier's).
void setDodgeProbabilityForTest(std::optional<float> probability);
/// TravellersGearLogic#travellersGearAutoRepair for one wearer at `gameTime`; the pieces repaired.
int autoRepair(Actor& wearer, std::int64_t gameTime);
/// Successful perfect dodges so far (selftests).
[[nodiscard]] int dodgeCount();
/// One perfect dodge roll for a projectile hitting `victim` (combat handler; selftests).
bool rollPerfectDodge(Actor& victim, Actor* projectile);
/// Zoom state of a player (is_using_goggles_zoom_modifier).
[[nodiscard]] bool usingZoom(Player const& player);
void               setZoom(Player& player, bool zoom);
/// MultifaceBlock#canAttachTo: the neighbour on `face` (FacingID) of `pos` has a full face towards it.
[[nodiscard]] bool canAttachRedThread(BlockSource& region, BlockPos const& pos, int face);
/// TFDataAttachments.TRAVELLERS_GOGGLES_RED_THREAD_VISION of a player (default true, kept on death,
/// stored in gf:tgear:red_thread_vision); the toggle needs the active modifier.
[[nodiscard]] bool redThreadVision(Player const& player);
bool               toggleRedThreadVision(Player& player);
/// PlayerSpawnPhantomsEvent DENY: the nearest player to a natural phantom spawn wears active
/// all-night goggles.
[[nodiscard]] bool deniesPhantomAt(int dim, Vec3 const& pos);

// ---- Command.cpp -------------------------------------------------------------------------------------
struct CommandOutcome {
    bool        ok = false;
    std::string message; ///< the success or error text
};
/// TravellersGearCommand for `player` (nullptr: not a player) and `modifierId` as typed.
CommandOutcome runCommand(Player* player, bool add, std::string const& modifierId);
/// sendSuccess(..., true): the other operators get "[<source>: <message>]" (gray, italic).
void broadcastToOps(Player* source, std::string const& message);

} // namespace gf::feature::tgear
