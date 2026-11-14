// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U30's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The castle mobs and the gazebo's WIP texts need a few helpers that F8 and F7 keep in their
// internal headers, not in their contracts. Every finalcastle mob / WIP file reaches them through
// this header, so a W0 refactor touches one place. What U30 uses:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, actorsIn, asMob, eyeOf, randomOf, uidOf}
//     and entity::mobs::{distanceSqr, hasLineOfSight, lookAt, moveTo, navigationDone,
//     stopNavigation, target}: Java's Mob / PathNavigation / LookControl calls of RangedAttackGoal and
//     the roving cube's goals (as U28 and U16 do).
//   F8 feature/boss/BossInternal.h (selftests only): boss::{spawnerRuleForTest, hasBar}.
//   F7 feature/structure/Internal.h + Region.h (selftests only): structure::markerFor (the registered
//     "final_castle_wip" handler run as a template placement runs it) and structure::
//     BlockSourceRegion (a live region for it).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace) and test seams of the boss
// and structure contracts; then this header includes the contracts only.
//
// The helper actors of the WIP texts (gloamingforest:final_castle_text, gloamingforest:
// final_castle_punch; assets/pack/bp/entities/finalcastle) use the "gloamingforest:" namespace for
// GloamingForest-only actors (as F8's gloamingforest:part and U4's gloamingforest:drying_rack_item):
// a contract extension for A8 to settle one namespace for every unit's helper actors.

#include "feature/boss/BossInternal.h"     // IWYU pragma: export
#include "feature/entity/EntityInternal.h" // IWYU pragma: export
#include "feature/structure/Internal.h"    // IWYU pragma: export
#include "feature/structure/Region.h"      // IWYU pragma: export
