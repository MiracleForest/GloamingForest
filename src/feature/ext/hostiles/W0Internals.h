// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U17's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The hostiles unit needs a few helpers that F8 keeps in its internal header, not in its contract:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, actorsIn, asMob, findState, randomOf,
//     uidOf, setMotion, push, removeLater, playersNear, eyeOf} and entity::mobs::{target,
//     lastHurtByMob, hasLineOfSight, distanceSqr, moveTo, navigationDone, stopNavigation, lookAt,
//     legacySolid, randomPosAway}: Java's Mob / PathNavigation / LookControl calls of upstream's goals
//     (RedcapShy / LightTNT / PlantTNT, the wraith's flight, RangedAttackGoal, AvoidEntityGoal).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace); then this header includes
// the contract only.

#include "feature/entity/EntityInternal.h" // IWYU pragma: export
