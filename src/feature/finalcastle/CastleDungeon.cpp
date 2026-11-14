// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonStepsComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonRoom31Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonExitComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonForgeRoomComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the dungeon under the keep: four flights of steps, two levels of 31-wide rooms
// with force-field walls (each level's entrance and exit pink), and the forge room at the bottom.
#include <cstdlib>

#include "feature/finalcastle/Castle.h"

#include "feature/ext/finalcastle/W0Internals.h"

namespace gf::feature::finalcastle::castle {

namespace st = ::gf::structure;

namespace {

constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

template <class T>
T* add(PieceList& list, std::unique_ptr<T> piece) {
    T* raw = piece.get();
    list.push_back(std::move(piece));
    return raw;
}

/// Air or a deadrock variant (upstream: "TODO add a deadrock tag, or a tag for castle replaceables").
bool replacing(Block const& b) {
    return st::blocks::isAir(b) || st::blocks::is(b, "gloamingforest:deadrock") || st::blocks::is(b, "gloamingforest:cracked_deadrock") ||
           st::blocks::is(b, "gloamingforest:weathered_deadrock");
}

} // namespace

// ==== FinalCastleDungeonStepsComponent ===========================================================================

DungeonSteps::DungeonSteps(int i, int x, int y, int z, Orientation rotation) : ComponentOld(type::DungeonSteps, i, x, y, z) {
    spawnListIndex = 2; // dungeon monsters
    setOrientation(rotation);
    bbox = getComponentToAddBoundingBox2(x, y, z, -2, -15, -3, 5, 15, 20, rotation);
}

DungeonSteps::DungeonSteps() : ComponentOld(type::DungeonSteps, 0, 0, 0, 0) {}

void DungeonSteps::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

DungeonSteps* DungeonSteps::buildMoreStepsTowards(PieceList& list, RandomSource& rand, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    int         sx        = 2;
    int         sy        = 0;
    int         sz        = 17;
    switch (rotation) {
    case Rotation::None: sz -= 5; break;
    case Rotation::Clockwise90: sx -= 5; break;
    case Rotation::Clockwise180: sz += 5; break;
    case Rotation::Counterclockwise90: sx += 6; break;
    }
    // find center of landing
    int dx = worldX(sx, sz);
    int dy = worldY(sy);
    int dz = worldZ(sx, sz);
    // build a new stairway there
    auto* steps = add(list, std::make_unique<DungeonSteps>(genDepth + 1, dx, dy, dz, direction));
    steps->addChildren(*this, list, rand);
    return steps;
}

DungeonEntrance* DungeonSteps::buildLevelUnder(PieceList& list, RandomSource& rand, int level) {
    // find center of landing
    int dx = worldX(2, 19);
    int dy = worldY(-7);
    int dz = worldZ(2, 19);
    // build a new dungeon level under there
    auto* room = add(list, std::make_unique<DungeonEntrance>(8, dx, dy, dz, getOrientation().value_or(Orientation::North), level));
    room->addChildren(*this, list, rand);
    return room;
}

ForgeRoom* DungeonSteps::buildBossRoomUnder(PieceList& list, RandomSource& rand) {
    // find center of landing
    int dx = worldX(2, 19);
    int dy = worldY(-31);
    int dz = worldZ(2, 19);
    // build a new dungeon level under there
    auto* room = add(list, std::make_unique<ForgeRoom>(8, dx, dy, dz, getOrientation().value_or(Orientation::North)));
    room->addChildren(*this, list, rand);
    return room;
}

void DungeonSteps::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& stairs = st::blocks::withJava(*deco->stairState, Json{{"facing", "south"}});
    for (int z = 0; z < 15; z++) {
        int y = 14 - z;
        fill(region, sbb, 0, y, z, 4, y, z, stairs, stairs, false);
        fillAir(region, sbb, 0, y + 1, z, 4, y + 6, z);
    }
    fillAir(region, sbb, 0, 0, 15, 4, 5, 19);
}

// ==== FinalCastleDungeonRoom31Component ==========================================================================

DungeonRoom31::DungeonRoom31(std::string typeId, int i, int x, int y, int z, Orientation direction, int lvl) : TowerWing(std::move(typeId), i, x, y, z) {
    setOrientation(direction);
    spawnListIndex = 2; // dungeon monsters
    size           = 31;
    height         = 7;
    level          = lvl;
    bbox           = componentBox(x, y, z, -15, 0, -15, size - 1, height - 1, size - 1, Orientation::South);
}

DungeonRoom31::DungeonRoom31(std::string typeId) : TowerWing(std::move(typeId), 0, 0, 0, 0) {}

::gf::structure::bases::TerrainAdjustment DungeonRoom31::getTerrainAdjustment() const { return ::gf::structure::bases::TerrainAdjustment::Bury; }

void DungeonRoom31::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);

    int mySpread  = genDepth - parent.genDepth;
    int maxSpread = (level == 1) ? 2 : 3;

    // add exit if we're far enough away and don't have one
    if (mySpread == maxSpread && !isExitBuildForLevel(parent)) {
        Rotation direction = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
        for (int i = 0; i < 8 && !isExitBuildForLevel(parent); i++) {
            direction = st::addRotation(direction, kRotations[static_cast<std::size_t>(i & 3)]);
            if (addDungeonExit(list, rand, direction)) setExitBuiltForLevel(parent, true);
        }
    }

    // add other rooms
    if (mySpread < maxSpread) {
        Rotation direction = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
        for (int i = 0; i < 12; i++) {
            direction = st::addRotation(direction, kRotations[static_cast<std::size_t>(i & 3)]);
            addDungeonRoom(parent, list, rand, direction, level);
        }
    }
}

bool DungeonRoom31::isExitBuildForLevel(Piece const& parent) {
    if (auto const* e = dynamic_cast<DungeonEntrance const*>(&parent)) return e->hasExit;
    return false;
}

void DungeonRoom31::setExitBuiltForLevel(Piece& parent, bool exit) {
    if (auto* e = dynamic_cast<DungeonEntrance*>(&parent)) e->hasExit = exit;
}

bool DungeonRoom31::addDungeonRoom(Piece& parent, PieceList& list, RandomSource& rand, Rotation rot, int lvl) {
    rot         = st::addRotation(rot, rotation);
    BlockPos rc = getNewRoomCoords(rand, rot);
    auto     dRoom = std::make_unique<DungeonRoom31>(type::DungeonRoom, genDepth + 1, rc.x, rc.y, rc.z, st::rotateDir(Orientation::South, rot), lvl);
    BlockBox largerBB = dRoom->bbox;
    if (findIntersectingExcluding(list, largerBB, this)) return false;
    add(list, std::move(dRoom))->addChildren(parent, list, rand);
    return true;
}

bool DungeonRoom31::addDungeonExit(PieceList& list, RandomSource& rand, Rotation rot) {
    // TODO in upstream: check if we are sufficiently near the castle center
    rot         = st::addRotation(rot, rotation);
    BlockPos rc = getNewRoomCoords(rand, rot);
    auto     dRoom = std::make_unique<DungeonExit>(genDepth + 1, rc.x, rc.y, rc.z, st::rotateDir(Orientation::South, rot), level);
    if (findIntersectingExcluding(list, dRoom->bbox, this)) return false;
    add(list, std::move(dRoom))->addChildren(*this, list, rand);
    return true;
}

BlockPos DungeonRoom31::getNewRoomCoords(RandomSource& rand, Rotation rot) const {
    // make the rooms connect around the corners, not the centers
    int offset = rand.nextInt(15) - 9;
    if (rand.nextBoolean()) offset += size;
    switch (rot) {
    case Rotation::Clockwise90: return BlockPos{bbox.minX + offset, bbox.minY, bbox.maxZ + 9};
    case Rotation::Clockwise180: return BlockPos{bbox.minX - 9, bbox.minY, bbox.minZ + offset};
    case Rotation::Counterclockwise90: return BlockPos{bbox.minX + offset, bbox.minY, bbox.minZ - 9};
    default: return BlockPos{bbox.maxX + 9, bbox.minY, bbox.minZ + offset};
    }
}

void DungeonRoom31::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const& pivot) {
    auto plateauBiomes = [](std::string const& biome) { return biome == "gloamingforest:thornlands" || biome == "gloamingforest:final_plateau"; };
    if (isBoundingBoxOutsideBiomes(region, plateauBiomes, pivot)) return;

    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));

    fillWithAir(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, replacing);

    Block const& floor  = J("gloamingforest:castle_brick");
    Block const& border = J("gloamingforest:thick_castle_brick");
    constexpr int cs    = 7;
    fillWithBlocks(region, sbb, cs, -1, cs, size - 1 - cs, -1, size - 1 - cs, border, floor, replacing);
    fillWithBlocks(region, sbb, cs, height, cs, size - 1 - cs, height, size - 1 - cs, border, floor, replacing);

    Block const& forceField  = getForceFieldColor(decoRNG);
    Block const& castleMagic = getRuneColor(forceField);

    for (auto rot : kRotations) {
        fillBlocksRotated(region, sbb, cs, 0, cs + 1, cs, height - 1, size - 2 - cs, forceField, rot);
        // verticals
        for (int z = cs; z < ((size - 1) - cs); z += 4) {
            fillBlocksRotated(region, sbb, cs, 0, z, cs, height - 1, z, castleMagic, rot);
            // horizontals
            int y = ((z - cs) % 8 == 0) ? decoRNG.nextInt(3) : decoRNG.nextInt(3) + 4;
            fillBlocksRotated(region, sbb, cs, y, z + 1, cs, y, z + 3, castleMagic, rot);
        }
    }
}

Block const& DungeonRoom31::getRuneColor(Block const& forceFieldColor) const {
    return st::blocks::is(forceFieldColor, id::GreenField) ? J(id::YellowRune) : J(id::BlueRune);
}

Block const& DungeonRoom31::getForceFieldColor(RandomSource& decoRNG) const {
    int i = decoRNG.nextInt(2) + 3;
    return i == 3 ? J(id::GreenField) : J(id::BlueField);
}

// ==== FinalCastleDungeonEntranceComponent ========================================================================

DungeonEntrance::DungeonEntrance(int i, int x, int y, int z, Orientation direction, int lvl) : DungeonRoom31(type::DungeonEntrance, i, x, y, z, direction, lvl) {}

DungeonEntrance::DungeonEntrance() : DungeonRoom31(type::DungeonEntrance) {}

void DungeonEntrance::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    deco             = castleDecorator();
    deco->blockState = &J(id::PinkRune);
    deco->fenceState = &J(id::PinkField);
    // this is going to be the parent for all rooms on this level
    DungeonRoom31::addChildren(*this, list, rand);
}

void DungeonEntrance::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    DungeonRoom31::postProcess(region, rand, sbb, pivot);
    Block const& stairs   = st::blocks::withJava(*deco->stairState, Json{{"facing", "south"}});
    Block const& deadRock = J(id::Deadrock);
    // stairs
    for (int y = 0; y <= height; y++) {
        int x = (size / 2) - 2;
        int z = (size / 2) - y + 2;
        fill(region, sbb, x, 0, z, x + 4, y - 1, z, deadRock, deadRock, false);
        fill(region, sbb, x, y, z, x + 4, y, z, stairs, stairs, false);
        fillAir(region, sbb, x, y + 1, z, x + 4, y + 6, z);
    }
    // door
    fill(region, sbb, 23, 0, 12, 23, 3, 14, J(id::PinkDoor), air(), false);
    fill(region, sbb, 23, 4, 12, 23, 4, 14, *deco->blockState, *deco->blockState, false);
}

Block const& DungeonEntrance::getForceFieldColor(RandomSource&) const { return J(id::PinkField); }

Block const& DungeonEntrance::getRuneColor(Block const&) const { return J(id::PinkRune); }

// ==== FinalCastleDungeonExitComponent ============================================================================

DungeonExit::DungeonExit(int i, int x, int y, int z, Orientation direction, int lvl) : DungeonRoom31(type::DungeonExit, i, x, y, z, direction, lvl) {}

DungeonExit::DungeonExit() : DungeonRoom31(type::DungeonExit) {}

void DungeonExit::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    // no need for additional rooms, we're along the outside anyways

    // add stairway down
    Rotation bestDir = findStairDirectionTowards(parent.bbox.minX, parent.bbox.minZ);
    // (upstream passes the box's minZ as the x coordinate)
    auto* steps0 = add(list, std::make_unique<DungeonSteps>(5, bbox.minZ + 15, bbox.minY, bbox.minZ + 15, st::rotateDir(Orientation::South, bestDir)));
    steps0->addChildren(*this, list, rand);

    // another level!?
    if (level == 1) steps0->buildLevelUnder(list, rand, level + 1);
    else steps0->buildBossRoomUnder(list, rand);
}

void DungeonExit::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    DungeonRoom31::postProcess(region, rand, sbb, pivot);
    // door
    fill(region, sbb, 7, 0, 16, 7, 3, 18, J(id::PinkDoor), air(), false);
    fill(region, sbb, 7, 4, 16, 7, 4, 18, *deco->blockState, *deco->blockState, false);
}

Rotation DungeonExit::findStairDirectionTowards(int x, int z) const {
    BlockPos center = st::boxCenter(bbox);
    // difference
    int dx = center.x - x;
    int dz = center.z - z;
    if (std::abs(dz) >= std::abs(dx)) return dz >= 0 ? Rotation::Clockwise180 : Rotation::None;
    return dx >= 0 ? Rotation::Counterclockwise90 : Rotation::Clockwise90;
}

Block const& DungeonExit::getForceFieldColor(RandomSource&) const { return J(id::PinkField); }

Block const& DungeonExit::getRuneColor(Block const&) const { return J(id::PinkRune); }

// ==== FinalCastleDungeonForgeRoomComponent =======================================================================

ForgeRoom::ForgeRoom(int i, int x, int y, int z, Orientation direction) : ComponentOld(type::ForgeRoom, i, x, y, z) {
    spawnListIndex = 3; // forge monsters
    setOrientation(direction);
    bbox = getComponentToAddBoundingBox2(x, y, z, -15, 0, -15, 50, 30, 50, direction);
}

ForgeRoom::ForgeRoom() : ComponentOld(type::ForgeRoom, 0, 0, 0, 0) {}

void ForgeRoom::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    fillAir(region, sbb, 0, 0, 0, 50, 30, 50);
    // sign
    placeSignAtCurrentPosition(region, 25, 0, 25, "Mini-boss 2", "Gives talisman", sbb);
}

} // namespace gf::feature::finalcastle::castle
