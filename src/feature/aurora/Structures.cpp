// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/AuroraPalaceStructure.java
//   src/main/java/gloamingforest/world/components/structures/type/MushroomTowerStructure.java
//   src/main/java/gloamingforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The aurora palace and the mushroom tower as F6 landmark generators. F6 creates the start (center
// x / z in the chunk + 7, y by the structure's terrain rule; the mushroom tower has no structure
// set, so only /gloamingforest place and forceStart make one) and hands the layout getFirstPiece's
// random; addChildren draws from Structure.GenerationContext#random() (ext::contextRandom).
#include "feature/aurora/Structures.h"

#include "feature/aurora/IceTower.h"
#include "feature/aurora/Mobs.h"
#include "feature/aurora/MushroomTower.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"
#include "mod/Log.h"
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

/// Where the client scenario looks into a new palace (one line per start): the Snow Queen's room
/// and a wing's lowest floor.
void logPalaceViews(structure::bases::PieceList const& pieces) {
    bool boss = false, wing = false;
    for (auto const& p : pieces) {
        if (!boss && p->typeId == "gloamingforest:TFITBoss") {
            boss    = true;
            // on the room's floor in a corner free of the ice stairs, looking up across the
            // glowstone ceiling (y 38 .. 39) past the spawner (7, 34, 7)
            auto at = p->getWorldPos(4, 35, 4), look = p->getWorldPos(10, 39, 10);
            gf::log().info("aurora: snow queen room view {} {} {} facing {} {} {}", at.x, at.y, at.z, look.x, look.y, look.z);
        }
        if (!wing && p->typeId == "gloamingforest:TFITWin") {
            wing    = true;
            auto at = p->getWorldPos(2, 2, 2), look = p->getWorldPos(6, 2, 6);
            gf::log().info("aurora: palace wing view {} {} {} facing {} {} {}", at.x, at.y, at.z, look.x, look.y, look.z);
        }
    }
}

class AuroraModule {
public:
    void install() {
        registerIceTowerPieces();
        registerMushroomTowerPieces();
        installMobs();
        installSnowQueen();
        installAuroraBlocks();
        gf::landmark::registerGenerator(kAuroraPalace, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                        auto ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                        auto const& c  = ctx.start.center;
                                        layoutAuroraPalace(ctx.random, *ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                        logPalaceViews(ctx.pieces);
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
