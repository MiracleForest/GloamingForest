#pragma once

// F10-internal parts of the death pipeline (selftests).

#include <functional>

#include "feature/death/Death.h"

class ActorDamageSource;

namespace gf::death {

/// Sets an entity's health (the attribute's current value).
void setHealth(Actor& actor, float value);
/// Runs the lethal handlers for `victim`; true when one kept it alive (health >= 1 then).
bool runLethal(Actor& victim, combat::DamageInfo const& damage);
/// After a hurt returned: a victim that survived lethal damage during it gets the health its
/// handler set back (the game writes the lethal health after `die`).
void reapplySurvival(Actor& victim);
/// Gives `player` the items kept through death back (respawn); true when there were any.
bool returnKeptItems(Player& player);
/// The death pipeline around the game's `die` (the hooks call it).
void handleDeath(Actor& self, ActorDamageSource const& source, std::function<void()> const& die);

} // namespace gf::death
