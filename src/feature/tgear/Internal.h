// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U11's internal interfaces between its server files (and its selftests). Not a contract.
#pragma once

#include <functional>

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
/// Fires gloamingforest:add_modifier for the crafted marks in the player's inventory.
void fireCraftedTriggers(Player& player);

// ---- WornModel.cpp ---------------------------------------------------------------------------------
/// The bits of a player's worn-model state (its synced mark variant, read by attachables as
/// q.mark_variant).
inline constexpr int kWornGloves = 1, kWornSlim = 2, kWornBelt = 4, kWornZoom = 8, kWornFirstPersonGloves = 16;
/// ArmorRender#isModelSlim: the player's skin has slim arms.
[[nodiscard]] bool hasSlimArms(Player const& player);
/// The worn-model bits of `player` (gloves, slim, belt, zoom, first-person glove overlay).
[[nodiscard]] int wornModelBits(Player const& player, bool zooming);
/// The state the player carries now (its mark variant).
[[nodiscard]] int wornModelState(Player const& player);
/// Writes the bits into the player's mark variant when they changed (synced to every viewer).
void refreshWornModel(Player& player);
/// The client mod's firstPersonGloveOverlay of `player` ("tgear.glove_overlay").
void setFirstPersonGloveOverlay(Player& player, bool on);
void forgetWornModel(Player const& player);

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
/// Mutable#removeFirstFree(slot): removes the first stored item that `mayPlace` accepts (any item when
/// it is empty) into `out`; false when none.
bool displayRemoveFirst(ItemStack& goggles, ItemStack& out, std::function<bool(ItemStack const&)> const& mayPlace = {});
/// The slot predicate of a secondary click (Slot#mayPlace) of the player's collection / index (the
/// selftests).
[[nodiscard]] bool displaySlotMayPlace(Player& player, std::string const& collection, int index, ItemStack const& item);
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
/// GogglesZoomPacket#handle: `fov` is the client's FOV times the zoom (0: 70 x the zoom).
void               setZoom(Player& player, bool zoom, float fov = 0.f);
/// MultifaceBlock#canAttachTo: the neighbour on `face` (FacingID) of `pos` has a full face towards it.
[[nodiscard]] bool canAttachRedThread(BlockSource& region, BlockPos const& pos, int face);
/// TFDataAttachments.TRAVELLERS_GOGGLES_RED_THREAD_VISION of a player (default true, kept on death,
/// stored in gf:tgear:red_thread_vision); the toggle needs the active modifier.
[[nodiscard]] bool redThreadVision(Player const& player);
bool               toggleRedThreadVision(Player& player);
/// PlayerSpawnPhantomsEvent DENY: the nearest player to a natural phantom spawn wears active
/// all-night goggles.
[[nodiscard]] bool deniesPhantomAt(int dim, Vec3 const& pos);

// ---- Machines.cpp ------------------------------------------------------------------------------------
/// removeModifiersFromTravellersGear: nullopt for no travellers piece (the game's result), an empty stack
/// when refused (two pieces, no insertable modifier), else the piece without its insertable modifiers.
[[nodiscard]] std::optional<ItemStack> grindstoneResult(ItemStack const& top, ItemStack const& bottom);
/// extractItemsFromSwapHotbarModifier: the stacks the grindstone gives back on take.
[[nodiscard]] std::vector<ItemStack> grindstoneReturns(ItemStack const& top, ItemStack const& bottom);
/// cancelCombiningTravellersGear and the book rule of a survival player.
[[nodiscard]] bool anvilRefuses(ItemStack const& input, ItemStack const& material, bool creative);
/// TravellersArmorBeltItem#canFitInsideContainerItems(stack) false: a belt with its container.
[[nodiscard]] bool storageRefuses(ItemStack const& item);
void               registerMachines();

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
