// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/darktower/DarkTowerMainComponent.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "feature/darktower/Runtime.h"
#include "feature/darktower/Tower.h"
#include "feature/darktower/TowerUtil.h"
#include "feature/features/Features.h"
#include "feature/structure/Internal.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::darktower::tower {

namespace util {

Block const& J(char const* id, Json const& props) { return ::gf::structure::blocks::java(id, props); }

char const* dirName(Orientation o) {
    switch (o) {
    case Orientation::North: return "north";
    case Orientation::South: return "south";
    case Orientation::West: return "west";
    default: return "east";
    }
}

Block const& lever(char const* face, Orientation facing, bool powered) {
    return J("minecraft:lever", Json{{"face", face}, {"facing", dirName(facing)}, {"powered", powered ? "true" : "false"}});
}

Block const& ladder(Orientation facing) { return J("minecraft:ladder", Json{{"facing", dirName(facing)}}); }

Rotation rot(Rotation a, Rotation b) { return ::gf::structure::addRotation(a, b); }

bool isAirBlock(Block const& block) { return ::gf::structure::blocks::isAir(block); }

std::int64_t decoSeed(Region& region, BlockBox const& box) {
    auto const s = static_cast<std::uint64_t>(region.seed()) + static_cast<std::uint64_t>(static_cast<std::int64_t>(box.minX)) * 321534781ULL;
    auto const m = static_cast<std::uint64_t>(static_cast<std::int64_t>(box.minZ)) * 756839ULL;
    return static_cast<std::int64_t>(s ^ m);
}

BlockBox componentBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir) {
    switch (dir) {
    case Orientation::West: return BlockBox::of(x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX);
    case Orientation::North: return BlockBox::of(x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ);
    case Orientation::East: return BlockBox::of(x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX);
    default: return BlockBox::of(x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ);
    }
}

void noteGenerated(Region& region, Block const& block, BlockPos const& pos, BlockBox const& chunkBox) {
    if (region.dimension() < 0 || !chunkBox.contains(pos.x, pos.y, pos.z)) return; // scratch regions keep nothing
    auto const& id = ::gf::structure::blocks::id(block);
    if (id == "twilightforest:antibuilder" || id == "twilightforest:carminite_builder" || id == "twilightforest:carminite_reactor" ||
        id == "twilightforest:ghast_trap")
        runtime::queueGenerated(region.dimension(), pos, Json{{"k", "track"}});
}

} // namespace util

using namespace util;
using ::gf::structure::bases::Maze;

// ==== DarkTowerMainComponent ===================================================================================

Main::Main(RandomSource& rand, int index, int x, int y, int z) : Main(rand, index, x + 10, y, z + 10, Orientation::North) {}

Main::Main(RandomSource& rand, int index, int x, int y, int z, Orientation rotation)
: Wing(kMain, index, x, y, z, 19, 56 + ((rand.nextInt(32) / 5) * 5), rotation) {
    // check to make sure we can build the whole tower
    if (bbox.maxY > 245) {
        int amtToLower = (((bbox.maxY - 245) / 5) * 5) + 5;
        gf::log().info("Lowering Dark Tower max height by {} to be within world bounds", amtToLower);
        height -= amtToLower;
        bbox.minY -= amtToLower;
        bbox.maxY -= amtToLower;
    }
}

Main::Main() : Wing(kMain, 0, 0, 0, 0, 19, 56, Orientation::North) {}

void Main::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // if this is not the first main part, add one
    if (genDepth > 0) addOpening(0, 1, size / 2, Rotation::Clockwise180);

    // one direction gets a new main tower, unless we're at tower #3
    std::optional<Rotation> mainDir;
    if (genDepth < 2) {
        mainDir = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
        // make sub towers
        for (auto rotation : kRotations) {
            if (rotation == *mainDir) continue;
            auto dest        = getValidOpening(rand, rotation);
            int  childHeight = validateChildHeight(21 + rand.nextInt(10));
            makeTowerWing(list, rand, genDepth, dest[0], dest[1], dest[2], 11, childHeight, rotation);
        }
    } else {
        // make boss trap towers
        for (auto i : kRotations) {
            auto dest = getValidOpening(rand, i);
            makeBossTrapWing(list, rand, genDepth, dest[0], dest[1], dest[2], i);
        }
    }

    if (genDepth > 0) {
        // sub towers at base of tower
        for (auto i : kRotations) {
            if (i == Rotation::Clockwise180) continue;
            auto dest = getValidOpening(rand, i);
            // move opening to tower base
            dest[1]         = 1;
            int childHeight = validateChildHeight(21 + rand.nextInt(10));
            makeTowerWing(list, rand, genDepth, dest[0], dest[1], dest[2], 11, childHeight, i);
        }
        // add a beard
        makeABeard(parent, list, rand);
    } else {
        // 2 entrance towers at base of tower
        for (auto rotation : {Rotation::None, Rotation::Clockwise180}) {
            auto dest = getValidOpening(rand, rotation);
            // move opening to tower base
            dest[1]         = 1;
            int childHeight = validateChildHeight(10 + rand.nextInt(5));
            makeEntranceTower(list, rand, 5, dest[0], dest[1], dest[2], 9, childHeight, rotation);
        }
    }

    // actually make main tower
    if (mainDir) {
        auto dest = getValidOpening(rand, *mainDir);
        makeNewLargeTower(list, rand, genDepth + 1, dest[0], dest[1], dest[2], *mainDir);
    }

    // add a roof?
    makeARoof(parent, list, rand);

    // flag certain towers for keys
    if (!placedKeys && genDepth < 2) {
        // count how many size 9 towers we have hanging off us (our own piece types, not game types)
        std::vector<Wing*> possibleKeyTowers;
        for (auto const& piece : list)
            if (auto* wing = dynamic_cast<Wing*>(piece.get()); wing && wing->size == 9 && wing->genDepth == genDepth) possibleKeyTowers.push_back(wing);
        for (int i = 0; i < 4; i++) {
            if (possibleKeyTowers.empty()) {
                gf::log().warn("Dark forest tower could not find four small towers to place keys in.");
                break;
            }
            int towerNum = rand.nextInt(static_cast<int>(possibleKeyTowers.size()));
            possibleKeyTowers[static_cast<std::size_t>(towerNum)]->setKeyTower(true);
            possibleKeyTowers.erase(possibleKeyTowers.begin() + towerNum);
        }
        placedKeys = true;
    }
}

bool Main::makeEntranceTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int childSize, int childHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 5, direction);
    auto        bridge    = std::make_unique<EntranceBridge>(index, dx[0], dx[1], dx[2], childSize, childHeight, direction);
    // if I'm doing this right, the main towers can't intersect
    auto* raw = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

bool Main::makeNewLargeTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 5, direction);
    auto        bridge    = std::make_unique<MainBridge>(index, dx[0], dx[1], dx[2], 15, 56, direction);
    auto*       raw       = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*this, list, rand);
    // lock the door!
    addOpening(x, y, z, rotation, Door::Locked);
    return true;
}

bool Main::makeBossTrapWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 5, direction);
    auto        bridge    = std::make_unique<BossBridge>(index, dx[0], dx[1], dx[2], 11, 9, direction);
    auto*       raw       = bridge.get();
    list.push_back(std::move(bridge));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

void Main::makeARoof(Piece& parent, PieceList& list, RandomSource& rand) {
    if (genDepth < 2) Wing::makeARoof(parent, list, rand);
}

void Main::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));

    // make walls
    makeEncasedWalls(region, rand, sbb, 0, 0, 0, size - 1, height - 1, size - 1);
    // clear inside
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);

    if (genDepth == 0) {
        // deco to ground
        for (int x = 0; x < size; x++)
            for (int z = 0; z < size; z++) placeBlock(region, *deco->accentState, x, -1, z, sbb);
    }

    int  totalFloors     = height / 5;
    bool beamMaze        = decoRNG.nextBoolean(); // false = builder platforms
    int  centerFloors    = beamMaze ? 4 : totalFloors / 2;
    int  bottomFloors    = (totalFloors - centerFloors) / 2;
    int  topFloorsStartY = height - (bottomFloors * 5 + 1);

    // add bottom and top floors
    addThreeQuarterFloors(region, decoRNG, sbb, 0, bottomFloors * 5);
    if (genDepth < 2) {
        addThreeQuarterFloors(region, decoRNG, sbb, topFloorsStartY, height - 1);
    } else {
        addThreeQuarterFloorsDecorateBoss(region, sbb, topFloorsStartY, height - 1);
        // boss destruction
        destroyTower(region, decoRNG, 12, height + 4, 3, 4, sbb);
        destroyTower(region, decoRNG, 3, height + 4, 12, 4, sbb);
        destroyTower(region, decoRNG, 3, height + 4, 3, 4, sbb);
        destroyTower(region, decoRNG, 12, height + 4, 12, 4, sbb);
        destroyTower(region, decoRNG, 8, height + 4, 8, 5, sbb);
        // make spawner where it will hopefully last
        decorateBossSpawner(region, sbb, Rotation::None, height - 6);
    }

    if (beamMaze) addTimberMaze(region, decoRNG, sbb, bottomFloors * 5, topFloorsStartY);
    else addBuilderPlatforms(region, decoRNG, sbb, bottomFloors * 5, topFloorsStartY);

    // openings
    makeOpenings(region, sbb);
}

void Main::addThreeQuarterFloors(Region& region, RandomSource& decoRNG, BlockBox const& sbb, int bottom, int top) {
    int      spacing  = 5;
    Rotation rotation = kRotations[static_cast<std::size_t>(std::abs((bbox.minY + bottom) % 4))];
    if (bottom == 0) {
        makeLargeStairsUp(region, sbb, rotation, 0);
        rotation = rot(rotation, Rotation::Counterclockwise90);
        makeBottomEntrance(region, sbb, rotation, bottom);
        bottom += spacing;
    }
    // fill with 3/4 floors
    for (int y = bottom; y < top; y += spacing) {
        bool isBottomFloor   = y == bottom && bottom != spacing;
        bool isTopFloor      = y >= top - spacing;
        bool isTowerTopFloor = y >= height - spacing - 2;
        makeThreeQuarterFloor(region, sbb, rotation, y, isBottomFloor, isTowerTopFloor);
        if (!isTopFloor) makeLargeStairsUp(region, sbb, rotation, y);
        if (!isTopFloor || isTowerTopFloor) decorateMainFloor(region, decoRNG, sbb, rotation, y, isBottomFloor, isTopFloor);
        rotation = rot(rotation, Rotation::Counterclockwise90);
    }
}

void Main::addThreeQuarterFloorsDecorateBoss(Region& region, BlockBox const& sbb, int bottom, int top) {
    int      spacing  = 5;
    Rotation rotation = kRotations[static_cast<std::size_t>(javaMod(bbox.minY + bottom, 4))];
    if (bottom == 0) {
        makeLargeStairsUp(region, sbb, rotation, 0);
        rotation = rot(rotation, Rotation::Counterclockwise90);
        bottom += spacing;
    }
    for (int y = bottom; y < top; y += spacing) {
        bool isBottomFloor   = y == bottom && bottom != spacing;
        bool isTopFloor      = y >= top - spacing;
        bool isTowerTopFloor = y >= height - spacing - 2;
        makeThreeQuarterFloor(region, sbb, rotation, y, isBottomFloor, isTowerTopFloor);
        if (!isTopFloor) {
            makeLargeStairsUp(region, sbb, rotation, y);
            decorateExperiment(region, sbb, rotation, y);
        }
        rotation = rot(rotation, Rotation::Counterclockwise90);
    }
}

void Main::decorateMainFloor(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTop) {
    // pick an appropriate decoration and use it (TF's fallthroughs kept)
    if (isTop) {
        switch (decoRNG.nextInt(3)) {
        case 1: decorateBotanical(region, decoRNG, sbb, rotation, y); break;
        case 2: decorateNetherwart(region, decoRNG, sbb, rotation, y, isTop); break;
        default: decorateAquarium(region, sbb, rotation, y); break;
        }
    } else if (isBottom) {
        switch (decoRNG.nextInt(4)) {
        case 1: decorateBotanical(region, decoRNG, sbb, rotation, y); break;
        case 2:
            if (y + bbox.minY > 64) {
                decorateNetherwart(region, decoRNG, sbb, rotation, y, isTop);
                break;
            }
            [[fallthrough]];
        case 3: decorateForge(region, decoRNG, sbb, rotation, y); break;
        default: decorateAquarium(region, sbb, rotation, y); break;
        }
    } else {
        switch (decoRNG.nextInt(8)) {
        case 2: decorateUnbuilderMaze(region, decoRNG, sbb, rotation, y); break;
        case 3: decorateAquarium(region, sbb, rotation, y); break;
        case 4: decorateBotanical(region, decoRNG, sbb, rotation, y); break;
        case 5:
            if (y + bbox.minY > 64) {
                decorateNetherwart(region, decoRNG, sbb, rotation, y, isTop);
                break;
            }
            [[fallthrough]];
        case 6: decorateMainLounge(region, decoRNG, sbb, rotation, y); break;
        case 7: decorateForge(region, decoRNG, sbb, rotation, y); break;
        default: decorateReappearingMaze(region, decoRNG, sbb, rotation, y); break;
        }
    }
}

void Main::makeThreeQuarterFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTowerTopFloor) {
    int half = size / 2;
    fillBlocksRotated(region, sbb, half + 1, y, 1, size - 2, y, half + 1, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, 1, y, half + 1, size - 2, y, size - 2, *deco->blockState, rotation);
    // don't make part of the fence if we have stairs coming up
    int startZ = isBottom ? 1 : 3;
    fillBlocksRotated(region, sbb, 1, y, half, half, y, half, *deco->accentState, rotation);
    fillBlocksRotated(region, sbb, half, y, startZ, half, y, half, *deco->accentState, rotation);
    fillBlocksRotated(region, sbb, 1, y + 1, half, half, y + 1, half, *deco->fenceState, rotation);
    fillBlocksRotated(region, sbb, half, y + 1, startZ, half, y + 1, half, *deco->fenceState, rotation);
    // little notch if we're at the tower top
    if (isTowerTopFloor) {
        fillBlocksRotated(region, sbb, 1, y, half - 2, 3, y, half, *deco->accentState, rotation);
        fillBlocksRotated(region, sbb, 1, y + 1, half - 2, 3, y + 1, half, *deco->fenceState, rotation);
        fillBlocksRotated(region, sbb, 1, y, half - 1, 2, y, half, *deco->fenceState, rotation);
        fillBlocksRotated(region, sbb, 1, y + 1, half - 1, 2, y + 1, half, air(), rotation);
    }
}

void Main::makeLargeStairsUp(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    for (int i = 0; i < 5; i++) {
        int z  = size / 2 - i + 4;
        int sy = y + i + 1;
        setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), 1, sy, z, rotation, sbb);
        setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), 2, sy, z, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 1, sy, z - 1, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 2, sy, z - 1, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 3, sy, z - 1, rotation, sbb);
        if (i > 0 && i < 4) {
            setBlockStateRotated(region, *deco->accentState, 3, sy, z, rotation, sbb);
            setBlockStateRotated(region, *deco->fenceState, 3, sy + 1, z, rotation, sbb);
            setBlockStateRotated(region, *deco->fenceState, 3, sy + 2, z, rotation, sbb);
        } else if (i == 0) {
            setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, false), 3, sy, z, rotation, sbb);
        }
    }
}

void Main::decorateReappearingMaze(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y) {
    int mazeSize = 6;
    // world.getSeed() + minX * 90342903L + y * 90342903L ^ minZ (Java precedence)
    auto const seed = (static_cast<std::uint64_t>(region.seed()) + static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minX)) * 90342903ULL +
                       static_cast<std::uint64_t>(static_cast<std::int64_t>(y)) * 90342903ULL) ^
                      static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minZ));
    decoRNG.setSeed(static_cast<std::int64_t>(seed));
    Maze maze(mazeSize, mazeSize, decoRNG);

    // tell it not to make outside walls by making them "ROOMS"
    for (int i = 0; i < 13; i++) {
        maze.putRaw(i, 0, Maze::ROOM);
        maze.putRaw(i, 12, Maze::ROOM);
        maze.putRaw(0, i, Maze::ROOM);
        maze.putRaw(12, i, Maze::ROOM);
    }
    // we need to set this before generation
    maze.doorRarity = 0.3F;

    // set some areas out of bounds and make the maze depending on where we start
    auto room = [&](int x0, int x1, int z0, int z1) {
        for (int x = x0; x < x1; x++)
            for (int z = z0; z < z1; z++) maze.putRaw(x, z, Maze::ROOM);
    };
    switch (rotation) {
    case Rotation::None:
        room(1, 6, 1, 6);
        for (int z = 6; z <= 9; z++) maze.putRaw(1, z, Maze::ROOM);
        maze.putRaw(1, 10, Maze::DOOR);
        maze.putRaw(6, 1, Maze::ROOM);
        maze.putRaw(7, 1, Maze::ROOM);
        maze.putRaw(8, 1, Maze::DOOR);
        maze.generateRecursiveBacktracker(0, 5, decoRNG);
        break;
    case Rotation::Clockwise90:
        room(7, 12, 1, 6);
        for (int x = 6; x >= 3; x--) maze.putRaw(x, 1, Maze::ROOM);
        maze.putRaw(2, 1, Maze::DOOR);
        maze.putRaw(11, 6, Maze::ROOM);
        maze.putRaw(11, 7, Maze::ROOM);
        maze.putRaw(11, 8, Maze::DOOR);
        maze.generateRecursiveBacktracker(0, 0, decoRNG);
        break;
    case Rotation::Clockwise180:
        room(7, 12, 7, 12);
        for (int z = 6; z >= 3; z--) maze.putRaw(11, z, Maze::ROOM);
        maze.putRaw(11, 2, Maze::DOOR);
        maze.putRaw(6, 11, Maze::ROOM);
        maze.putRaw(5, 11, Maze::ROOM);
        maze.putRaw(4, 11, Maze::DOOR);
        maze.generateRecursiveBacktracker(5, 0, decoRNG);
        break;
    case Rotation::Counterclockwise90:
        room(1, 6, 7, 12);
        for (int x = 6; x <= 9; x++) maze.putRaw(x, 11, Maze::ROOM);
        maze.putRaw(10, 11, Maze::DOOR);
        maze.putRaw(1, 6, Maze::ROOM);
        maze.putRaw(1, 5, Maze::ROOM);
        maze.putRaw(1, 4, Maze::DOOR);
        maze.generateRecursiveBacktracker(5, 5, decoRNG);
        break;
    }

    // copy the maze to us!
    maze.wallBlockState   = deco->blockState;
    maze.headBlockState   = deco->accentState;
    maze.pillarBlockState = deco->accentState;
    maze.doorBlockState   = &J("twilightforest:reappearing_block");
    maze.torchRarity      = 0;
    maze.tall             = 3;
    maze.head             = 1;
    maze.oddBias          = 2;
    maze.copyToStructure(region, 0, y + 1, 0, *this, sbb, decoRNG);

    decorateMazeDeadEnds(region, maze, y, sbb);
}

void Main::decorateMazeDeadEnds(Region& region, Maze const& maze, int y, BlockBox const& sbb) {
    for (int x = 0; x < maze.width; x++)
        for (int z = 0; z < maze.depth; z++) {
            bool w = maze.isWall(x, z, x - 1, z), e = maze.isWall(x, z, x + 1, z), n = maze.isWall(x, z, x, z - 1), s = maze.isWall(x, z, x, z + 1);
            // dead ends
            if (!w && e && n && s) decorateMazeDeadEnd(region, x, y, z, 3, sbb);
            if (w && !e && n && s) decorateMazeDeadEnd(region, x, y, z, 1, sbb);
            if (w && e && !n && s) decorateMazeDeadEnd(region, x, y, z, 0, sbb);
            if (w && e && n && !s) decorateMazeDeadEnd(region, x, y, z, 2, sbb);
        }
}

void Main::decorateMazeDeadEnd(Region& region, int mx, int y, int mz, int facing, BlockBox const& sbb) {
    int          x = mx * 3 + 1;
    int          z = mz * 3 + 1;
    Block const& a = *deco->accentState;
    switch (facing) {
    case 0:
        placeBlock(region, a, x, y + 1, z + 1, sbb);
        placeBlock(region, a, x + 1, y + 1, z + 1, sbb);
        setDoubleLootChest(region, x, y + 2, z + 1, x + 1, y + 2, z + 1, Orientation::South, kLootCache, sbb, false);
        break;
    case 1:
        placeBlock(region, a, x, y + 1, z, sbb);
        placeBlock(region, a, x, y + 1, z + 1, sbb);
        setDoubleLootChest(region, x, y + 2, z, x, y + 2, z + 1, Orientation::West, kLootCache, sbb, false);
        break;
    case 2:
        placeBlock(region, a, x, y + 1, z, sbb);
        placeBlock(region, a, x + 1, y + 1, z, sbb);
        setDoubleLootChest(region, x + 1, y + 2, z, x, y + 2, z, Orientation::North, kLootCache, sbb, false);
        break;
    case 3:
        placeBlock(region, a, x + 1, y + 1, z, sbb);
        placeBlock(region, a, x + 1, y + 1, z + 1, sbb);
        setDoubleLootChest(region, x + 1, y + 2, z + 1, x + 1, y + 2, z, Orientation::East, kLootCache, sbb, false);
        break;
    default: break;
    }
}

void Main::decorateUnbuilderMaze(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y) {
    // fill in posts
    for (int x = size / 2; x < size - 1; x++)
        for (int z = 3; z < size - 1; z++) {
            if (x % 2 == 1 && z % 2 == 1) {
                for (int py = 1; py < 5; py++) setBlockStateRotated(region, *deco->pillarState, x, y + py, z, rotation, sbb);
            } else if (x % 2 == 1 || z % 2 == 1) {
                for (int py = 1; py < 5; py++) setBlockStateRotated(region, *deco->fenceState, x, y + py, z, rotation, sbb);
                if (x != (size / 2) + 2 && x != size - 1 && z != size - 1) {
                    int ay = decoRNG.nextInt(4) + 1;
                    setBlockStateRotated(region, air(), x, y + ay, z, rotation, sbb);
                    if (x > size - 7) {
                        ay = decoRNG.nextInt(3) + 1;
                        setBlockStateRotated(region, air(), x, y + ay, z, rotation, sbb);
                    }
                }
            }
        }
    // place unbuilders
    Block const& antiBuilder = J("twilightforest:antibuilder");
    setBlockStateRotated(region, antiBuilder, 15, y + 2, 7, rotation, sbb);
    setBlockStateRotated(region, antiBuilder, 11, y + 3, 7, rotation, sbb);
    setBlockStateRotated(region, antiBuilder, 15, y + 2, 13, rotation, sbb);
    setBlockStateRotated(region, antiBuilder, 11, y + 3, 13, rotation, sbb);
    setBlockStateRotated(region, antiBuilder, 5, y + 3, 13, rotation, sbb);
}

void Main::decorateMainLounge(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y) {
    // brewing area in corner - walls
    fillBlocksRotated(region, sbb, 17, y + 1, 1, 17, y + 4, 6, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 1, 17, y + 4, 1, *deco->pillarState, rotation);
    // floors
    fillBlocksRotated(region, sbb, 13, y + 1, 2, 16, y + 1, 5, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 2, 12, y + 1, 6, getStairState(*deco->stairState, Orientation::West, false), rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 6, 16, y + 1, 6, getStairState(*deco->stairState, Orientation::South, false), rotation);
    // furnaces
    makeDispenserPillar(region, 13, y, 1, Orientation::South, rotation, sbb);
    makeDispenserPillar(region, 15, y, 1, Orientation::South, rotation, sbb);
    makeDispenserPillar(region, 17, y, 3, Orientation::West, rotation, sbb);
    makeDispenserPillar(region, 17, y, 5, Orientation::West, rotation, sbb);
    // framing pillars
    makeStonePillar(region, 12, y, 1, rotation, sbb);
    makeStonePillar(region, 17, y, 6, rotation, sbb);
    // cauldron and brewing stand
    setBlockStateRotated(region, J("minecraft:brewing_stand"), 13, y + 2, 5, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:water_cauldron", Json{{"level", "3"}}), 15, y + 2, 3, rotation, sbb);
    // bookshelves in corner
    fillBlocksRotated(region, sbb, 10, y + 1, 17, 17, y + 4, 17, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, 17, y + 1, 10, 17, y + 4, 17, *deco->blockState, rotation);
    Block const& bookshelf = J("minecraft:bookshelf");
    fillBlocksRotated(region, sbb, 11, y + 1, 17, 12, y + 4, 17, bookshelf, rotation);
    fillBlocksRotated(region, sbb, 14, y + 1, 17, 15, y + 4, 17, bookshelf, rotation);
    fillBlocksRotated(region, sbb, 17, y + 1, 11, 17, y + 4, 12, bookshelf, rotation);
    fillBlocksRotated(region, sbb, 17, y + 1, 14, 17, y + 4, 15, bookshelf, rotation);
    // table
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, true), 13, y + 1, 14, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, true), 14, y + 1, 14, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, true), 14, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, true), 13, y + 1, 13, rotation, sbb);
    // chairs
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, false), 11, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), 13, y + 1, 11, rotation, sbb);
    // center lamp
    setBlockStateRotated(region, J("minecraft:redstone_lamp"), 8, y + 3, 8, rotation, sbb);
    setBlockStateRotated(region, lever("ceiling", decoRNG.nextBoolean() ? Orientation::East : Orientation::North, false), 8, y + 2, 8, rotation, sbb);
    // planter for trees
    placeTreePlanter(region, decoRNG.nextInt(5), 6, y + 1, 12, rotation, sbb);
}

void Main::makeDispenserPillar(Region& region, int x, int y, int z, Orientation stairMeta, Rotation rotation, BlockBox const& sbb) {
    setBlockStateRotated(region, getStairState(*deco->stairState, stairMeta, true), x, y + 2, z, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:dispenser", Json{{"facing", dirName(::gf::structure::opposite(stairMeta))}}), x, y + 3, z, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, stairMeta, false), x, y + 4, z, rotation, sbb);
}

void Main::decorateBossSpawner(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    setBlockStateRotated(region, J("twilightforest:ur_ghast_boss_spawner"), 9, y + 4, 9, rotation, sbb);
}

void Main::decorateExperiment(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    Block const& obsidian        = J("minecraft:obsidian");
    Block const& netherrack      = J("minecraft:netherrack");
    Block const& redstone        = J("minecraft:redstone_block");
    Block const& inactiveReactor = J("twilightforest:carminite_reactor");

    // crafting area in corner - walls
    fillBlocksRotated(region, sbb, 17, y + 1, 1, 17, y + 4, 6, *deco->pillarState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 1, 17, y + 4, 1, *deco->pillarState, rotation);
    // floors
    fillBlocksRotated(region, sbb, 13, y + 1, 2, 16, y + 1, 5, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 2, 12, y + 1, 6, getStairState(*deco->stairState, Orientation::West, false), rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 6, 16, y + 1, 6, getStairState(*deco->stairState, Orientation::South, false), rotation);

    makeWoodPillar(region, 13, y, 1, rotation, sbb);
    makeWoodPillar(region, 15, y, 1, rotation, sbb);
    makeWoodPillar(region, 17, y, 3, rotation, sbb);
    makeWoodPillar(region, 17, y, 5, rotation, sbb);
    // framing pillars
    makeStonePillar(region, 12, y, 1, rotation, sbb);
    makeStonePillar(region, 17, y, 6, rotation, sbb);
    // workbench
    setBlockStateRotated(region, J("minecraft:crafting_table"), 14, y + 2, 4, rotation, sbb);

    // recipes in frames (TF's order: row by row from the bottom)
    static constexpr char const* kSouth[3][3] = {{"twilightforest:borer_essence", "minecraft:redstone", "twilightforest:borer_essence"},
                                                 {"minecraft:redstone", "minecraft:ghast_tear", "minecraft:redstone"},
                                                 {"twilightforest:borer_essence", "minecraft:redstone", "twilightforest:borer_essence"}};
    static constexpr char const* kWest[3][3]  = {{"twilightforest:encased_towerwood", "twilightforest:towerwood", "twilightforest:encased_towerwood"},
                                                 {"twilightforest:towerwood", "twilightforest:carminite", "twilightforest:towerwood"},
                                                 {"twilightforest:encased_towerwood", "twilightforest:towerwood", "twilightforest:encased_towerwood"}};
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) placeItemFrameRotated(region, 13 + col, y + 2 + row, 1, rotation, Orientation::South, kSouth[row][col], sbb);
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) placeItemFrameRotated(region, 17, y + 2 + row, 3 + col, rotation, Orientation::West, kWest[row][col], sbb);

    if (y < height - 13) {
        // device bottom
        setBlockStateRotated(region, obsidian, 13, y + 1, 13, rotation, sbb);
        setBlockStateRotated(region, obsidian, 15, y + 1, 13, rotation, sbb);
        setBlockStateRotated(region, obsidian, 13, y + 1, 15, rotation, sbb);
        setBlockStateRotated(region, obsidian, 15, y + 1, 15, rotation, sbb);
        setBlockStateRotated(region, netherrack, 13, y + 1, 14, rotation, sbb);
        setBlockStateRotated(region, netherrack, 14, y + 1, 13, rotation, sbb);
        setBlockStateRotated(region, netherrack, 15, y + 1, 14, rotation, sbb);
        setBlockStateRotated(region, netherrack, 14, y + 1, 15, rotation, sbb);
        setBlockStateRotated(region, redstone, 14, y + 1, 14, rotation, sbb);
        // middle
        setBlockStateRotated(region, netherrack, 13, y + 2, 13, rotation, sbb);
        setBlockStateRotated(region, netherrack, 15, y + 2, 13, rotation, sbb);
        setBlockStateRotated(region, netherrack, 13, y + 2, 15, rotation, sbb);
        setBlockStateRotated(region, netherrack, 15, y + 2, 15, rotation, sbb);
        setBlockStateRotated(region, inactiveReactor, 14, y + 2, 14, rotation, sbb);
        // device top
        setBlockStateRotated(region, obsidian, 13, y + 3, 13, rotation, sbb);
        setBlockStateRotated(region, obsidian, 15, y + 3, 13, rotation, sbb);
        setBlockStateRotated(region, obsidian, 13, y + 3, 15, rotation, sbb);
        setBlockStateRotated(region, obsidian, 15, y + 3, 15, rotation, sbb);
        setBlockStateRotated(region, netherrack, 13, y + 3, 14, rotation, sbb);
        setBlockStateRotated(region, netherrack, 14, y + 3, 13, rotation, sbb);
        setBlockStateRotated(region, netherrack, 15, y + 3, 14, rotation, sbb);
        setBlockStateRotated(region, netherrack, 14, y + 3, 15, rotation, sbb);
        setBlockStateRotated(region, redstone, 14, y + 3, 14, rotation, sbb);
    }

    // short piston plunger 1
    setBlockStateRotated(region, *deco->accentState, 14, y + 1, 17, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::East, false), 13, y + 1, 17, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:piston", Json{{"facing", "south"}}), 14, y + 2, 17, rotation, sbb);
    setBlockStateRotated(region, redstone, 14, y + 2, 16, rotation, sbb);
    // short piston plunger 2
    setBlockStateRotated(region, *deco->accentState, 17, y + 1, 14, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::South, false), 17, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:piston", Json{{"facing", "east"}}), 17, y + 2, 14, rotation, sbb);
    setBlockStateRotated(region, redstone, 16, y + 2, 14, rotation, sbb);
    // long piston plunger 1
    setBlockStateRotated(region, redstone, 14, y + 2, 11, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, 14, y + 1, 11, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::East, true), 13, y + 1, 11, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:piston", Json{{"facing", "north"}}), 14, y + 2, 10, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, 14, y + 1, 9, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::East, false), 13, y + 1, 9, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:sticky_piston", Json{{"facing", "north"}}), 14, y + 2, 9, rotation, sbb);
    // long piston plunger 2
    setBlockStateRotated(region, redstone, 11, y + 2, 14, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, 11, y + 1, 14, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::South, true), 11, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:piston", Json{{"facing", "west"}}), 10, y + 2, 14, rotation, sbb);
    setBlockStateRotated(region, *deco->accentState, 9, y + 1, 14, rotation, sbb);
    setBlockStateRotated(region, lever("wall", Orientation::South, false), 9, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:sticky_piston", Json{{"facing", "west"}}), 9, y + 2, 14, rotation, sbb);
}

void Main::makeWoodPillar(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    Block const& log = J("twilightforest:dark_log");
    for (int dy = 2; dy <= 4; dy++) setBlockStateRotated(region, log, x, y + dy, z, rotation, sbb);
}

void Main::placeItemFrameRotated(Region& region, int x, int y, int z, Rotation rotation, Orientation direction, char const* itemId, BlockBox const& sbb) {
    int dx = getXWithOffsetRotated(x, z, rotation);
    int dy = worldY(y);
    int dz = getZWithOffsetRotated(x, z, rotation);
    // this.rotation.getRotated(rotation).rotate(direction).getOpposite()
    Orientation facing = ::gf::structure::opposite(::gf::structure::rotateDir(direction, rot(this->rotation, rotation)));
    BlockPos    pos{dx + ::gf::structure::stepX(facing), dy, dz + ::gf::structure::stepZ(facing)};
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    // Bedrock's item frame is a block (facing_direction 2 north, 3 south, 4 west, 5 east) whose item
    // lives in its block actor: the frame goes in now, the item once the chunk is loaded
    int fd = facing == Orientation::North ? 2 : facing == Orientation::South ? 3 : facing == Orientation::West ? 4 : 5;
    if (auto const* frame = game::blockWith("minecraft:frame", {{"facing_direction", fd}})) region.setBlock(pos, *frame, 2);
    if (region.dimension() >= 0) runtime::queueGenerated(region.dimension(), pos, Json{{"k", "frame"}, {"item", itemId}});
}

void Main::decorateAquarium(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    // main aquarium
    makePillarFrame(region, sbb, *deco, rotation, 12, y, 3, 4, 4, 13, false);
    fillBlocksRotated(region, sbb, 13, y + 4, 4, 14, y + 4, 14, J("minecraft:water"), rotation);
    // little one
    makePillarFrame(region, sbb, *deco, rotation, 6, y, 12, 4, 4, 4, false);
    fillBlocksRotated(region, sbb, 6, y + 5, 12, 9, y + 5, 15, *deco->accentState, rotation);
    fillBlocksRotated(region, sbb, 7, y + 4, 13, 8, y + 5, 14, J("minecraft:water"), rotation);
}

void Main::decorateForge(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y) {
    Decorator const& forgeDeco = *deco;
    // stone walls in corner
    fillBlocksRotated(region, sbb, 17, y + 1, 1, 17, y + 4, 6, *forgeDeco.pillarState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 1, 17, y + 4, 1, *forgeDeco.pillarState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 17, 17, y + 4, 17, *forgeDeco.pillarState, rotation);
    fillBlocksRotated(region, sbb, 17, y + 1, 12, 17, y + 4, 17, *forgeDeco.pillarState, rotation);
    // floors
    fillBlocksRotated(region, sbb, 13, y + 1, 2, 16, y + 1, 5, *forgeDeco.blockState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 2, 12, y + 1, 6, getStairState(*deco->stairState, Orientation::West, false), rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 6, 16, y + 1, 6, getStairState(*deco->stairState, Orientation::South, false), rotation);
    fillBlocksRotated(region, sbb, 13, y + 1, 13, 16, y + 1, 16, *forgeDeco.blockState, rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 12, 12, y + 1, 16, getStairState(*deco->stairState, Orientation::West, false), rotation);
    fillBlocksRotated(region, sbb, 12, y + 1, 12, 16, y + 1, 12, getStairState(*deco->stairState, Orientation::North, false), rotation);
    // furnaces
    makeFurnacePillar(region, decoRNG, 13, y, 1, Orientation::South, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 15, y, 1, Orientation::South, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 17, y, 3, Orientation::West, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 17, y, 5, Orientation::West, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 13, y, 17, Orientation::North, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 15, y, 17, Orientation::North, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 17, y, 13, Orientation::West, rotation, sbb);
    makeFurnacePillar(region, decoRNG, 17, y, 15, Orientation::West, rotation, sbb);
    // framing pillars
    makeStonePillar(region, 17, y, 6, rotation, sbb);
    makeStonePillar(region, 12, y, 17, rotation, sbb);
    makeStonePillar(region, 17, y, 12, rotation, sbb);
    // extra pillars
    makeStonePillar(region, 17, y, 9, rotation, sbb);
    makeStonePillar(region, 9, y, 17, rotation, sbb);
    // anvils (Direction.Plane.HORIZONTAL: north, east, south, west)
    static constexpr char const*                anvils[] = {"minecraft:anvil", "minecraft:chipped_anvil", "minecraft:damaged_anvil"};
    static constexpr std::array<Orientation, 4> horizontal{Orientation::North, Orientation::East, Orientation::South, Orientation::West};
    for (int z : {5, 13}) {
        char const* anvil  = anvils[decoRNG.nextInt(3)];
        Orientation facing = horizontal[static_cast<std::size_t>(decoRNG.nextInt(4))];
        setBlockStateRotated(region, J(anvil, Json{{"facing", dirName(facing)}}), 13, y + 2, z, rotation, sbb);
    }
    // fire pit
    makeFirePit(region, 6, y + 1, 12, rotation, sbb);
}

void Main::makeFurnacePillar(Region& region, RandomSource& rand, int x, int y, int z, Orientation direction, Rotation rotation, BlockBox const& sbb) {
    setBlockStateRotated(region, getStairState(*deco->stairState, direction, true), x, y + 2, z, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:furnace", Json{{"facing", dirName(::gf::structure::opposite(direction))}}), x, y + 3, z, rotation, sbb);
    // randomly put some charcoal in the furnace burn slot
    int amount = rand.nextBoolean() ? rand.nextInt(5) + 4 : 0;
    if (amount > 0) {
        BlockPos pos{getXWithOffsetRotated(x, z, rotation), worldY(y + 3), getZWithOffsetRotated(x, z, rotation)};
        if (sbb.contains(pos.x, pos.y, pos.z) && ::gf::structure::blocks::is(region.getBlock(pos), "minecraft:furnace") && region.dimension() >= 0)
            runtime::queueGenerated(region.dimension(), pos, Json{{"k", "fuel"}, {"item", "minecraft:charcoal"}, {"count", amount}});
    }
    setBlockStateRotated(region, getStairState(*deco->stairState, direction, false), x, y + 4, z, rotation, sbb);
}

void Main::makeStonePillar(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    for (int py = 1; py <= 4; py++) setBlockStateRotated(region, *deco->pillarState, x, y + py, z, rotation, sbb);
}

void Main::makeFirePit(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, false), x - 1, y, z, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, false), x + 1, y, z, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), x, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, false), x, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x + 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x + 1, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x - 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x - 1, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:netherrack"), x, y, z, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:fire"), x, y + 1, z, rotation, sbb);
}

void Main::decorateNetherwart(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y, bool isTop) {
    // wart container
    makePillarFrame(region, sbb, *deco, rotation, 12, y, 9, 4, 4, 7, true);
    fillBlocksRotated(region, sbb, 13, y + 1, 10, 14, y + 1, 14, J("minecraft:soul_sand"), rotation);
    // getNetherPlant: Util.getRandom(list, random)
    static constexpr char const* plants[] = {"minecraft:nether_wart", "twilightforest:blightberry_bush", "twilightforest:duskberry_bush",
                                             "twilightforest:skyberry_bush", "twilightforest:stingberry_bush"};
    fillBlocksRotated(region, sbb, 13, y + 2, 10, 14, y + 2, 14, J(plants[decoRNG.nextInt(5)]), rotation);
    fillBlocksRotated(region, sbb, 13, y + 4, 10, 14, y + 4, 14, J("minecraft:soul_sand"), rotation);
    // blaze container
    makePillarFrame(region, sbb, *deco, rotation, 5, y, 12, 3, isTop ? 4 : 9, 3, true);
    setBlockStateRotated(region, *deco->blockState, 6, y + 1, 13, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, 6, y + (isTop ? 4 : 9), 13, rotation, sbb);
    setSpawnerRotated(region, 6, y + 3, 13, rotation, "minecraft:blaze", sbb);
    // destruction blob
    destroyTower(region, decoRNG, 12, y, 3, 2, sbb);
}

void Main::decorateBotanical(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y) {
    // main part
    makePillarFrame(region, sbb, *deco, rotation, 12, y, 12, 4, 4, 4, true);
    fillBlocksRotated(region, sbb, 13, y + 1, 13, 14, y + 1, 14, *deco->blockState, rotation);
    fillBlocksRotated(region, sbb, 13, y + 4, 13, 14, y + 4, 14, *deco->blockState, rotation);
    placeRandomPlant(region, decoRNG, 13, y + 2, 13, rotation, sbb);
    placeRandomPlant(region, decoRNG, 13, y + 2, 14, rotation, sbb);
    placeRandomPlant(region, decoRNG, 14, y + 2, 13, rotation, sbb);
    placeRandomPlant(region, decoRNG, 14, y + 2, 14, rotation, sbb);
    // toolbench
    for (int py = 1; py <= 4; py++) {
        setBlockStateRotated(region, *deco->pillarState, 12, y + py, 4, rotation, sbb);
        setBlockStateRotated(region, *deco->pillarState, 15, y + py, 4, rotation, sbb);
    }
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, true), 13, y + 1, 4, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, true), 14, y + 1, 4, rotation, sbb);
    placeTreasureRotated(region, 13, y + 2, 4, getOrientation().value_or(Orientation::North), rotation, kLootCache, sbb);
    setBlockStateRotated(region, J("minecraft:crafting_table"), 14, y + 2, 4, rotation, sbb);

    Block const& slab = J("minecraft:spruce_slab", Json{{"type", "top"}});
    for (int bz : {7, 10}) {
        // benches
        setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, true), 12, y + 1, bz, rotation, sbb);
        setBlockStateRotated(region, slab, 13, y + 1, bz, rotation, sbb);
        setBlockStateRotated(region, slab, 14, y + 1, bz, rotation, sbb);
        setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, true), 15, y + 1, bz, rotation, sbb);
    }
    // plants on benches
    for (int x = 12; x <= 15; x++) {
        placeRandomPlant(region, decoRNG, x, y + 2, 7, rotation, sbb);
        placeRandomPlant(region, decoRNG, x, y + 2, 10, rotation, sbb);
    }
    // planter for trees
    placeTreePlanter(region, decoRNG.nextInt(5), 6, y + 1, 12, rotation, sbb);
}

void Main::placeTreePlanter(Region& region, int treeNum, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    setBlockStateRotated(region, *deco->pillarState, x + 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x + 1, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x - 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, *deco->pillarState, x - 1, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::West, false), x - 1, y, z, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::East, false), x + 1, y, z, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, false), x, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, false), x, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, J("minecraft:dirt"), x, y, z, rotation, sbb);

    BlockPos pos{getXWithOffsetRotated(x, z, rotation), worldY(y + 1), getZWithOffsetRotated(x, z, rotation)};
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    char const* treeGen = "minecraft:oak";
    switch (treeNum) {
    case 1: treeGen = "twilightforest:tree/smaller_jungle_tree"; break; // made a custom one so it doesnt cut through the floor
    case 2: treeGen = "minecraft:birch"; break;
    case 3: treeGen = "twilightforest:tree/twilight_oak_tree"; break;
    case 4: treeGen = "twilightforest:tree/rainbow_oak"; break;
    default: break;
    }
    // grow a tree with world.getRandom() (the region's random)
    for (int i = 0; i < 100; i++)
        if (::gf::features::place(treeGen, region, pos, ::gf::structure::regionRandom(region))) break;
}

void Main::placeRandomPlant(Region& region, RandomSource& decoRNG, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    // BuiltInRegistries.BLOCK.getTag(DARK_TOWER_ALLOWED_POTS).flatMap(tag -> tag.getRandomElement(decoRNG))
    auto const                 pots = ::gf::tags::members(::gf::tags::Registry::Block, "twilightforest:dark_tower_allowed_pots");
    std::optional<std::string> optional;
    if (!pots.empty()) optional = pots[static_cast<std::size_t>(decoRNG.nextInt(static_cast<int>(pots.size())))];
    std::string plant = decoRNG.nextInt(10) != 0 && optional ? *optional : std::string{"minecraft:flower_pot"};
    placeIdRotated(region, plant, x, y, z, rotation, sbb);
}

void Main::makeBottomEntrance(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    makeFirePit(region, 13, y + 1, 3, rotation, sbb);
    makeFirePit(region, 3, y + 1, 13, rotation, sbb);
    makeFirePit(region, 13, y + 1, 13, rotation, sbb);
    makePillarFrame(region, sbb, *deco, rotation, 7, y, 7, 3, 4, 3, false);
}

void Main::addTimberMaze(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top) {
    int      spacing   = 5;
    Rotation floorside = Rotation::None;
    if (bottom == 0) bottom += spacing;
    for (int y = bottom; y < top; y += spacing) {
        floorside = rot(floorside, Rotation::Clockwise90);
        makeTimberBeams(region, rand, sbb, floorside, y, y == bottom && bottom != spacing, y >= (top - spacing), top);
    }
}

void Main::makeTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTop, int top) {
    Block const& beamStateNS = J("twilightforest:twilight_oak_log", Json{{"axis", "z"}});
    Block const& beamStateUD = J("twilightforest:twilight_oak_log", Json{{"axis", "y"}});
    Block const& beamStateEW = J("twilightforest:twilight_oak_log", Json{{"axis", "x"}});

    // three beams going n/s
    for (int z = 1; z < size - 1; z++) {
        setBlockStateRotated(region, beamStateNS, 4, y, z, rotation, sbb);
        setBlockStateRotated(region, beamStateNS, 9, y, z, rotation, sbb);
        setBlockStateRotated(region, beamStateNS, 14, y, z, rotation, sbb);
    }
    // a few random cross beams
    int z = pickBetweenExcluding(3, size - 3, rand, 4, 9, 14);
    for (int x = 5; x < 9; x++) setBlockStateRotated(region, beamStateEW, x, y, z, rotation, sbb);
    z = pickBetweenExcluding(3, size - 3, rand, 4, 9, 14);
    for (int x = 10; x < 14; x++) setBlockStateRotated(region, beamStateEW, x, y, z, rotation, sbb);

    // beams going down
    int x1 = 4;
    int z1 = pickFrom(rand, 4, 9, 14);
    int x2 = 9;
    int z2 = pickFrom(rand, 4, 9, 14);
    int x3 = 14;
    int z3 = pickFrom(rand, 4, 9, 14);
    for (int by = 1; by < 5; by++) {
        if (!isBottom || checkPost(region, x1, y - 5, z1, rotation, sbb)) {
            setBlockStateRotated(region, beamStateUD, x1, y - by, z1, rotation, sbb);
            setBlockStateRotated(region, ladder(Orientation::West), x1 + 1, y - by, z1, rotation, sbb);
        }
        if (!isBottom || checkPost(region, x2, y - 5, z2, rotation, sbb)) setBlockStateRotated(region, beamStateUD, x2, y - by, z2, rotation, sbb);
        if (!isBottom || checkPost(region, x3, y - 5, z3, rotation, sbb)) {
            setBlockStateRotated(region, beamStateUD, x3, y - by, z3, rotation, sbb);
            setBlockStateRotated(region, ladder(Orientation::East), x3 - 1, y - by, z3, rotation, sbb);
        }
    }

    // do we need a beam going up?
    if (isTop) {
        Rotation topFloorRotation = kRotations[static_cast<std::size_t>(javaMod(bbox.minY + top + 1, 4))];
        int      ladderX          = 4;
        int      ladderZ          = 10;
        for (int by = 1; by < 5; by++) {
            setBlockStateRotated(region, beamStateUD, ladderX, y + by, 9, topFloorRotation, sbb);
            setBlockStateRotated(region, ladder(Orientation::North), ladderX, y + by, ladderZ, topFloorRotation, sbb);
        }
        // fence thing
        setBlockStateRotated(region, air(), ladderX, y + 6, 9, topFloorRotation, sbb);
        setBlockStateRotated(region, *deco->fenceState, ladderX + 1, y + 5, ladderZ, topFloorRotation, sbb);
        setBlockStateRotated(region, *deco->fenceState, ladderX - 1, y + 5, ladderZ, topFloorRotation, sbb);
        setBlockStateRotated(region, *deco->fenceState, ladderX + 1, y + 6, ladderZ, topFloorRotation, sbb);
        setBlockStateRotated(region, *deco->fenceState, ladderX - 1, y + 6, ladderZ, topFloorRotation, sbb);
    }

    if (!isBottom && !isTop) {
        // spawners
        int sx = pickFrom(rand, 6, 7, 11);
        int sz = pickFrom(rand, 6, 11, 12);
        makeMiniGhastSpawner(region, y, sx, sz, sbb);
    }

    // lamps
    int lx = pickFrom(rand, 2, 12, 16);
    int lz = 2 + rand.nextInt(15);
    setBlockStateRotated(region, J("minecraft:redstone_lamp"), lx, y + 2, lz, rotation, sbb);
    setBlockStateRotated(region, lever("ceiling", rand.nextBoolean() ? Orientation::East : Orientation::North, false), lx, y + 1, lz, rotation, sbb);
}

void Main::makeMiniGhastSpawner(Region& region, int y, int sx, int sz, BlockBox const& sbb) {
    setSpawner(region, sx, y + 2, sz, sbb, "twilightforest:carminite_ghastling", [](Json& spawner) {
        spawner["SpawnRange"]        = 16;
        spawner["MaxNearbyEntities"] = 2;
        spawner["SpawnCount"]        = 1;
    });
}

void Main::addBuilderPlatforms(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top) {
    int      spacing   = 5;
    Rotation floorside = Rotation::None;
    if (bottom == 0) bottom += spacing;
    // fill platforms, aside from bottom and top platform
    for (int y = bottom; y < (top - spacing); y += spacing) {
        makeBuilderPlatforms(region, rand, sbb, floorside, y);
        floorside = rot(floorside, Rotation::Clockwise90);
        floorside = rot(floorside, kRotations[static_cast<std::size_t>(rand.nextInt(3))]);
    }
    // add the bottom platforms
    makeBuilderPlatform(region, rand, Rotation::Clockwise90, bottom, 5, true, sbb);
    makeBuilderPlatform(region, rand, Rotation::Counterclockwise90, bottom, 5, true, sbb);
    for (int y = bottom - 4; y < bottom; y++) {
        setBlockStateRotated(region, ladder(Orientation::West), 1, y, 5, Rotation::Clockwise90, sbb);
        setBlockStateRotated(region, ladder(Orientation::West), 1, y, 5, Rotation::Counterclockwise90, sbb);
    }
    // top platform
    addTopBuilderPlatform(region, rand, top, spacing, sbb);
}

void Main::makeBuilderPlatforms(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y) {
    int z = size / 2 + rand.nextInt(5) - rand.nextInt(5);
    // bottom platform
    makeBuilderPlatform(region, rand, rotation, y, z, false, sbb);
    // ladder
    for (int dy = 1; dy <= 4; dy++) setBlockStateRotated(region, ladder(Orientation::West), 1, y + dy, z, rotation, sbb);
    makeBuilderPlatform(region, rand, rotation, y + 5, z, true, sbb);
    if (y % 2 == 1) {
        // reverter blocks
        int sx = pickFrom(rand, 5, 9, 13);
        int sz = (sx == 9) ? (rand.nextBoolean() ? 5 : 13) : 9;
        setBlockStateRotated(region, J("twilightforest:antibuilder"), sx, y + 2, sz, rotation, sbb);
    } else {
        // lamp cluster
        int sx = rand.nextBoolean() ? 5 : 13;
        int sz = rand.nextBoolean() ? 5 : 13;
        makeLampCluster(region, rand, sx, y, sz, rotation, sbb);
    }
}

void Main::addTopBuilderPlatform(Region& region, RandomSource& rand, int top, int spacing, BlockBox const& sbb) {
    Rotation rotation = kRotations[static_cast<std::size_t>(javaMod(bbox.minY + top + 1, 4))];
    // platform
    fillBlocksRotated(region, sbb, 5, top - spacing, 9, 7, top - spacing, 11, *deco->accentState, rotation);
    // ladder ascender
    fillBlocksRotated(region, sbb, 6, top - spacing, 9, 6, top, 9, *deco->accentState, rotation);
    fillBlocksRotated(region, sbb, 6, top - spacing + 1, 10, 6, top - 1, 10, ladder(Orientation::North), rotation);
    setBlockStateRotated(region, air(), 6, top + 1, 9, rotation, sbb);
    setBlockStateRotated(region, *deco->fenceState, 5, top, 10, rotation, sbb);
    setBlockStateRotated(region, *deco->fenceState, 7, top, 10, rotation, sbb);
    setBlockStateRotated(region, *deco->fenceState, 5, top + 1, 10, rotation, sbb);
    setBlockStateRotated(region, *deco->fenceState, 7, top + 1, 10, rotation, sbb);
    // builder & lever
    setBlockStateRotated(region, J("twilightforest:carminite_builder"), 7, top - spacing, 10, rotation, sbb);
    setBlockStateRotated(region, lever("floor", rand.nextBoolean() ? Orientation::East : Orientation::North, false), 7, top - spacing + 1, 11, rotation, sbb);
}

void Main::makeBuilderPlatform(Region& region, RandomSource& rand, Rotation rotation, int y, int z, bool hole, BlockBox const& sbb) {
    Block const& a = *deco->accentState;
    setBlockStateRotated(region, a, 1, y, z - 1, rotation, sbb);
    if (!hole) setBlockStateRotated(region, a, 1, y, z, rotation, sbb);
    setBlockStateRotated(region, a, 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(region, a, 2, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, a, 2, y, z, rotation, sbb);
    setBlockStateRotated(region, a, 2, y, z + 1, rotation, sbb);
    // builder & lever
    setBlockStateRotated(region, J("twilightforest:carminite_builder"), 2, y, hole ? z + 1 : z - 1, rotation, sbb);
    setBlockStateRotated(region, lever("floor", rand.nextBoolean() ? Orientation::East : Orientation::North, false), 2, y + 1, z, rotation, sbb);
}

void Main::makeLampCluster(Region& region, RandomSource& rand, int sx, int y, int sz, Rotation rotation, BlockBox const& sbb) {
    int radius = 4;
    // Direction.values(): down, up, north, south, west, east
    static constexpr int stepX[] = {0, 0, 0, 0, -1, 1};
    static constexpr int stepY[] = {-1, 1, 0, 0, 0, 0};
    static constexpr int stepZ[] = {0, 0, -1, 1, 0, 0};
    auto pick = [&] {
        int direction = rand.nextInt(8);
        if (direction > 5) direction -= 2;
        return direction;
    };
    auto out = [&](int lx, int ly, int lz) { return lx > sx + radius || lx < sx - radius || ly > y + radius || ly < y - radius || lz > sz + radius || lz < sz - radius; };
    Block const& lamp = J("minecraft:redstone_lamp");

    // make lamps
    for (int i = 0; i < 5; i++) {
        int lx = sx, ly = y, lz = sz;
        for (int move = 0; move < 10; move++) {
            setBlockStateRotated(region, lamp, lx, ly, lz, rotation, sbb);
            int d = pick();
            lx += stepX[d];
            ly += stepY[d];
            lz += stepZ[d];
            if (out(lx, ly, lz)) break;
        }
    }
    // make switches
    for (int i = 0; i < 5; i++) {
        int lx = sx, ly = y, lz = sz;
        // we need to always call rand.nextInt the same amount of times
        int directions[10];
        for (int& d : directions) d = pick();
        for (int d : directions) {
            lx += stepX[d];
            ly += stepY[d];
            lz += stepZ[d];
            if (out(lx, ly, lz)) break;
            // if there is no lamp, place a switch and quit
            if (!::gf::structure::blocks::is(getBlockStateFromPosRotated(region, lx, ly, lz, sbb, rotation), "minecraft:redstone_lamp")) {
                char const* face        = "wall";
                Orientation orientation = Orientation::North;
                switch (d) {
                case 2: orientation = Orientation::South; break; // north
                case 3: orientation = Orientation::North; break; // south
                case 5: orientation = Orientation::West; break;  // east
                case 4: orientation = Orientation::East; break;  // west
                case 1:                                          // up
                    face        = "floor";
                    orientation = Orientation::East;
                    break;
                default: // down
                    face        = "ceiling";
                    orientation = Orientation::North;
                    break;
                }
                setBlockStateRotated(region, lever(face, orientation, false), lx, ly, lz, rotation, sbb);
                break;
            }
        }
    }
}

} // namespace gf::feature::darktower::tower
