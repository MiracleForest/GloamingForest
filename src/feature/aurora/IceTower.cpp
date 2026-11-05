// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBossWingComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerEntranceComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerStairsComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerRoofComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBeardComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerDecorator.java
//   src/main/java/twilightforest/world/components/structures/selectors/IceTowerRandomBlockSelectorFactory.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/aurora/IceTower.h"

#include <algorithm>
#include <cmath>

#include "mc/world/level/block/Block.h"

#include "core/aurora/FloorPlan.h"
#include "feature/aurora/Structures.h"
#include "feature/aurora/StructureUtil.h"
#include "feature/ext/aurora/W0Internals.h"
#include "mod/Log.h"

namespace gf::feature::aurora {

namespace blocks = structure::blocks;
using structure::Json;
using structure::bases::ComponentOld;
using structure::bases::Decorator;
using structure::bases::TowerWing;

namespace {

constexpr char kWing[]     = "twilightforest:TFITWin";
constexpr char kMain[]     = "twilightforest:TFITMai";
constexpr char kBoss[]     = "twilightforest:TFITBoss";
constexpr char kEntrance[] = "twilightforest:TFITEnt";
constexpr char kBridge[]   = "twilightforest:TFITBri";
constexpr char kStairs[]   = "twilightforest:TFITSt";
constexpr char kRoof[]     = "twilightforest:TFITRoof";
constexpr char kBeard[]    = "twilightforest:TFITBea";

constexpr char kAuroraRoom[]  = "twilightforest:aurora_room";
constexpr char kAuroraCache[] = "twilightforest:aurora_cache";

struct IceTowerDecoratorImpl final : Decorator {
    [[nodiscard]] std::string decoString() const override { return "DecoIceTower"; }
};

Block const& withAxis(Block const& pillar, char const* axis) { return blocks::withJava(pillar, Json{{"axis", axis}}); }
Block const& slab(Block const& platform, char const* type) { return blocks::withJava(platform, Json{{"type", type}}); }

Rotation rot(Rotation a, Rotation b) { return structure::addRotation(a, b); }

/// Direction#getAxis: X for east / west, Z for north / south.
char const* axisOf(Orientation o) { return (o == Orientation::East || o == Orientation::West) ? "x" : "z"; }

} // namespace

std::unique_ptr<Decorator> iceTowerDecorator() {
    auto d           = std::make_unique<IceTowerDecoratorImpl>();
    auto def         = structure::bases::defaultDecorator();
    d->roofState     = def->roofState; // TFStructureDecorator's default (IceTowerDecorator keeps it)
    d->blockState    = &blocks::java("twilightforest:aurora_block");
    d->accentState   = &blocks::java("minecraft:birch_planks");
    d->fenceState    = &blocks::java("minecraft:oak_fence");
    d->stairState    = &blocks::java("minecraft:birch_stairs");
    d->pillarState   = &blocks::java("twilightforest:aurora_pillar", Json{{"axis", "y"}});
    d->platformState = &blocks::java("minecraft:birch_slab");
    d->floorState    = &blocks::java("minecraft:birch_planks");
    d->randomBlocks  = structure::selectors::get("twilightforest:ice_tower");
    return d;
}

// ==== IceTowerWingComponent ====================================================================================

IceTowerWing::IceTowerWing(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing(std::move(typeId), index, x, y, z, pSize, pHeight, direction) {
    deco = iceTowerDecorator();
}

Json IceTowerWing::save() const {
    Json j             = TowerWing::save();
    j["hasBase"]       = hasBase;
    j["treasureFloor"] = treasureFloor;
    return j;
}

void IceTowerWing::load(Json const& data) {
    TowerWing::load(data);
    hasBase       = data.value("hasBase", false);
    treasureFloor = data.value("treasureFloor", 0);
}

void IceTowerWing::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);

    hasBase = shouldHaveBase(list, rand);

    makeARoof(parent, list, rand);
    if (!hasBase) {
        makeABeard(parent, list, rand);
    } else {
        int minY = bbox.minY;
        bool any = false;
        for (auto const& p : list) {
            minY = any ? std::min(minY, p->bbox.minY) : p->bbox.minY;
            any  = true;
        }
        list.push_back(std::make_unique<structure::bases::UtilityPiece>(genDepth + 1, BlockBox{bbox.minX, minY, bbox.minZ, bbox.maxX, bbox.minY, bbox.maxZ}));
    }

    // limit sprawl to a reasonable amount
    if (genDepth < 5) {
        Rotation dirOffset = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
        for (auto rotation : kRotations) {
            Rotation dir  = rot(dirOffset, rotation);
            auto     dest = getValidOpening(rand, dir);
            auto*    main = dynamic_cast<IceTowerMain*>(&parent);
            if (genDepth == 4 && main && !main->hasBossWing) {
                bool hasBoss      = makeBossTowerWing(list, rand, genDepth + 1, dest[0], dest[1], dest[2], 15, 41, dir);
                main->hasBossWing = hasBoss;
            } else {
                int childHeight = (rand.nextInt(2) + rand.nextInt(2) + 2) * 10 + 1;
                makeTowerWing(list, rand, genDepth + 1, dest[0], dest[1], dest[2], SIZE, childHeight, dir);
            }
        }
    }

    // figure out where the treasure goes
    int floors = height / 10;
    if (treasureFloor == -1 && floors > 1) treasureFloor = rand.nextInt(floors - 1);
}

bool IceTowerWing::shouldHaveBase(PieceList const& list, RandomSource& rand) {
    for (auto const& p : list)
        if (p.get() != this && awtIntersects(p->bbox, bbox)) return false;
    return genDepth == 0 || rand.nextBoolean();
}

bool IceTowerWing::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);

    // stop if out of range
    if (!list.empty() && isOutOfRange(list.front()->bbox, dx[0], dx[2], RANGE)) return false;

    auto        wing = std::make_unique<IceTowerWing>(kWing, index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    auto const& sbb  = wing->bbox;
    int         grow = javaRound(static_cast<float>(size) * 1.414F);
    Piece*      intersect =
        structure::bases::findCollisionPiece(list, BlockBox{sbb.minX, sbb.minY - grow, sbb.minZ, sbb.maxX, sbb.maxY + grow, sbb.maxZ});
    if (intersect && intersect != this) return false;
    for (auto const& p : list) {
        // TF compares with this wing's rectangle, not the new wing's
        if ((p->typeId == kEntrance || p->typeId == kBridge) && p.get() != this && awtIntersects(p->bbox, bbox)) return false;
    }

    auto* raw = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*list.front(), list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

bool IceTowerWing::makeBossTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    auto        wing      = std::make_unique<IceTowerBossWing>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece*      intersect = structure::bases::findCollisionPiece(list, wing->bbox);
    if (intersect && intersect != this) return false;
    auto* raw = wing.get();
    list.push_back(std::move(wing));
    raw->addChildren(*list.front(), list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

int IceTowerWing::getYByStairs(int, RandomSource& rand, Rotation) {
    int floors = height / 10;
    return 11 + (rand.nextInt(std::max(1, floors - 1)) * 10);
}

void IceTowerWing::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    // RandomSource.create(seed + minX * 321534781L ^ minZ * 756839L), Java precedence
    auto const s = static_cast<std::uint64_t>(region.seed()) + static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minX)) * 321534781ULL;
    auto const m = static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minZ)) * 756839ULL;
    core::jrand::LegacyRandom decoRNG(static_cast<std::int64_t>(s ^ m));

    // make walls, clear inside
    randomFill(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, false, rand, *deco->randomBlocks);
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);

    if (hasBase) {
        // deco to ground
        for (int x = 0; x < size; x++)
            for (int z = 0; z < size; z++) fillColumnDown(region, *deco->blockState, x, -1, z, sbb);
    }

    makeFloorsForTower(region, decoRNG, sbb);
    makeOpenings(region, sbb);
    applyAuroraVariants(region, bbox, sbb);
}

void IceTowerWing::makeFloorsForTower(Region& region, RandomSource& decoRNG, BlockBox const& sbb) {
    int const floorHeight = 10;
    int const floors      = height / floorHeight;
    int const topFloor    = floors - 1;

    Rotation ladderDir     = Rotation::Counterclockwise90;
    Rotation downLadderDir = rot(ladderDir, Rotation::Clockwise90);

    decorateTopFloor(region, decoRNG, topFloor, topFloor * floorHeight, (topFloor * floorHeight) + floorHeight, ladderDir, downLadderDir, sbb);

    std::vector<core::aurora::Door> doors;
    for (auto const& o : openings) doors.push_back({o.x, o.y, o.z});
    auto plan = core::aurora::createFloorPlan(floors - 1, doors, decoRNG, [&] {
        auto c = structure::boxCenter(sbb);
        gf::log().error("Unable to generate floor plan for aurora palace at {} {} {}. Please report mod version, coords, and seed to Twilight Forest devs", c.x,
                        c.y, c.z);
    });

    for (int i = floors - 2; i >= 0; i--) {
        placeFloor(region, decoRNG, sbb, floorHeight, i);
        auto const& floor = plan[static_cast<std::size_t>(floors - 2 - i)];
        ladderDir         = Rotation::Counterclockwise90;
        for (int t = 0; t < floor.rotation; t++) ladderDir = rot(ladderDir, Rotation::Clockwise90);
        downLadderDir = rot(ladderDir, Rotation::Clockwise90);

        if (isBossWing()) decorateFloor(region, decoRNG, i, i * floorHeight, (i + 1) * floorHeight, ladderDir, downLadderDir, sbb);
        else decorateFloorOfType(region, decoRNG, static_cast<int>(floor.type), i, i * floorHeight, (i + 1) * floorHeight, ladderDir, downLadderDir, sbb);
    }
}

Orientation IceTowerWing::getChestDirection(Rotation rotation) const { return structure::rotateDir(orientation, rotation); }

void IceTowerWing::placeFloor(Region& region, RandomSource&, BlockBox const& sbb, int floorHeight, int floor) {
    for (int x = 1; x < size - 1; x++)
        for (int z = 1; z < size - 1; z++) placeBlock(region, *deco->floorState, x, (floor * floorHeight) + floorHeight, z, sbb);
}

void IceTowerWing::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    TowerWing::makeDoorOpening(region, dx, dy, dz, sbb);
    if (!blocks::is(getBlock(region, dx, dy + 2, dz, sbb), "minecraft:air")) placeBlock(region, *deco->accentState, dx, dy + 2, dz, sbb);
}

void IceTowerWing::decorateFloorOfType(Region& region, RandomSource&, int type, int floor, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                       BlockBox const& sbb) {
    bool const hasTreasure = treasureFloor == floor;
    using FT               = core::aurora::FloorType;
    switch (static_cast<FT>(type)) {
    case FT::WraparoundWallSteps: decorateWraparoundWallSteps(region, bottom, top, ladderUpDir, hasTreasure, sbb); break;
    case FT::FarWallSteps: decorateFarWallSteps(region, bottom, top, ladderUpDir, hasTreasure, sbb); break;
    case FT::WraparoundWallStepsPillars: decorateWraparoundWallStepsPillars(region, bottom, top, ladderUpDir, ladderDownDir, hasTreasure, sbb); break;
    case FT::Platform: decoratePlatform(region, bottom, top, ladderUpDir, ladderDownDir, hasTreasure, sbb); break;
    case FT::PillarParkour: decoratePillarParkour(region, bottom, top, ladderUpDir, ladderDownDir, hasTreasure, sbb); break;
    case FT::PillarPlatforms: decoratePillarPlatforms(region, bottom, top, ladderUpDir, hasTreasure, sbb); break;
    case FT::PillarPlatformsOutside: decoratePillarPlatformsOutside(region, bottom, top, ladderUpDir, hasTreasure, sbb); break;
    case FT::QuadPillarStairs: decorateQuadPillarStairs(region, bottom, top, ladderUpDir, ladderDownDir, hasTreasure, sbb); break;
    default: break;
    }
}

void IceTowerWing::decorateTopFloor(Region& region, RandomSource& rand, int, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                    BlockBox const& sbb) {
    if (rand.nextBoolean()) decoratePillarsCorners(region, bottom, top, ladderDownDir, sbb);
    else decoratePillarsGrid(region, bottom, top, ladderDownDir, sbb);
    // generate treasure last so it doesn't get deleted
    if (isDeadEnd()) decorateTopFloorTreasure(region, bottom, ladderUpDir, sbb);
}

void IceTowerWing::decorateTopFloorTreasure(Region& region, int bottom, Rotation rotation, BlockBox const& sbb) {
    fillBlocksRotated(region, sbb, 5, bottom + 1, 5, 5, bottom + 4, 5, *deco->pillarState, rotation);
    placeTreasureRotated(region, 5, bottom + 5, 5, getChestDirection(rotation), rotation, kAuroraRoom, false, sbb);
}

void IceTowerWing::decoratePillars(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb) {
    fillBlocksRotated(region, sbb, 3, bottom + 1, 3, 3, top - 1, 3, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 1, 3, 7, top - 1, 3, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 3, bottom + 1, 7, 3, top - 1, 7, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 1, 7, 7, top - 1, 7, *deco->pillarState, rotation);
}

void IceTowerWing::decoratePillarsGrid(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb) {
    auto const& pillarEW = withAxis(*deco->pillarState, "z");
    auto const& pillarNS = withAxis(*deco->pillarState, "x");
    fillBlocksRotated(region, sbb, 3, bottom + 5, 1, 3, bottom + 5, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 1, 7, bottom + 5, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 3, 9, bottom + 5, 3, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 7, 9, bottom + 5, 7, pillarNS, rotation);
    decoratePillars(region, bottom, top, rotation, sbb);
}

void IceTowerWing::decoratePillarsCorners(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb) {
    auto const& pillarEW = withAxis(*deco->pillarState, "z");
    auto const& pillarNS = withAxis(*deco->pillarState, "x");
    fillBlocksRotated(region, sbb, 3, bottom + 5, 1, 3, bottom + 5, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 1, 7, bottom + 5, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 3, 9, bottom + 5, 3, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 7, 9, bottom + 5, 7, pillarNS, rotation);
    fillAirRotated(region, sbb, 3, bottom + 5, 3, 7, bottom + 5, 7, rotation);
    decoratePillars(region, bottom, top, rotation, sbb);
}

void IceTowerWing::decorateFarWallSteps(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb) {
    // far set of stairs
    for (int z = 1; z < 10; z++) {
        int y = bottom + 10 - (z / 2);
        setBlockStateRotated(region, (z % 2 == 0) ? *deco->pillarState : *deco->platformState, 9, y, z, ladderUpDir, sbb);
        for (int by = bottom + 1; by < y; by++) setBlockStateRotated(region, *deco->pillarState, 9, by, z, ladderUpDir, sbb);
    }
    // near set of stairs
    for (int z = 1; z < 10; z++) {
        int y = bottom + 1 + (z / 2);
        setBlockStateRotated(region, (z % 2 == 0) ? *deco->platformState : *deco->pillarState, 8, y, z, ladderUpDir, sbb);
        for (int by = bottom + 1; by < y; by++) setBlockStateRotated(region, *deco->pillarState, 8, by, z, ladderUpDir, sbb);
    }
    // entry stair
    setBlockStateRotated(region, *deco->platformState, 7, bottom + 1, 1, ladderUpDir, sbb);
    // clear floor above
    for (int z = 2; z < 7; z++) setBlockStateRotated(region, air(), 9, top, z, ladderUpDir, sbb);

    auto const& pillarNS = withAxis(*deco->pillarState, "x");
    if (hasTreasure) {
        placeTreasureRotated(region, 1, bottom + 8, 5, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Counterclockwise90), ladderUpDir,
                             kAuroraCache, false, sbb);
        setBlockStateRotated(region, pillarNS, 1, bottom + 7, 5, ladderUpDir, sbb);
    }
}

void IceTowerWing::decorateWraparoundWallSteps(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb) {
    auto const& topPlatform    = slab(*deco->platformState, "top");
    auto const& bottomPlatform = slab(*deco->platformState, "bottom");
    // far set of stairs
    for (int z = 1; z < 10; z++) {
        int y = bottom + 10 - (z / 2);
        setBlockStateRotated(region, (z % 2 == 0) ? topPlatform : bottomPlatform, 9, y, z, ladderUpDir, sbb);
    }
    // right set of stairs
    for (int x = 1; x < 9; x++) {
        int y = bottom + 2 + ((x - 1) / 2);
        setBlockStateRotated(region, (x % 2 == 0) ? topPlatform : bottomPlatform, x, y, 9, ladderUpDir, sbb);
    }
    // entry stairs
    setBlockStateRotated(region, topPlatform, 1, bottom + 1, 8, ladderUpDir, sbb);
    setBlockStateRotated(region, *deco->platformState, 1, bottom + 1, 7, ladderUpDir, sbb);
    // clear floor above
    for (int z = 2; z < 7; z++) setBlockStateRotated(region, air(), 9, top, z, ladderUpDir, sbb);

    if (hasTreasure) {
        placeTreasureRotated(region, 1, bottom + 5, 5, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Counterclockwise90), ladderUpDir,
                             kAuroraCache, false, sbb);
        setBlockStateRotated(region, withAxis(*deco->pillarState, "x"), 1, bottom + 4, 5, ladderUpDir, sbb);
    }
}

void IceTowerWing::decorateWraparoundWallStepsPillars(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure,
                                                      BlockBox const& sbb) {
    Rotation    rotation = ladderDownDir;
    auto const& pillarEW = withAxis(*deco->pillarState, "z");
    auto const& pillarNS = withAxis(*deco->pillarState, "x");

    decorateWraparoundWallSteps(region, bottom, top, ladderUpDir, false, sbb);
    decoratePillars(region, bottom, top, rotation, sbb);

    fillBlocksRotated(region, sbb, 3, bottom + 5, 1, 3, bottom + 5, 2, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 1, 7, bottom + 5, 2, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 8, bottom + 1, 3, 9, bottom + 1, 3, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 8, bottom + 7, 7, 9, bottom + 7, 7, pillarNS, rotation);

    fillBlocksRotated(region, sbb, 1, bottom + 2, 3, 2, bottom + 2, 3, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 6, 3, 2, bottom + 6, 3, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 4, 7, 2, bottom + 4, 7, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 8, 7, 2, bottom + 8, 7, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 3, bottom + 6, 8, 3, bottom + 6, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 8, 8, 7, bottom + 8, 9, pillarEW, rotation);

    if (hasTreasure)
        placeTreasureRotated(region, 7, bottom + 7, 1, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Clockwise90), ladderUpDir, kAuroraCache,
                             false, sbb);
}

void IceTowerWing::decoratePlatform(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure,
                                    BlockBox const& sbb) {
    auto const& topPlatform    = slab(*deco->platformState, "top");
    auto const& bottomPlatform = slab(*deco->platformState, "bottom");

    decoratePillars(region, bottom, top, ladderDownDir, sbb);
    fillBlocksRotated(region, sbb, 3, bottom + 5, 3, 7, bottom + 5, 7, *deco->floorState, ladderDownDir);

    // one flight
    for (int z = 6; z < 10; z++) {
        int y = bottom - 2 + (z / 2);
        setBlockStateRotated(region, (z % 2 == 1) ? topPlatform : bottomPlatform, 1, y, z, ladderDownDir, sbb);
    }
    // two flight
    for (int x = 2; x < 6; x++) {
        int y = bottom + 2 + (x / 2);
        setBlockStateRotated(region, (x % 2 == 1) ? topPlatform : bottomPlatform, x, y, 9, ladderDownDir, sbb);
    }
    // connector
    setBlockStateRotated(region, *deco->platformState, 5, bottom + 5, 8, ladderDownDir, sbb);

    // connector
    setBlockStateRotated(region, *deco->platformState, 5, bottom + 6, 2, ladderUpDir, sbb);
    // two flight
    for (int x = 5; x < 10; x++) {
        int y = bottom + 4 + (x / 2);
        setBlockStateRotated(region, (x % 2 == 1) ? topPlatform : bottomPlatform, x, y, 1, ladderUpDir, sbb);
        if (x > 6) setBlockStateRotated(region, air(), x, top, 1, ladderUpDir, sbb);
    }
    // one flight
    for (int z = 2; z < 5; z++) {
        int y = bottom + 8 + (z / 2);
        setBlockStateRotated(region, air(), 9, top, z, ladderUpDir, sbb);
        setBlockStateRotated(region, (z % 2 == 1) ? topPlatform : bottomPlatform, 9, y, z, ladderUpDir, sbb);
    }

    if (hasTreasure)
        placeTreasureRotated(region, 5, bottom + 6, 5, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Clockwise90), ladderDownDir,
                             kAuroraCache, false, sbb);
}

void IceTowerWing::decorateQuadPillarStairs(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure,
                                            BlockBox const& sbb) {
    decoratePillars(region, bottom, top, ladderDownDir, sbb);

    auto const& topPlatform    = slab(*deco->platformState, "top");
    auto const& bottomPlatform = slab(*deco->platformState, "bottom");

    // one flight
    for (int z = 6; z < 9; z++) {
        int y = bottom - 2 + (z / 2);
        setBlockStateRotated(region, z % 2 == 1 ? topPlatform : bottomPlatform, 2, y, z, ladderDownDir, sbb);
    }
    // two flight
    for (int x = 3; x < 9; x++) {
        int y = bottom + 1 + (x / 2);
        setBlockStateRotated(region, x % 2 == 1 ? topPlatform : bottomPlatform, x, y, 8, ladderDownDir, sbb);
    }
    // three flight
    for (int z = 7; z > 1; z--) {
        int y = top - 2 - ((z - 1) / 2);
        if (z < 4) setBlockStateRotated(region, air(), 8, top, z, ladderDownDir, sbb);
        setBlockStateRotated(region, (z % 2 == 1) ? topPlatform : bottomPlatform, 8, y, z, ladderDownDir, sbb);
    }
    // last flight
    for (int x = 7; x > 3; x--) {
        int y = top + 1 - ((x - 1) / 2);
        setBlockStateRotated(region, air(), x, top, 2, ladderDownDir, sbb);
        setBlockStateRotated(region, (x % 2 == 1) ? topPlatform : bottomPlatform, x, y, 2, ladderDownDir, sbb);
    }

    if (hasTreasure) {
        int  treasureX = 3, treasureY = bottom + 7, treasureZ = 5;
        auto facing    = structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Clockwise90);
        placeTreasureRotated(region, treasureX, treasureY, treasureZ, facing, ladderUpDir, kAuroraCache, false, sbb);
        fillBlocksRotated(region, sbb, treasureX, treasureY - 1, treasureZ - 1, treasureX, treasureY - 1, treasureZ + 1, withAxis(*deco->pillarState, axisOf(facing)),
                          ladderUpDir);
    }
}

void IceTowerWing::decoratePillarPlatforms(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb) {
    // clear
    fillAirRotated(region, sbb, 2, top, 2, 8, top, 4, ladderUpDir);
    fillAirRotated(region, sbb, 2, top, 2, 4, top, 6, ladderUpDir);

    // extra pillar tops
    setBlockStateRotated(region, *deco->platformState, 7, top, 3, ladderUpDir, sbb);
    setBlockStateRotated(region, *deco->platformState, 3, top, 3, ladderUpDir, sbb);

    // platforms
    for (int y = 1; y < 10; y++) {
        int x = (y + 1) % 4 < 2 ? 3 : 7;
        int z = y % 4 < 2 ? 3 : 7;
        fillBlocksRotated(region, sbb, x - 1, bottom + y, z - 1, x + 1, bottom + y, z + 1, *deco->floorState, ladderUpDir);
    }

    decoratePillars(region, bottom, top, ladderUpDir, sbb);

    if (hasTreasure)
        placeTreasureRotated(region, 3, bottom + 5, 2, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Counterclockwise90), ladderUpDir,
                             kAuroraCache, false, sbb);
}

void IceTowerWing::decoratePillarPlatformsOutside(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb) {
    // platforms
    for (auto r : kRotations) {
        Rotation rotation = rot(ladderUpDir, r);
        fillBlocksRotated(region, sbb, 1, bottom, 1, 6, bottom, 3, *deco->floorState, rotation);
        if (r == Rotation::Counterclockwise90) continue; // to make space for floor entrance
        fillBlocksRotated(region, sbb, 1, bottom + 1, 1, 3, bottom + 1, 3, *deco->platformState, rotation);
    }

    buildStairway(region, bottom, top, ladderUpDir, sbb);
    decoratePillars(region, bottom, top, ladderUpDir, sbb);

    if (hasTreasure) {
        int treasureX = 3, treasureY = bottom + 5, treasureZ = 2;
        placeTreasureRotated(region, treasureX, treasureY, treasureZ, structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Counterclockwise90),
                             ladderUpDir, kAuroraCache, false, sbb);
        auto axis = axisOf(structure::rotateDir(getChestDirection(ladderUpDir), Rotation::Clockwise90));
        fillBlocksRotated(region, sbb, treasureX, treasureY - 1, treasureZ - 1, treasureX, treasureY - 1, treasureZ, withAxis(*deco->pillarState, axis),
                          ladderUpDir);
    }
}

void IceTowerWing::buildStairway(Region& region, int bottom, int top, Rotation ladderUpDir, BlockBox const& sbb) {
    Rotation rotation = rot(ladderUpDir, Rotation::Clockwise180);
    fillAirRotated(region, sbb, 8, top, 1, 9, top + 2, 9, rotation);

    auto const& slabTypeTop    = slab(*deco->platformState, "top");
    auto const& slabTypeBottom = slab(*deco->platformState, "bottom");

    for (int step = 0; step < 22; step++) {
        Block const& s               = step % 2 == 0 ? slabTypeTop : slabTypeBottom;
        int          firstDirection  = std::min(step, 7);
        int          secondDirection = std::min(step - firstDirection, 8);
        int          thirdDirection  = std::min(step - firstDirection - secondDirection, 8);

        int firstWidthAdjustment  = firstDirection < 7 ? 1 : 0;
        int secondWidthAdjustment = secondDirection > 0 && secondDirection < 7 ? 1 : 0;
        int thirdWidthAdjustment  = thirdDirection > 0 ? 1 : 0;
        int heightAdjustment      = (secondDirection > 0 ? 1 : 0) + (thirdDirection > 0 ? 1 : 0);

        fillBlocksRotated(region, sbb, 9 - secondDirection - firstWidthAdjustment, top - step / 2 + heightAdjustment, 8 - firstDirection + thirdDirection,
                          9 - secondDirection + thirdWidthAdjustment, top - step / 2 + heightAdjustment, 8 - firstDirection + thirdDirection + secondWidthAdjustment,
                          s, rotation);
    }

    placeFullBlocks(region, bottom, top, sbb, rotation);
}

void IceTowerWing::placeFullBlocks(Region& region, int bottom, int top, BlockBox const& sbb, Rotation rotation) {
    fillBlocksRotated(region, sbb, 8, top, 8, 9, top, 9, *deco->floorState, rotation);
    fillBlocksRotated(region, sbb, 8, top - 3, 1, 9, top - 3, 2, *deco->floorState, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 4, 1, 2, bottom + 4, 2, *deco->floorState, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 1, 7, 2, bottom + 1, 9, *deco->floorState, rotation);
}

void IceTowerWing::decoratePillarParkour(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure,
                                         BlockBox const& sbb) {
    Rotation    rotation = ladderDownDir;
    auto const& pillarEW = withAxis(*deco->pillarState, "z");
    auto const& pillarNS = withAxis(*deco->pillarState, "x");

    // 4 pillars
    decoratePillars(region, bottom, top, rotation, sbb);
    // center pillar
    setBlockStateRotated(region, *deco->pillarState, 5, bottom + 1, 5, rotation, sbb);
    // pillar 2
    fillBlocksRotated(region, sbb, 5, bottom + 2, 7, 5, bottom + 2, 9, pillarEW, rotation);
    // gap 3
    fillBlocksRotated(region, sbb, 1, bottom + 3, 7, 2, bottom + 3, 7, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 3, bottom + 3, 8, 3, bottom + 3, 9, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 7, 7, 2, bottom + 7, 7, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 3, bottom + 7, 8, 3, bottom + 7, 9, pillarEW, rotation);
    fillAirRotated(region, sbb, 3, bottom + 4, 7, 3, bottom + 6, 7, rotation);
    // pillar 4
    fillBlocksRotated(region, sbb, 1, bottom + 4, 5, 2, bottom + 4, 5, pillarNS, rotation);
    // gap 5
    fillBlocksRotated(region, sbb, 3, bottom + 5, 1, 3, bottom + 5, 2, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 3, 2, bottom + 5, 3, pillarNS, rotation);
    fillAirRotated(region, sbb, 3, bottom + 6, 3, 3, bottom + 8, 3, rotation);
    // pillar 6
    fillBlocksRotated(region, sbb, 5, bottom + 6, 1, 5, bottom + 6, 2, pillarEW, rotation);
    // gap 7
    fillAirRotated(region, sbb, 7, bottom + 8, 3, 7, bottom + 10, 3, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 7, 1, 7, bottom + 7, 2, pillarEW, rotation);
    fillBlocksRotated(region, sbb, 8, bottom + 7, 3, 9, bottom + 7, 3, pillarNS, rotation);
    // pillar 8
    fillBlocksRotated(region, sbb, 8, bottom + 8, 5, 9, bottom + 8, 5, pillarNS, rotation);
    // gap 9 (no gap?)
    fillBlocksRotated(region, sbb, 8, bottom + 9, 7, 9, bottom + 9, 7, pillarNS, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 9, 8, 7, bottom + 9, 9, pillarEW, rotation);
    // holes in ceiling
    fillAirRotated(region, sbb, 6, top, 6, 8, top, 8, ladderUpDir);
    fillAirRotated(region, sbb, 2, top, 6, 5, top, 8, ladderUpDir);

    if (hasTreasure)
        placeTreasureRotated(region, 8, bottom + 8, 7, structure::opposite(getChestDirection(ladderUpDir)), ladderUpDir, kAuroraCache, false, sbb);
}

void IceTowerWing::makeARoof(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc = getLocatorPosition();
    tryToFitRoof(list, rand, std::make_unique<IceTowerRoof>(genDepth + 1, *this, loc.x, loc.y, loc.z));
}

void IceTowerWing::makeABeard(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc   = getLocatorPosition();
    auto     beard = std::make_unique<IceTowerBeard>(genDepth + 1, *this, loc.x, loc.y, loc.z);
    auto*    raw   = beard.get();
    list.push_back(std::move(beard));
    raw->addChildren(*this, list, rand);
}

// ==== IceTowerMainComponent ====================================================================================

IceTowerMain::IceTowerMain(RandomSource& rand, int index, int x, int y, int z)
: IceTowerWing(kMain, index, x + SIZE, y + 40, z + SIZE, SIZE, 31 + (rand.nextInt(3) * 10), Orientation::North) {}

IceTowerMain::IceTowerMain() : IceTowerWing(kMain, 0, 0, 0, 0, SIZE, 31, Orientation::North) {}

Json IceTowerMain::save() const {
    Json j           = IceTowerWing::save();
    j["hasBossWing"] = hasBossWing;
    return j;
}

void IceTowerMain::load(Json const& data) {
    IceTowerWing::load(data);
    hasBossWing = data.value("hasBossWing", false);
}

void IceTowerMain::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    auto* main = dynamic_cast<IceTowerMain*>(&parent);
    if (!main) return;
    while (!main->hasBossWing) {
        rand.setSeed(rand.nextLong());
        openings.clear();
        addOpening(0, 1, size / 2, Rotation::Clockwise180);
        // start.pieces.clear(); start.pieces.add(parent);
        std::unique_ptr<Piece> keep;
        for (auto& p : list)
            if (p.get() == &parent) keep = std::move(p);
        list.clear();
        if (keep) list.push_back(std::move(keep));

        // the entrance: TF only handles the door it always has (x == 0); the other cases of its
        // towerBB arithmetic never happen
        BlockPos myDoor       = openings.front();
        BlockPos entranceDoor = myDoor;
        if (myDoor.x == 0) {
            int length = 30 + rand.nextInt(20);
            if (length >= 0) {
                entranceDoor.x -= length; // BlockPos#west
                makeEntranceBridge(list, rand, genDepth + 1, myDoor.x, myDoor.y, myDoor.z, length, Rotation::Clockwise180);
            }
        }
        makeEntranceTower(list, rand, genDepth + 1, entranceDoor.x, entranceDoor.y, entranceDoor.z, SIZE, 11, rotation);
        IceTowerWing::addChildren(parent, list, rand);
    }
}

void IceTowerMain::makeEntranceBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int length, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    BlockPos    dest      = offsetTowerCCoords(x, y, z, 5, direction);
    auto        bridge    = std::make_unique<IceTowerBridge>(index, dest.x, dest.y, dest.z, length, 4, direction);
    auto*       raw       = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*list.front(), list, rand);
}

bool IceTowerMain::makeEntranceTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    auto        entrance  = std::make_unique<IceTowerEntrance>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    auto*       raw       = entrance.get();
    list.push_back(std::move(entrance));
    raw->addChildren(*list.front(), list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

// ==== IceTowerBossWingComponent ================================================================================

IceTowerBossWing::IceTowerBossWing(int index, int x, int y, int z, int wingSize, int wingHeight, Orientation direction)
: IceTowerWing(kBoss, index, x, y, z, wingSize, wingHeight, direction) {
    spawnListIndex = -1; // no spawns
}

bool IceTowerBossWing::shouldHaveBase(PieceList const&, RandomSource&) { return false; }

void IceTowerBossWing::placeFloor(Region& region, RandomSource& rand, BlockBox const& sbb, int floorHeight, int floor) {
    for (int x = 1; x < size - 1; x++)
        for (int z = 1; z < size - 1; z++) {
            Block const& ice       = blocks::java(rand.nextInt(4) == 0 ? "minecraft:ice" : "minecraft:packed_ice");
            int          thickness = 1 + rand.nextInt(2) + rand.nextInt(2) + rand.nextInt(2);
            for (int y = 0; y < thickness; y++) placeBlock(region, ice, x, (floor * floorHeight) + floorHeight - y, z, sbb);
        }
}

void IceTowerBossWing::decorateFloor(Region& region, RandomSource& rand, int, int bottom, int, std::optional<Rotation>, std::optional<Rotation> ladderDownDir,
                                     BlockBox const& sbb) {
    Rotation r = ladderDownDir.value_or(Rotation::None);
    for (int y = 0; y < 3; y++) {
        placeIceStairs(region, sbb, rand, bottom + (y * 3), r);
        placeIceStairs(region, sbb, rand, bottom + (y * 3), rot(r, Rotation::Clockwise180));
        r = rot(r, Rotation::Clockwise90);
    }
}

void IceTowerBossWing::placeIceStairs(Region& region, BlockBox const& sbb, RandomSource& rand, int y, Rotation rotation) {
    Block const& packedIce = blocks::java("minecraft:packed_ice");
    fillBlocksRotated(region, sbb, 8, y + 1, 1, 10, y + 1, 3, packedIce, rotation);
    if (y > 1) randomlyFillBlocksRotated(region, sbb, rand, 0.5F, 8, y, 1, 10, y, 3, packedIce, air(), rotation);
    fillBlocksRotated(region, sbb, 11, y + 2, 1, 13, y + 2, 3, packedIce, rotation);
    randomlyFillBlocksRotated(region, sbb, rand, 0.5F, 11, y + 1, 1, 13, y + 1, 3, packedIce, air(), rotation);
    fillBlocksRotated(region, sbb, 11, y + 3, 4, 13, y + 3, 6, packedIce, rotation);
    randomlyFillBlocksRotated(region, sbb, rand, 0.5F, 11, y + 2, 4, 13, y + 2, 6, packedIce, air(), rotation);
}

void IceTowerBossWing::decorateTopFloor(Region& region, RandomSource& rand, int, int, int top, Rotation, Rotation, BlockBox const& sbb) {
    for (int x = 1; x < size - 1; x++)
        for (int z = 1; z < size - 1; z++) {
            Block const& ice       = blocks::java(rand.nextInt(10) == 0 ? "minecraft:glowstone" : "minecraft:packed_ice");
            int          thickness = rand.nextInt(2) + rand.nextInt(2);
            for (int y = 0; y < thickness; y++) placeBlock(region, ice, x, top - 1 - y, z, sbb);
        }
    setBlockStateRotated(region, blocks::java("twilightforest:snow_queen_boss_spawner"), 7, top - 6, 7, Rotation::None, sbb);
    fillAir(region, sbb, 5, top - 3, 5, 9, top - 1, 9);
}

// ==== IceTowerEntranceComponent ================================================================================

IceTowerEntrance::IceTowerEntrance(int index, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: IceTowerWing(kEntrance, index, x, y, z, pSize, pHeight, direction) {}

bool IceTowerEntrance::shouldHaveBase(PieceList const&, RandomSource&) { return true; }

void IceTowerEntrance::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(size - 1, 1, size / 2, Rotation::None);
    // stairs
    addStairs(list, rand, genDepth + 1, 0, 1, size / 2, Rotation::Clockwise180);
    addStairs(list, rand, genDepth + 1, size / 2, 1, 0, Rotation::Counterclockwise90);
    addStairs(list, rand, genDepth + 1, size / 2, 1, size - 1, Rotation::Clockwise90);
    hasBase = shouldHaveBase(list, rand);
    makeARoof(parent, list, rand);
}

bool IceTowerEntrance::addStairs(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation) {
    addOpening(x, y, z, rotation);
    Orientation direction = getStructureRelativeRotation(rotation);
    BlockPos    dx        = offsetTowerCCoords(x, y, z, size, direction);
    auto        stairs    = std::make_unique<IceTowerStairs>(index, dx.x, dx.y, dx.z, size, height, direction);
    auto*       raw       = stairs.get();
    list.push_back(std::move(stairs));
    raw->addChildren(*this, list, rand);
    return true;
}

bool IceTowerEntrance::makeTowerWing(PieceList&, RandomSource&, int, int, int, int, int, int, Rotation) { return false; }

void IceTowerEntrance::makeFloorsForTower(Region& region, RandomSource&, BlockBox const& sbb) { decoratePillarsCornersHigh(region, 0, 11, Rotation::None, sbb); }

void IceTowerEntrance::decoratePillarsCornersHigh(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb) {
    auto const& pillarXAxis = withAxis(*deco->pillarState, "x");
    auto const& pillarZAxis = withAxis(*deco->pillarState, "z");
    fillBlocksRotated(region, sbb, 3, bottom + 5, 1, 3, bottom + 5, 9, pillarZAxis, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 1, 7, bottom + 5, 9, pillarZAxis, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 3, 9, bottom + 5, 3, pillarXAxis, rotation);
    fillBlocksRotated(region, sbb, 1, bottom + 5, 7, 9, bottom + 5, 7, pillarXAxis, rotation);
    fillAirRotated(region, sbb, 3, bottom + 5, 3, 7, bottom + 5, 7, rotation);
    // pillars connected only to ceiling
    fillBlocksRotated(region, sbb, 3, bottom + 5, 3, 3, top - 1, 3, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 3, 7, top - 1, 3, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 3, bottom + 5, 7, 3, top - 1, 7, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 7, bottom + 5, 7, 7, top - 1, 7, *deco->pillarState, rotation);
}

// ==== IceTowerBridgeComponent ==================================================================================

IceTowerBridge::IceTowerBridge(int index, int x, int y, int z, int pLength, int zLength, Orientation direction)
: ComponentOld(kBridge, index, x, y, z), length(pLength), extraZlength(zLength) {
    setOrientation(direction);
    bbox = componentToAddBoundingBox(x, y, z, 0, 0, -zLength, length, 6, 5 + zLength, direction);
    deco = iceTowerDecorator();
}

Json IceTowerBridge::save() const {
    Json j            = ComponentOld::save();
    j["bridgeLength"] = length;
    j["extraZlength"] = extraZlength;
    return j;
}

void IceTowerBridge::load(Json const& data) {
    ComponentOld::load(data);
    length       = data.value("bridgeLength", 0);
    extraZlength = data.value("extraZlength", 0);
}

void IceTowerBridge::addChildren(Piece&, PieceList&, RandomSource&) {}

void IceTowerBridge::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    fillAir(region, sbb, 0, 1, 0, length, 5, 4);
    // make floor/ceiling
    fill(region, sbb, 0, 0, extraZlength, length, 0, extraZlength + 4, *deco->blockState, *deco->blockState, false);
    fill(region, sbb, 0, 6, extraZlength, length, 6, extraZlength + 4, *deco->blockState, *deco->blockState, false);
    // pillars
    for (int x = 2; x < length; x += 3) {
        fill(region, sbb, x, 1, extraZlength, x, 5, extraZlength, *deco->pillarState, *deco->pillarState, false);
        fill(region, sbb, x, 1, extraZlength + 4, x, 5, extraZlength + 4, *deco->pillarState, *deco->pillarState, false);
    }
    applyAuroraVariants(region, bbox, sbb);
}

// ==== IceTowerStairsComponent ==================================================================================

IceTowerStairs::IceTowerStairs(int index, int x, int y, int z, int pSize, int, Orientation direction)
: TowerWing(kStairs, index, x, y + 1 - stairsLength, z, pSize, stairsLength, direction) {
    deco = iceTowerDecorator();
}

void IceTowerStairs::addChildren(Piece&, PieceList&, RandomSource&) {}

void IceTowerStairs::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    for (int x = 1; x < size; x++) {
        placeStairs(region, sbb, x, stairsLength - x, stairsLength);
        for (int z = 0; z <= x; z++) {
            if (z > 0 && z <= size / 2) {
                placeStairs(region, sbb, x, stairsLength - x, stairsLength - z);
                placeStairs(region, sbb, x, stairsLength - x, stairsLength + z);
            }
            if (x <= size / 2) {
                placeStairs(region, sbb, z, stairsLength - x, stairsLength - x);
                placeStairs(region, sbb, z, stairsLength - x, stairsLength + x);
            }
        }
    }
    placeBlock(region, *deco->blockState, 0, stairsLength - 1, stairsLength, sbb);
    applyAuroraVariants(region, bbox, sbb);
}

void IceTowerStairs::placeStairs(Region& region, BlockBox const& sbb, int x, int y, int z) {
    if (blocks::canBeReplaced(getBlock(region, x, y, z, sbb))) {
        placeBlock(region, *deco->blockState, x, y, z, sbb);
        placeBlock(region, *deco->blockState, x, y - 1, z, sbb);
    }
}

// ==== IceTowerRoofComponent ====================================================================================

IceTowerRoof::IceTowerRoof(int index, TowerWing const& wing, int x, int y, int z) : TowerRoof(kRoof, index, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = javaRound(javaSqrt(static_cast<float>(size * size + size * size)));
    deco   = iceTowerDecorator();
    makeCapBB(wing);
}

IceTowerRoof::IceTowerRoof() : TowerRoof(kRoof, 0, 0, 0, 0) { deco = iceTowerDecorator(); }

void IceTowerRoof::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    TowerRoof::postProcess(region, rand, sbb, pivot);
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) {
            int rHeight = javaRound(javaSqrt(static_cast<float>(x * x + z * z)));
            for (int y = 0; y < rHeight; y++) placeBlock(region, *deco->blockState, x, y, z, sbb);
        }
    applyAuroraVariants(region, bbox, sbb);
}

// ==== IceTowerBeardComponent ===================================================================================

IceTowerBeard::IceTowerBeard(int index, TowerWing const& wing, int x, int y, int z) : ComponentOld(kBeard, index, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = javaRound(static_cast<float>(size) * 1.414F);
    deco   = iceTowerDecorator();
    bbox   = BlockBox::of(wing.bbox.minX, wing.bbox.minY - height, wing.bbox.minZ, wing.bbox.maxX, wing.bbox.minY, wing.bbox.maxZ);
}

IceTowerBeard::IceTowerBeard() : ComponentOld(kBeard, 0, 0, 0, 0) { deco = iceTowerDecorator(); }

Json IceTowerBeard::save() const {
    Json j           = ComponentOld::save();
    j["beardSize"]   = size;
    j["beardHeight"] = height;
    return j;
}

void IceTowerBeard::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("beardSize", 0);
    height = data.value("beardHeight", 0);
}

void IceTowerBeard::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) {
            int rHeight = javaRound(javaSqrt(static_cast<float>(x * x + z * z)));
            for (int y = 0; y < rHeight; y++) placeBlock(region, *deco->blockState, x, height - y, z, sbb);
        }
    applyAuroraVariants(region, bbox, sbb);
}

// ==== registration =============================================================================================

void registerIceTowerPieces() {
    structure::bases::registerDecorator("DecoIceTower", [] { return iceTowerDecorator(); });
    auto wingLike = [](char const* type, auto make) {
        structure::registerPieceType(type, [make](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = make();
            p->load(saved);
            return p;
        });
    };
    wingLike(kWing, [] { return std::make_unique<IceTowerWing>(kWing, 0, 0, 0, 0, IceTowerWing::SIZE, 21, Orientation::North); });
    wingLike(kMain, [] { return std::make_unique<IceTowerMain>(); });
    wingLike(kBoss, [] { return std::make_unique<IceTowerBossWing>(0, 0, 0, 0, 15, 41, Orientation::North); });
    wingLike(kEntrance, [] { return std::make_unique<IceTowerEntrance>(0, 0, 0, 0, IceTowerWing::SIZE, 11, Orientation::North); });
    wingLike(kBridge, [] { return std::make_unique<IceTowerBridge>(0, 0, 0, 0, 0, 4, Orientation::North); });
    wingLike(kStairs, [] { return std::make_unique<IceTowerStairs>(0, 0, 0, 0, IceTowerWing::SIZE, 11, Orientation::North); });
    wingLike(kRoof, [] { return std::make_unique<IceTowerRoof>(); });
    wingLike(kBeard, [] { return std::make_unique<IceTowerBeard>(); });
}

} // namespace gf::feature::aurora
