// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/GiantHouseStructure.java
//   src/main/java/twilightforest/world/components/structures/trollcave/CloudCastleComponent.java (constructor)
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java (generatePieces)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:giant_house: the cloud castle above every highlands landmark centre. Its pieces
// (TFClCa, TFCloud, TFClTr) are U27's piece types (docs/spec/caves.md 1.1); this unit creates the
// first piece by its type id (rule R2) from the saved form a CloudCastleComponent(1, x, y + 168, z)
// has (Java's NBT constructor: StructurePiece(type, tag) + TFStructureComponentOld#setOrientation),
// and the piece adds its cloud and trees. The generator is registered once the piece type exists
// (F6 keeps the sites reserved until then).
#include "feature/giants/Structure.h"

#include "feature/ext/giants/W0Internals.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/giants/Giants.h"
#include "feature/landmark/Landmark.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::giants {

namespace {
bool& registered() {
    static bool r = false;
    return r;
}
} // namespace

structure::Json cloudCastleTag(int x, int y, int z) {
    // round to nearest mult of 4
    x &= ~0b11;
    y &= ~0b11;
    z &= ~0b11;
    // BoundingBoxUtils.getComponentToAddBoundingBox(x, y, z, -8, -4, -8, 64, 16, 64, SOUTH, false)
    auto box = structure::BlockBox::of(x - 8, y - 4, z - 8, x + 64 - 8, y + 16 - 4, z + 64 - 8);
    return structure::Json{
        {"id", kCloudCastlePiece},
        {"BB", structure::boxToJson(box)},
        {"O", 0},        // Direction.SOUTH (2D data value 0)
        {"GD", 1},       // index 1
        {"rotation", 2}, // TFStructureComponentOld#setOrientation(SOUTH): CLOCKWISE_180, no mirror
        {"mirror", 0},
        {"rot", 2},
        {"si", 0},
        {"deco", ""},
    };
}

bool cloudCastleAvailable() {
    static bool known = false;
    if (known) return true;
    known = structure::loadPiece(cloudCastleTag(0, 168, 0)) != nullptr;
    return known;
}

bool layoutGiantHouse(structure::RandomSource& contextRandom, int x, int y, int z, structure::bases::PieceList& pieces) {
    // GiantHouseStructure#getFirstPiece: new CloudCastleComponent(1, x, y + 168, z)
    auto first = structure::loadPiece(cloudCastleTag(x, y + 168, z));
    if (!first) return false;
    auto* raw = first.get();
    pieces.push_back(std::move(first));
    raw->addChildren(*raw, pieces, contextRandom);
    return true;
}

void registerGiantHouse() {
    if (registered()) return;
    if (!cloudCastleAvailable()) {
        gf::log().warn("giants: the cloud castle piece type {} is not registered (U27): giant_house sites stay reserved", kCloudCastlePiece);
        return;
    }
    registered() = true;
    gf::landmark::registerGenerator(id::GiantHouse, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                        auto        ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                        auto const& c         = ctx.start.center;
                                        return layoutGiantHouse(*ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                    }});
    gf::log().info("giants: giant_house generator registered");
}

void installStructure() {
    // the piece types of every unit are registered by the time a world is ready
    game::db::onWorldReady([] { registerGiantHouse(); });
}

} // namespace gf::feature::giants
