// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleMazeTower13Component.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleDamagedTowerComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleWreckedTowerComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/DestroyArea.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleEntranceTowerComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleEntranceSideTowerComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleEntranceBottomTowerComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleEntranceStairsComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleBellTower21Component.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleFoundation13Component.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleFoundation13ComponentThorns.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleBellFoundation21Component.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the two tower mazes from the keep to the entrance tower (yellow runes) and to the
// bell tower (blue runes), their bridges, the damaged and wrecked side towers and the foundations.
#include <cstdlib>

#include "feature/finalcastle/Castle.h"

#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::castle {

namespace st  = ::gf::structure;
namespace ext = ::gf::structure::ext;
using core::finalcastle::DestroyArea;

namespace {

constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

template <class T>
T* add(PieceList& list, std::unique_ptr<T> piece) {
    T* raw = piece.get();
    list.push_back(std::move(piece));
    return raw;
}

bool isWithinRange(int centerX, int centerZ, int posX, int posZ, int range) { return std::abs(centerX - posX) < range && std::abs(centerZ - posZ) < range; }

BlockBox cloneWithAdjustments(BlockBox const& b, int x1, int y1, int z1, int x2, int y2, int z2) {
    return BlockBox::of(b.minX + x1, b.minY + y1, b.minZ + z1, b.maxX + x2, b.maxY + y2, b.maxZ + z2);
}

bool isDeadrock(Block const& b) { return st::blocks::is(b, id::Deadrock); }

} // namespace

// ==== FinalCastleMazeTower13Component ============================================================================

MazeTower13::MazeTower13(std::string typeId, RandomSource& rand, int i, int x, int y, int z, char const* color, Orientation direction)
: TowerWing(std::move(typeId), i, x, y, z), mColor(color) {
    setOrientation(direction);
    size = 13;
    // decide a number of floors, 2-5
    int floors = rand.nextInt(3) + 2;
    height     = floors * 8 + 1;
    // entrance should be on a random floor
    int entranceFloor = rand.nextInt(floors);
    // rationalize entrance floor if the tower is going to be too low, put the entrance floor at bottom.  Too high, put it at top
    if ((y - (entranceFloor * 8)) < LOWEST_DOOR) entranceFloor = 0;
    if ((y + ((floors - entranceFloor) * 8)) > HIGHEST_DOOR) entranceFloor = floors - 1;
    bbox = componentBox(x, y, z, -6, -(entranceFloor * 8), -6, size - 1, height, size - 1, Orientation::South);
    // we should have a door where we started
    addOpening(0, entranceFloor * 8 + 1, size / 2, Rotation::Clockwise180);
}

MazeTower13::MazeTower13(std::string typeId, int i, int x, int y, int z, int floors, int entranceFloor, char const* color, Orientation direction)
: TowerWing(std::move(typeId), i, x, y, z), mColor(color) {
    setOrientation(direction);
    size   = 13;
    height = floors * 8 + 1;
    bbox   = componentBox(x, y, z, -6, -(entranceFloor * 8), -6, size - 1, height, size - 1, Orientation::South);
    addOpening(0, entranceFloor * 8 + 1, size / 2, Rotation::Clockwise180);
}

MazeTower13::MazeTower13(std::string typeId) : TowerWing(std::move(typeId), 0, 0, 0, 0) {}

Json MazeTower13::save() const {
    Json j     = TowerWing::save();
    j["color"] = Json{{"Name", mColor}};
    return j;
}

void MazeTower13::load(Json const& data) {
    TowerWing::load(data);
    mColor = data.contains("color") && data["color"].is_object() ? data["color"].value("Name", std::string{}) : std::string{};
}

void MazeTower13::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<Foundation13>(type::Foundation13, 4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add roof
    std::unique_ptr<ComponentOld> roof;
    if (rand.nextBoolean()) roof = std::make_unique<Roof13Conical>(rand, 4, *this, loc.x, loc.y, loc.z);
    else roof = std::make_unique<Roof13Cren>(4, *this);
    add(list, std::move(roof))->addChildren(*this, list, rand);
}

void MazeTower13::buildTowards(Piece& parent, PieceList& list, RandomSource& rand, BlockPos const& dest) {
    // regular building first, adds roof/foundation
    addChildren(parent, list, rand);

    if (genDepth < 20) {
        // are we there?
        if (isWithinRange(dest.x, dest.z, bbox.minX + 6, bbox.minZ + 6, 30)) {
            int howFar = 20;
            if (!buildEndTowerTowards(list, rand, dest, findBestDirectionTowards(dest), howFar))
                if (!buildEndTowerTowards(list, rand, dest, findSecondDirectionTowards(dest), howFar))
                    buildEndTowerTowards(list, rand, dest, findThirdDirectionTowards(dest), howFar);
        } else {
            int         howFar = 14 + rand.nextInt(24);
            Orientation facing = findBestDirectionTowards(dest);
            // build left or right, not straight if we can help it
            if (facing == ori() || !buildContinueTowerTowards(list, rand, dest, facing, howFar)) {
                facing = findSecondDirectionTowards(dest);
                if (facing == ori() || !buildContinueTowerTowards(list, rand, dest, facing, howFar)) {
                    facing = findThirdDirectionTowards(dest);
                    if (facing == ori() || !buildContinueTowerTowards(list, rand, dest, facing, howFar)) {
                        // fine, just go straight
                        buildContinueTowerTowards(list, rand, dest, ori(), howFar);
                    }
                }
            }
        }
    }

    // finally, now that the critical path is built, let's add some other towers for atmosphere and complication
    buildNonCriticalTowers(list, rand);
}

void MazeTower13::buildNonCriticalTowers(PieceList& list, RandomSource& rand) {
    // pick a random direction (RotationUtil.CARDINALS: N, S, E, W)
    static constexpr Orientation kCardinals[4] = {Orientation::North, Orientation::South, Orientation::East, Orientation::West};
    Orientation                  dir           = kCardinals[rand.nextInt(4)];
    Rotation                     relative      = st::relativeRotation(ori(), dir);
    // if there isn't something in that direction, check if we can add a wrecked tower
    if (!openingTowards[static_cast<std::size_t>(st::rotationIndex(relative))]) {
        if (!buildDamagedTower(list, rand, dir)) {
            dir = kCardinals[rand.nextInt(4)];
            if (!buildDamagedTower(list, rand, dir)) {
                // maybe just a balcony?
            }
        }
    }
}

Orientation MazeTower13::findBestDirectionTowards(BlockPos const& dest) const {
    // center of tower
    int cx = bbox.minX + 6;
    int cz = bbox.minZ + 6;
    // difference
    int dx = cx - dest.x;
    int dz = cz - dest.z;
    if (std::abs(dx) > std::abs(dz)) return dx >= 0 ? Orientation::East : Orientation::West;
    return dz >= 0 ? Orientation::South : Orientation::North;
}

Orientation MazeTower13::findSecondDirectionTowards(BlockPos const& dest) const {
    int cx = bbox.minX + 6;
    int cz = bbox.minZ + 6;
    int dx = cx - dest.x;
    int dz = cz - dest.z;
    if (std::abs(dx) < std::abs(dz)) // reversed from findBestDirectionTowards
        return dx >= 0 ? Orientation::East : Orientation::West;
    return dz >= 0 ? Orientation::South : Orientation::North;
}

Orientation MazeTower13::findThirdDirectionTowards(BlockPos const& dest) const {
    Orientation first  = findBestDirectionTowards(dest);
    Orientation second = findSecondDirectionTowards(dest);
    static constexpr Orientation kCardinals[4] = {Orientation::North, Orientation::South, Orientation::East, Orientation::West};
    for (auto f : kCardinals)
        if (f != first && f != second && f != st::rotateDir(ori(), Rotation::Clockwise180)) return f;
    // should not get here
    return ori();
}

bool MazeTower13::buildContinueTowerTowards(PieceList& list, RandomSource& rand, BlockPos const& dest, Orientation facing, int howFar) {
    BlockPos opening = getValidOpeningCC(rand, facing);

    // adjust opening towards dest.getY() if we are getting close to dest
    int adjustmentRange = 60;
    if (isWithinRange(dest.x, dest.z, bbox.minX + 6, bbox.minZ + 6, adjustmentRange)) opening = BlockPos{opening.x, adjustOpening(opening.y, dest), opening.z};

    // build towards
    BlockPos tc = offsetTowerCCoords(opening.x, opening.y, opening.z, howFar, facing);

    // find start
    Piece const& start   = *list.front();
    int          centerX = ((start.bbox.minX + 128) >> 8) << 8;
    int          centerZ = ((start.bbox.minZ + 128) >> 8) << 8;

    if (!isWithinRange(centerX, centerZ, tc.x, tc.z, 128)) return false; // tower out of range

    auto     sTower   = std::make_unique<MazeTower13>(type::MazeTower, rand, genDepth + 1, tc.x, tc.y, tc.z, mColor.c_str(), facing);
    BlockBox largerBB = BlockBox::of(sTower->bbox.minX - 6, 0, sTower->bbox.minZ - 6, sTower->bbox.maxX + 6, 255, sTower->bbox.maxZ + 6);
    if (st::bases::findCollisionPiece(list, largerBB)) return false; // tower blocked

    add(list, std::move(sTower))->buildTowards(*this, list, rand, dest);
    // add bridge
    BlockPos bc = offsetTowerCCoords(opening.x, opening.y, opening.z, 1, facing);
    add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, facing))->addChildren(*this, list, rand);
    // opening
    addOpening(opening.x, opening.y + 1, opening.z, facing);
    return true;
}

bool MazeTower13::buildDamagedTower(PieceList& list, RandomSource& rand, Orientation facing) {
    BlockPos opening = getValidOpeningCC(rand, facing);
    int      howFar  = 14 + rand.nextInt(24);
    // build towards
    BlockPos tc = offsetTowerCCoords(opening.x, opening.y, opening.z, howFar, facing);
    // what color of tower?
    auto     eTower   = makeNewDamagedTower(rand, facing, tc);
    BlockBox largerBB = cloneWithAdjustments(eTower->bbox, -6, 0, -6, 6, 0, 6);
    if (st::bases::findCollisionPiece(list, largerBB)) return false; // damaged tower blocked

    add(list, std::move(eTower))->addChildren(*this, list, rand);
    // add bridge
    BlockPos bc = offsetTowerCCoords(opening.x, opening.y, opening.z, 1, facing);
    add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, facing))->addChildren(*this, list, rand);
    // opening
    addOpening(opening.x, opening.y + 1, opening.z, facing);
    return true;
}

std::unique_ptr<MazeTower13> MazeTower13::makeNewDamagedTower(RandomSource& rand, Orientation facing, BlockPos const& tc) {
    return std::make_unique<DamagedTower>(type::Damaged, rand, genDepth + 1, tc.x, tc.y, tc.z, facing);
}

int MazeTower13::adjustOpening(int posY, BlockPos const& dest) const {
    int openY        = posY;
    int realOpeningY = worldY(openY);
    if (realOpeningY - dest.y < 12) {
        // if it is too low, move it to the top floor
        openY = height - 9;
    } else if (dest.y - realOpeningY < 12) {
        // if the opening is too high, move it to the bottom floor
        openY = 0;
    }
    return openY;
}

bool MazeTower13::buildEndTowerTowards(PieceList& list, RandomSource& rand, BlockPos const& dest, Orientation facing, int howFar) {
    BlockPos opening = getValidOpeningCC(rand, facing);
    // adjust opening towards dest.getY()
    opening = BlockPos{opening.x, adjustOpening(opening.y, dest), opening.z};
    // build towards
    BlockPos tc = offsetTowerCCoords(opening.x, opening.y, opening.z, howFar, facing);
    // what color of tower?
    std::unique_ptr<MazeTower13> eTower;
    if (mColor == id::YellowRune) eTower = std::make_unique<EntranceTower>(genDepth + 1, tc.x, tc.y, tc.z, facing);
    else eTower = std::make_unique<BellTower>(genDepth + 1, tc.x, tc.y, tc.z, facing);
    BlockBox largerBB = cloneWithAdjustments(eTower->bbox, -6, 0, -6, 6, 0, 6);
    if (st::bases::findCollisionPiece(list, largerBB)) return false; // end tower blocked

    add(list, std::move(eTower))->addChildren(*this, list, rand);
    // add bridge
    BlockPos bc = offsetTowerCCoords(opening.x, opening.y, opening.z, 1, facing);
    add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, facing))->addChildren(*this, list, rand);
    // opening
    addOpening(opening.x, opening.y + 1, opening.z, facing);
    return true;
}

BlockPos MazeTower13::getValidOpeningCC(RandomSource& rand, Orientation facing) {
    Rotation relative = st::relativeRotation(ori(), facing);
    int      floors   = height / 8;
    // for directions 0 or 2, the wall lies along the z axis
    if (relative == Rotation::None || relative == Rotation::Clockwise180) {
        int rx = relative == Rotation::None ? 12 : 0;
        int rz = 6;
        int ry = rand.nextInt(floors) * 8;
        return BlockPos{rx, ry, rz};
    }
    // for directions 1 or 3, the wall lies along the x axis
    int rx = 6;
    int rz = relative == Rotation::Clockwise90 ? 12 : 0;
    int ry = rand.nextInt(floors) * 8;
    return BlockPos{rx, ry, rz};
}

BlockPos MazeTower13::offsetTowerCCoords(int x, int y, int z, int howFar, Orientation direction) const {
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

void MazeTower13::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));
    // walls
    randomFill(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, false, rand, *deco->randomBlocks);
    // stone to ground
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) fillColumnDown(region, *deco->blockState, x, -1, z, sbb);
    // add branching runes
    int numBranches = 2 + decoRNG.nextInt(4) + decoRNG.nextInt(3);
    for (int i = 0; i < numBranches; i++) makeGlyphBranches(region, decoRNG, getGlyphMeta(), sbb);
    // floors
    addFloors(region, sbb);
    // openings
    makeOpenings(region, sbb);
}

Block const& MazeTower13::getGlyphMeta() const {
    if (mColor.empty()) {
        gf::log().warn("finalcastle: Final Castle tower has null for glyph color, this is a bug.");
        return J(id::BlueRune);
    }
    return J(mColor.c_str());
}

void MazeTower13::addFloors(Region& region, BlockBox const& sbb) {
    // only add floors up to highest opening
    int      floors   = (highestOpening / 8) + 1;
    Rotation rotation = Rotation::Clockwise90;
    for (int i = 1; i < floors; i++) {
        fill(region, sbb, 1, i * 8, 1, 11, i * 8, 11, *deco->blockState, *deco->blockState, false);
        rotation = st::addRotation(rotation, Rotation::Clockwise180);
        // stairs
        addStairsDown(region, sbb, rotation, i * 8);
    }
    if (hasAccessibleRoof()) {
        // add stairs to roof
        addStairsDown(region, sbb, kRotations[static_cast<std::size_t>((floors + 2) & 3)], height - 1);
    }
}

bool MazeTower13::hasAccessibleRoof() const { return height - highestOpening < 9; }

void MazeTower13::addStairsDown(Region& region, BlockBox const& sbb, Rotation rotation, int y) {
    Block const& west  = getStairState(*deco->stairState, Orientation::West, false);
    Block const& north = getStairState(*deco->stairState, Orientation::North, false);
    // top flight
    for (int i = 0; i < 4; i++) {
        int sx = 8 - i;
        int sy = y - i;
        int sz = 9;
        setBlockStateRotated(region, west, sx, sy, sz, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz, rotation, sbb);
        setBlockStateRotated(region, west, sx, sy, sz - 1, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz - 1, rotation, sbb);
        fillAirRotated(region, sbb, sx, sy + 1, sz - 1, sx, sy + 3, sz, rotation);
    }
    // landing
    fillBlocksRotated(region, sbb, 3, y - 4, 8, 4, y - 4, 9, *deco->blockState, rotation);
    // bottom flight
    for (int i = 0; i < 4; i++) {
        int sx = 4;
        int sy = y - i - 4;
        int sz = 7 - i;
        setBlockStateRotated(region, north, sx, sy, sz, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, sx, sy - 1, sz, rotation, sbb);
        setBlockStateRotated(region, north, sx - 1, sy, sz, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, sx - 1, sy - 1, sz, rotation, sbb);
        fillAirRotated(region, sbb, sx, sy + 1, sz, sx - 1, sy + 3, sz, rotation);
    }
}

void MazeTower13::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    Block const& doorState = doorColor();
    // clear the door
    if (dx == 0 || dx == size - 1) {
        fill(region, sbb, dx, dy - 1, dz - 2, dx, dy + 4, dz + 2, *deco->accentState, air(), false);
        fill(region, sbb, dx, dy, dz - 1, dx, dy + 3, dz + 1, doorState, air(), false);
    }
    if (dz == 0 || dz == size - 1) {
        fill(region, sbb, dx - 2, dy - 1, dz, dx + 2, dy + 4, dz, *deco->accentState, air(), false);
        fill(region, sbb, dx - 1, dy, dz, dx + 1, dy + 3, dz, doorState, air(), false);
    }
}

Block const& MazeTower13::doorColor() const {
    if (mColor == id::PinkRune) return J(id::PinkDoor);
    if (mColor == id::BlueRune) return J(id::BlueDoor);
    if (mColor == id::YellowRune) return J(id::YellowDoor);
    if (mColor == id::VioletRune) return J(id::VioletDoor);
    gf::log().warn("finalcastle: Couldn't add door to tower, rune color couldn't be read");
    return air();
}

// ==== FinalCastleDamagedTowerComponent / FinalCastleWreckedTowerComponent =======================================

DamagedTower::DamagedTower(std::string typeId, RandomSource& rand, int i, int x, int y, int z, Orientation direction)
: MazeTower13(std::move(typeId), rand, i, x, y, z, id::YellowRune, direction) {} // TODO in TF: change rune color

DamagedTower::DamagedTower(std::string typeId) : MazeTower13(std::move(typeId)) {}

void DamagedTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<Foundation13>(type::Foundation13, 0, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add thorns
    add(list, std::make_unique<ThornFoundation>(0, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // keep on building?
    buildNonCriticalTowers(list, rand);
}

std::unique_ptr<MazeTower13> DamagedTower::makeNewDamagedTower(RandomSource& rand, Orientation facing, BlockPos const& tc) {
    return std::make_unique<WreckedTower>(rand, genDepth + 1, tc.x, tc.y, tc.z, facing);
}

void DamagedTower::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    MazeTower13::postProcess(region, rand, sbb, pivot);
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));
    destroyTower(region, decoRNG, sbb);
}

void DamagedTower::destroyTower(Region& region, RandomSource& rand, BlockBox const& sbb) {
    // make list of destroyed areas
    auto areas       = makeInitialDestroyList(rand);
    bool hitDeadRock = false;
    // go down from the top of the tower to the ground, taking out rectangular chunks
    for (int y = bbox.maxY; !hitDeadRock && y > 64; y--) {
        for (int x = bbox.minX - 2; x <= bbox.maxX + 2; x++) {
            for (int z = bbox.minZ - 2; z <= bbox.maxZ + 2; z++) {
                if (sbb.contains(x, y, z)) {
                    if (isDeadrock(region.getBlock(BlockPos{x, y, z}))) hitDeadRock = true;
                    determineBlockDestroyed(region, areas, y, x, z);
                }
            }
        }
        // check to see if any of our DestroyAreas are entirely above the current y value
        std::optional<std::size_t> removeIndex;
        bool                       removeNull = false;
        for (std::size_t i = 0; i < areas.size(); i++) {
            if (!areas[i] || areas[i]->isEntirelyAbove(y)) {
                removeIndex = i;
                removeNull  = !areas[i];
            }
        }
        // if so, replace them with new ones (Java: areas.remove(null) when the last match is a null)
        if (removeIndex && !removeNull) {
            areas.erase(areas.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            areas.push_back(DestroyArea::createNonIntersecting(bbox, rand, y, areas));
        }
    }
}

std::vector<std::optional<DestroyArea>> DamagedTower::makeInitialDestroyList(RandomSource& rand) {
    std::vector<std::optional<DestroyArea>> areas;
    for (int i = 0; i < 3; i++) areas.push_back(DestroyArea::createNonIntersecting(bbox, rand, bbox.maxY - 1, areas));
    return areas;
}

void DamagedTower::determineBlockDestroyed(Region& region, std::vector<std::optional<DestroyArea>> const& areas, int y, int x, int z) {
    for (auto const& a : areas)
        if (a && a->isVecInside(x, y, z)) region.setBlock(BlockPos{x, y, z}, air(), 3);
}

WreckedTower::WreckedTower(RandomSource& rand, int i, int x, int y, int z, Orientation direction)
: DamagedTower(type::Wrecked, rand, i, x, y, z, direction) {}

WreckedTower::WreckedTower() : DamagedTower(type::Wrecked) {}

void WreckedTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add thorns
    add(list, std::make_unique<ThornFoundation>(0, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
}

Block const& WreckedTower::getGlyphMeta() const { return J(id::YellowRune); }

std::vector<std::optional<DestroyArea>> WreckedTower::makeInitialDestroyList(RandomSource& rand) {
    std::vector<std::optional<DestroyArea>> areas;
    for (int i = 0; i < 4; i++) areas.push_back(DestroyArea::createNonIntersecting(bbox, rand, bbox.maxY - 1, areas));
    return areas;
}

void WreckedTower::determineBlockDestroyed(Region& region, std::vector<std::optional<DestroyArea>> const& areas, int y, int x, int z) {
    bool isInside = false;
    for (auto const& a : areas)
        if (a && a->isVecInside(x, y, z)) isInside = true;
    if (!isInside) region.setBlock(BlockPos{x, y, z}, air(), 3);
}

// ==== the entrance ===============================================================================================

EntranceTower::EntranceTower(int i, int x, int y, int z, Orientation direction)
: MazeTower13(type::EntranceTower, i, x, y, z, 3, 2, id::YellowRune, direction) {}

EntranceTower::EntranceTower() : MazeTower13(type::EntranceTower) {}

void EntranceTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<Foundation13>(type::Foundation13, 4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add roof
    add(list, std::make_unique<Roof13Peaked>(4, *this))->addChildren(*this, list, rand);

    // how many floors until the bottom?
    int missingFloors = (bbox.minY - 127) / 8;
    // place half on the bottom
    int bottomFloors = missingFloors / 2;
    // how many are left for the middle?
    int middleFloors = missingFloors - bottomFloors;

    // what direction can we put the side tower in, if any?
    Orientation facing = st::rotateDir(ori(), Rotation::Clockwise90);
    int         howFar = 20;
    if (!buildSideTower(list, rand, middleFloors + 1, facing, howFar)) {
        facing = st::rotateDir(ori(), Rotation::Counterclockwise90);
        if (!buildSideTower(list, rand, middleFloors + 1, facing, howFar)) {
            facing = ori();
            if (!buildSideTower(list, rand, middleFloors + 1, facing, howFar)) {
                // side tower no worky
            }
        }
    }

    // add bottom tower
    add(list, std::make_unique<EntranceBottomTower>(genDepth + 1, bbox.minX + 6, bbox.minY - (middleFloors * 8), bbox.minZ + 6, bottomFloors + 1, bottomFloors,
                                                    st::opposite(facing)))
        ->addChildren(*this, list, rand);

    // add bridge to bottom
    BlockPos opening = getValidOpeningCC(rand, facing);
    opening.y -= middleFloors * 8;
    BlockPos bc = offsetTowerCCoords(opening.x, opening.y, opening.z, 1, facing);
    add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, facing))->addChildren(*this, list, rand);
}

bool EntranceTower::buildSideTower(PieceList& list, RandomSource& rand, int middleFloors, Orientation facing, int howFar) {
    BlockPos opening = getValidOpeningCC(rand, facing);
    // build towards
    BlockPos tc       = offsetTowerCCoords(opening.x, opening.y, opening.z, howFar, facing);
    auto     eTower   = std::make_unique<EntranceSideTower>(genDepth + 1, tc.x, tc.y, tc.z, middleFloors, middleFloors - 1, facing);
    BlockBox largerBB = cloneWithAdjustments(eTower->bbox, -6, 0, -6, 6, 0, 6);
    if (st::bases::findCollisionPiece(list, largerBB)) return false; // side entrance tower blocked

    add(list, std::move(eTower))->addChildren(*this, list, rand);
    // add bridge
    BlockPos bc = offsetTowerCCoords(opening.x, opening.y, opening.z, 1, facing);
    add(list, std::make_unique<Bridge>(genDepth + 1, bc.x, bc.y, bc.z, howFar - 7, facing))->addChildren(*this, list, rand);
    // opening
    addOpening(opening.x, opening.y + 1, opening.z, facing);
    return true;
}

BlockPos EntranceTower::getValidOpeningCC(RandomSource& rand, Orientation facing) {
    BlockPos opening = MazeTower13::getValidOpeningCC(rand, facing);
    return BlockPos{opening.x, 0, opening.z};
}

EntranceSideTower::EntranceSideTower(int i, int x, int y, int z, int floors, int entranceFloor, Orientation direction)
: MazeTower13(type::EntranceSide, i, x, y, z, floors, entranceFloor, id::YellowRune, direction) {
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
}

EntranceSideTower::EntranceSideTower() : MazeTower13(type::EntranceSide) {}

void EntranceSideTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<Foundation13>(type::Foundation13, 4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add roof
    add(list, std::make_unique<Roof13Peaked>(4, *this))->addChildren(*this, list, rand);
}

EntranceBottomTower::EntranceBottomTower(int i, int x, int y, int z, int floors, int entranceFloor, Orientation direction)
: MazeTower13(type::EntranceBottom, i, x, y, z, floors, entranceFloor, id::YellowRune, direction) {}

EntranceBottomTower::EntranceBottomTower() : MazeTower13(type::EntranceBottom) {}

void EntranceBottomTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    // stairs
    addStairs(list, rand, genDepth + 1, size - 1, 1, size / 2, Rotation::None);
    addStairs(list, rand, genDepth + 1, 0, 1, size / 2, Rotation::Clockwise180);
    addStairs(list, rand, genDepth + 1, size / 2, 1, 0, Rotation::Counterclockwise90);
    addStairs(list, rand, genDepth + 1, size / 2, 1, size - 1, Rotation::Clockwise90);
}

bool EntranceBottomTower::addStairs(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation) {
    // add door
    addOpening(x, y, z, rotation);
    Orientation direction = getStructureRelativeRotation(rotation);
    BlockPos    dx        = offsetTowerCCoords(x, y, z, 0, direction);
    auto*       stairs    = add(list, std::make_unique<EntranceStairs>(index, dx.x, dx.y, dx.z, direction));
    stairs->addChildren(*list.front(), list, rand);
    return true;
}

EntranceStairs::EntranceStairs(int index, int x, int y, int z, Orientation direction) : ComponentOld(type::EntranceStairs, index, x, y, z) {
    setOrientation(direction);
    bbox = getComponentToAddBoundingBox2(x, y, z, 0, -1, -5, 12, 0, 12, direction);
}

EntranceStairs::EntranceStairs() : ComponentOld(type::EntranceStairs, 0, 0, 0, 0) {}

void EntranceStairs::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void EntranceStairs::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    int size = 13;
    for (int x = 1; x < size; x++) {
        placeStairs(region, sbb, x, 1 - x, 5, Orientation::East);
        for (int z = 0; z <= x; z++) {
            if (z > 0 && z <= size / 2) {
                placeStairs(region, sbb, x, 1 - x, 5 - z, Orientation::East);
                placeStairs(region, sbb, x, 1 - x, 5 + z, Orientation::East);
            }
            if (x <= size / 2) {
                placeStairs(region, sbb, z, 1 - x, 5 - x, Orientation::North);
                placeStairs(region, sbb, z, 1 - x, 5 + x, Orientation::South);
            }
        }
    }
    fillColumnDown(region, *deco->blockState, 0, 0, 5, sbb);
}

void EntranceStairs::placeStairs(Region& region, BlockBox const& sbb, int x, int y, int z, Orientation facing) {
    if (st::blocks::canBeReplaced(getBlock(region, x, y, z, sbb))) {
        placeBlock(region, st::blocks::withJava(*deco->stairState, Json{{"facing", st::blocks::directionName(facing)}}), x, y, z, sbb);
        fillColumnDown(region, *deco->blockState, x, y - 1, z, sbb);
    }
}

// ==== FinalCastleBellTower21Component ============================================================================

BellTower::BellTower(int i, int x, int y, int z, Orientation direction) : MazeTower13(type::BellTower, i, x, y, z, 8, 1, id::BlueRune, direction) {
    size   = 21;
    height = 8 * 8 + 1;
    bbox   = getComponentToAddBoundingBox2(x, y, z, -6, -8, -size / 2, size - 1, height, size - 1, direction);
    openings.clear();
    addOpening(0, 9, size / 2, Rotation::Clockwise180);
}

BellTower::BellTower() : MazeTower13(type::BellTower) {}

void BellTower::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
    BlockPos loc = getLocatorPosition();
    // add foundation
    add(list, std::make_unique<BellFoundation>(4, *this, loc.x, loc.y, loc.z))->addChildren(*this, list, rand);
    // add roof
    add(list, std::make_unique<Roof13Cren>(4, *this))->addChildren(*this, list, rand);

    BlockPos pos      = getWorldPos(0, 9, 10);
    auto     oriented = ext::rotate(ext::FrontAndTop::WestUp, rotation);
    auto templatePiece = ext::initializeTemplateFromPool(id::BellTowerPool, pos, oriented, id::RoomJigsaw, rand, genDepth + 1);
    if (templatePiece) list.push_back(std::move(templatePiece));
}

void BellTower::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    MazeTower13::postProcess(region, rand, sbb, pivot);
    // openings!
    Block const& field = J(id::BlueField);
    for (auto rotation : kRotations) {
        int y = 48;
        for (int x = 5; x < size - 4; x += 2) fillBlocksRotated(region, sbb, x, y, 0, x, y + 14, 0, field, rotation);
        y = 24;
        for (int x = 1; x < size - 1; x += 8) {
            fillBlocksRotated(region, sbb, x, y, 0, x, y + 14, 0, field, rotation);
            fillBlocksRotated(region, sbb, x + 2, y, 0, x + 2, y + 14, 0, field, rotation);
        }
    }
    // sign placed by template
}

// ==== FinalCastleBridgeComponent =================================================================================

Bridge::Bridge(int i, int x, int y, int z, int length, Orientation direction) : ComponentOld(type::Bridge, i, x, y, z) {
    setOrientation(direction);
    bbox = getComponentToAddBoundingBox2(x, y, z, 0, -1, -3, length - 1, 5, 6, direction);
}

Bridge::Bridge() : ComponentOld(type::Bridge, 0, 0, 0, 0) {}

void Bridge::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Bridge::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    auto o      = getOrientation();
    int  length = (o == Orientation::South || o == Orientation::North) ? bbox.maxX - bbox.minX : bbox.maxZ - bbox.minZ;
    auto& randomBlocks = *deco->randomBlocks;
    // span
    randomFill(region, sbb, 0, 0, 0, length, 1, 6, false, rand, randomBlocks);
    // rails
    Block const& castlePillar = J("twilightforest:bold_castle_brick_pillar", Json{{"axis", "x"}});
    fill(region, sbb, 0, 2, 0, length, 2, 0, castlePillar, castlePillar, false);
    fill(region, sbb, 0, 2, 6, length, 2, 6, castlePillar, castlePillar, false);
    // supports
    int l3 = length / 3;
    for (int i = 0; i < l3; i++) {
        int sl = core::finalcastle::bridgeArchDepth(l3, i); // this could be better, maybe?
        randomFill(region, sbb, i, -sl, 0, i, 0, 0, false, rand, randomBlocks);
        randomFill(region, sbb, i, -sl, 6, i, 0, 6, false, rand, randomBlocks);
        randomFill(region, sbb, length - i, -sl, 0, length - i, 0, 0, false, rand, randomBlocks);
        randomFill(region, sbb, length - i, -sl, 6, length - i, 0, 6, false, rand, randomBlocks);
    }
    // doorframes
    fill(region, sbb, 0, 2, 1, 0, 7, 1, *deco->pillarState, *deco->pillarState, false);
    fill(region, sbb, 0, 2, 5, 0, 7, 5, *deco->pillarState, *deco->pillarState, false);
    fill(region, sbb, 0, 6, 2, 0, 6, 4, *deco->accentState, *deco->accentState, false);
    placeBlock(region, *deco->pillarState, 0, 7, 3, sbb);
    fill(region, sbb, length, 2, 1, length, 7, 1, *deco->pillarState, *deco->pillarState, false);
    fill(region, sbb, length, 2, 5, length, 7, 5, *deco->pillarState, *deco->pillarState, false);
    fill(region, sbb, length, 6, 2, length, 6, 4, *deco->accentState, *deco->accentState, false);
    placeBlock(region, *deco->pillarState, length, 7, 3, sbb);
}

// ==== foundations ================================================================================================

Foundation13::Foundation13(std::string typeId, int i, ComponentOld const& sideTower, int x, int y, int z) : ComponentOld(std::move(typeId), i, x, y, z) {
    setOrientation(sideTower.getOrientation());
    auto const& t = sideTower.bbox;
    bbox          = BlockBox::of(t.minX - 2, t.minY - 1, t.minZ - 2, t.minX + 2, t.minY, t.maxZ + 2);
}

Foundation13::Foundation13(std::string typeId) : ComponentOld(std::move(typeId), 0, 0, 0, 0) {}

void Foundation13::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Foundation13::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // offset bounding box to average ground level
    if (groundLevel < 0) {
        groundLevel = findGroundLevel(region, sbb, 150, isDeadrock); // is 150 a good place to start? :)
        if (groundLevel < 0) return;
    }
    // how tall are we
    int height = bbox.maxY - groundLevel;
    int mid    = height / 2;
    // assume square
    int size = bbox.maxX - bbox.minX;
    for (auto rotation : kRotations) {
        // do corner
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 1, -1, 1, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 2, -1, 1, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 2, -mid, 0, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 1, -1, 2, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 0, -mid, 2, rotation, sbb);
        for (int x = 6; x < (size - 3); x += 4) {
            replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, x, -1, 1, rotation, sbb);
            replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, x, -mid, 0, rotation, sbb);
        }
    }
}

ThornFoundation::ThornFoundation(int i, ComponentOld const& sideTower, int x, int y, int z) : Foundation13(type::Thorns, i, sideTower, x, y, z) {
    auto const& t = sideTower.bbox;
    bbox          = BlockBox::of(t.minX - 5, t.maxY - 1, t.minZ - 5, t.maxX + 5, t.maxY, t.maxZ + 5);
}

ThornFoundation::ThornFoundation() : Foundation13(type::Thorns) {}

void ThornFoundation::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // thorns
    core::jrand::LegacyRandom decoRNG(decoSeed(region, bbox));
    for (auto rotation : kRotations) makeThornVine(region, decoRNG, rotation, sbb);
}

void ThornFoundation::makeThornVine(Region& region, RandomSource& decoRNG, Rotation rotation, BlockBox const& sbb) {
    int x        = 3 + decoRNG.nextInt(13);
    int z        = 3 + decoRNG.nextInt(13);
    int y        = (bbox.maxY - bbox.minY + 1) + 5;
    int twist    = decoRNG.nextInt(4);
    int twistMod = 3 + decoRNG.nextInt(3);

    Block const& thorns = J("twilightforest:brown_thorns");
    while (!isDeadrock(getBlockStateFromPosRotated(region, x, y, z, sbb, rotation)) && worldY(y) > 90) {
        setBlockStateRotated(region, thorns, x, y, z, rotation, sbb);
        // twist vines around the center block
        switch (twist) {
        case 0:
            setBlockStateRotated(region, thorns, x + 1, y, z, rotation, sbb);
            setBlockStateRotated(region, thorns, x, y, z + 1, rotation, sbb);
            setBlockStateRotated(region, thorns, x + 1, y, z + 1, rotation, sbb);
            break;
        case 1:
            setBlockStateRotated(region, thorns, x + 1, y, z, rotation, sbb);
            setBlockStateRotated(region, thorns, x, y, z - 1, rotation, sbb);
            setBlockStateRotated(region, thorns, x + 1, y, z - 1, rotation, sbb);
            break;
        case 2:
            setBlockStateRotated(region, thorns, x - 1, y, z, rotation, sbb);
            setBlockStateRotated(region, thorns, x, y, z - 1, rotation, sbb);
            setBlockStateRotated(region, thorns, x - 1, y, z - 1, rotation, sbb);
            break;
        case 3:
            setBlockStateRotated(region, thorns, x - 1, y, z, rotation, sbb);
            setBlockStateRotated(region, thorns, x, y, z + 1, rotation, sbb);
            setBlockStateRotated(region, thorns, x - 1, y, z + 1, rotation, sbb);
            break;
        default: break;
        }
        // make branch
        if (std::abs(y % twistMod) == 1) makeThornBranch(region, x, y, z, rotation, sbb);
        // twist randomly
        if (y % twistMod == 0) {
            twist++;
            twist = twist % 4;
        }
        y--;
    }
}

void ThornFoundation::makeThornBranch(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    core::jrand::LegacyRandom rand(core::finalcastle::thornBranchSeed(region.seed(), x, y, z));
    // pick a direction
    Rotation dir = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
    // initialize direction variables
    int dx = 0;
    int dz = 0;
    switch (dir) {
    case Rotation::None: dx = 1; break;
    case Rotation::Clockwise90: dz = 1; break;
    case Rotation::Clockwise180: dx = -1; break;
    case Rotation::Counterclockwise90: dz = -1; break;
    }
    // how far do we branch?
    int dist = 2 + rand.nextInt(3);
    // check to make sure there's room
    int destX = x + (dist * dx);
    int destZ = z + (dist * dz);
    if (destX > 0 && destX < (bbox.maxX - bbox.minX + 1) && destZ > 0 && destZ < (bbox.maxZ - bbox.minZ + 1)) {
        for (int i = 0; i < dist; i++) {
            // go out that far
            Rotation     added  = st::addRotation(st::addRotation(dir, rotation), this->rotation);
            char const*  axis   = added == Rotation::None || added == Rotation::Clockwise180 ? "x" : "z";
            Block const& thorns = J("twilightforest:green_thorns", Json{{"axis", axis}});
            if (i > 0) setBlockStateRotated(region, thorns, x + (dx * i), y, z + (dz * i), rotation, sbb);
            // go up that far
            setBlockStateRotated(region, J("twilightforest:green_thorns", Json{{"axis", "y"}}), destX, y + i, destZ, rotation, sbb);
            // go back half that far
            if (i > (dist / 2)) setBlockStateRotated(region, thorns, x + (dx * i), y + dist - 1, z + (dz * i), rotation, sbb);
        }
    }
}

BellFoundation::BellFoundation(int i, ComponentOld const& sideTower, int x, int y, int z) : Foundation13(type::BellFoundation, i, sideTower, x, y, z) {
    auto const& t = sideTower.bbox;
    bbox          = BlockBox::of(t.minX - 2, t.maxY - 1, t.minZ - 2, t.minX + 2, t.maxY, t.maxZ + 2);
}

BellFoundation::BellFoundation() : Foundation13(type::BellFoundation) {}

void BellFoundation::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    int mid = 16;
    int low = 32;
    // assume square
    int size = bbox.maxX - bbox.minX;
    for (auto rotation : kRotations) {
        // do corner
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 2, -1, 1, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 2, -mid, 0, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 1, -1, 2, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 0, -mid, 2, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 1, -low, 1, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 0, -low, 1, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 1, -low, 0, rotation, sbb);
        replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, 0, -low, 0, rotation, sbb);
        for (int x = 6; x < (size - 3); x += 4) {
            replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, x, -1, 1, rotation, sbb);
            replaceAirAndLiquidDownwardsRotated(region, *deco->blockState, x, -mid, 0, rotation, sbb);
        }
    }
}

} // namespace gf::feature::finalcastle::castle
