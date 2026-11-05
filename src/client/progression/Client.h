#pragma once

// U14, client mod: toasts and the locked biome countdown (selftest hooks). Client build only.

#ifdef GF_CLIENT

#include <optional>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"

class LocalPlayer;

namespace gf::client::progression {

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

} // namespace gf::client::progression

#endif
