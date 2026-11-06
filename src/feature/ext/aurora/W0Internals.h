// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// The aurora unit needs a few helpers that F8 and F7 keep in their internal headers, not in their
// contracts. Every aurora file reaches them through this header, so a W0 refactor touches one place.
// What U28 uses:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, actorsIn, asMob, onLevelTick, push,
//     removeLater, setMotion} and entity::mobs::{distanceSqr, doHurtTarget, hasLineOfSight, lookAt,
//     moveTo, navigationDone, stopNavigation, target}: Java's Mob / PathNavigation / LookControl calls
//     of TF's goals (HoverBaseGoal and its three phases, RangedAttackGoal).
//   F7 feature/structure/Internal.h: structure::{blocks::java, blocks::air, blocks::canBeReplaced,
//     selectors, boxCenter, rotateDir, addRotation, opposite, loadPiece, resetRegionRandom}: the block
//     helpers of TF's StructurePiece code (IceTower*, MushroomTower*); F7 feature/structure/Region.h:
//     structure::BlockSourceRegion (the selftests place pieces into the live world).
// The fold: expose these in feature/entity/Entity.h (a `mobs` namespace) and feature/structure/
// Structure.h (blocks / rotation helpers); then this header includes the contracts only.

#include "feature/entity/EntityInternal.h"   // IWYU pragma: export
#include "feature/structure/Internal.h"      // IWYU pragma: export
#include "feature/structure/Region.h"        // IWYU pragma: export
