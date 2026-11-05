#pragma once

// U26 (darktower): the Ur-Ghast (UrGhast extends BaseTFBoss) and the carminite ghasts, as far as
// the unit's other parts (ghast trap, reactor, selftests) need them.

#include <vector>

#include "mc/world/level/BlockPos.h"

class Actor;
class Mob;

namespace gf::feature::darktower::ghasts {

/// Java's `instanceof Ghast || instanceof UrGhast` (the ghast trap's pull).
[[nodiscard]] bool isTrappable(Actor const& actor);
[[nodiscard]] bool isUrGhast(Actor const& actor);

/// The ghast trap's effect on an Ur-Ghast: setInTantrum(false), setInTrap() (20 ticks), noPhysics.
void trapUrGhast(Mob& ghast);
/// UrGhast#resetDamageUntilNextPhase.
void resetPhaseDamage(Mob& ghast);

[[nodiscard]] bool  inTantrum(Actor& ghast);
void                setTantrum(Mob& ghast, bool tantrum);
[[nodiscard]] float damageUntilNextPhase(Actor& ghast);
[[nodiscard]] int   trappedTicks(Actor& ghast);
[[nodiscard]] std::vector<BlockPos> trapLocations(Actor& ghast);
/// UrGhast#spawnGhastsAtTraps.
void spawnGhastsAtTraps(Mob& ghast);

/// CarminiteGhastling#makeBossMinion (wander 0.005, max health 6).
void makeBossMinion(Mob& ghastling);

/// Registers the ghasts' behaviours (module install).
void install();

} // namespace gf::feature::darktower::ghasts
