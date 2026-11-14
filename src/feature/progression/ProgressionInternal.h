#pragma once

// F10-internal parts of gf::progression (the death pipeline fires kills; selftests).

#include <optional>
#include <string>

#include "feature/progression/Progression.h"

namespace gf::progression {

/// upstream's MissingAdvancementToast (MissingAdvancementToastPacket from GloamingPortalBlock#entityInside and
/// ProgressionEvents#checkForPortalCreation): UPPER_TEXT, the title and icon sent when the
/// advancement has no display, and the time it is shown (10 s).
inline constexpr char kMissingUpperText[] = "misc.gloamingforest.advancement_required";
inline constexpr char kMissingNoTitle[]   = "gloamingforest.ui.advancement.no_title";
inline constexpr char kMissingIcon[]      = "gloamingforest:gloaming_portal_miniature_structure";
inline constexpr int  kMissingToastMs     = 10000;

/// What showMissing sends for `advancementId`: the advancement's display title (a translation key
/// when `titleIsKey`) and icon item, or upstream's "no title" fallback with the portal miniature.
struct MissingToast {
    std::string title;
    bool        titleIsKey = true;
    std::string icon;
};
[[nodiscard]] MissingToast missingToastFor(std::string const& advancementId);

/// A player killed `victim` with damage type `damageType`: the onKilled listeners and the
/// "minecraft:player_killed_entity" trigger.
void fireKilled(Player& killer, Actor& victim, std::string const& damageType);
/// Drops the cached progress of `player` (it is read back from the world DB on the next use).
void forgetPlayer(Player const& player);
/// The biome id at the player ("gloamingforest:dark_forest", "minecraft:plains"; "" when unloaded).
[[nodiscard]] std::string biomeIdAt(Player& player);

#ifdef GF_CLIENT
namespace client {
/// The last missing advancement toast the client mod queued (ProgressionClient.cpp): what the
/// server sent and what went to F12's toast (gf::client::toast::show). Selftests.
struct ShownMissingToast {
    std::string advancement; ///< the advancement the server named
    std::string titleKey;    ///< the toast's first line (UPPER_TEXT)
    std::string bodyKey;     ///< the advancement's title (a translation key or plain text)
    std::string iconItem;
    int         ms = 0;
};
[[nodiscard]] std::optional<ShownMissingToast> lastMissingToast();
} // namespace client
#endif

} // namespace gf::progression
