// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerWingComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerMainBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBossBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBossTrapComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBalconyComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBeardComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofAntennaComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofCactusComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofFourPostComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofRingsComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/StructureDecoratorDarkTower.java
//   src/main/java/gloamingforest/world/components/structures/type/DarkTowerStructure.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/darktower/Tower.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "feature/darktower/TowerUtil.h"
#include "feature/structure/Internal.h"

namespace gf::feature::darktower::tower {

using ::gf::structure::bases::ComponentOld;
using ::gf::structure::bases::TowerWing;
using namespace util;

namespace {

struct DarkTowerDecorator final : Decorator {
    [[nodiscard]] std::string decoString() const override { return "DecoDarkTower"; }
};

} // namespace

std::unique_ptr<Decorator> darkTowerDecorator() {
    auto d           = std::make_unique<DarkTowerDecorator>();
    auto def         = ::gf::structure::bases::defaultDecorator();
    d->floorState    = def->floorState; // GloamingStructureDecorator's defaults the dark tower keeps
    d->roofState     = def->roofState;
    d->blockState    = &J("gloamingforest:towerwood");
    d->accentState   = &J("gloamingforest:encased_towerwood");
    d->fenceState    = &J("minecraft:oak_fence");
    d->stairState    = &J("minecraft:spruce_stairs");
    d->pillarState   = &J("gloamingforest:encased_towerwood");
    d->platformState = &J("gloamingforest:encased_towerwood");
    d->randomBlocks  = ::gf::structure::selectors::get("gloamingforest:towerwood");
    return d;
}

// ==== DarkTowerWingComponent ===================================================================================

Wing::Wing(std::string typeId, int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing(std::move(typeId), i, x, y, z, pSize, pHeight, direction) {
    deco = darkTowerDecorator();
}

Json Wing::save() const {
    Json j        = TowerWing::save();
    j["keyTower"] = keyTower;
    Json doors    = Json::array();
    for (auto d : openingTypes) doors.push_back(static_cast<int>(d));
    j["doorTypeInts"] = doors;
    return j;
}

void Wing::load(Json const& data) {
    TowerWing::load(data);
    keyTower = data.value("keyTower", false);
    openingTypes.clear();
    for (auto const& t : data.value("doorTypeInts", Json::array())) openingTypes.push_back(static_cast<Door>(std::clamp(t.get<int>(), 0, 2)));
}

void Wing::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
    // add a roof?
    makeARoof(parent, list, rand);
    // add a beard
    makeABeard(parent, list, rand);

    if (size > 10) {
        // sub towers
        for (auto direction : kRotations) {
            auto dest        = getValidOpening(rand, direction);
            int  childHeight = validateChildHeight(height - 4 + rand.nextInt(10) - rand.nextInt(10));
            bool madeWing    = makeTowerWing(list, rand, genDepth, dest[0], dest[1], dest[2], size - 2, childHeight, direction);
            // occasional balcony
            if (!madeWing && (direction == Rotation::Clockwise180 || rand.nextBoolean()))
                makeTowerBalcony(list, rand, genDepth, dest[0], dest[1], dest[2], direction);
        }
    } else if (rand.nextInt(4) == 0 && !isEntrance()) {
        // occasional balcony on small towers too
        Rotation direction = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
        auto     dest      = getValidOpening(rand, direction);
        makeTowerBalcony(list, rand, genDepth, dest[0], dest[1], dest[2], direction);
    }
}

void Wing::makeARoof(Piece&, PieceList& list, RandomSource& rand) {
    int                   index = genDepth;
    BlockPos              loc   = getLocatorPosition();
    std::unique_ptr<Roof> roof;
    switch (rand.nextInt(5)) {
    case 2: roof = std::make_unique<RoofCactus>(kRoofCactus, index, *this, loc.x, loc.y, loc.z); break;
    case 3: roof = std::make_unique<RoofRings>(kRoofRings, index, *this, loc.x, loc.y, loc.z); break;
    case 4: roof = std::make_unique<RoofFourPost>(kRoofFourPost, index, *this, loc.x, loc.y, loc.z); break;
    default: roof = std::make_unique<RoofAntenna>(kRoofAntenna, index, *this, loc.x, loc.y, loc.z); break;
    }
    auto* raw = roof.get();
    list.push_back(std::move(roof));
    raw->addChildren(*this, list, rand);
    roofType = raw->typeId;
}

void Wing::makeABeard(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc   = getLocatorPosition();
    auto     beard = std::make_unique<Beard>(genDepth + 1, *this, loc.x, loc.y, loc.z);
    auto*    raw   = beard.get();
    list.push_back(std::move(beard));
    raw->addChildren(*this, list, rand);
}

bool Wing::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    // kill too-small towers
    if (wingHeight < 8) return false;
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 5, direction);
    // end of the world!
    if (dx[1] + wingHeight > 250) return false;

    auto   bridge    = std::make_unique<Bridge>(kBridge, index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece* intersect = ::gf::structure::bases::findCollisionPiece(list, bridge->bbox);
    if (!intersect || intersect == this) intersect = ::gf::structure::bases::findCollisionPiece(list, bridge->getWingBB());
    else return false;
    if (intersect && intersect != this) return false;
    auto* raw = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

bool Wing::makeTowerBalcony(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 5, direction);
    auto        balcony   = std::make_unique<Balcony>(index, dx[0], dx[1], dx[2], direction);
    Piece*      intersect = ::gf::structure::bases::findCollisionPiece(list, balcony->bbox);
    if (intersect && intersect != this) return false;
    auto* raw = balcony.get();
    list.push_back(std::move(balcony));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation, Door::Reappearing);
    return true;
}

void Wing::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));

    // make walls
    makeEncasedWalls(region, rand, sbb, 0, 0, 0, size - 1, height - 1, size - 1);
    // clear inside
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);

    if (size > 9) {
        // half floors, always starting at y = 4
        addHalfFloors(region, decoRNG, sbb, 4, height - 1);
    } else if (decoRNG.nextInt(3) == 0) {
        addSmallTimberBeams(region, decoRNG, sbb, 4, height - 1);
    } else {
        addHalfFloors(region, decoRNG, sbb, 4, height - 1);
    }

    // openings
    makeOpenings(region, sbb);

    // destroy some towers
    if (decoRNG.nextBoolean() && !isKeyTower() && height > 8) {
        int blobs = 1;
        if (size > 9 && decoRNG.nextBoolean()) blobs++;
        for (int i = 0; i < blobs; i++) {
            // find a random spot in the tower
            int x = decoRNG.nextInt(size);
            int y = decoRNG.nextInt(height - 7) + 2;
            int z = decoRNG.nextInt(size);
            destroyTower(region, decoRNG, x, y, z, 3, sbb);
        }
    }
}

void Wing::destroyTower(Region& region, RandomSource& decoRNG, int x, int y, int z, int amount, BlockBox const& sbb) {
    int initialRadius = decoRNG.nextInt(amount) + amount;
    drawBlob(region, x, y, z, initialRadius, air(), sbb);
    for (int i = 0; i < 3; i++) {
        int dx = x + (initialRadius - 1) * (decoRNG.nextBoolean() ? 1 : -1);
        int dy = y + (initialRadius - 1) * (decoRNG.nextBoolean() ? 1 : -1);
        int dz = z + (initialRadius - 1) * (decoRNG.nextBoolean() ? 1 : -1);
        netherTransformBlob(region, decoRNG, dx, dy, dz, initialRadius - 1, sbb);
        drawBlob(region, dx, dy, dz, initialRadius - 2, air(), sbb);
    }
}

void Wing::netherTransformBlob(Region& region, RandomSource& inRand, int sx, int sy, int sz, int rad, BlockBox const& sbb) {
    core::jrand::LegacyRandom rand(inRand.nextLong());
    // then trace out a quadrant
    for (int dx = 0; dx <= rad; dx++)
        for (int dy = 0; dy <= rad; dy++)
            for (int dz = 0; dz <= rad; dz++) {
                if (blobDist(dx, dy, dz) > rad) continue;
                // do eight at a time for easiness! (upstream tests the first corner twice)
                testAndChangeToNetherrack(region, rand, sx + dx, sy + dy, sz + dz, sbb);
                testAndChangeToNetherrack(region, rand, sx + dx, sy + dy, sz + dz, sbb);
                testAndChangeToNetherrack(region, rand, sx + dx, sy + dy, sz - dz, sbb);
                testAndChangeToNetherrack(region, rand, sx - dx, sy + dy, sz + dz, sbb);
                testAndChangeToNetherrack(region, rand, sx - dx, sy + dy, sz - dz, sbb);
                testAndChangeToNetherrack(region, rand, sx + dx, sy - dy, sz + dz, sbb);
                testAndChangeToNetherrack(region, rand, sx + dx, sy - dy, sz - dz, sbb);
                testAndChangeToNetherrack(region, rand, sx - dx, sy - dy, sz + dz, sbb);
                testAndChangeToNetherrack(region, rand, sx - dx, sy - dy, sz - dz, sbb);
            }
}

void Wing::testAndChangeToNetherrack(Region& region, RandomSource& rand, int x, int y, int z, BlockBox const& sbb) {
    if (!isAirBlock(getBlock(region, x, y, z, sbb))) {
        placeBlock(region, J("minecraft:netherrack"), x, y, z, sbb);
        if (isAirBlock(getBlock(region, x, y + 1, z, sbb)) && rand.nextBoolean()) placeBlock(region, J("minecraft:fire"), x, y + 1, z, sbb);
    }
}

void Wing::drawBlob(Region& region, int sx, int sy, int sz, int rad, Block const& state, BlockBox const& sbb) {
    for (int dx = 0; dx <= rad; dx++)
        for (int dy = 0; dy <= rad; dy++)
            for (int dz = 0; dz <= rad; dz++) {
                if (blobDist(dx, dy, dz) > rad) continue;
                placeBlock(region, state, sx + dx, sy + dy, sz + dz, sbb);
                placeBlock(region, state, sx + dx, sy + dy, sz - dz, sbb);
                placeBlock(region, state, sx - dx, sy + dy, sz + dz, sbb);
                placeBlock(region, state, sx - dx, sy + dy, sz - dz, sbb);
                placeBlock(region, state, sx + dx, sy - dy, sz + dz, sbb);
                placeBlock(region, state, sx + dx, sy - dy, sz - dz, sbb);
                placeBlock(region, state, sx - dx, sy - dy, sz + dz, sbb);
                placeBlock(region, state, sx - dx, sy - dy, sz - dz, sbb);
            }
}

void Wing::addHalfFloors(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top) {
    int      spacing  = 4;
    Rotation rotation = kRotations[static_cast<std::size_t>(javaMod(bbox.minY + bottom, 3))];
    if (bottom == 0) bottom += spacing;

    // fill with half floors
    for (int y = bottom; y < top; y += spacing) {
        rotation = rot(rotation, Rotation::Clockwise180);
        if (y >= top - spacing) {
            makeFullFloor(region, sbb, y);
            if (isDeadEnd()) decorateTreasureRoom(region, sbb, rotation, y, 4, *deco);
        } else {
            makeHalfFloor(region, sbb, rotation, y);
            // decorate (upstream's fallthrough: case 1 gets double weight when size >= 11)
            switch (rand.nextInt(8)) {
            case 0:
                if (size < 11) {
                    decorateReappearingFloor(region, sbb, rotation, y);
                    break;
                }
                [[fallthrough]];
            case 1: decorateSpawner(region, rand, sbb, rotation, y); break;
            case 2: decorateWingLounge(region, sbb, rotation, y); break;
            case 3: decorateWingLibrary(region, sbb, rotation, y); break;
            case 4: decorateExperimentPulser(region, sbb, rotation, y); break;
            case 5: decorateExperimentLamp(region, sbb, rotation, y); break;
            case 6: decoratePuzzleChest(region, sbb, rotation, y); break;
            default: break;
            }
        }
        addStairsDown(region, sbb, rotation, y, size - 2, spacing);
        // double wide staircase
        if (size > 9) addStairsDown(region, sbb, rotation, y, size - 3, spacing);
    }
    rotation = rot(rotation, Rotation::Clockwise180);
    // stairs to roof
    addStairsDown(region, sbb, rotation, height - 1, size - 2, spacing);
}

void Wing::makeHalfFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    fillBlocksRotated(region, sbb, size / 2, y, 1, size - 2, y, size - 2, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, size / 2 - 1, y, 1, size / 2 - 1, y, size - 2, *deco->accentState, rotation);
}

void Wing::makeFullFloor(Region& region, BlockBox const& sbb, int y) {
    fill(region, sbb, 1, y, 1, size - 2, y, size - 2, *deco->blockState, air(), false);
    fill(region, sbb, size / 2, y, 1, size / 2, y, size - 2, *deco->accentState, air(), true);
}

void Wing::decorateTreasureRoom(Region& region, BlockBox const& sbb, Rotation rotation, int y, int, Decorator const& myDeco) {
    // treasure chest!
    int x = size / 2;
    int z = size / 2;
    makePillarFrame(region, sbb, *deco, rotation, x - 1, y, z - 1, true);
    setBlockStateRotated(region, *myDeco.platformState, x, y + 1, z, rotation, sbb);
    placeTreasureAtCurrentPosition(region, x, y + 2, z, isKeyTower() ? kLootKey : kLootCache, sbb);
}

void Wing::decorateSpawner(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y) {
    int         x = size > 9 ? 4 : 3;
    int         z = size > 9 ? 5 : 4;
    std::string mobID;
    if (size > 9) mobID = rand.nextBoolean() ? "gloamingforest:carminite_golem" : "gloamingforest:carminite_broodling";
    else mobID = "gloamingforest:carminite_broodling";
    // pillar frame
    makePillarFrame(region, sbb, *deco, rotation, x, y, z, true);
    setSpawnerRotated(region, x + 1, y + 2, z + 1, rotation, mobID, sbb);
}

void Wing::decorateWingLounge(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    int cx = size > 9 ? 9 : 7;
    int cz = size > 9 ? 4 : 3;
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), cx, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, false), cx, y + 1, cz + 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, false), cx, y + 1, cz + 2, rotation, sbb);
    cx = size > 9 ? 5 : 3;
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, true), cx, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:spruce_slab", Json{{"type", "top"}}), cx, y + 1, cz + 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, true), cx, y + 1, cz + 2, rotation, sbb);
}

void Wing::decorateReappearingFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    Block const& inactiveReappearing = J("gloamingforest:reappearing_block");
    Block const& woodenPressurePlate = J("minecraft:oak_pressure_plate");
    // floor
    fillBlocksRotated(region, sbb, 4, y, 3, 7, y, 5, inactiveReappearing, rotation);
    // plates
    fillBlocksRotated(region, sbb, 4, y + 1, 2, 7, y + 1, 2, woodenPressurePlate, rotation);
    fillBlocksRotated(region, sbb, 4, y + 1, 6, 7, y + 1, 6, woodenPressurePlate, rotation);
}

void Wing::decorateExperimentLamp(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    int cx = size > 9 ? 5 : 3;
    int cz = size > 9 ? 5 : 4;
    setBlockStateRotated(region, J("minecraft:sticky_piston", Json{{"facing", "up"}}), cx, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:redstone_lamp"), cx, y + 2, cz, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, cx, y + 1, cz + 1, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::North, false), cx, y + 1, cz + 2, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, cx, y + 3, cz - 1, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::South, true), cx, y + 3, cz - 2, rotation, sbb);
}

void Wing::decorateExperimentPulser(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    int          cx                  = size > 9 ? 6 : 5;
    int          cz                  = size > 9 ? 4 : 3;
    Block const& redstoneWire        = J("minecraft:redstone_wire");
    Block const& woodenPressurePlate = J("minecraft:oak_pressure_plate");
    Block const& stickyPiston        = J("minecraft:sticky_piston", Json{{"facing", "south"}});
    Block const& unpoweredRepeater   = J("minecraft:repeater", Json{{"powered", "false"}, {"facing", "west"}, {"delay", "2"}});
    setBlockStateRotated(region, stickyPiston, cx, y + 1, cz + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, cx, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, redstoneWire, cx + 1, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, woodenPressurePlate, cx + 2, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, unpoweredRepeater, cx - 1, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, redstoneWire, cx - 2, y + 1, cz, rotation, sbb);
    setBlockStateRotated(region, redstoneWire, cx - 2, y + 1, cz + 1, rotation, sbb);
    setBlockStateRotated(region, redstoneWire, cx - 1, y + 1, cz + 1, rotation, sbb);
}

void Wing::decorateWingLibrary(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    makeSmallBookshelf(region, sbb, rotation, y, size > 9 ? 4 : 3, size > 9 ? 3 : 2);
    makeSmallBookshelf(region, sbb, rotation, y, size > 9 ? 9 : 7, size > 9 ? 3 : 2);
}

void Wing::makeSmallBookshelf(Region& region, BlockBox const& sbb, Rotation rotation, int y, int bx, int bz) {
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, false), bx, y + 1, bz, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, true), bx, y + 2, bz, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), bx, y + 1, bz + 3, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, true), bx, y + 2, bz + 3, rotation, sbb);
    Block const& bookshelf = J("minecraft:bookshelf");
    setBlockStateRotated(region, bookshelf, bx, y + 1, bz + 1, rotation, sbb);
    setBlockStateRotated(region, bookshelf, bx, y + 2, bz + 1, rotation, sbb);
    setBlockStateRotated(region, bookshelf, bx, y + 1, bz + 2, rotation, sbb);
    setBlockStateRotated(region, bookshelf, bx, y + 2, bz + 2, rotation, sbb);
}

void Wing::decoratePuzzleChest(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    int x = size > 9 ? 4 : 3;
    int z = size > 9 ? 5 : 4;
    // pillar frameframe
    makePillarFrame(region, sbb, *deco, rotation, x, y, z, true);
    // reinforce with towerwood
    setBlockStateRotated(region, *deco->platformState, x + 1, y + 1, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x + 2, y + 1, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x, y + 1, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x + 1, y + 1, z + 2, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x + 1, y + 1, z, rotation, sbb);

    setBlockStateRotated(region, *deco->blockState, x + 2, y + 3, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x, y + 3, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x + 1, y + 3, z + 2, rotation, sbb);
    setBlockStateRotated(region, air(), x + 1, y + 3, z, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, x + 1, y + 3, z + 1, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:sticky_piston", Json{{"facing", "north"}}), x + 1, y + 3, z - 1, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, x + 1, y + 3, z - 2, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::West, false), x + 2, y + 3, z - 2, rotation, sbb);

    placeTreasureRotated(region, x + 1, y + 2, z + 1, getOrientation().value_or(Orientation::North), rotation, kLootCache, sbb);
}

void Wing::makePillarFrame(Region& region, BlockBox const& sbb, Decorator const& myDeco, Rotation rotation, int x, int y, int z, bool fenced) {
    makePillarFrame(region, sbb, myDeco, rotation, x, y, z, 3, 3, 3, fenced);
}

void Wing::makePillarFrame(Region& region, BlockBox const& sbb, Decorator const& myDeco, Rotation rotation, int x, int y, int z, int width,
                           int frameHeight, int length, bool fenced) {
    // fill in posts
    for (int dx = 0; dx < width; dx++) {
        for (int dz = 0; dz < length; dz++) {
            if ((dx % 3 == 0 || dx == width - 1) && (dz % 3 == 0 || dz == length - 1)) {
                for (int py = 1; py <= frameHeight; py++) setBlockStateRotated(region, *myDeco.pillarState, x + dx, y + py, z + dz, rotation, sbb);
            } else {
                // the stairs come from this tower's decorator (deco.stairState), as in upstream
                std::optional<Orientation> stairDir;
                if (dx == 0) stairDir = Orientation::West;
                else if (dx == width - 1) stairDir = Orientation::East;
                else if (dz == 0) stairDir = Orientation::North;
                else if (dz == length - 1) stairDir = Orientation::South;
                if (stairDir) {
                    setBlockStateRotated(region, getStairState(*deco->stairState, *stairDir, false), x + dx, y + 1, z + dz, rotation, sbb);
                    setBlockStateRotated(region, getStairState(*deco->stairState, *stairDir, true), x + dx, y + frameHeight, z + dz, rotation, sbb);
                }
                if (fenced && (dx == 0 || dx == width - 1 || dz == 0 || dz == length - 1)) {
                    for (int fy = 2; fy <= frameHeight - 1; fy++) setBlockStateRotated(region, *myDeco.fenceState, x + dx, y + fy, z + dz, rotation, sbb);
                }
            }
        }
    }
}

void Wing::addStairsDown(Region& region, BlockBox const& sbb, Rotation rotation, int y, int sz, int spacing) {
    for (int i = 0; i < spacing; i++) {
        int sx = size - 3 - i;
        setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, false), sx, y - i, sz, rotation, sbb);
        setBlockStateRotated(region, *deco->accentState, sx, y - 1 - i, sz, rotation, sbb);
        setBlockStateRotated(region, air(), sx, y + 1 - i, sz, rotation, sbb);
        setBlockStateRotated(region, air(), sx, y + 2 - i, sz, rotation, sbb);
        setBlockStateRotated(region, air(), sx - 1, y + 2 - i, sz, rotation, sbb);
        setBlockStateRotated(region, air(), sx, y + 3 - i, sz, rotation, sbb);
        setBlockStateRotated(region, air(), sx - 1, y + 3 - i, sz, rotation, sbb);
    }
}

void Wing::addSmallTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top) {
    int      spacing  = 4;
    Rotation rotation = Rotation::None;
    if (bottom == 0) bottom += spacing;
    // fill with 3/4 floors
    for (int y = bottom; y < top; y += spacing) {
        rotation = rot(rotation, Rotation::Clockwise90);
        if (y >= top - spacing && isDeadEnd()) {
            makeTimberFloor(region, sbb, rotation, y);
            auto logDeco           = darkTowerDecorator();
            logDeco->pillarState   = &J("gloamingforest:dark_log");
            logDeco->platformState = &J("gloamingforest:dark_log");
            decorateTreasureRoom(region, sbb, rotation, y, 4, *logDeco);
        } else {
            makeSmallTimberBeams(region, rand, sbb, rotation, y, y == bottom && bottom != spacing);
        }
    }
}

void Wing::makeTimberFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    Block const& beamStateNS = J("gloamingforest:dark_log", Json{{"axis", "z"}});
    Block const& beamStateUD = J("gloamingforest:dark_log", Json{{"axis", "y"}});
    Block const& beamStateEW = J("gloamingforest:dark_log", Json{{"axis", "x"}});
    for (int z = 1; z < size - 1; z++)
        for (int x = 1; x < size - 1; x++) setBlockStateRotated(region, x < z ? beamStateNS : beamStateEW, x, y, z, rotation, sbb);
    // beams going down
    for (int by = 1; by < 4; by++) {
        setBlockStateRotated(region, beamStateUD, 2, y - by, 2, rotation, sbb);
        setBlockStateRotated(region, ladder(Orientation::West), 2 + 1, y - by, 2, rotation, sbb);
        setBlockStateRotated(region, beamStateUD, 6, y - by, 6, rotation, sbb);
        setBlockStateRotated(region, ladder(Orientation::East), 6 - 1, y - by, 6, rotation, sbb);
    }
    // holes for entrance
    setBlockStateRotated(region, air(), 3, y, 2, rotation, sbb);
    setBlockStateRotated(region, air(), 5, y, 6, rotation, sbb);
}

void Wing::makeSmallTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y, bool bottom) {
    // upstream's names: "NS" is axis X and "EW" axis Z here
    Block const& beamStateNS = J("gloamingforest:dark_log", Json{{"axis", "x"}});
    Block const& beamStateUD = J("gloamingforest:dark_log", Json{{"axis", "y"}});
    Block const& beamStateEW = J("gloamingforest:dark_log", Json{{"axis", "z"}});
    // two beams going e/w
    for (int z = 1; z < size - 1; z++) {
        setBlockStateRotated(region, beamStateEW, 2, y, z, rotation, sbb);
        setBlockStateRotated(region, beamStateEW, 6, y, z, rotation, sbb);
    }
    // a few random cross beams
    int z = pickBetweenExcluding(3, size - 3, rand, 2, 2, 6);
    for (int x = 3; x < 6; x++) setBlockStateRotated(region, beamStateNS, x, y, z, rotation, sbb);
    // beams going down
    int x1 = 2;
    int z1 = rand.nextBoolean() ? 2 : 6;
    int x3 = 6;
    int z3 = rand.nextBoolean() ? 2 : 6;
    for (int by = 1; by < 4; by++) {
        if (!bottom || checkPost(region, x1, y - 4, z1, rotation, sbb)) {
            setBlockStateRotated(region, beamStateUD, x1, y - by, z1, rotation, sbb);
            setBlockStateRotated(region, ladder(Orientation::West), x1 + 1, y - by, z1, rotation, sbb);
        }
        if (!bottom || checkPost(region, x3, y - 4, z3, rotation, sbb)) {
            setBlockStateRotated(region, beamStateUD, x3, y - by, z3, rotation, sbb);
            setBlockStateRotated(region, ladder(Orientation::East), x3 - 1, y - by, z3, rotation, sbb);
        }
    }
}

int Wing::pickBetweenExcluding(int low, int high, RandomSource& rand, int k, int l, int m) {
    int result;
    do {
        result = rand.nextInt(high - low) + low;
    } while (result == k || result == l || result == m);
    return result;
}

int Wing::pickFrom(RandomSource& rand, int i, int j, int k) {
    switch (rand.nextInt(3)) {
    case 1: return j;
    case 2: return k;
    default: return i;
    }
}

bool Wing::checkPost(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    BlockPos vec{getXWithOffsetRotated(x, z, rotation), worldY(y), getZWithOffsetRotated(x, z, rotation)};
    if (!sbb.contains(vec.x, vec.y, vec.z)) return false;
    Block const& blockState = region.getBlock(vec);
    return !isAirBlock(blockState) && &blockState != deco->accentState;
}

void Wing::makeEncasedWalls(Region& region, RandomSource& rand, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    for (int x = minX; x <= maxX; x++)
        for (int y = minY; y <= maxY; y++)
            for (int z = minZ; z <= maxZ; z++) {
                if (x != minX && x != maxX && y != minY && y != maxY && z != minZ && z != maxZ) continue;
                // wall (upstream compares x with minY here)
                if (((y == minY || y == maxY) && ((x == minY || x == maxX) || (z == minZ || z == maxZ))) ||
                    ((z == minZ || z == maxZ) && ((x == minY || x == maxX) || (y == minY || y == maxY)))) {
                    placeBlock(region, *deco->accentState, x, y, z, sbb);
                } else {
                    placeBlock(region, deco->randomBlocks->next(rand, x, y, z, true), x, y, z, sbb);
                }
            }

    // corners
    Block const& a = *deco->accentState;
    placeBlock(region, a, minX + 1, minY + 1, minZ, sbb);
    placeBlock(region, a, minX + 1, minY + 1, maxZ, sbb);
    placeBlock(region, a, maxX - 1, minY + 1, minZ, sbb);
    placeBlock(region, a, maxX - 1, minY + 1, maxZ, sbb);
    placeBlock(region, a, minX + 1, maxY - 1, minZ, sbb);
    placeBlock(region, a, minX + 1, maxY - 1, maxZ, sbb);
    placeBlock(region, a, maxX - 1, maxY - 1, minZ, sbb);
    placeBlock(region, a, maxX - 1, maxY - 1, maxZ, sbb);

    placeBlock(region, a, minX, minY + 1, minZ + 1, sbb);
    placeBlock(region, a, minX, minY + 1, maxZ - 1, sbb);
    placeBlock(region, a, maxX, minY + 1, minZ + 1, sbb);
    placeBlock(region, a, maxX, minY + 1, maxZ - 1, sbb);
    placeBlock(region, a, minX, maxY - 1, minZ + 1, sbb);
    placeBlock(region, a, minX, maxY - 1, maxZ - 1, sbb);
    placeBlock(region, a, maxX, maxY - 1, minZ + 1, sbb);
    placeBlock(region, a, maxX, maxY - 1, maxZ - 1, sbb);

    placeBlock(region, a, minX + 1, minY, minZ + 1, sbb);
    placeBlock(region, a, minX + 1, minY, maxZ - 1, sbb);
    placeBlock(region, a, maxX - 1, minY, minZ + 1, sbb);
    placeBlock(region, a, maxX - 1, minY, maxZ - 1, sbb);
    placeBlock(region, a, minX + 1, maxY, minZ + 1, sbb);
    placeBlock(region, a, minX + 1, maxY, maxZ - 1, sbb);
    placeBlock(region, a, maxX - 1, maxY, minZ + 1, sbb);
    placeBlock(region, a, maxX - 1, maxY, maxZ - 1, sbb);
}

std::array<int, 3> Wing::getValidOpening(RandomSource&, Rotation direction) {
    int verticalOffset = size == 19 ? 5 : 4;
    // for directions 0 or 2, the wall lies along the z axis
    if (direction == Rotation::None || direction == Rotation::Clockwise180) return {direction == Rotation::None ? size - 1 : 0, height - verticalOffset, size / 2};
    // for directions 1 or 3, the wall lies along the x axis
    return {size / 2, height - verticalOffset, direction == Rotation::Clockwise90 ? size - 1 : 0};
}

void Wing::addOpening(int dx, int dy, int dz, Rotation direction) { addOpening(dx, dy, dz, direction, Door::Vanishing); }

void Wing::addOpening(int dx, int dy, int dz, Rotation direction, Door type) {
    TowerWing::addOpening(dx, dy, dz, direction);
    // openingTypes.add(openings.indexOf(new BlockPos(dx, dy, dz)), type)
    std::size_t index = 0;
    while (index < openings.size() && !(openings[index].x == dx && openings[index].y == dy && openings[index].z == dz)) index++;
    index = std::min(index, openingTypes.size());
    openingTypes.insert(openingTypes.begin() + static_cast<std::ptrdiff_t>(index), type);
}

void Wing::makeOpenings(Region& region, BlockBox const& sbb) {
    for (std::size_t i = 0; i < openings.size(); i++) {
        auto const& door = openings[i];
        Door        type = openingTypes.size() > i ? openingTypes[i] : Door::Vanishing;
        switch (type) {
        case Door::Reappearing: makeReappearingDoorOpening(region, door.x, door.y, door.z, sbb); break;
        case Door::Locked: makeLockedDoorOpening(region, door.x, door.y, door.z, sbb); break;
        default: makeDoorOpening(region, door.x, door.y, door.z, sbb); break;
        }
    }
}

void Wing::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    Block const& inactiveVanish = J("gloamingforest:unbreakable_vanishing_block");
    if (dx == 0 || dx == size - 1) {
        fill(region, sbb, dx, dy - 1, dz - 2, dx, dy + 3, dz + 2, *deco->accentState, air(), false);
        fill(region, sbb, dx, dy, dz - 1, dx, dy + 2, dz + 1, inactiveVanish, air(), false);
    }
    if (dz == 0 || dz == size - 1) {
        fill(region, sbb, dx - 2, dy - 1, dz, dx + 2, dy + 3, dz, *deco->accentState, air(), false);
        fill(region, sbb, dx - 1, dy, dz, dx + 1, dy + 2, dz, inactiveVanish, air(), false);
    }
}

void Wing::makeReappearingDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    Block const& inactiveReappearing = J("gloamingforest:reappearing_block");
    if (dx == 0 || dx == size - 1) {
        fill(region, sbb, dx, dy - 1, dz - 2, dx, dy + 3, dz + 2, *deco->accentState, air(), false);
        fill(region, sbb, dx, dy, dz - 1, dx, dy + 2, dz + 1, inactiveReappearing, air(), false);
    }
    if (dz == 0 || dz == size - 1) {
        fill(region, sbb, dx - 2, dy - 1, dz, dx + 2, dy + 3, dz, *deco->accentState, air(), false);
        fill(region, sbb, dx - 1, dy, dz, dx + 1, dy + 2, dz, inactiveReappearing, air(), false);
    }
}

void Wing::makeLockedDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    Block const& lockedVanish   = J("gloamingforest:locked_vanishing_block");
    Block const& inactiveVanish = J("gloamingforest:unbreakable_vanishing_block");
    if (dx == 0 || dx == size - 1) {
        fill(region, sbb, dx, dy - 1, dz - 2, dx, dy + 3, dz + 2, *deco->accentState, air(), false);
        fill(region, sbb, dx, dy, dz - 1, dx, dy + 2, dz + 1, inactiveVanish, air(), false);
        placeBlock(region, lockedVanish, dx, dy, dz + 1, sbb);
        placeBlock(region, lockedVanish, dx, dy, dz - 1, sbb);
        placeBlock(region, lockedVanish, dx, dy + 2, dz + 1, sbb);
        placeBlock(region, lockedVanish, dx, dy + 2, dz - 1, sbb);
    }
    if (dz == 0 || dz == size - 1) {
        fill(region, sbb, dx - 2, dy - 1, dz, dx + 2, dy + 3, dz, *deco->accentState, air(), false);
        fill(region, sbb, dx - 1, dy, dz, dx + 1, dy + 2, dz, inactiveVanish, air(), false);
        placeBlock(region, lockedVanish, dx + 1, dy, dz, sbb);
        placeBlock(region, lockedVanish, dx - 1, dy, dz, sbb);
        placeBlock(region, lockedVanish, dx + 1, dy + 2, dz, sbb);
        placeBlock(region, lockedVanish, dx - 1, dy + 2, dz, sbb);
    }
}

bool Wing::isDeadEnd() const {
    // we have to modify this to ignore door type 2 since that leads to balconies
    int nonBalconies = 0;
    for (auto type : openingTypes)
        if (type != Door::Reappearing) nonBalconies++;
    return nonBalconies <= 1;
}

void Wing::placeBlock(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox) {
    TowerWing::placeBlock(region, block, x, y, z, chunkBox);
    noteGenerated(region, block, getWorldPos(x, y, z), chunkBox);
}

void Wing::placeIdRotated(Region& region, std::string const& javaId, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    // ComponentOld#setBlockStateRotated's temporary orientation, for the id form of placeBlock
    auto savedO = orientation;
    auto savedR = this->rotation;
    auto savedM = mirror;
    if (auto o = getOrientation()) setOrientation(::gf::structure::rotateDir(*o, rotation));
    Piece::placeBlock(region, javaId, Json::object(), x, y, z, sbb);
    orientation    = savedO;
    this->rotation = savedR;
    mirror         = savedM;
}

// ==== DarkTowerBridgeComponent =================================================================================

Bridge::Bridge(std::string typeId, int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing(std::move(typeId), i, x, y, z, 5, 5, direction) {
    dSize   = pSize;
    dHeight = pHeight;
    deco    = darkTowerDecorator();
}

void Bridge::addChildren(Piece&, PieceList& list, RandomSource& rand) { makeTowerWing(list, rand, genDepth, 4, 1, 2, dSize, dHeight, Rotation::None); }

bool Bridge::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    // kill too-small towers
    if (wingHeight < 6) return false;
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    // end of the world!
    if (dx[1] + wingHeight > 255) return false;
    auto   wing      = std::make_unique<Wing>(kWing, index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece* intersect = ::gf::structure::bases::findCollisionPiece(list, wing->bbox);
    if (intersect && intersect != this) return false;
    auto* raw = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

void Bridge::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // make walls
    fill(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, *deco->blockState, *deco->blockState, false);
    // accents
    for (int x = 0; x < size; x++) {
        placeBlock(region, *deco->accentState, x, 0, 0, sbb);
        placeBlock(region, *deco->accentState, x, height - 1, 0, sbb);
        placeBlock(region, *deco->accentState, x, 0, size - 1, sbb);
        placeBlock(region, *deco->accentState, x, height - 1, size - 1, sbb);
    }
    // clear inside
    fillAir(region, sbb, 0, 1, 1, size - 1, height - 2, size - 2);
}

BlockBox Bridge::getWingBB() const {
    auto o    = getOrientation().value_or(Orientation::North);
    auto dest = offsetTowerCoords(4, 1, 2, dSize, Orientation::North);
    return componentBox(dest[0], dest[1], dest[2], 0, 0, 0, dSize - 1, dHeight - 1, dSize - 1, Orientation::North);
}

EntranceBridge::EntranceBridge(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: Bridge(kEntranceBridge, i, x, y, z, pSize, pHeight, direction) {}

bool EntranceBridge::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    // make an entrance tower
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    auto        wing      = std::make_unique<Entrance>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    auto*       raw       = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

MainBridge::MainBridge(int i, int x, int y, int z, int, int pHeight, Orientation direction) : Bridge(kMainBridge, i, x, y, z, 15, pHeight, direction) {}

bool MainBridge::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int, int, Rotation rotation) {
    // make another size 15 main tower
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 19, direction);
    auto        wing      = std::make_unique<Main>(rand, index, dx[0], dx[1], dx[2], direction);
    auto*       raw       = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

BossBridge::BossBridge(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: Bridge(kBossBridge, i, x, y, z, pSize, pHeight, direction) {}

bool BossBridge::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    auto        wing      = std::make_unique<BossTrap>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    auto*       raw       = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

// ==== DarkTowerEntranceComponent ===============================================================================

Entrance::Entrance(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: Wing(kEntrance, i, x, y, z, pSize, pHeight, direction) {}

void Entrance::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    Wing::addChildren(parent, list, rand);
    // a few more openings
    addOpening(size / 2, 1, 0, Rotation::Clockwise90, Door::Reappearing);
    addOpening(size / 2, 1, size - 1, Rotation::Counterclockwise90, Door::Reappearing);
}

void Entrance::makeABeard(Piece&, PieceList&, RandomSource&) {}
void Entrance::makeARoof(Piece&, PieceList&, RandomSource&) {}

void Entrance::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    // make walls
    makeEncasedWalls(region, rand, sbb, 0, 0, 0, size - 1, height - 1, size - 1);
    // deco to ground
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) placeBlock(region, *deco->accentState, x, -1, z, sbb);
    // clear inside
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);
    // openings
    makeOpenings(region, sbb);
}

::gf::structure::bases::TerrainAdjustment Entrance::getTerrainAdjustment() const { return ::gf::structure::bases::TerrainAdjustment::BeardBox; }

// ==== DarkTowerBossTrapComponent ===============================================================================

BossTrap::BossTrap(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: Wing(kBossTrap, i, x, y, z, pSize, pHeight, direction) {
    // no spawns
    spawnListIndex = -1;
}

void BossTrap::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
    // add a beard
    makeABeard(parent, list, rand);
    for (auto i : kRotations) {
        if (i == Rotation::Clockwise180 || rand.nextBoolean()) continue;
        // occasional balcony
        auto dest = getValidOpening(rand, i);
        // move opening to tower base
        dest[1] = 1;
        makeTowerBalcony(list, rand, genDepth, dest[0], dest[1], dest[2], i);
    }
}

void BossTrap::makeARoof(Piece&, PieceList&, RandomSource&) {}

void BossTrap::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));
    // make walls
    makeEncasedWalls(region, rand, sbb, 0, 0, 0, size - 1, height - 1, size - 1);
    // clear inside
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);
    // openings
    makeOpenings(region, sbb);
    // half floors, always starting at y = 4
    addBossTrapFloors(region, sbb);
    // demolish some
    destroyTower(region, decoRNG, 5, height + 2, 5, 4, sbb);
    destroyTower(region, decoRNG, 0, height, 0, 3, sbb);
    destroyTower(region, decoRNG, 0, height, 8, 4, sbb);
    // hole for boss trap beam
    destroyTower(region, decoRNG, 5, 6, 5, 2, sbb);
    // redraw some of the floor in case we destroyed it
    fill(region, sbb, 1, 0, 1, size / 2, 0, size - 2, *deco->blockState, air(), false);
    fill(region, sbb, 1, 1, 1, size / 2, 1, size - 2, air(), air(), false);
    // add boss trap
    placeBlock(region, J("gloamingforest:ghast_trap"), 5, 1, 5, sbb);
    placeBlock(region, J("minecraft:redstone_wire"), 5, 1, 6, sbb);
    placeBlock(region, J("minecraft:redstone_wire"), 5, 1, 7, sbb);
    placeBlock(region, J("minecraft:redstone_wire"), 5, 1, 8, sbb);
    placeBlock(region, J("minecraft:redstone_wire"), 4, 1, 8, sbb);
    placeBlock(region, J("minecraft:redstone_wire"), 3, 1, 8, sbb);
    placeBlock(region, J("minecraft:oak_pressure_plate"), 2, 1, 8, sbb);
}

void BossTrap::addBossTrapFloors(Region& region, BlockBox const& sbb) {
    makeFullFloor(region, sbb, 4);
    addStairsDown(region, sbb, Rotation::Counterclockwise90, 4, size - 2, 4);
    addStairsDown(region, sbb, Rotation::Counterclockwise90, 4, size - 3, 4);
    // stairs to roof
    addStairsDown(region, sbb, Rotation::Clockwise90, height - 1, size - 2, 4);
    addStairsDown(region, sbb, Rotation::Clockwise90, height - 1, size - 3, 4);
}

// ==== DarkTowerBalconyComponent ================================================================================

Balcony::Balcony(int i, int x, int y, int z, Orientation direction) : TowerWing(kBalcony, i, x, y, z, 5, 5, direction) { deco = darkTowerDecorator(); }

void Balcony::addChildren(Piece&, PieceList&, RandomSource&) {}

void Balcony::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // make floor
    fill(region, sbb, 0, 0, 0, 2, 0, 4, *deco->accentState, air(), false);
    fill(region, sbb, 0, 0, 1, 1, 0, 3, *deco->blockState, air(), false);
    fill(region, sbb, 0, 1, 0, 2, 1, 4, *deco->fenceState, air(), false);
    placeBlock(region, *deco->accentState, 2, 1, 0, sbb);
    placeBlock(region, *deco->accentState, 2, 1, 4, sbb);
    // clear inside
    fillAir(region, sbb, 0, 1, 1, 1, 1, 3);
}

// ==== DarkTowerBeardComponent ==================================================================================

Beard::Beard(int i, TowerWing const& wing, int x, int y, int z) : ComponentOld(kBeard, i, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = size / 2;
    // just hang out at the very bottom of the tower
    bbox = BlockBox::of(wing.bbox.minX, wing.bbox.minY - height, wing.bbox.minZ, wing.bbox.maxX, wing.bbox.maxY, wing.bbox.maxZ);
    deco = darkTowerDecorator();
}

Beard::Beard() : ComponentOld(kBeard, 0, 0, 0, 0) { deco = darkTowerDecorator(); }

Json Beard::save() const {
    Json j           = ComponentOld::save();
    j["beardSize"]   = size;
    j["beardHeight"] = height;
    return j;
}

void Beard::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("beardSize", 0);
    height = data.value("beardHeight", 0);
}

void Beard::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) { makeDarkBeard(region, sbb, 0, 0, size - 1, height - 1, size - 1); }

void Beard::makeDarkBeard(Region& region, BlockBox const& sbb, int minX, int minZ, int maxX, int maxY, int maxZ) {
    Block const& frameState = J("gloamingforest:encased_towerwood");
    for (int x = minX; x <= maxX; x++)
        for (int z = minZ; z <= maxZ; z++) {
            if (x != minX && x != maxX && z != minZ && z != maxZ) continue;
            int length = std::min(std::abs(x - height) - 1, std::abs(z - height) - 1);
            if (length == height - 1) length++;
            if (length == -1) length = 1;
            // wall
            for (int y = maxY; y >= height - length; y--) placeBlock(region, frameState, x, y, z, sbb);
        }
}

// ==== DarkTowerRoofComponent and its kinds ====================================================================

Roof::Roof(std::string typeId, int i, TowerWing const& wing, int x, int y, int z) : TowerRoof(std::move(typeId), i, x, y, z) {
    // same alignment
    setOrientation(wing.getOrientation());
    // same size
    size   = wing.size;
    height = 12;
    // just hang out at the very top of the tower
    makeCapBB(wing);
    // spawn list!
    spawnListIndex = 1;
    deco           = darkTowerDecorator();
}

Roof::Roof(std::string typeId) : TowerRoof(std::move(typeId), 0, 0, 0, 0) {
    spawnListIndex = 1;
    deco           = darkTowerDecorator();
}

void Roof::addChildren(Piece&, PieceList&, RandomSource&) {}

void Roof::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // a fence around the roof!
    for (int x = 0; x <= size - 1; x++)
        for (int z = 0; z <= size - 1; z++)
            if (x == 0 || x == size - 1 || z == 0 || z == size - 1) placeBlock(region, *deco->fenceState, x, 1, z, sbb);
    placeBlock(region, *deco->accentState, 0, 1, 0, sbb);
    placeBlock(region, *deco->accentState, size - 1, 1, 0, sbb);
    placeBlock(region, *deco->accentState, 0, 1, size - 1, sbb);
    placeBlock(region, *deco->accentState, size - 1, 1, size - 1, sbb);
}

void RoofAntenna::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // stick with a ball on top antenna (no fence: upstream does not call super)
    Block const& a = *deco->accentState;
    int          c = size / 2;
    for (int y = 1; y < 10; y++) placeBlock(region, a, c, y, c, sbb);
    placeBlock(region, a, c - 1, 1, c, sbb);
    placeBlock(region, a, c + 1, 1, c, sbb);
    placeBlock(region, a, c, 1, c - 1, sbb);
    placeBlock(region, a, c, 1, c + 1, sbb);
    for (int y = 7; y < 10; y++) {
        placeBlock(region, a, c - 1, y, c, sbb);
        placeBlock(region, a, c + 1, y, c, sbb);
        placeBlock(region, a, c, y, c - 1, sbb);
        placeBlock(region, a, c, y, c + 1, sbb);
    }
    placeBlock(region, a, c - 1, 8, c - 1, sbb);
    placeBlock(region, a, c - 1, 8, c + 1, sbb);
    placeBlock(region, a, c + 1, 8, c - 1, sbb);
    placeBlock(region, a, c + 1, 8, c + 1, sbb);
}

void RoofCactus::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& a = *deco->accentState;
    int          c = size / 2;
    // antenna (no fence: upstream does not call super)
    for (int y = 1; y < 10; y++) placeBlock(region, *deco->blockState, c, y, c, sbb);
    placeBlock(region, a, c, 10, c, sbb);
    placeBlock(region, a, c - 1, 1, c, sbb);
    placeBlock(region, a, c + 1, 1, c, sbb);
    placeBlock(region, a, c, 1, c - 1, sbb);
    placeBlock(region, a, c, 1, c + 1, sbb);
    // cactus things
    placeBlock(region, a, c + 1, 7, c, sbb);
    placeBlock(region, a, c + 2, 7, c, sbb);
    placeBlock(region, a, c + 2, 8, c, sbb);
    placeBlock(region, a, c + 2, 9, c, sbb);
    placeBlock(region, a, c + 3, 9, c, sbb);
    placeBlock(region, a, c, 6, c + 1, sbb);
    placeBlock(region, a, c, 6, c + 2, sbb);
    placeBlock(region, a, c, 7, c + 2, sbb);
    placeBlock(region, a, c, 8, c + 2, sbb);
    placeBlock(region, a, c, 8, c + 3, sbb);
    placeBlock(region, a, c - 1, 5, c, sbb);
    placeBlock(region, a, c - 2, 5, c, sbb);
    placeBlock(region, a, c - 2, 6, c, sbb);
    placeBlock(region, a, c - 2, 7, c, sbb);
    placeBlock(region, a, c - 3, 7, c, sbb);
    placeBlock(region, a, c, 4, c - 1, sbb);
    placeBlock(region, a, c, 4, c - 2, sbb);
    placeBlock(region, a, c, 5, c - 2, sbb);
    placeBlock(region, a, c, 6, c - 2, sbb);
    placeBlock(region, a, c, 6, c - 3, sbb);
}

void RoofFourPost::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    Roof::postProcess(region, rand, sbb, pivot);
    makeSmallAntenna(region, sbb, 4, size - 2, size - 2);
    makeSmallAntenna(region, sbb, 5, 1, size - 2);
    makeSmallAntenna(region, sbb, 6, size - 2, 1);
    makeSmallAntenna(region, sbb, 7, 1, 1);
}

void RoofFourPost::makeSmallAntenna(Region& region, BlockBox const& sbb, int h, int x, int z) {
    for (int y = 1; y < h; y++) placeBlock(region, *deco->blockState, x, y, z, sbb);
    Block const& a = *deco->accentState;
    placeBlock(region, a, x, h, z, sbb);
    placeBlock(region, a, x, h + 1, z, sbb);
    placeBlock(region, a, x + 1, h + 1, z, sbb);
    placeBlock(region, a, x - 1, h + 1, z, sbb);
    placeBlock(region, a, x, h + 1, z + 1, sbb);
    placeBlock(region, a, x, h + 1, z - 1, sbb);
    placeBlock(region, a, x, h + 2, z, sbb);
}

void RoofRings::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    Roof::postProcess(region, rand, sbb, pivot);
    Block const& a = *deco->accentState;
    int          c = size / 2;
    for (int y = 1; y < 10; y++) placeBlock(region, *deco->blockState, c, y, c, sbb);
    placeBlock(region, a, c, 10, c, sbb);
    placeBlock(region, a, c - 1, 1, c, sbb);
    placeBlock(region, a, c + 1, 1, c, sbb);
    placeBlock(region, a, c, 1, c - 1, sbb);
    placeBlock(region, a, c, 1, c + 1, sbb);
    makeARing(region, 6, sbb);
    makeARing(region, 8, sbb);
}

void RoofRings::makeARing(Region& region, int y, BlockBox const& sbb) {
    Block const& a = *deco->accentState;
    int          c = size / 2;
    placeBlock(region, a, c - 2, y, c + 1, sbb);
    placeBlock(region, a, c - 2, y, c, sbb);
    placeBlock(region, a, c - 2, y, c - 1, sbb);
    placeBlock(region, a, c + 2, y, c + 1, sbb);
    placeBlock(region, a, c + 2, y, c, sbb);
    placeBlock(region, a, c + 2, y, c - 1, sbb);
    placeBlock(region, a, c + 1, y, c - 2, sbb);
    placeBlock(region, a, c, y, c - 2, sbb);
    placeBlock(region, a, c - 1, y, c - 2, sbb);
    placeBlock(region, a, c + 1, y, c + 2, sbb);
    placeBlock(region, a, c, y, c + 2, sbb);
    placeBlock(region, a, c - 1, y, c + 2, sbb);
}

// ==== layout and registration ==================================================================================

void layout(RandomSource& firstPieceRandom, RandomSource& contextRandom, int x, int y, int z, PieceList& pieces) {
    auto  main = std::make_unique<Main>(firstPieceRandom, 0, x, y, z);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
}

void registerPieces() {
    ::gf::structure::bases::registerDecorator("DecoDarkTower", [] { return darkTowerDecorator(); });
    auto reg = [](char const* type, auto make) {
        ::gf::structure::registerPieceType(type, [make](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = make();
            p->load(saved);
            return p;
        });
    };
    reg(kWing, [] { return std::make_unique<Wing>(kWing, 0, 0, 0, 0, 9, 9, Orientation::North); });
    reg(kMain, [] { return std::make_unique<Main>(); });
    reg(kBridge, [] { return std::make_unique<Bridge>(kBridge, 0, 0, 0, 0, 0, 0, Orientation::North); });
    reg(kEntranceBridge, [] { return std::make_unique<EntranceBridge>(0, 0, 0, 0, 0, 0, Orientation::North); });
    reg(kMainBridge, [] { return std::make_unique<MainBridge>(0, 0, 0, 0, 0, 0, Orientation::North); });
    reg(kBossBridge, [] { return std::make_unique<BossBridge>(0, 0, 0, 0, 0, 0, Orientation::North); });
    reg(kEntrance, [] { return std::make_unique<Entrance>(0, 0, 0, 0, 9, 9, Orientation::North); });
    reg(kBossTrap, [] { return std::make_unique<BossTrap>(0, 0, 0, 0, 11, 9, Orientation::North); });
    reg(kBalcony, [] { return std::make_unique<Balcony>(0, 0, 0, 0, Orientation::North); });
    reg(kBeard, [] { return std::make_unique<Beard>(); });
    reg(kRoof, [] { return std::make_unique<Roof>(kRoof); });
    reg(kRoofAntenna, [] { return std::make_unique<RoofAntenna>(kRoofAntenna); });
    reg(kRoofCactus, [] { return std::make_unique<RoofCactus>(kRoofCactus); });
    reg(kRoofFourPost, [] { return std::make_unique<RoofFourPost>(kRoofFourPost); });
    reg(kRoofRings, [] { return std::make_unique<RoofRings>(kRoofRings); });
}

} // namespace gf::feature::darktower::tower
