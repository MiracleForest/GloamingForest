#pragma once

// U14: biome restriction enforcement, test hooks (Restrictions.cpp).

#include <optional>
#include <string>

#include "mc/world/level/GameType.h"

#include "core/progression/Rules.h"

class Player;

namespace gf::feature::progression_content {

/// Selftests: the biome the enforcement and the vanilla toast read for `player` ("" clears it);
/// with `forTicks` it ends by itself after that many ticks and puts `restoreType` back.
void setBiomeOverride(Player const& player, std::string const& biome, int forTicks = 0, std::optional<GameType> restoreType = std::nullopt);
/// How often an enforcement acted on `player` (selftests).
[[nodiscard]] int enforcementCount(Player const& player, core::progression::Enforcement e);
void              resetEnforcementCounts(Player const& player);
/// How many locked biome toasts a vanilla player got (selftests).
[[nodiscard]] int vanillaLockedToasts(Player const& player);

} // namespace gf::feature::progression_content
