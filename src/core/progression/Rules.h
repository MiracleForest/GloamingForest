// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/Restriction.java
//   src/main/java/gloamingforest/util/Enforcement.java
//   src/main/java/gloamingforest/init/custom/Enforcements.java
//   src/main/java/gloamingforest/events/ProgressionEvents.java
//   src/main/java/gloamingforest/client/event/LockedBiomeToastHandler.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Biome progression (U14), pure half: the restriction records, the enforcement timing (the outer
// 20-tick gate of ProgressionEvents and each enforcement's own inner gate), the locked biome
// toast's countdown and the advancement tree's layout.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::core::progression {

/// upstream's five enforcements (gloamingforest:<name>).
enum class Enforcement { Darkness, Hunger, Fire, Frost, AcidRain, Unknown };
[[nodiscard]] Enforcement enforcementOf(std::string const& id);
[[nodiscard]] char const* enforcementName(Enforcement e);

/// One entry of the gloamingforest:restrictions registry
/// (data/<ns>/gloaming/restrictions/<biome path>.json).
struct Restriction {
    std::string              biome;        ///< the registry key: the restricted biome
    std::string              structureKey; ///< hint structure ("" none)
    std::string              enforcementId;
    Enforcement              enforcement = Enforcement::Unknown;
    float                    multiplier  = 0;
    std::string              toastIcon;    ///< locked_biome_toast item id ("" none)
    std::vector<std::string> advancements; ///< all needed to lift the restriction (non-empty)

    /// Parses a restriction file; nullopt when it is invalid (no enforcement, no advancements).
    [[nodiscard]] static std::optional<Restriction> fromJson(std::string const& biome, nlohmann::json const& j);
    /// PlayerHelper#playerHasRequiredAdvancements on the server: every listed advancement.
    [[nodiscard]] bool liftedFor(std::function<bool(std::string const&)> const& has) const;
};

/// ProgressionEvents#performProtectionAndPortalChecks: `player.tickCount % 20 == 0`.
[[nodiscard]] constexpr bool outerGate(int tickCount) { return tickCount % 20 == 0; }
/// Each enforcement's own `player.tickCount % n == 0` (60, acid rain 5).
[[nodiscard]] int innerPeriod(Enforcement e);
[[nodiscard]] bool innerGate(Enforcement e, int tickCount);
/// Both gates: whether the enforcement acts on the player at `tickCount`.
[[nodiscard]] bool enforcementDue(Enforcement e, int tickCount);
/// HUNGER: the new amplifier (the current one + (int) m, or (int) m without Hunger; current -1).
[[nodiscard]] int hungerAmplifier(int currentAmplifier, float multiplier);

/// LockedBiomeToastHandler's static state (timeUntilToast 60, shownToast false), stepped on each
/// client tick where `player.tickCount % 5 == 0`, progression is enforced, the player is neither
/// creative nor spectator and the toasts are not disabled.
struct LockedToastCountdown {
    int  timeUntilToast = 60;
    bool shownToast     = false;
    /// One qualifying tick: `lockedWithIcon` when the player stands in a restricted biome whose
    /// restriction has a toast icon. Returns true when the toast is to be shown now.
    bool step(bool lockedWithIcon);
};

/// The advancement tree's layout, as Minecraft 1.21.1 computes it on the server
/// (ServerAdvancementManager#apply runs net.minecraft.advancements.TreeNodePosition over every
/// tree whose root has a display; the positions travel to the clients in DisplayInfo's x / y):
/// Walker's tidy-tree algorithm in Buchheim's linear form. Every node with a display gets a
/// column `x` (its depth among displayed ancestors) and a row `y`; a node without a display is
/// skipped and its children hang from the nearest displayed ancestor. Every displayed node is
/// laid out, whether or not a player can see it (hidden nodes keep their rows, as in Java).
/// Nodes come in any order; siblings keep their order of appearance (Java's order is the
/// AdvancementNode child set's, an identity hash order that changes between runs).
struct TreeNode {
    std::string id;
    std::string parent;          ///< "" for a root
    bool        display = true;  ///< the advancement has a display
    float       x       = 0;     ///< column (Java's int x)
    float       y       = 0;     ///< row
    bool        placed  = false; ///< a position was computed (a displayed node under a displayed root)
};
void layoutTree(std::vector<TreeNode>& nodes);

/// AdvancementToast plays ui.toast.challenge_complete (Mojang's, All Rights Reserved; no Bedrock
/// counterpart) when a challenge toast appears. GloamingForest plays U14's original synthesized
/// sound event in its place (assets/pack/rp/sounds/progression/challenge_complete.ogg, rendered
/// from tools/sounds/recipes/progression.json; category ui).
inline constexpr char kChallengeSound[] = "gf.progression.challenge_complete";
/// A Minecraft text component as a string: a string, {"text"}, {"translate", "with",
/// "fallback"} (each argument rendered first), lists and "extra" appended. `translate(key,
/// fallback, args)` formats a translation (fallback: the component's "fallback", else the key).
using TranslateFn = std::function<std::string(std::string const& key, std::string const& fallback, std::vector<std::string> const& args)>;
[[nodiscard]] std::string renderComponent(nlohmann::json const& component, TranslateFn const& translate);

} // namespace gf::core::progression
