// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U16's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The fauna unit needs a few helpers that F8 and F9 keep in their internal headers, not in their
// contracts. Every fauna file reaches them through this header, so a W0 refactor touches one place.
// What U16 uses:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, actorsIn, asMob, findState, playersNear,
//     randomOf, setMotion, uidOf} and entity::mobs::{distanceSqr, lastHurtByMob, lookAt, moveTo,
//     navigationDone, stopNavigation, target, attackable}: Java's Mob / PathNavigation / LookControl
//     calls of TF's goals (SeekBreadGoal, FlyingBird's flight, the prey target goals on vanilla mobs),
//     and the goal selectors' presence (the native goals are added again after F8's sweep).
//   F9 feature/item/DataFiles.h: item::data::{list, read}: the tiny_bird_variant and
//     dwarf_rabbit_variant registries (data/twilightforest/twilight/<registry>/*.json).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace) and a data-files contract.

#include "feature/entity/EntityInternal.h" // IWYU pragma: export
#include "feature/item/DataFiles.h"        // IWYU pragma: export
