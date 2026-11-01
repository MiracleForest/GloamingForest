// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/common/ValueBox.h, click and hold), LGPL-3.0-or-later.
#pragma once

// Click and hold on a block (W0a): one shared message pair for "a short right-click does one
// thing, holding the button does another". Bedrock sends use-on packets, not button states, so
// the server asks the player's client mod to watch the use button:
//
//     if (gf::hold::awaitHold(player, pos, onShort, onHold)) return ia::Result::Consume;
//     // vanilla players: false, keep your own click fallback
//
// The client mod answers `hold.release {token, state: hold|short|cancel}` (held for more than 3
// ticks, released earlier, looked away / sneaked); the action runs with the player looked up
// again. While `holdPending(player, pos)`, consume the button's repeats without acting. Never add
// a second hold channel.

#include <functional>

#include "core/GlobalPos.h"

class Player;

namespace gf::hold {

using Action = std::function<void(Player&)>;

/// Asks the companion's client to watch the use button at `pos`. False (nothing sent) for
/// vanilla players.
bool awaitHold(Player& player, core::GlobalPos const& pos, Action onShort, Action onHold);
/// A hold of `player` at `pos` waits for the client's answer.
[[nodiscard]] bool holdPending(Player const& player, core::GlobalPos const& pos);

} // namespace gf::hold
