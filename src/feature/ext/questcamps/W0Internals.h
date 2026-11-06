// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U19's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The questcamps unit needs helpers that F8, F7 and F9 keep in their internal headers, not in
// their contracts. Every questcamps file reaches them through this header, so a W0 refactor
// touches one place. What U19 uses:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, asMob, uidOf, actorsIn, randomOf,
//     playersNear} and entity::mobs::{distanceSqr, hasLineOfSight, lookAt, moveTo, navigationDone,
//     stopNavigation}: Java's Mob / PathNavigation / LookControl calls of the Questing Ram's goals
//     (QuestRamEatWoolGoal, TemptGoal);
//   F7 feature/structure/Internal.h: structure::{blocks::*, blockentities::{spawner, lootTable,
//     entity}, rotateDir, relativeRotation, transformPos}: the block helpers of TF's template
//     features (FeatureLogic, the druid hut's and wells' marker handling, the graveyard);
//     feature/structure/Region.h (BlockSourceRegion: the selftests place into the live world);
//   F9 feature/item/DataFiles.h: data::{list, read, onReload}: the twilight/quests registry
//     (QuestReloadListener).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace), feature/structure/
// Structure.h (blocks / block entity helpers) and a data contract.

#include "feature/entity/EntityInternal.h"   // IWYU pragma: export
#include "feature/item/DataFiles.h"          // IWYU pragma: export
#include "feature/structure/Internal.h"      // IWYU pragma: export
#include "feature/structure/Region.h"        // IWYU pragma: export
