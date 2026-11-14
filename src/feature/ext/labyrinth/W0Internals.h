// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U23's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b), the same
// set U28 uses (feature/ext/aurora/W0Internals.h):
//   F8 feature/entity/EntityInternal.h: entity::{actorById, asMob, uidOf, randomOf, setMotion, push,
//     playersNear, actorsIn, onLevelTick} and entity::mobs::{target, distanceSqr, hasLineOfSight,
//     lookAt, moveTo, navigationDone, stopNavigation, isWithinMeleeAttackRange}: the Java Mob /
//     PathNavigation / LookControl calls of the minotaurs' goals (GroundAttackGoal, MeleeAttackGoal).
//   F7 feature/structure/Internal.h: structure::{blocks::java, blocks::is, blocks::air, selectors,
//     resetRegionRandom}: the block helpers of the minotaur maze pieces; F7 feature/structure/Region.h:
//     structure::BlockSourceRegion (the selftests place pieces into the live world).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace) and feature/structure/
// Structure.h (blocks helpers); then this header includes the contracts only.

#include "feature/entity/EntityInternal.h" // IWYU pragma: export
#include "feature/structure/Internal.h"    // IWYU pragma: export
#include "feature/structure/Region.h"      // IWYU pragma: export
