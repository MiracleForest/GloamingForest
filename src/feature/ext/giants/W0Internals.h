// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U29's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b).
//
// What U29 uses that the contracts do not expose:
//   F8 feature/entity/EntityInternal.h: entity::{actorById, playerById, asMob, onLevelTick,
//     playersNear, actorsIn, uidOf, currentTick} and entity::mobs::{legacySolid, emptyCollision}:
//     the per-tick driver of the beanstalk growers (a block entity's tick), Java's
//     BlockStateBase#isSolid of the uberous soil and the giants' spawn check;
//   F7 feature/structure/Internal.h: structure::{blocks::blocksMotion, blocks::canBeReplaced,
//     blocks::java, boxToJson, loadPiece}: MOTION_BLOCKING's opacity (cloud weather), the
//     replaceable test of GiantBlock#getStateForPlacement and the cloud castle's first piece by its
//     saved form (the piece type is U27's, rule R2).
// The fold: expose these in feature/entity/Entity.h and feature/structure/Structure.h.

#include "feature/entity/EntityInternal.h" // IWYU pragma: export
#include "feature/structure/Internal.h"    // IWYU pragma: export
#include "feature/structure/Region.h"      // IWYU pragma: export
