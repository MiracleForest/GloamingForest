#pragma once

// F12 additions to the server halves of "client.ui" and "client.input+movement" (rule R18: new
// files only; folded by A8 in W2b).
//
// - input::onKey: key bindings registered with gf::client::keys::registerKey are forwarded by
//   the client mod (channel "input.key" {id, down}); servers react to them here (companions
//   only; vanilla players use item-use fallbacks).
// - ui::parseTitleSkin and the title marker: what the resource pack's JSON-UI reads from a
//   container title made by containerTitleSkin.
// - movement: the auth-input edge detector's input (selftests feed samples), the validators'
//   semantics, their removal and the correction counter.

#include <cstddef>
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

/// Container title skins for vanilla clients do not exist on 1.26.51 (spike S5): containerTitleSkin
/// returns `titleKey` and vanilla players see the vanilla container. The vanilla chest screen gets
/// its title only as the `$container_title` variable, and only property values see it: `requires`
/// expressions (on the panel and on the screen element: prefix, equality and substring tests are all
/// false), view bindings (even ones over literals) and the `#title_text` binding see no title, and
/// `ignored` does not take the screen's variables. An image whose texture is `$container_title`
/// does draw a skin named by the title, but every other chest then shows the missing-texture
/// colour, so no title can switch a skin on for one container only.
inline constexpr bool kJsonUiSkins = false;

/// DDUI documents waiting for an answer (tests). A document ends with its answer, when a newer
/// document of the same screen replaces it or when its player disconnects.
[[nodiscard]] std::size_t dduiSessions();

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

// Validators (gf::movement::validate). The auth input hook checks every PlayerAuthInputPacket of a
// real client before the server queues it: `from` is the feet position of the player's last
// accepted move (the server's position for a player's first packet and after a dimension change),
// `to` the feet position the client claims (the packet's position minus the eye height). A packet
// that acknowledges a teleport of the server (HandledTeleport: a correction, /tp, a portal, a
// respawn) is not checked: its position is the new start. A rejected move is corrected two server
// ticks later, once the queued input was applied: the player is teleported back to `from`. Until
// the client acknowledges that teleport (or 40 packets at most) its moves are not checked, so the
// moves it sent before the teleport arrived do not cause more corrections. SimulatedPlayers send no
// auth input: selftests call validateMove.

/// Number of registered validators (tests).
[[nodiscard]] std::size_t validatorCount();
/// Runs the validators on a move; false when one rejects it.
[[nodiscard]] bool validateMove(Player& player, Vec3 const& from, Vec3 const& to);
/// Removes the validator `id` (selftests and fixtures); false when there was none.
bool removeValidator(std::string const& id);
/// Corrections sent since the server started (tests).
[[nodiscard]] std::size_t corrections();

} // namespace gf::movement
