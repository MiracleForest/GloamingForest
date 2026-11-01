#pragma once

// F12 additions to the server halves of "client.ui" and "client.input+movement" (rule R18: new
// files only; folded by A8 in W2b).
//
// - input::onKey: key bindings registered with gf::client::keys::registerKey are forwarded by
//   the client mod (channel "input.key" {id, down}); servers react to them here (companions
//   only; vanilla players use item-use fallbacks).
// - ui::parseTitleSkin: the inverse of containerTitleSkin (the JSON-UI's view of a title).
// - movement::Input: one auth-input sample, the edge detector's input (selftests feed samples).

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "mc/deps/core/math/Vec3.h"

class Player;

namespace gf::input {

/// Called on the server thread when a companion presses (`down`) or releases a key bound to `id`.
/// Several handlers per id are allowed.
void onKey(std::string const& id, std::function<void(Player& player, bool down)> fn);
/// Runs the handlers of `id` as if `player`'s client had sent it (selftests).
void dispatchKey(Player& player, std::string const& id, bool down);

} // namespace gf::input

namespace gf::ui {

/// What the resource pack's JSON-UI reads from a container title made by containerTitleSkin.
struct TitleSkin {
    std::string skinId;   ///< "" when the title carries no skin
    std::string titleKey; ///< the title shown (translation key or text)
};
[[nodiscard]] TitleSkin parseTitleSkin(std::string const& title);
/// The skins the resource pack's JSON-UI (assets/pack/rp/ui/chest_screen.json) can draw: texture
/// paths under textures/gui/. A title is "gfskin:<id>;<title>"; the JSON-UI matches the whole
/// marker, so a new skin needs an entry there and here.
inline constexpr std::array<std::string_view, 1> kJsonUiSkins{"ui/fixture_container"};

} // namespace gf::ui

namespace gf::movement {

/// One input sample of a player (PlayerAuthInputPacket on the server).
struct InputSample {
    bool jump     = false;
    bool sneak    = false;
    bool sprint   = false;
    bool onGround = true;
};
/// Feeds a sample to the edge detector (the auth input hook does this every input tick;
/// selftests feed SimulatedPlayers, which send no input packets).
void feedInput(Player& player, InputSample const& sample);
/// Number of registered validators (tests).
[[nodiscard]] std::size_t validatorCount();
/// Runs the validators on a move; false when one rejects it (the auth input hook corrects the
/// player then).
[[nodiscard]] bool validateMove(Player& player, Vec3 const& from, Vec3 const& to);

} // namespace gf::movement
