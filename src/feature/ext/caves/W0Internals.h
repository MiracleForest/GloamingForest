// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U27's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The caves unit needs helpers that F8 and F7 keep in their internal headers, not in their
// contracts. Every caves file reaches them through this header, so a W0 refactor touches one place.
// What U27 uses:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, actorsIn, asMob, onLevelTick, push,
//     removeLater, setMotion, randomOf, uidOf, playersNear} and entity::mobs::{distanceSqr,
//     doHurtTarget, hasLineOfSight, lookAt, moveTo, navigationDone, stopNavigation, target,
//     isWithinMeleeAttackRange, attackable}: the Mob / PathNavigation / LookControl calls of TF's
//     goals (MeleeAttackGoal, RangedAttackGoal, ThrowRiderGoal, YetiRampageGoal, YetiTiredGoal).
//   F7 feature/structure/Internal.h: structure::{blocks::java, blocks::air, blocks::is,
//     blocks::inTag, speleothems::{lattice, latticePositions, shouldDoAStalactite,
//     shouldDoAStalagmite, spike}, boxCenter, intersection, kSeaLevel}: the block helpers and the
//     speleothem lattice of TF's troll cave and yeti cave pieces; F7 feature/structure/Region.h:
//     structure::BlockSourceRegion (selftests place pieces into the live world).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace) and
// feature/structure/Structure.h (blocks / speleothem lattice helpers); then this header includes
// the contracts only.

#include "feature/entity/EntityInternal.h" // IWYU pragma: export
#include "feature/structure/Internal.h"    // IWYU pragma: export
#include "feature/structure/Region.h"      // IWYU pragma: export
