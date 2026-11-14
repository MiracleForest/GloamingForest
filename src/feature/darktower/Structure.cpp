// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/DarkTowerStructure.java
//   src/main/java/gloamingforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The dark tower as an F6 landmark generator. F6 makes the start (center x / z in the chunk + 7,
// y by the decoration config's elevation rule) and hands the layout getFirstPiece's random;
// addChildren draws from Structure.GenerationContext#random() (ext::contextRandom).
//
// Terrain: NeoForge's Beardifier takes every TFStructureComponentOld (a PieceBeardifierModifier)
// with its own adjustment, which is NONE except for DarkTowerEntranceComponent (BEARD_BOX, its box,
// ground level delta 1 from TowerWingComponent); the structure's BEARD_THIN applies to no piece.
// The terraformer adds the entrance towers' beard boxes.
#include "feature/darktower/Structure.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>

#include "core/jrand/JRand.h"
#include "feature/darktower/Tower.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"

namespace gf::feature::darktower::landmarkgen {

namespace {

using Key = std::tuple<int, int, int, int>;

/// The entrance towers' boxes of a start. The layout is deterministic: it is laid out again from the
/// start's randoms (thread safe, unlike asking the store for the saved pieces from a worldgen
/// thread) and cached.
std::vector<core::BlockBox> entranceBoxes(gf::landmark::Start const& start) {
    static std::mutex                                  mutex;
    static std::map<Key, std::vector<core::BlockBox>> cache;
    Key                                                key{start.dimension, start.center.x, start.center.y, start.center.z};
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(key); it != cache.end()) return it->second;
    }
    int  cx    = start.center.x >> 4;
    int  cz    = start.center.z >> 4;
    auto chunk = start.data.is_object() ? start.data.value("chunk", nlohmann::json::array()) : nlohmann::json::array();
    if (chunk.is_array() && chunk.size() == 2 && chunk[0].is_number_integer() && chunk[1].is_number_integer()) {
        cx = chunk[0].get<int>();
        cz = chunk[1].get<int>();
    }
    // LandmarkStructure#findGenerationPoint: RandomSource.create(seed + chunkX * 25117L + chunkZ * 151121L)
    core::jrand::LegacyRandom first(static_cast<std::int64_t>(static_cast<std::uint64_t>(start.seed) +
                                                              static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) * 25117ULL +
                                                              static_cast<std::uint64_t>(static_cast<std::int64_t>(cz)) * 151121ULL));
    auto                            context = gf::landmark::ext::contextRandom(start);
    gf::structure::bases::PieceList pieces;
    tower::layout(first, *context, start.center.x, start.center.y, start.center.z, pieces);
    std::vector<core::BlockBox> boxes;
    for (auto const& p : pieces)
        if (p->typeId == tower::kEntrance) boxes.push_back(p->bbox);
    std::lock_guard lock(mutex);
    if (cache.size() > 256) cache.clear();
    cache[key] = boxes;
    return boxes;
}

} // namespace

void install() {
    tower::registerPieces();
    gf::landmark::registerGenerator(tower::kDarkTower,
                                    {.layout =
                                         [](gf::landmark::LayoutContext& ctx) {
                                             auto        ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                             auto const& c         = ctx.start.center;
                                             tower::layout(ctx.random, *ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                             return true;
                                         },
                                     .terraform = [](gf::landmark::Start const& start, int x, int y, int z) -> double {
                                         auto boxes = entranceBoxes(start);
                                         if (boxes.empty()) return 0.0;
                                         // TowerWingComponent#getGroundLevelDelta: 1
                                         return gf::landmark::ext::boxDensity(boxes, 1, 0, "beard_box", x, y, z);
                                     }});
}

} // namespace gf::feature::darktower::landmarkgen
