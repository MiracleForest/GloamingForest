// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerMainBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/TowerRoofMushroomComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerDecorator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/aurora/MushroomTower.h"

#include <algorithm>
#include <cmath>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/aurora/StructureUtil.h"
#include "feature/ext/aurora/W0Internals.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::aurora {

namespace blocks = structure::blocks;
using structure::BlockBox;
using structure::Json;
using structure::Orientation;
using structure::Piece;
using structure::RandomSource;
using structure::Region;
using structure::Rotation;
using structure::bases::Decorator;
using structure::bases::PieceList;
using structure::bases::TowerWing;

namespace {

constexpr char kWing[]       = "twilightforest:TFMTWin";
constexpr char kMain[]       = "twilightforest:TFMTMai";
constexpr char kBridge[]     = "twilightforest:TFMTBri";
constexpr char kMainBridge[] = "twilightforest:TFMTMB";
constexpr char kRoof[]       = "twilightforest:TFMTRoofMush";

struct MushroomTowerDecoratorImpl final : Decorator {
    [[nodiscard]] std::string decoString() const override { return "DecoMushroomTower"; }
};

Block const& junglePlanks() { return blocks::java("minecraft:jungle_planks"); }

/// A huge mushroom block by Bedrock's huge_mushroom_bits (F7's Java mapping does not carry the
/// up / down / side faces: 10 = stem sides with pores up and down, 14 = cap on every side).
Block const& mushroom(char const* id, int bits, Json const& javaProps) {
    if (auto const* b = gf::game::blockWithStates(id, Json{{"huge_mushroom_bits", bits}})) return *b;
    return blocks::java(id, javaProps);
}

} // namespace

std::unique_ptr<Decorator> mushroomTowerDecorator() {
    auto d   = std::make_unique<MushroomTowerDecoratorImpl>();
    auto def = structure::bases::defaultDecorator();
    // the TFStructureDecorator defaults MushroomTowerDecorator keeps
    d->platformState = def->platformState;
    d->roofState     = def->roofState;
    d->randomBlocks  = def->randomBlocks;
    Json const stem  = {{"up", false}, {"down", false}};
    d->blockState    = &mushroom("minecraft:mushroom_stem", 10, stem);
    d->accentState   = &mushroom("minecraft:red_mushroom_block", 14, Json::object());
    d->fenceState    = &blocks::java("minecraft:oak_fence");
    d->stairState    = &blocks::java("minecraft:spruce_stairs");
    d->pillarState   = &mushroom("minecraft:mushroom_stem", 10, stem);
    d->floorState    = &blocks::java("minecraft:oak_planks");
    return d;
}

// ==== MushroomTowerWingComponent ===============================================================================

MushroomTowerWing::MushroomTowerWing(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing(std::move(typeId), index, x, y, z, pSize, pHeight, direction) {
    // TF copies the parent's decorator in addChildren; every piece of the tower has the same one
    deco = mushroomTowerDecorator();
}

Json MushroomTowerWing::save() const {
    Json j          = TowerWing::save();
    j["hasBase"]    = hasBase;
    j["isAscender"] = isAscender;
    return j;
}

void MushroomTowerWing::load(Json const& data) {
    TowerWing::load(data);
    hasBase    = data.value("hasBase", false);
    isAscender = data.value("isAscender", false);
}

bool MushroomTowerWing::isBridge() const { return typeId == kBridge || typeId == kMainBridge; }

void MushroomTowerWing::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
    hasBase = size > 3;

    // if we are an ascender, build the destination tower
    if (isAscender) {
        auto dest       = getValidOpening(rand, Rotation::Clockwise180);
        dest[1]         = height - 3;
        int childHeight = (rand.nextInt(3) + rand.nextInt(3) + 2) * FLOOR_HEIGHT + 1;
        bool madeIt     = makeMainBridge(list, rand, genDepth + 1, dest[0], dest[1], dest[2], childHeight, Rotation::Clockwise180);
        if (!madeIt) gf::log().info("Did not make bridge back to new main");
        else gf::log().debug("Made bridge back to new main");
    }

    // limit sprawl to a reasonable amount
    if (genDepth < 5 && size > 6) {
        for (auto i : kRotations) {
            if (size < MAIN_SIZE && i == Rotation::Clockwise180) continue;
            auto dest        = getValidOpening(rand, i);
            int  childHeight = (rand.nextInt(2) + rand.nextInt(2) + 2) * FLOOR_HEIGHT + 1;
            makeBridge(list, rand, genDepth + 1, dest[0], dest[1], dest[2], size - 4, childHeight, i);
        }
    }

    if (isHighest(bbox, size, list) || !hasBase) makeARoof(*this, list, rand);
}

bool MushroomTowerWing::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);

    // stop if out of range
    if (isOutOfRange(bbox, dx[0], dx[2], RANGE)) return false;
    // adjust coordinates to fit an existing tower
    if (wingSize > 3) dx = adjustCoordinates(dx[0], dx[1], dx[2], wingSize, direction, list);

    auto   wing      = std::make_unique<MushroomTowerWing>(kWing, index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece* intersect = structure::bases::findCollisionPiece(list, wing->bbox);
    if (!intersect || intersect == this || intersect->typeId == kRoof) {
        // if we are coming from an ascender bridge, mark the destination component
        if (isBridge() && isAscender) wing->isAscender = true;
        auto* raw = wing.get();
        list.push_back(std::move(wing));
        raw->addChildren(*this, list, rand);
        addOpening(x, y, z, rotation);
        return true;
    }
    return false;
}

std::array<int, 3> MushroomTowerWing::adjustCoordinates(int x, int y, int z, int wingSize, Orientation direction, PieceList const& list) const {
    for (auto const& p : list) {
        auto const* otherWing = dynamic_cast<TowerWing const*>(p.get());
        if (!otherWing || p->typeId == kBridge || p->typeId == kMainBridge) continue;
        auto const& b = otherWing->bbox;
        // BoundingBox#intersects(minX, minZ, maxX, maxZ)
        if (wingSize == otherWing->size && b.maxX >= x - 3 && b.minX <= x + 3 && b.maxZ >= z - 3 && b.minZ <= z + 3) {
            switch (direction) {
            case Orientation::South: return {b.minX, y, b.minZ};
            case Orientation::West: return {b.maxX, y, b.minZ};
            case Orientation::North: return {b.maxX, y, b.maxZ};
            case Orientation::East: return {b.minX, y, b.maxZ};
            default: return {x, y, z};
            }
        }
    }
    return {x, y, z};
}

bool MushroomTowerWing::isHighest(BlockBox const& box, int wingSize, PieceList const& list) const {
    BlockBox boxAbove{box.minX, box.minY, box.minZ, box.maxX, 256, box.maxZ};
    for (auto const& p : list) {
        if (p.get() == this) continue;
        auto const* otherWing = dynamic_cast<TowerWing const*>(p.get());
        if (!otherWing || p->typeId == kBridge || p->typeId == kMainBridge) continue;
        if (wingSize == otherWing->size && otherWing->bbox.intersects(boxAbove)) return false;
    }
    return true;
}

void MushroomTowerWing::makeARoof(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc  = getLocatorPosition();
    auto     roof = std::make_unique<TowerRoofMushroom>(genDepth + 1, *this, 1.6F, loc.x, loc.y, loc.z);
    auto     isMushroomRoof = [&](BlockBox const& box) {
        Piece* hit = structure::bases::findCollisionPiece(list, box);
        return hit && hit->typeId == kRoof;
    };
    if (!isMushroomRoof(roof->bbox)) {
        roof = std::make_unique<TowerRoofMushroom>(genDepth + 1, *this, 1.0F, loc.x, loc.y, loc.z);
        if (!isMushroomRoof(roof->bbox)) roof = std::make_unique<TowerRoofMushroom>(genDepth + 1, *this, 0.6F, loc.x, loc.y, loc.z);
    }
    auto* raw = roof.get();
    list.push_back(std::move(roof));
    raw->addChildren(*this, list, rand);
}

bool MushroomTowerWing::makeBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    return makeBridge(list, rand, index, x, y, z, wingSize, wingHeight, rotation, false);
}

bool MushroomTowerWing::makeBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation,
                                   bool ascender) {
    // bridges are size 3 always
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 3, direction);
    // adjust height for those stupid little things
    if (wingSize == 3) wingHeight = 4;

    auto bridge        = std::make_unique<MushroomTowerBridge>(kBridge, index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    bridge->isAscender = ascender;
    Piece* intersect   = structure::bases::findCollisionPiece(list, bridge->bbox);
    if (!intersect || intersect == this) intersect = structure::bases::findCollisionPiece(list, bridge->getWingBB());
    else return false;
    if (!intersect || intersect == this) {
        auto* raw = bridge.get();
        list.push_back(std::move(bridge));
        raw->addChildren(*this, list, rand);
        addOpening(x, y, z, rotation);
        return true;
    }
    return false;
}

bool MushroomTowerWing::makeMainBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 3, direction);
    auto        bridge    = std::make_unique<MushroomTowerMainBridge>(index, dx[0], dx[1], dx[2], wingHeight, direction);
    auto*       raw       = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

std::array<int, 3> MushroomTowerWing::getValidOpening(RandomSource& rand, Rotation direction) {
    int wLength = std::min(size / 3, 3);   // wall length
    int offset  = (size - wLength) / 2;    // wall thickness
    if (direction == Rotation::None || direction == Rotation::Clockwise180) {
        int rx = direction == Rotation::None ? size - 1 : 0;
        int rz = offset + rand.nextInt(wLength);
        int ry = getYByStairs(rz, rand, direction);
        return {rx, ry, rz};
    }
    if (direction == Rotation::Clockwise90 || direction == Rotation::Counterclockwise90) {
        int rx = offset + rand.nextInt(wLength);
        int rz = direction == Rotation::Clockwise90 ? size - 1 : 0;
        int ry = getYByStairs(rx, rand, direction);
        return {rx, ry, rz};
    }
    return {0, 0, 0};
}

int MushroomTowerWing::getYByStairs(int, RandomSource& rand, Rotation) {
    int floors = height / FLOOR_HEIGHT;
    return FLOOR_HEIGHT + 1 + (rand.nextInt(std::max(1, floors - 1)) * FLOOR_HEIGHT);
}

void MushroomTowerWing::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    makeTrunk(region, sbb);
    makeFloorsForTower(region, sbb);
    makeOpenings(region, sbb);
}

void MushroomTowerWing::makeTrunk(Region& region, BlockBox const& sbb) {
    int diameter = size / 2;
    int hollow   = static_cast<int>(diameter * 0.8);
    for (int dx = -diameter; dx <= diameter; dx++)
        for (int dz = -diameter; dz <= diameter; dz++) {
            int ax   = std::abs(dx);
            int az   = std::abs(dz);
            int dist = static_cast<int>(std::max(ax, az) + (std::min(ax, az) * 0.4));
            if (dist <= diameter) {
                // make floor/ceiling
                placeBlock(region, *deco->floorState, dx + diameter, 0, dz + diameter, sbb);
                placeBlock(region, *deco->floorState, dx + diameter, height, dz + diameter, sbb);
                // make walls
                if (dist > hollow) {
                    for (int dy = 0; dy <= height; dy++) placeBlock(region, *deco->blockState, dx + diameter, dy, dz + diameter, sbb);
                } else {
                    for (int dy = 1; dy <= height - 1; dy++) placeBlock(region, air(), dx + diameter, dy, dz + diameter, sbb);
                }
                // make base
                if (hasBase) fillColumnDown(region, *deco->blockState, dx + diameter, -1, dz + diameter, sbb);
            }
        }
}

void MushroomTowerWing::makeFloorsForTower(Region& region, BlockBox const& sbb) {
    int floors = height / FLOOR_HEIGHT;
    for (int i = 0; i < floors; i++) placeFloor(region, i * FLOOR_HEIGHT, sbb);
}

void MushroomTowerWing::placeFloor(Region& region, int dy, BlockBox const& sbb) {
    int diameter = size / 2;
    int hollow   = static_cast<int>(diameter * 0.8);
    for (int dx = -diameter; dx <= diameter; dx++)
        for (int dz = -diameter; dz <= diameter; dz++) {
            int ax   = std::abs(dx);
            int az   = std::abs(dz);
            int dist = static_cast<int>(std::max(ax, az) + (std::min(ax, az) * 0.4));
            if (dist <= hollow) placeBlock(region, isAscender ? junglePlanks() : *deco->floorState, dx + diameter, dy, dz + diameter, sbb);
        }
}

void MushroomTowerWing::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    TowerWing::makeDoorOpening(region, dx, dy, dz, sbb);
    if (!getBlock(region, dx, dy + 2, dz, sbb).isAir()) placeBlock(region, *deco->accentState, dx, dy + 2, dz, sbb);
}

void MushroomTowerWing::decorateFloor(Region&, RandomSource&, int, int, int, std::optional<Rotation>, std::optional<Rotation>, BlockBox const&) {}

// ==== MushroomTowerMainComponent ===============================================================================

MushroomTowerMain::MushroomTowerMain(RandomSource& rand, int index, int x, int y, int z)
: MushroomTowerWing(kMain, index, x + MAIN_SIZE, y + 4, z + MAIN_SIZE, MAIN_SIZE, 8 + (rand.nextInt(3) * FLOOR_HEIGHT), Orientation::North) {}

MushroomTowerMain::MushroomTowerMain(int index, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: MushroomTowerWing(kMain, index, x, y, z, pSize, pHeight, direction) {}

void MushroomTowerMain::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
    hasBase = true;

    std::optional<Rotation> mainDir;
    if (genDepth < 3) {
        // make a special sub-tower that will lead back here; try 6 times
        for (int i = 0; i < 6; i++) {
            mainDir = makeAscenderTower(list, rand);
            if (mainDir) break;
        }
        // make sub towers
        for (auto i : kRotations) {
            if (mainDir && i == *mainDir) continue;
            auto dest        = getValidOpening(rand, i);
            int  childHeight = (rand.nextInt(2) + rand.nextInt(2) + 2) * FLOOR_HEIGHT + 1;
            makeBridge(list, rand, genDepth + 1, dest[0], dest[1], dest[2], size - 4, childHeight, i);
        }
    } else {
        makeARoof(parent, list, rand);
    }
}

std::optional<Rotation> MushroomTowerMain::makeAscenderTower(PieceList& list, RandomSource& rand) {
    Rotation mainDir     = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
    auto     dest        = getValidOpening(rand, mainDir);
    int      childHeight = (height - dest[1]) + ((rand.nextInt(2) + rand.nextInt(2) + 3) * FLOOR_HEIGHT) + 1;
    bool     madeIt      = makeBridge(list, rand, genDepth + 1, dest[0], dest[1], dest[2], size - 4, childHeight, mainDir, true);
    if (madeIt) {
        gf::log().debug("Main tower made a bridge to another tower");
        return mainDir;
    }
    gf::log().info("Main tower failed to branch off at index {}", genDepth);
    return std::nullopt;
}

void MushroomTowerMain::makeARoof(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc  = getLocatorPosition();
    auto     roof = std::make_unique<TowerRoofMushroom>(genDepth + 1, *this, 1.6F, loc.x, loc.y, loc.z);
    auto*    raw  = roof.get();
    list.push_back(std::move(roof));
    raw->addChildren(*this, list, rand);
}

void MushroomTowerMain::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    MushroomTowerWing::makeDoorOpening(region, dx, dy, dz, sbb);
    // try to remove blocks inside this door
    if (dx == 0) {
        placeBlock(region, air(), dx + 1, dy, dz, sbb);
        placeBlock(region, air(), dx + 1, dy + 1, dz, sbb);
    }
    if (dx == size - 1) {
        placeBlock(region, air(), dx - 1, dy, dz, sbb);
        placeBlock(region, air(), dx - 1, dy + 1, dz, sbb);
    }
    if (dz == 0) {
        placeBlock(region, air(), dx, dy, dz + 1, sbb);
        placeBlock(region, air(), dx, dy + 1, dz + 1, sbb);
    }
    if (dz == size - 1) {
        placeBlock(region, air(), dx, dy, dz - 1, sbb);
        placeBlock(region, air(), dx, dy + 1, dz - 1, sbb);
    }
}

// ==== MushroomTowerBridgeComponent =============================================================================

MushroomTowerBridge::MushroomTowerBridge(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: MushroomTowerWing(std::move(typeId), index, x, y, z, pSize, pHeight, direction), dSize(pSize), dHeight(pHeight) {
    bbox = componentToAddBoundingBox(x, y, z, 0, 0, 0, size - 1, height - 1, 3, direction);
}

Json MushroomTowerBridge::save() const {
    Json j          = MushroomTowerWing::save();
    j["destSize"]   = dSize;
    j["destHeight"] = dHeight;
    return j;
}

void MushroomTowerBridge::load(Json const& data) {
    MushroomTowerWing::load(data);
    dSize   = data.value("destSize", 0);
    dHeight = data.value("destHeight", 0);
}

void MushroomTowerBridge::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    std::array<int, 3> dest{dSize - 1, 1, 1};
    bool               madeWing = makeTowerWing(list, rand, genDepth, dest[0], dest[1], dest[2], dSize, dHeight, Rotation::None);
    if (!madeWing) {
        auto dx = offsetTowerCoords(dest[0], dest[1], dest[2], dSize, Orientation::South);
        gf::log().info("Making tower wing failed when bridge was already made.  Size = {}, x = {}, z = {}", dSize, dx[0], dx[2]);
    }
}

BlockBox MushroomTowerBridge::getWingBB() const {
    auto o    = getOrientation().value_or(Orientation::South);
    auto dest = offsetTowerCoords(dSize - 1, 1, 1, dSize, o);
    return componentToAddBoundingBox(dest[0], dest[1], dest[2], 0, 0, 0, dSize - 1, dHeight - 1, dSize - 1, o);
}

void MushroomTowerBridge::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // make walls
    for (int x = 0; x < dSize; x++) {
        placeBlock(region, *deco->fenceState, x, 1, 0, sbb);
        placeBlock(region, *deco->fenceState, x, 1, 2, sbb);
        placeBlock(region, isAscender ? junglePlanks() : *deco->floorState, x, 0, 1, sbb);
    }
    // clear bridge walkway
    fillAir(region, sbb, 0, 1, 1, 2, 2, 1);
}

// ==== MushroomTowerMainBridgeComponent =========================================================================

MushroomTowerMainBridge::MushroomTowerMainBridge(int index, int x, int y, int z, int pHeight, Orientation direction)
: MushroomTowerBridge(kMainBridge, index, x, y, z, 11, pHeight, direction) {}

bool MushroomTowerMainBridge::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int, int wingHeight, Rotation rotation) {
    // make a new size 15 main tower
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 15, direction);
    // adjust coordinates to fit an existing tower
    dx        = adjustCoordinates(dx[0], dx[1], dx[2], 15, direction, list);
    auto wing = std::make_unique<MushroomTowerMain>(index, dx[0], dx[1], dx[2], 15, wingHeight, direction);
    auto* raw = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*list.front(), list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

// ==== TowerRoofMushroomComponent ===============================================================================

TowerRoofMushroom::TowerRoofMushroom(int index, TowerWing const& wing, float pHang, int x, int y, int z) : TowerRoof(kRoof, index, x, y, z) {
    height       = wing.size;
    int overhang = static_cast<int>(static_cast<float>(height) * pHang);
    size         = height + (overhang * 2);
    setOrientation(Orientation::South);
    auto const& w = wing.bbox;
    bbox          = BlockBox::of(w.minX - overhang, w.maxY + 2, w.minZ - overhang, w.maxX + overhang, w.maxY + height + 1, w.maxZ + overhang);
    deco          = mushroomTowerDecorator();
}

TowerRoofMushroom::TowerRoofMushroom() : TowerRoof(kRoof, 0, 0, 0, 0) { deco = mushroomTowerDecorator(); }

void TowerRoofMushroom::addChildren(Piece&, PieceList&, RandomSource&) {}

void TowerRoofMushroom::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    for (int y = 0; y <= height; y++) {
        float s      = core::jrand::mthSin((static_cast<float>(y) + static_cast<float>(height) / 1.2F) / (static_cast<float>(height) * 2.05F) * 3.14F);
        int   radius = static_cast<int>(s * static_cast<float>(size) / 2.0F);
        int   hollow = core::mth::floorf(static_cast<float>(radius) * .9F);
        if ((height - y) < 3) hollow = -1;
        makeCircle(region, y, radius, hollow, sbb);
    }
}

void TowerRoofMushroom::makeCircle(Region& region, int y, int radius, int hollow, BlockBox const& sbb) {
    int cx = size / 2;
    int cz = size / 2;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dz = -radius; dz <= radius; dz++) {
            float dist = javaSqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist <= (static_cast<float>(radius) + 0.5F)) {
                // the shell and the spore inside are both the accent's default state
                (void)hollow;
                placeBlock(region, *deco->accentState, dx + cx, y, dz + cz, sbb);
            }
        }
}

// ==== registration =============================================================================================

void registerMushroomTowerPieces() {
    structure::bases::registerDecorator("DecoMushroomTower", [] { return mushroomTowerDecorator(); });
    auto reg = [](char const* type, auto make) {
        structure::registerPieceType(type, [make](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = make();
            p->load(saved);
            return p;
        });
    };
    reg(kWing, [] { return std::make_unique<MushroomTowerWing>(kWing, 0, 0, 0, 0, 11, 9, Orientation::North); });
    reg(kMain, [] { return std::make_unique<MushroomTowerMain>(0, 0, 0, 0, MushroomTowerWing::MAIN_SIZE, 8, Orientation::North); });
    reg(kBridge, [] { return std::make_unique<MushroomTowerBridge>(kBridge, 0, 0, 0, 0, 11, 9, Orientation::North); });
    reg(kMainBridge, [] { return std::make_unique<MushroomTowerMainBridge>(0, 0, 0, 0, 9, Orientation::North); });
    reg(kRoof, [] { return std::make_unique<TowerRoofMushroom>(); });
}

} // namespace gf::feature::aurora
