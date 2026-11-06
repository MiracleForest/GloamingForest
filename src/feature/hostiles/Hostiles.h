// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U17 (hostiles): the parts of the unit's module and the seams its selftests use.

#include <cstdint>
#include <string>

class Actor;
class BlockPos;
class Mob;

namespace gf::feature::hostiles {

namespace wolves {
void install();
} // namespace wolves

namespace spiders {
void install();
} // namespace spiders

namespace druid {
void install();
/// Makes a druid a baby before its first tick (SkeletonDruid#setBaby on a fresh jockey).
void markBaby(Actor& druid);
/// The druid's ranged attack (SkeletonDruid#performRangedAttack) at `target`.
void performRangedAttack(Mob& druid, Actor& target);
} // namespace druid

namespace redcaps {
void install();
/// Redcap#heldTNT's count (TNTLeft).
[[nodiscard]] int tntLeft(Actor& redcap);
} // namespace redcaps

namespace wraith {
void install();
} // namespace wraith

namespace beetles {
void install();
/// SlimeBeetle#performRangedAttack at `target`.
void slimeShoot(Mob& beetle, Actor& target);
} // namespace beetles

/// Spawn placements, spawn behaviours (all U17 mobs).
void installSpawns();

} // namespace gf::feature::hostiles
