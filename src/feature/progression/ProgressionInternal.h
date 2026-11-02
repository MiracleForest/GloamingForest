#pragma once

// F10-internal parts of gf::progression (the death pipeline fires kills; selftests).

#include <string>

#include "feature/progression/Progression.h"

namespace gf::progression {

/// A player killed `victim` with damage type `damageType`: the onKilled listeners and the
/// "minecraft:player_killed_entity" trigger.
void fireKilled(Player& killer, Actor& victim, std::string const& damageType);
/// Drops the cached progress of `player` (it is read back from the world DB on the next use).
void forgetPlayer(Player const& player);
/// The biome id at the player ("twilightforest:dark_forest", "minecraft:plains"; "" when unloaded).
[[nodiscard]] std::string biomeIdAt(Player& player);

} // namespace gf::progression
