#pragma once

// U11 extension (rule R18: new files only; to be folded into F12's client contracts by A8 in W2b).
// Client build only.
//
// - FOV: F12's camera::addFovModifier (client/world/Sky.h) wraps LocalPlayer::getFieldOfViewModifier,
//   which Bedrock 1.26.51 no longer reads for the world camera (round 1 of U11's review: zoom.png and
//   zoom-off.png were identical while the modifier asked for 0.3). The world FOV is
//   LevelRendererPlayer::getFov; this hook multiplies its result by the factor set here, as Java's
//   ComputeFovModifierEvent multiplies the FOV modifier (TF's goggles zoom).
// - Turning: LocalPlayer::localPlayerTurn (the mouse / stick turn of the local player) scaled by the
//   factor set here (TF's slowZoomSensitivity, CalculatePlayerTurnEvent).
// - Secondary clicks in container screens (Java's Item#overrideStackedOnOther / overrideOtherStackedOnMe,
//   the bundle-style clicks of TF's goggles): ContainerScreenController::_handlePlaceOne (a secondary
//   click with an item on the cursor) and _handleTakeHalf (with an empty cursor) ask the handler first;
//   true takes the click (the game's own split / place-one does not run).
//
//     gf::ext::tgear::client::setFovFactor(zooming ? 0.3f : 1.f);

#ifdef GF_CLIENT

#include <cstdint>
#include <functional>
#include <string>

class ItemStack;

namespace gf::ext::tgear::client {

/// Multiplies the world FOV (1 = unchanged).
void setFovFactor(float factor);
[[nodiscard]] float fovFactor();
/// Scales the local player's turn deltas (1 = unchanged).
void setTurnScale(float scale);
/// How many times the world FOV was asked for (tests and e2e: the hook is live).
[[nodiscard]] std::uint64_t fovCalls();
/// The last world FOV the game computed before the factor (degrees).
[[nodiscard]] float lastBaseFov();

/// A secondary click on slot `index` of collection `collection` ("hotbar_items", "inventory_items",
/// "armor_items", "offhand_items", ...) with `cursor` on the cursor (empty for take-half).
using SlotClickFn = std::function<bool(std::string const& collection, int index, ItemStack const& slot, ItemStack const& cursor)>;
void setSlotClickHandler(SlotClickFn fn);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT
