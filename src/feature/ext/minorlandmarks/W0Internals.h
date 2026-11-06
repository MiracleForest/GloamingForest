// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U18's only door into W0 internals (rule R18 extension request, for A8 to fold in W2b), the same
// door U28 opened (feature/ext/aurora/W0Internals.h).
//
// The minor landmarks write TF's StructurePiece / Feature code against a worldgen::Region and need
// F7's block helpers and block entity queue, which F7 keeps in its internal header:
//   F7 feature/structure/Internal.h:
//     structure::blocks::{java, fromJava, fromJavaState, is, inTag, rotate, mirror, javaProperty,
//       withJava, isAir, isLiquid, isWater, canBeReplaced, worldGenReplaceable,
//       replaceableByStructures, blocksMotion, directionName}: Java block states on Bedrock
//       (BlockState#rotate / #mirror / #is / #canBeReplaced, FeatureLogic#worldGenReplaceable);
//     structure::blockentities::{container, spawner, entity}: a chest with a facing that is not
//       Blocks.CHEST (the fallen trunk's twilight oak chest, the leaf dungeon's loot block), a
//       spawner, and an entity added from a generation thread (MonolithFeature's ravens:
//       WorldGenLevel#addFreshEntity), queued for the chunk like F7's own;
//     structure::{rotateDir, opposite, clockwise, boxCenter, intersection}.
//   F7 feature/structure/Region.h: structure::BlockSourceRegion (selftests place pieces and
//     features into the live world).
// The fold: expose these in feature/structure/Structure.h (blocks / block entities on a Region),
// then this header includes the contract only.

#include "feature/structure/Internal.h" // IWYU pragma: export
#include "feature/structure/Region.h"   // IWYU pragma: export
