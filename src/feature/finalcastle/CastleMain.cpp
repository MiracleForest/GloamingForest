// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleFoundation48Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof48CrenellatedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBossGazeboComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleStairTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleLargeTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleMuralComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the keep and what stands on it. The gazebo, the large tower's interior and the
// bell tower's interior are template pieces (F7 GloamingJigsawPiece) of original .gft templates
// (assets/structures/finalcastle/final_castle/temp, rule R19).
#include "feature/finalcastle/Castle.h"

#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::feature::finalcastle::castle {

namespace st  = ::gf::structure;
namespace ext = ::gf::structure::ext;

namespace {

/// RotationUtil.ROTATIONS.
constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

template <class T>
T* add(PieceList& list, std::unique_ptr<T> piece) {
    T* raw = piece.get();
    list.push_back(std::move(piece));
    return raw;
}

} // namespace

// ==== FinalCastleMainComponent ===================================================================================

Main::Main(int i, int x, int y, int z, std::int64_t overworldSeed) : ComponentOld(type::Main, i, x, y, z), mOverworldSeed(overworldSeed) {
    setOrientation(Orientation::South);
    spawnListIndex = 1; // main monsters
    bbox           = componentBox(x, y, z, -24, 120, -24, 48, 40, 48, Orientation::South);
    // decorator
    deco = castleDecorator();
}

Main::Main() : ComponentOld(type::Main, 0, 0, 0, 0) {}

void Main::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<Foundation48>(4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add roof
    add(list, std::make_unique<Roof48>(4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // boss gazebo on roof
    add(list, std::make_unique<Gazebo>(5, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);

    // build 4 towers on sides
    add(list, std::make_unique<StairTower>(3, bbox.minX, bbox.minY + 3, bbox.minZ, Orientation::North))->addChildren(*this, list, rand);
    add(list, std::make_unique<LargeTower>(3, bbox.maxX, bbox.minY + 3, bbox.minZ, Orientation::East))->addChildren(*this, list, rand);
    add(list, std::make_unique<StairTower>(3, bbox.minX, bbox.minY + 3, bbox.maxZ, Orientation::West))->addChildren(*this, list, rand);
    add(list, std::make_unique<StairTower>(3, bbox.maxX, bbox.minY + 3, bbox.maxZ, Orientation::South))->addChildren(*this, list, rand);

    // tower maze towards entrance
    BlockPos dest{bbox.minX - 4, bbox.maxY, bbox.minZ - 24};
    buildTowerMaze(list, rand, 48, 0, 24, 60, Orientation::South, id::YellowRune, dest);

    // another tower/bridge maze towards the clock tower
    dest = BlockPos{bbox.maxX + 4, bbox.minY, bbox.maxZ + 24};
    buildTowerMaze(list, rand, 0, 30, 24, 60, Orientation::North, id::BlueRune, dest);

    // initial stairs down towards dungeon
    auto* steps0 = add(list, std::make_unique<DungeonSteps>(5, bbox.minX + 18, bbox.minY + 1, bbox.minZ + 18, Orientation::South));
    steps0->addChildren(*this, list, rand);
    // continued steps
    auto* steps1 = steps0->buildMoreStepsTowards(list, rand, Rotation::Counterclockwise90);
    auto* steps2 = steps1->buildMoreStepsTowards(list, rand, Rotation::Counterclockwise90);
    auto* steps3 = steps2->buildMoreStepsTowards(list, rand, Rotation::Counterclockwise90);
    // start dungeon
    steps3->buildLevelUnder(list, rand, 1);

    // mural on front
    BlockPos mc = offsetTowerCCoords(48, 23, 25, 1, Orientation::South);
    add(list, std::make_unique<MuralPiece>(7, mc.x, mc.y, mc.z, 35, 30, Orientation::South, mOverworldSeed))->addChildren(*this, list, rand);
    // mural inside (upstream passes the first mural's z)
    BlockPos mc1 = offsetTowerCCoords(48, 33, 24, -1, Orientation::South);
    add(list, std::make_unique<MuralPiece>(7, mc1.x, mc1.y, mc.z, 19, 12, Orientation::North, mOverworldSeed))->addChildren(*this, list, rand);
}

void Main::buildTowerMaze(PieceList& list, RandomSource& rand, int x, int y, int z, int howFar, Orientation direction, char const* type,
                          BlockPos const& dest) {
    bool complete   = false;
    int  iterations = 0;
    while (!complete && iterations < 15) {
        iterations++;
        // duplicate list (only pieces are appended during an attempt: keep the count)
        std::size_t before = list.size();

        // build
        BlockPos tc     = offsetTowerCCoords(x, y, z, howFar, direction);
        auto     sTower = std::make_unique<MazeTower13>(type::MazeTower, rand, 3, tc.x, tc.y, tc.z, type, direction);

        // add bridge
        BlockPos bc = offsetTowerCCoords(x, y, z, 1, direction);
        add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, direction))->addChildren(*this, list, rand);

        // don't check if the bounding box is clear, there's either nothing there or we've made a terrible mistake
        add(list, std::move(sTower))->buildTowards(*this, list, rand, dest);

        // check if we've successfully built the end tower
        if (isMazeComplete(list, type)) complete = true;
        else list.erase(list.begin() + static_cast<std::ptrdiff_t>(before), list.end());
    }
}

bool Main::isMazeComplete(PieceList const& list, char const* type) {
    std::string colour = type;
    for (auto const& p : list) {
        if (colour == id::YellowRune && p->typeId == type::EntranceTower) return true;
        if (colour == id::BlueRune && p->typeId == type::BellTower) return true;
    }
    return false;
}

BlockPos Main::offsetTowerCCoords(int x, int y, int z, int howFar, Orientation direction) const {
    int dx = worldX(x, z);
    int dy = worldY(y);
    int dz = worldZ(x, z);
    switch (direction) {
    case Orientation::South: dx += howFar; break;
    case Orientation::West: dz += howFar; break;
    case Orientation::North: dx -= howFar; break;
    case Orientation::East: dz -= howFar; break;
    }
    // ugh?
    return BlockPos{dx, dy, dz};
}

void Main::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    auto& randomBlocks = *deco->randomBlocks;
    // walls
    randomFill(region, sbb, 0, 0, 0, 48, 40, 48, false, rand, randomBlocks);

    // 2M
    randomFill(region, sbb, 13, 30, 1, 47, 30, 12, false, rand, randomBlocks);
    fill(region, sbb, 13, 31, 12, 36, 31, 12, *deco->fenceState, *deco->fenceState, false);
    randomFill(region, sbb, 13, 30, 36, 47, 30, 47, false, rand, randomBlocks);
    fill(region, sbb, 13, 31, 36, 36, 31, 36, *deco->fenceState, *deco->fenceState, false);
    randomFill(region, sbb, 1, 30, 1, 12, 30, 47, false, rand, randomBlocks);
    fill(region, sbb, 12, 31, 12, 12, 31, 36, *deco->fenceState, *deco->fenceState, false);

    // second floor stairs to mezzanine
    randomFill(region, sbb, 38, 25, 13, 47, 25, 35, false, rand, randomBlocks);

    for (int i = 0; i < 5; i++) {
        int y = 30 - i;
        makeMezzTopStairs(region, sbb, y, 10 + i, Orientation::South);
        makeMezzTopStairs(region, sbb, y, 38 - i, Orientation::North);

        y                   = 25 - i;
        int          x      = 37 - i;
        Block const& stairs = getStairState(*deco->stairState, Orientation::West, false);
        fill(region, sbb, x, y, 14, x, y, 22, stairs, stairs, false);
        fill(region, sbb, x, y - 1, 14, x, y - 1, 22, *deco->blockState, *deco->blockState, false);
        fill(region, sbb, x, y, 26, x, y, 34, stairs, stairs, false);
        fill(region, sbb, x, y - 1, 26, x, y - 1, 34, *deco->blockState, *deco->blockState, false);
    }

    // pillars
    for (int x = 11; x < 47; x += 12) {
        for (int z = 11; z < 47; z += 12) {
            fill(region, sbb, x, 1, z, x + 2, 40, z + 2, *deco->pillarState, *deco->blockState, false);
            makePillarBase(region, sbb, x, z, 1, false);
            makePillarBase(region, sbb, x, z, 19, true);
            makePillarBase(region, sbb, x, z, 21, false);
            makePillarBase(region, sbb, x, z, 39, true);
        }
    }

    // side pillars
    for (auto rotation : kRotations) {
        for (int z = 11; z < 47; z += 12) {
            // no middle pillars on walls with entrances
            if (z == 23 && (rotation == Rotation::None || rotation == Rotation::Clockwise180)) continue;
            fillBlocksRotated(region, sbb, 1, 1, z, 1, 40, z + 2, *deco->pillarState, rotation);
            makeHalfPillarBase(region, sbb, rotation, 1, z, false);
            makeHalfPillarBase(region, sbb, rotation, 19, z, true);
            makeHalfPillarBase(region, sbb, rotation, 21, z, false);
            makeHalfPillarBase(region, sbb, rotation, 39, z, true);
        }
    }

    // second floor
    randomFill(region, sbb, 1, 20, 1, 47, 20, 47, false, rand, randomBlocks);

    // force field around dungeon stairs
    Block const& field = J(id::PinkField);
    fill(region, sbb, 12, 1, 12, 24, 10, 12, field, field, false);
    fill(region, sbb, 12, 1, 12, 12, 10, 24, field, field, false);
    fill(region, sbb, 24, 1, 12, 24, 10, 24, field, field, false);
    fill(region, sbb, 12, 1, 24, 24, 10, 24, field, field, false);
    fill(region, sbb, 13, 10, 12, 23, 10, 24, field, field, false);
    fill(region, sbb, 12, 10, 12, 12, 10, 24, field, field, false);
    fill(region, sbb, 24, 10, 12, 24, 10, 24, field, field, false);

    // doors in dungeon force field
    fill(region, sbb, 17, 1, 12, 19, 4, 12, J(id::PinkDoor), air(), false);
    fill(region, sbb, 17, 1, 24, 19, 4, 24, J(id::PinkDoor), air(), false);

    // stairs to stair towers
    makeSmallTowerStairs(region, sbb, Rotation::None);
    makeSmallTowerStairs(region, sbb, Rotation::Clockwise90);
    makeSmallTowerStairs(region, sbb, Rotation::Counterclockwise90);
    makeLargeTowerStairs(region, sbb, Rotation::Clockwise180);

    // door, first floor
    fill(region, sbb, 48, 1, 23, 48, 4, 25, J(id::YellowDoor), air(), false);
    // door, second floor
    fill(region, sbb, 0, 31, 23, 0, 34, 25, J(id::BlueDoor), air(), false);
}

void Main::makeSmallTowerStairs(Region& region, BlockBox const& sbb, Rotation rotation) {
    for (int y = 1; y < 4; y++) {
        int z = 40 + y;
        fillBlocksRotated(region, sbb, 1, 1, z, 4, y, z, *deco->blockState, rotation);
        fillBlocksRotated(region, sbb, 2, y, z, 3, y, z, getStairState(*deco->stairState, Orientation::North, false), rotation);
    }
}

void Main::makeLargeTowerStairs(Region& region, BlockBox const& sbb, Rotation rotation) {
    Block const& stairs = getStairState(*deco->stairState, Orientation::North, false);
    for (int y = 1; y < 4; y++) {
        int z = 38 + y;
        fillBlocksRotated(region, sbb, 2, 1, z, 6, y, z, *deco->blockState, rotation);
        fillBlocksRotated(region, sbb, 3, y, z, 5, y, z, stairs, rotation);
    }
}

void Main::makeMezzTopStairs(Region& region, BlockBox const& sbb, int y, int z, Orientation stairMeta) {
    Block const& stairs = getStairState(*deco->stairState, stairMeta, false);
    fill(region, sbb, 38, y, z, 46, y, z, stairs, stairs, false);
    fill(region, sbb, 38, y - 1, z, 46, y - 1, z, *deco->blockState, *deco->blockState, false);
    fillAir(region, sbb, 38, y + 1, z, 46, y + 3, z);
}

void Main::makeHalfPillarBase(Region& region, BlockBox const& sbb, Rotation rotation, int y, int z, bool isFlipped) {
    fillBlocksRotated(region, sbb, 2, y, z - 1, 2, y, z + 3, getStairState(*deco->stairState, Orientation::East, isFlipped), rotation);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::North, isFlipped), 1, y, z - 1, rotation, sbb);
    setBlockStateRotated(region, getStairState(*deco->stairState, Orientation::South, isFlipped), 1, y, z + 3, rotation, sbb);
}

void Main::makePillarBase(Region& region, BlockBox const& sbb, int x, int z, int y, bool isFlipped) {
    fill(region, sbb, x, y, z + 3, x + 3, y, z + 3, getStairState(*deco->stairState, Orientation::South, isFlipped), air(), false);
    fill(region, sbb, x - 1, y, z - 1, x + 2, y, z - 1, getStairState(*deco->stairState, Orientation::North, isFlipped), air(), false);
    fill(region, sbb, x + 3, y, z - 1, x + 3, y, z + 2, getStairState(*deco->stairState, Orientation::East, isFlipped), air(), false);
    fill(region, sbb, x - 1, y, z, x - 1, y, z + 3, getStairState(*deco->stairState, Orientation::West, isFlipped), air(), false);
}

// ==== FinalCastleFoundation48Component ===========================================================================

Foundation48::Foundation48(int i, ComponentOld const& keep, int x, int y, int z) : ComponentOld(type::Foundation48, i, x, y, z) {
    setOrientation(keep.getOrientation());
    bbox = BlockBox::of(keep.bbox.minX, keep.bbox.minY, keep.bbox.minZ, keep.bbox.maxX, keep.bbox.maxY - 1, keep.bbox.maxZ);
}

Foundation48::Foundation48() : ComponentOld(type::Foundation48, 0, 0, 0, 0) {}

void Foundation48::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Foundation48::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // foundation
    for (int x = 4; x < 45; x++)
        for (int z = 4; z < 45; z++) fillColumnDown(region, *deco->blockState, x, -1, z, sbb);
    int mid = 16;
    for (auto rotation : kRotations) {
        // do corner
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 3, -2, 3, rotation, sbb);
        // directly under castle
        fillBlocksRotated(region, sbb, 2, -2, 1, 46, -1, 1, *deco->blockState, rotation);
        fillBlocksRotated(region, sbb, 2, -4, 2, 45, -1, 2, *deco->blockState, rotation);
        fillBlocksRotated(region, sbb, 4, -6, 3, 44, -1, 3, *deco->blockState, rotation);
        // pilings
        for (int i = 9; i < 45; i += 6) makePiling(region, sbb, mid, rotation, i);
        makePiling(region, sbb, mid, rotation, 4);
        makePiling(region, sbb, mid, rotation, 44);
    }
    // add supports for entrance bridge
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 21, -2, 0, Rotation::Clockwise90, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 21, -4, 1, Rotation::Clockwise90, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 21, -6, 2, Rotation::Clockwise90, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 27, -2, 0, Rotation::Clockwise90, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 27, -4, 1, Rotation::Clockwise90, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 27, -6, 2, Rotation::Clockwise90, sbb);
}

void Foundation48::makePiling(Region& region, BlockBox const& sbb, int mid, Rotation rotation, int i) {
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, i, -7, 3, rotation, sbb);
    replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, i, -mid, 2, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, i, -1, 0, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, i, -3, 1, rotation, sbb);
    setBlockStateRotated(region, *deco->blockState, i, -5, 2, rotation, sbb);
}

// ==== FinalCastleRoof48CrenellatedComponent ======================================================================

Roof48::Roof48(int i, ComponentOld const& keep, int x, int y, int z) : ComponentOld(type::Roof48, i, x, y, z) {
    int height = 5;
    setOrientation(keep.getOrientation());
    bbox = BlockBox::of(keep.bbox.minX - 2, keep.bbox.maxY - 1, keep.bbox.minZ - 2, keep.bbox.maxX + 2, keep.bbox.maxY + height - 1, keep.bbox.maxZ + 2);
}

Roof48::Roof48() : ComponentOld(type::Roof48, 0, 0, 0, 0) {}

void Roof48::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Roof48::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // add second layer of floor
    Block const& castleMagic = J(id::VioletRune);
    fill(region, sbb, 2, 2, 2, 50, 2, 50, castleMagic, castleMagic, false);
    // crenellations
    for (auto rotation : kRotations) {
        fillBlocksRotated(region, sbb, 3, 1, 1, 45, 3, 1, *deco->blockState, rotation);
        for (int i = 10; i < 41; i += 5) {
            fillBlocksRotated(region, sbb, i, 1, 0, i + 2, 5, 2, *deco->blockState, rotation);
            setBlockStateRotated(region, *deco->blockState, i + 1, 0, 1, rotation, sbb);
        }
    }
}

// ==== FinalCastleBossGazeboComponent =============================================================================

Gazebo::Gazebo(int i, ComponentOld const& keep, int x, int y, int z) : ComponentOld(type::Gazebo, i, x, y, z) {
    spawnListIndex = -1; // no monsters
    setOrientation(keep.getOrientation());
    bbox = BlockBox::of(keep.bbox.minX + 14, keep.bbox.maxY + 2, keep.bbox.minZ + 14, keep.bbox.maxX - 14, keep.bbox.maxY + 13, keep.bbox.maxZ - 14);
}

Gazebo::Gazebo() : ComponentOld(type::Gazebo, 0, 0, 0, 0) {}

void Gazebo::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    deco              = castleDecorator();
    deco->blockState  = &J(id::VioletRune);
    deco->fenceState  = &J(id::VioletField);
    BlockPos pos      = getWorldPos(10, -1, 10);
    auto     oriented = ext::rotate(ext::FrontAndTop::UpSouth, rotation);
    auto templatePiece = ext::initializeTemplateFromPool(id::GazeboPool, pos, oriented, id::FinalBossJigsaw, rand, genDepth + 1);
    if (templatePiece) list.push_back(std::move(templatePiece));
}

void Gazebo::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {
    // placed by the template (upstream 4.8 comments out the procedural walls, roof and texts)
}

// ==== FinalCastleStairTowerComponent =============================================================================

StairTower::StairTower(int i, int x, int y, int z, Orientation rotation) : TowerWing(type::StairTower, i, x, y, z) {
    setOrientation(rotation);
    size   = 9;
    height = 51;
    bbox   = componentBox(x, y, z, -4, 0, -4, 8, 50, 8, Orientation::South);
}

StairTower::StairTower() : TowerWing(type::StairTower, 0, 0, 0, 0) {}

void StairTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    // add crown
    add(list, std::make_unique<Roof9Cren>(4, *this))->addChildren(*this, list, rand);
}

Block const& StairTower::getGlyphMeta() const { return J(id::YellowRune); }

void StairTower::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));

    randomFill(region, sbb, 0, 0, 0, 8, 49, 8, false, rand, *deco->randomBlocks);

    // add branching runes
    int numBranches = 6 + decoRNG.nextInt(4);
    for (int i = 0; i < numBranches; i++) makeGlyphBranches(region, decoRNG, getGlyphMeta(), sbb);

    // beard
    for (int i = 1; i < 4; i++) randomFill(region, sbb, i, -(i * 2), i, 8 - i, 1 - (i * 2), 8 - i, false, rand, *deco->randomBlocks);
    placeBlock(region, *deco->blockState, 4, -7, 4, sbb);

    // door, first floor
    Block const& castleDoor = J(id::YellowDoor);
    fill(region, sbb, 0, 1, 1, 0, 3, 2, castleDoor, air(), false);

    // stairs
    Block const& stairs   = getStairState(*deco->stairState, Orientation::West, false);
    Rotation     rotation = Rotation::Clockwise90;
    for (int f = 0; f < 5; f++) {
        rotation = st::addRotation(rotation, Rotation::Clockwise90);
        int y    = f * 3 + 1;
        for (int i = 0; i < 3; i++) {
            int sx = 3 + i;
            int sy = y + i;
            int sz = 1;
            setBlockStateRotated(region, stairs, sx, sy, sz, rotation, sbb);
            setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz, rotation, sbb);
            setBlockStateRotated(region, stairs, sx, sy, sz + 1, rotation, sbb);
            setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz + 1, rotation, sbb);
        }
        // landing
        fillBlocksRotated(region, sbb, 6, y + 2, 1, 7, y + 2, 2, *deco->blockState, rotation);
    }

    // door, second floor
    fill(region, sbb, 1, 18, 0, 2, 20, 0, castleDoor, air(), false);

    Block const& southStairs = getStairState(*deco->stairState, Orientation::South, false);
    // second floor landing
    fill(region, sbb, 1, 17, 1, 3, 17, 3, *deco->blockState, *deco->blockState, false);
    fill(region, sbb, 1, 17, 4, 2, 17, 4, southStairs, southStairs, false);
    fill(region, sbb, 1, 16, 4, 2, 16, 4, *deco->blockState, *deco->blockState, false);
    fill(region, sbb, 1, 16, 5, 2, 16, 5, southStairs, southStairs, false);
    fill(region, sbb, 1, 15, 5, 2, 15, 5, *deco->blockState, *deco->blockState, false);

    // door, roof
    fill(region, sbb, 1, 39, 0, 2, 41, 0, J(id::VioletDoor), air(), false);

    // stairs
    rotation = Rotation::Counterclockwise90;
    for (int f = 0; f < 7; f++) {
        rotation = st::addRotation(rotation, Rotation::Clockwise90);
        int y    = f * 3 + 18;
        for (int i = 0; i < 3; i++) {
            int sx = 3 + i;
            int sy = y + i;
            int sz = 1;
            setBlockStateRotated(region, stairs, sx, sy, sz, rotation, sbb);
            setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz, rotation, sbb);
            setBlockStateRotated(region, stairs, sx, sy, sz + 1, rotation, sbb);
            setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz + 1, rotation, sbb);
        }
        // landing
        fillBlocksRotated(region, sbb, 6, y + 2, 1, 7, y + 2, 2, *deco->blockState, rotation);
    }

    // roof access landing
    fill(region, sbb, 1, 38, 1, 3, 38, 5, *deco->blockState, *deco->blockState, false);
    fill(region, sbb, 3, 39, 1, 3, 39, 5, *deco->fenceState, *deco->fenceState, false);
}

// ==== FinalCastleLargeTowerComponent =============================================================================

LargeTower::LargeTower(int i, int x, int y, int z, Orientation rotation) : TowerWing(type::LargeTower, i, x, y, z) {
    setOrientation(rotation);
    size   = 13;
    height = 61;
    bbox   = componentBox(x, y, z, -6, 0, -6, 12, 60, 12, Orientation::South);
}

LargeTower::LargeTower() : TowerWing(type::LargeTower, 0, 0, 0, 0) {}

void LargeTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    // add crown
    add(list, std::make_unique<Roof9Cren>(4, *this))->addChildren(*this, list, rand);

    BlockPos pos      = getWorldPos(0, 1, 2);
    auto     oriented = ext::rotate(ext::FrontAndTop::WestUp, rotation);
    auto templatePiece = ext::initializeTemplateFromPool(id::LargeTowerPool, pos, oriented, id::LargeTowerJigsaw, rand, genDepth + 1);
    if (templatePiece) list.push_back(std::move(templatePiece));
}

Block const& LargeTower::getGlyphMeta() const { return J(id::BlueRune); }

void LargeTower::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));

    randomFill(region, sbb, 0, 0, 0, 12, 59, 12, false, rand, *deco->randomBlocks);

    // add branching runes
    int numBranches = 6 + decoRNG.nextInt(4);
    for (int i = 0; i < numBranches; i++) makeGlyphBranches(region, decoRNG, getGlyphMeta(), sbb);

    // beard
    for (int i = 1; i < 4; i++) randomFill(region, sbb, i, -(i * 2), i, 8 - i, 1 - (i * 2), 8 - i, false, rand, *deco->randomBlocks);
    placeBlock(region, *deco->blockState, 4, -7, 4, sbb);

    // door, first floor
    fill(region, sbb, 0, 1, 1, 0, 4, 3, J(id::YellowDoor), air(), false);
    // sign placed by template
}

// ==== FinalCastleMuralComponent ==================================================================================

MuralPiece::MuralPiece(int i, int x, int y, int z, int width, int height, Orientation direction, std::int64_t overworldSeed)
: ComponentOld(type::Mural, i, x, y, z) {
    setOrientation(direction);
    bbox = getComponentToAddBoundingBox2(x, y, z, 0, -height / 2, -width / 2, 1, height - 1, width - 1, direction);
    int muralHeight = bbox.maxY - bbox.minY + 1;
    int muralWidth  = (direction == Orientation::South || direction == Orientation::North) ? bbox.maxZ - bbox.minZ + 1 : bbox.maxX - bbox.minX + 1;
    mMural          = core::finalcastle::Mural(muralWidth, muralHeight, core::finalcastle::decoSeed(overworldSeed, bbox.minX, bbox.minZ));
}

MuralPiece::MuralPiece() : ComponentOld(type::Mural, 0, 0, 0, 0) {}

Json MuralPiece::save() const {
    Json j           = ComponentOld::save();
    j["muralWidth"]  = mMural.width();
    j["muralHeight"] = mMural.height();
    Json bytes       = Json::array();
    for (auto b : mMural.bytes()) bytes.push_back(static_cast<int>(b));
    j["muralBytes"] = std::move(bytes);
    return j;
}

void MuralPiece::load(Json const& data) {
    ComponentOld::load(data);
    if (data.contains("muralBytes") && data["muralBytes"].is_array()) {
        std::vector<std::int8_t> bytes;
        for (auto const& b : data["muralBytes"]) bytes.push_back(static_cast<std::int8_t>(b.get<int>()));
        mMural = core::finalcastle::Mural(data.value("muralWidth", 0), data.value("muralHeight", 0), std::move(bytes));
    } else {
        auto o          = getOrientation();
        int  muralHeight = bbox.maxY - bbox.minY + 1;
        int  muralWidth  = (o == Orientation::South || o == Orientation::North) ? bbox.maxZ - bbox.minZ + 1 : bbox.maxX - bbox.minX + 1;
        mMural = core::finalcastle::Mural(muralWidth, muralHeight, core::finalcastle::decoSeed(::gf::worldgen::seed(), bbox.minX, bbox.minZ));
    }
}

void MuralPiece::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& castleMagic = J(id::YellowRune);
    // copy mural to world
    for (int x = 0; x < mMural.width(); x++)
        for (int y = 0; y < mMural.height(); y++)
            if (mMural.get(x, y) > 0) placeBlock(region, castleMagic, 0, y, x, sbb);
}

} // namespace gf::feature::finalcastle::castle
