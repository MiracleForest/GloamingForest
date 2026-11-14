// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/LabyrinthStructure.java
//   src/main/java/gloamingforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The labyrinth as an F6 landmark generator. F6 makes the start (the chunk origin + 7, y by the
// structure's adjustForTerrain: no elevation adjustment, the sea level) and hands the layout
// getFirstPiece's random, which MazeRuinsComponent does not use; addChildren draws from
// Structure.GenerationContext#random() (ext::contextRandom), which the maze levels reseed.
//
// Terrain: the mound and its entrance trenches are LabyrinthStructure#getStructureTerraformer
// (core/labyrinth/Terraformer.h) over the start's box. The structure's BURY adaptation applies to
// no piece: every TFStructureComponentOld is a PieceBeardifierModifier of adjustment NONE.
#include "feature/labyrinth/Structure.h"

#include "core/labyrinth/Terraformer.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/labyrinth/Pieces.h"
#include "feature/landmark/Landmark.h"

namespace gf::feature::labyrinth::structure {

void install() {
    maze::registerPieces();
    gf::landmark::registerGenerator(maze::kLabyrinth, {.layout =
                                                           [](gf::landmark::LayoutContext& ctx) {
                                                               auto        context = gf::landmark::ext::contextRandom(ctx.start);
                                                               auto const& c       = ctx.start.center;
                                                               maze::layout(*context, c.x, c.y, c.z, ctx.pieces);
                                                               return true;
                                                           },
                                                       .terraform = [](gf::landmark::Start const& start, int x, int y, int z) -> double {
                                                           return gf::core::labyrinth::terraformer(start.box, x, y, z);
                                                       }});
}

} // namespace gf::feature::labyrinth::structure
