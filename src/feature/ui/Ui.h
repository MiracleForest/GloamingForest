#pragma once

// Contract "client.ui", server side (header owned by F0, implemented by F12; spike S5).
//
// Vanilla-client fallbacks for TF's screens (rule: vanilla clients never receive companion
// packets): server forms, Data-Driven UI (DDUI) documents where the game supports them, and the
// container-title convention the resource pack's JSON-UI uses to skin vanilla container screens.
// Companions get the native screens (client/ui/Ui.h, opened through feature/common/Screens.h).
//
//     gf::ui::form(player, title, body, {{.text = "Uncraft", .onClick = [](Player& p) { ... }}});

#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class Player;

namespace gf::ui {

struct FormButton {
    std::string                  text;
    std::string                  icon; ///< texture path or URL ("" = none)
    std::function<void(Player&)> onClick;
};
/// A simple button form; `onClose` runs when it was closed without a choice.
void form(Player& player, std::string const& title, std::string const& body, std::vector<FormButton> buttons, std::function<void(Player&)> onClose = {});
/// A DDUI document `document` for screen `screenId`; false when DDUI is not available for this
/// player (then the caller shows a `form`). `onResponse` gets the client's answer.
bool ddui(Player& player, std::string const& screenId, nlohmann::json const& document, std::function<void(Player&, nlohmann::json const& response)> onResponse);
/// The container title text that makes the resource pack's JSON-UI draw skin `skinId` around a
/// vanilla container titled with `titleKey` (vanilla clients).
[[nodiscard]] std::string containerTitleSkin(std::string const& titleKey, std::string const& skinId);

} // namespace gf::ui
