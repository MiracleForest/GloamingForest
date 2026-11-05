#pragma once

// U14, client mod: toasts and the locked biome countdown (selftest hooks). Client build only.

#ifdef GF_CLIENT

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

class LocalPlayer;

namespace gf::client::screen {
class Canvas;
}

namespace gf::client::progression {

/// A toast of this unit (Toasts.cpp: Java's AdvancementToast, TF's LockedBiomeToast).
struct ToastInfo {
    std::string what;              ///< the advancement id, or "locked_biome"
    std::string kind;              ///< "advancement" or "locked_biome"
    std::string frame;             ///< task / goal / challenge (advancements)
    std::string title;             ///< the first line (the type's toast name, "Biome Locked!")
    std::string body;              ///< the advancement's title, or the second line
    std::string icon;              ///< the icon's item id
    unsigned    titleColor = 0xFFFFFF00;
    int         ms         = 5000; ///< display time
};
/// Queues a toast; `iconItem`: the icon as item JSON with its components (else `info.icon`).
void queueToast(ToastInfo info, nlohmann::json const& iconItem = {});
/// Draws the toasts (HUD layer: fromScreen false; the advancement screen over itself: true).
void drawToasts(screen::Canvas& canvas, bool fromScreen);
/// The toast of `what`, waiting or shown (selftests).
[[nodiscard]] std::optional<ToastInfo> toastOf(std::string const& what);
/// Removes the toasts of `what` (selftests); returns how many.
std::size_t cancelToasts(std::string const& what);
void        registerToastLayer();
/// StringSplitter#splitLines: `text` on lines of at most `width` GUI pixels.
[[nodiscard]] std::vector<std::string> splitLines(screen::Canvas& canvas, std::string const& text, float width);

/// The last toast this unit showed (selftests).
struct ShownToast {
    std::string what; ///< the advancement id, or "locked_biome"
    std::string title;
    std::string body;
    std::string icon;
    int         ms = 0;
};
[[nodiscard]] std::optional<ShownToast> lastToast();
/// The locked biome countdown (timeUntilToast, shownToast) and how many locked toasts were shown.
[[nodiscard]] std::pair<int, bool> countdown();
void                               resetCountdown();
[[nodiscard]] int                  lockedToastCount();
/// Selftests: the biome the locked toast reads for the local player ("" clears it).
void setClientBiomeOverride(std::string const& biome);
/// One client tick of LockedBiomeToastHandler (`biomeOverride`: selftests).
void tickLockedToast(LocalPlayer& player, std::string const& biomeOverride);
/// Shows an advancement toast as "progression.toast" does.
void showAdvancementToast(nlohmann::json const& msg);
/// A text component (the advancement's title or description, with its arguments) in the game's
/// language.
[[nodiscard]] std::string clientText(nlohmann::json const& component);

} // namespace gf::client::progression

#endif
