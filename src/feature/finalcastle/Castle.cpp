// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/FinalCastleStructure.java
//   src/main/java/gloamingforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the final castle as an F6 landmark generator. F6 makes the start (centre of the
// chunk + 7, y at the sea level: adjust_structure_elevation is false) and hands the layout
// getFirstPiece's random; FinalCastleMainComponent draws nothing from it, and addChildren draws from
// Structure.GenerationContext#random() (ext::contextRandom).
//
// Terrain: the structure's BEARD_BOX applies to no piece. NeoForge's Beardifier takes every
// GloamingStructureComponentOld (a PieceBeardifierModifier) with its own adjustment, which is NONE except
// for the dungeon rooms (FinalCastleDungeonRoom31Component and its entrance / exit: BURY, ground
// level delta 0); the template pieces keep their pool's NONE. The terraformer buries the rooms.
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

#include "feature/finalcastle/Castle.h"
#include "feature/finalcastle/Parts.h"

#include "core/jrand/JRand.h"
#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"

namespace gf::feature::finalcastle::castle {

namespace {

using Key   = std::tuple<std::int64_t, int, int, int, int>;
using Boxes = std::shared_ptr<std::vector<core::BlockBox> const>;

bool isDungeonRoom(std::string const& t) { return t == type::DungeonRoom || t == type::DungeonEntrance || t == type::DungeonExit; }

/// The dungeon rooms' boxes of a start. The layout is deterministic: it is laid out again from the
/// start's random (thread safe, unlike asking the store for the saved pieces from a worldgen thread)
/// and cached.
Boxes roomBoxes(gf::landmark::Start const& start) {
    static std::mutex            mutex;
    static std::map<Key, Boxes>  cache;
    Key                          key{start.seed, start.dimension, start.center.x, start.center.y, start.center.z};
    // the terraformer asks per density sample: the worldgen thread's last start skips the lock
    thread_local Key   lastKey{};
    thread_local Boxes lastBoxes;
    if (lastBoxes && lastKey == key) return lastBoxes;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(key); it != cache.end()) {
            lastKey   = key;
            lastBoxes = it->second;
            return lastBoxes;
        }
    }
    auto      context = gf::landmark::ext::contextRandom(start);
    PieceList pieces;
    layout(*context, start.center.x, start.center.y, start.center.z, start.seed, pieces);
    auto boxes = std::make_shared<std::vector<core::BlockBox>>();
    for (auto const& p : pieces)
        if (isDungeonRoom(p->typeId)) boxes->push_back(p->bbox);
    std::lock_guard lock(mutex);
    if (cache.size() > 64) cache.clear();
    cache[key] = boxes;
    lastKey    = key;
    lastBoxes  = boxes;
    return boxes;
}

template <class T>
void reg(char const* typeId) {
    ::gf::structure::registerPieceType(typeId, [](Json const& saved) -> std::unique_ptr<Piece> {
        auto p = std::make_unique<T>();
        p->load(saved);
        return p;
    });
}

} // namespace

void layout(RandomSource& contextRandom, int x, int y, int z, std::int64_t overworldSeed, PieceList& pieces) {
    auto  main = std::make_unique<Main>(0, x, y, z, overworldSeed);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
}

void registerPieces() {
    ::gf::structure::bases::registerDecorator("DecoCastle", [] { return castleDecorator(); });
    reg<Main>(type::Main);
    reg<Foundation48>(type::Foundation48);
    reg<Roof48>(type::Roof48);
    reg<Gazebo>(type::Gazebo);
    reg<StairTower>(type::StairTower);
    reg<LargeTower>(type::LargeTower);
    ::gf::structure::registerPieceType(type::MazeTower, [](Json const& saved) -> std::unique_ptr<Piece> {
        auto p = std::make_unique<MazeTower13>(type::MazeTower);
        p->load(saved);
        return p;
    });
    reg<Bridge>(type::Bridge);
    reg<DamagedTower>(type::Damaged);
    reg<WreckedTower>(type::Wrecked);
    reg<Foundation13>(type::Foundation13);
    reg<ThornFoundation>(type::Thorns);
    reg<EntranceTower>(type::EntranceTower);
    reg<EntranceSideTower>(type::EntranceSide);
    reg<EntranceBottomTower>(type::EntranceBottom);
    reg<EntranceStairs>(type::EntranceStairs);
    reg<BellTower>(type::BellTower);
    reg<BellFoundation>(type::BellFoundation);
    reg<Roof13Conical>(type::Roof13Conical);
    reg<Roof13Cren>(type::Roof13Cren);
    reg<Roof13Peaked>(type::Roof13Peaked);
    reg<Roof9Cren>(type::Roof9Cren);
    reg<DungeonSteps>(type::DungeonSteps);
    reg<DungeonRoom31>(type::DungeonRoom);
    reg<DungeonEntrance>(type::DungeonEntrance);
    reg<DungeonExit>(type::DungeonExit);
    reg<ForgeRoom>(type::ForgeRoom);
    reg<MuralPiece>(type::Mural);
}

void install() {
    registerPieces();
    gf::landmark::registerGenerator(kFinalCastle, {.layout =
                                                       [](gf::landmark::LayoutContext& ctx) {
                                                           auto        ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                                           auto const& c         = ctx.start.center;
                                                           // WorldUtil.getOverworldSeed(): Bedrock has one world seed
                                                           layout(*ctxRandom, c.x, c.y, c.z, ctx.start.seed, ctx.pieces);
                                                           return true;
                                                       },
                                                   .terraform = [](gf::landmark::Start const& start, int x, int y, int z) -> double {
                                                       auto boxes = roomBoxes(start);
                                                       if (boxes->empty()) return 0.0;
                                                       // FinalCastleDungeonRoom31Component: BURY, ground level delta 0
                                                       return gf::landmark::ext::boxDensity(*boxes, 0, 0, "bury", x, y, z);
                                                   }});
}

} // namespace gf::feature::finalcastle::castle
