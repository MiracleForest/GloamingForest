// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/AuroraPalaceStructure.java
//   src/main/java/twilightforest/world/components/structures/type/MushroomTowerStructure.java
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The aurora palace and the mushroom tower as F6 landmark generators. F6 creates the start (center
// x / z in the chunk + 7, y by the structure's terrain rule; the mushroom tower has no structure
// set, so only /twilightforest place and forceStart make one) and hands the layout getFirstPiece's
// random; addChildren draws from Structure.GenerationContext#random() (ext::contextRandom).
#include "feature/aurora/Structures.h"

#include "feature/aurora/IceTower.h"
#include "feature/aurora/Mobs.h"
#include "feature/aurora/MushroomTower.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"
#include "mod/Module.h"

namespace gf::feature::aurora {

void layoutAuroraPalace(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                        structure::bases::PieceList& pieces) {
    auto  main = std::make_unique<IceTowerMain>(firstPieceRandom, 0, x, y, z);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
}

void layoutMushroomTower(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                         structure::bases::PieceList& pieces) {
    auto  main = std::make_unique<MushroomTowerMain>(firstPieceRandom, 0, x, y, z);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
}

namespace {

class AuroraModule {
public:
    void install() {
        registerIceTowerPieces();
        registerMushroomTowerPieces();
        installMobs();
        installSnowQueen();
        gf::landmark::registerGenerator(kAuroraPalace, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                        auto ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                        auto const& c  = ctx.start.center;
                                        layoutAuroraPalace(ctx.random, *ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                        return true;
                                    }});
        gf::landmark::registerGenerator(kMushroomTower, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                        auto ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                        auto const& c  = ctx.start.center;
                                        layoutMushroomTower(ctx.random, *ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                        return true;
                                    }});
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::aurora

GF_REGISTER_MODULE(gf::feature::aurora::AuroraModule);
