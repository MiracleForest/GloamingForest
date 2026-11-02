#pragma once

// F10-internal parts of gf::progression (the death pipeline fires kills; selftests).

#include <string>

#include "feature/progression/Progression.h"

namespace gf::progression {

/// TF's MissingAdvancementToast (MissingAdvancementToastPacket from TFPortalBlock#entityInside and
/// ProgressionEvents#checkForPortalCreation): UPPER_TEXT, the title and icon sent when the
/// advancement has no display, and the time it is shown (10 s).
inline constexpr char kMissingUpperText[] = "misc.twilightforest.advancement_required";
inline constexpr char kMissingNoTitle[]   = "twilightforest.ui.advancement.no_title";
inline constexpr char kMissingIcon[]      = "twilightforest:twilight_portal_miniature_structure";
inline constexpr int  kMissingToastMs     = 10000;

/// What showMissing sends for `advancementId`: the advancement's display title (a translation key
/// when `titleIsKey`) and icon item, or TF's "no title" fallback with the portal miniature.
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
/// The biome id at the player ("twilightforest:dark_forest", "minecraft:plains"; "" when unloaded).
[[nodiscard]] std::string biomeIdAt(Player& player);

} // namespace gf::progression
