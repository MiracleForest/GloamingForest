// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/KnightStrongholdStructure.java
//   src/main/java/gloamingforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The knight stronghold as an F6 landmark generator. F6 makes the start (center x / z in the chunk
// + 7, y by the decoration config: sea level, the elevation is not adjusted) and hands the layout
// getFirstPiece's random; addChildren draws from Structure.GenerationContext#random()
// (ext::contextRandom). Terrain: the beardifier (U2) buries the protected pieces by upstream's table
// (KnightStrongholdComponent#getTerrainAdjustment), so the generator has no terraformer.
#include "feature/stronghold/Structure.h"

#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"
#include "feature/stronghold/Pieces.h"

namespace gf::feature::stronghold::structure {

void install() {
    pieces::registerPieces();
    gf::landmark::registerGenerator(pieces::kStructure, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                                             auto        contextRandom = gf::landmark::ext::contextRandom(ctx.start);
                                                             auto const& c             = ctx.start.center;
                                                             pieces::layout(ctx.random, *contextRandom, c.x, c.y, c.z, ctx.pieces);
                                                             return true;
                                                         }});
}

} // namespace gf::feature::stronghold::structure
