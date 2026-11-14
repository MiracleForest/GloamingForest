#pragma once

// Contract "client.input+movement", client side (header owned by F0, implemented by F12).
// Client build only; the server half is feature/movement/Movement.h.
//
// Key bindings (upstream's 4 keybinds: red_thread_vision, item_display_map_cycle, zoom, swap_hotbar;
// keyboard defaults only, touch and controller players use item-use fallbacks) and the local
// player's movement hook (client-authoritative movement: double jump, glide, step up, water
// walk...). Units never hook LocalPlayer movement (rule R8).
//
//     gf::client::keys::registerKey("tgear.zoom", 'Z', "key.categories.gloamingforest.travellers_gear", onZoom, onZoomEnd);
//     gf::client::movement::onLocalTick(0, [](gf::client::movement::MoveState& s) { ... });

#ifdef GF_CLIENT

#include <functional>
#include <string>

#include "mc/deps/core/math/Vec3.h"

namespace gf::client::keys {

/// Registers a key binding (`defaultKey`: Windows virtual-key code, 0 = unbound; shown in the
/// game's controls under `category`, a translation key).
void registerKey(std::string const& id, int defaultKey, std::string const& category, std::function<void()> onPress, std::function<void()> onRelease = {});
/// Whether the key bound to `id` is held.
[[nodiscard]] bool down(std::string const& id);

} // namespace gf::client::keys

namespace gf::client::movement {

/// The local player's movement for this tick (read and changed by handlers, applied after them).
struct MoveState {
    Vec3  velocity{};
    float forward = 0, strafe = 0; ///< input axes (-1..1)
    bool  onGround   = false;
    bool  inWater    = false;
    bool  jumpHeld   = false;
    bool  jumpEdge   = false;  ///< jump pressed this tick
    bool  sneakHeld  = false;
    bool  sprinting  = false;
    bool  gliding    = false;
    float stepHeight = 0.6f;
    float fallDistance = 0;
};
/// Runs every client tick for the local player, by descending priority.
void onLocalTick(int priority, std::function<void(MoveState& state)> fn);

} // namespace gf::client::movement

#endif // GF_CLIENT
