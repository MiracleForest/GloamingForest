#pragma once

// U11 extension (rule R18: new files only; to be folded into F12's client contracts by A8 in W2b).
// Client build only.
//
// - Turning: LocalPlayer::localPlayerTurn (the mouse / stick turn of the local player) scaled by the
//   factor set here (TF's slowZoomSensitivity, CalculatePlayerTurnEvent). This is an R8 exception
//   (LocalPlayer movement is F12's): F12's movement contract (client/input/Input.h onLocalTick) has no
//   turn scale. A8 folds it into F12's movement / camera contract.
// - The zoom FOV needs no client hook: the server narrows the zooming player's camera with a camera
//   FOV instruction (feature/tgear, ComputeFovModifierEvent). F12's camera::addFovModifier
//   (LocalPlayer::getFieldOfViewModifier), LevelRendererPlayer::getFov and CameraAPI::tryGetFOV were
//   all tried in rounds 1-2 and none reaches the 1.26.51 world projection (the vanilla camera's FOV
//   component), so no FOV hook is installed.
// - Secondary clicks in container screens (Java's Item#overrideStackedOnOther / overrideOtherStackedOnMe,
//   the bundle-style clicks of TF's goggles): ContainerScreenController::_handlePlaceOne (a secondary
//   click with an item on the cursor) and _handleTakeHalf (with an empty cursor) ask the handler first;
//   true takes the click (the game's own split / place-one does not run).
//
//     gf::ext::tgear::client::setTurnScale(zooming ? 0.2f : 1.f);

#ifdef GF_CLIENT

#include <cstdint>
#include <functional>
#include <string>

class ItemStack;

namespace gf::ext::tgear::client {

/// Scales the local player's turn deltas (1 = unchanged).
void setTurnScale(float scale);

/// A secondary click on slot `index` of collection `collection` ("hotbar_items", "inventory_items",
/// "armor_items", "offhand_items", "container_items", ...) with `cursor` on the cursor (empty for
/// take-half).
using SlotClickFn = std::function<bool(std::string const& collection, int index, ItemStack const& slot, ItemStack const& cursor)>;
void setSlotClickHandler(SlotClickFn fn);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT