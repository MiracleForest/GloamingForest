// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/common/Screens.h), LGPL-3.0-or-later.
#pragma once

// Server side of the screen framework (W0a). Native screens are drawn by the client mod
// (client/screen/base/Screen.h; upstream's GUIs through client/ui/Ui.h), vanilla players get the form.
//
// Every screen has a native version (drawn by the client mod, src/client/screen/) and a form
// fallback for vanilla clients. A unit registers a screen *type* once:
//
//     screens::registerType({
//         .id      = "uncraftmaps.uncrafting",
//         .data    = [](Player& p, Json const& ctx) -> std::optional<Json> { ... },  // state to show
//         .action  = [](Player& p, Json const& ctx, Json const& action) { ... },     // native clicks
//         .form    = [](Player& p, Json const& ctx) { ... },                         // vanilla fallback
//     });
//     screens::open(player, "uncraftmaps.uncrafting", {{"pos", {0, 64, 0}}});
//
// `open` sends "screen.open" {type, context, data} to companions (the client builds the screen
// registered for `type`), or calls `form` for vanilla players. Native actions arrive as
// "screen.action" {type, context, action}; after the handler runs, the server sends
// "screen.update" {type, context, data} with fresh data (or "screen.close" when `data` returns
// nullopt, e.g. the block is gone). The context identifies what the screen edits
// (a hand slot, a block position) and is re-validated by `data` on every action.

#include <functional>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

class Player;

namespace gf::feature::common::screens {

using Json = nlohmann::json;

struct ScreenType {
    std::string id; ///< "<unit>.<screen>"
    /// The data the native screen shows; nullopt when the screen can't be (or stay) open.
    std::function<std::optional<Json>(Player&, Json const& context)> data;
    /// A native screen action (companions only).
    std::function<void(Player&, Json const& context, Json const& action)> action;
    /// The vanilla fallback (server forms).
    std::function<void(Player&, Json const& context)> form;
    /// The player closed the native screen (Esc, the inventory key; "screen.closed"). Optional.
    std::function<void(Player&, Json const& context)> closed;
};

void registerType(ScreenType type);

/// Opens a screen: native for companions, the form otherwise. Returns false when `data`
/// refuses (nothing opened).
bool open(Player& player, std::string const& type, Json const& context = Json::object());
/// Opens the form fallback even for a companion (e.g. a "use forms" preference).
bool openForm(Player& player, std::string const& type, Json const& context = Json::object());
/// Sends fresh data to an open native screen (no-op for vanilla players).
void refresh(Player& player, std::string const& type, Json const& context);
/// Closes the player's native screen of `type`.
void close(Player& player, std::string const& type);

} // namespace gf::feature::common::screens
namespace gf { namespace screens = feature::common::screens; }
