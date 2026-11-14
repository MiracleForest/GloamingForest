// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/stronghold/KnightStrongholdComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdAccessChamberComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdAtriumComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdBalconyRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdBossRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdCrossingComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdDeadEndComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdDecorator.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdFoundryComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdLeftTurnComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdPieceWeight.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdPieces.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdRightTurnComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdShieldStructure.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdSmallHallwayComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdSmallStairsComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdTrainingRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdTreasureCorridorComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdTreasureRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdUpperAscenderComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdUpperCorridorComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdUpperLeftTurnComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdUpperRightTurnComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/StrongholdUpperTIntersectionComponent.java
//   src/main/java/gloamingforest/world/components/structures/type/KnightStrongholdStructure.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/stronghold/Pieces.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "feature/ext/structure/Placement.h"
#include "feature/features/Features.h"
#include "feature/structure/Internal.h"
#include "mod/Log.h"

namespace gf::feature::stronghold::pieces {

namespace blocks = ::gf::structure::blocks;
using ::gf::structure::bases::ComponentOld;
using ::gf::structure::bases::TerrainAdjustment;

namespace {

Block const& J(char const* id, Json const& props = Json::object()) { return blocks::java(id, props); }

/// Rotation#rotate(Direction.WEST).
Orientation W(Rotation r) { return ::gf::structure::rotateDir(Orientation::West, r); }

Rotation rot(Rotation a, Rotation b) { return ::gf::structure::addRotation(a, b); }

Orientation ccw(Orientation o) { return ::gf::structure::rotateDir(o, Rotation::Counterclockwise90); }

struct StrongholdDeco final : Decorator {
    [[nodiscard]] std::string decoString() const override { return "DecoStronghold"; }
};

/// Vanilla 1.21.1 TreeFeatures.OAK / BIRCH / JUNGLE_TREE as configured feature JSON (F5 lists only
/// upstream's configured features; these are the same data as the vanilla registry entries).
Json const& vanillaTree(int which) {
    auto make = [](char const* log, char const* leaves, int baseHeight, int randA, Json decorators) {
        return Json{
            {"type", "minecraft:tree"},
            {"config",
             {{"decorators", std::move(decorators)},
              {"dirt_provider", {{"type", "minecraft:simple_state_provider"}, {"state", {{"Name", "minecraft:dirt"}}}}},
              {"foliage_placer", {{"type", "minecraft:blob_foliage_placer"}, {"height", 3}, {"offset", 0}, {"radius", 2}}},
              {"foliage_provider",
               {{"type", "minecraft:simple_state_provider"},
                {"state", {{"Name", leaves}, {"Properties", {{"distance", "7"}, {"persistent", "false"}, {"waterlogged", "false"}}}}}}},
              {"force_dirt", false},
              {"ignore_vines", true},
              {"minimum_size", {{"type", "minecraft:two_layers_feature_size"}, {"limit", 1}, {"lower_size", 0}, {"upper_size", 1}}},
              {"trunk_placer", {{"type", "minecraft:straight_trunk_placer"}, {"base_height", baseHeight}, {"height_rand_a", randA}, {"height_rand_b", 0}}},
              {"trunk_provider", {{"type", "minecraft:simple_state_provider"}, {"state", {{"Name", log}, {"Properties", {{"axis", "y"}}}}}}}}}};
    };
    // createStraightBlobTree(log, leaves, base, randA, 0, 2).ignoreVines(); the jungle tree with
    // CocoaDecorator(0.2), TrunkVineDecorator, LeaveVineDecorator(0.25)
    static Json const oak    = make("minecraft:oak_log", "minecraft:oak_leaves", 4, 2, Json::array());
    static Json const birch  = make("minecraft:birch_log", "minecraft:birch_leaves", 5, 2, Json::array());
    static Json const jungle = make("minecraft:jungle_log", "minecraft:jungle_leaves", 4, 8,
                                    Json::array({Json{{"type", "minecraft:cocoa"}, {"probability", 0.2}}, Json{{"type", "minecraft:trunk_vine"}},
                                                 Json{{"type", "minecraft:leave_vine"}, {"probability", 0.25}}}));
    return which == 1 ? jungle : which == 2 ? birch : oak;
}

thread_local std::shared_ptr<StrongholdPieces::Weight> tLastPieceMade;

} // namespace

std::unique_ptr<Decorator> strongholdDecorator() {
    auto d           = std::make_unique<StrongholdDeco>();
    auto def         = ::gf::structure::bases::defaultDecorator();
    d->floorState    = def->floorState; // TFStructureDecorator's defaults
    d->roofState     = def->roofState;
    d->blockState    = &J("gloamingforest:underbrick");
    d->accentState   = &J("gloamingforest:cracked_underbrick");
    d->fenceState    = &J("minecraft:cobblestone_wall");
    d->stairState    = &J("minecraft:stone_brick_stairs");
    d->pillarState   = &J("minecraft:mossy_stone_bricks");
    d->platformState = &J("minecraft:smooth_stone_slab", Json{{"type", "top"}});
    d->randomBlocks  = ::gf::structure::selectors::get("gloamingforest:knight_stones");
    return d;
}

BlockBox orientBox(int x, int y, int z, int offX, int offY, int offZ, int sizeX, int sizeY, int sizeZ, Orientation facing) {
    switch (facing) {
    case Orientation::North:
        return BlockBox::of(x + offX, y + offY, z - sizeZ + 1 + offZ, x + sizeX - 1 + offX, y + sizeY - 1 + offY, z + offZ);
    case Orientation::West:
        return BlockBox::of(x - sizeZ + 1 + offZ, y + offY, z + offX, x + offZ, y + sizeY - 1 + offY, z + sizeX - 1 + offX);
    case Orientation::East:
        return BlockBox::of(x + offZ, y + offY, z + offX, x + sizeZ - 1 + offZ, y + sizeY - 1 + offY, z + sizeX - 1 + offX);
    default:
        return BlockBox::of(x + offX, y + offY, z + offZ, x + sizeX - 1 + offX, y + sizeY - 1 + offY, z + sizeZ - 1 + offZ);
    }
}

// ==== KnightStrongholdComponent ================================================================================

Knight::Knight(std::string typeId) : ComponentOld(std::move(typeId), 0, 0, 0, 0) { deco = strongholdDecorator(); }

void Knight::init(int i, Orientation facing, int x, int y, int z) {
    genDepth = i;
    bbox     = generateBoundingBox(facing, x, y, z);
    setOrientation(facing);
    spawnListIndex = isComponentProtected() ? 0 : -1;
}

Json Knight::save() const {
    Json j    = ComponentOld::save();
    Json ints = Json::array();
    for (auto const& d : doors) {
        ints.push_back(d.x);
        ints.push_back(d.y);
        ints.push_back(d.z);
    }
    j["doorInts"] = ints;
    return j;
}

void Knight::load(Json const& data) {
    ComponentOld::load(data);
    doors.clear();
    if (auto it = data.find("doorInts"); it != data.end() && it->is_array())
        for (std::size_t i = 0; i + 2 < it->size(); i += 3) doors.emplace_back((*it)[i].get<int>(), (*it)[i + 1].get<int>(), (*it)[i + 2].get<int>());
    if (!deco) deco = strongholdDecorator();
}

BlockBox Knight::getComponentToAddBoundingBox(int x, int y, int z, int xOff, int yOff, int zOff, int xSize, int ySize, int zSize, Orientation facing) {
    switch (facing) {
    case Orientation::West:
        return BlockBox::of(x - zSize + 1 + zOff, y + yOff, z + xOff, x + zOff, y + ySize - 1 + yOff, z + xSize - 1 + xOff);
    case Orientation::North:
        return BlockBox::of(x - xSize + 1 - xOff, y + yOff, z - zSize + 1 + zOff, x - xOff, y + ySize - 1 + yOff, z + zOff);
    case Orientation::East:
        return BlockBox::of(x + zOff, y + yOff, z - xSize + 1 - xOff, x + zSize - 1 + zOff, y + ySize - 1 + yOff, z - xOff);
    default:
        return BlockBox::of(x + xOff, y + yOff, z + zOff, x + xSize - 1 + xOff, y + ySize - 1 + yOff, z + zSize - 1 + zOff);
    }
}

void Knight::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto* old = dynamic_cast<ComponentOld*>(&parent); old && old->deco) deco = ::gf::structure::bases::decoratorFor(old->deco->decoString());
}

void Knight::addNewComponent(Piece& entrance, PieceList& list, RandomSource& random, Rotation facing, int x, int y, int z) {
    int         index   = genDepth + 1;
    Orientation nFacing = getStructureRelativeRotation(facing);
    int         nx      = worldX(x, z);
    int         ny      = worldY(y);
    int         nz      = worldZ(x, z);
    // limit sprawl to a reasonable amount
    if (index > 30 || isOutOfRange(entrance, nx, nz, 75)) return;
    // are we looking at a point we can possibly break in to?
    auto* breakIn = dynamic_cast<Knight*>(findBreakInComponent(list, nx, ny, nz));
    if (breakIn && breakIn->attemptToBreakIn(nx, ny, nz)) {
        // success!
        addDoorwayTo(x, y, z, facing);
        return;
    }
    auto* start = dynamic_cast<Entrance*>(&entrance);
    if (!start) return;
    auto next = start->lowerPieces.getNextComponent(entrance, list, random, index, nFacing, nx, ny, nz);
    // is it clear?
    if (next) {
        auto* raw = next.get();
        list.push_back(std::move(next));
        raw->addChildren(entrance, list, random);
        addDoorwayTo(x, y, z, facing);
    }
}

Piece* Knight::findBreakInComponent(PieceList& list, int x, int y, int z) {
    for (auto const& component : list)
        if (component && component->bbox.contains(x, y, z)) return component.get();
    return nullptr;
}

void Knight::addNewUpperComponent(Piece& parent, PieceList& list, RandomSource& random, Rotation facing, int x, int y, int z) {
    int         index   = genDepth + 1;
    Orientation nFacing = getStructureRelativeRotation(facing);
    int         nx      = worldX(x, z);
    int         ny      = worldY(y);
    int         nz      = worldZ(x, z);
    // limit sprawl to a reasonable amount
    if (index > 100 || isOutOfRange(parent, nx, nz, 48)) return;
    // find a new component
    std::unique_ptr<Knight> attempted;
    switch (random.nextInt(5)) {
    case 1: attempted = std::make_unique<UpperSmall>(kUpperLeftTurn, index, nFacing, nx, ny, nz); break;
    case 2: attempted = std::make_unique<UpperSmall>(kUpperRightTurn, index, nFacing, nx, ny, nz); break;
    case 3: attempted = std::make_unique<UpperSmall>(kUpperCorridor, index, nFacing, nx, ny, nz); break;
    case 4: attempted = std::make_unique<UpperAscender>(index, nFacing, nx, ny, nz); break;
    default: attempted = std::make_unique<UpperSmall>(kUpperTIntersection, index, nFacing, nx, ny, nz); break;
    }
    // is it clear?
    if (::gf::structure::bases::findCollisionPiece(list, attempted->bbox) == nullptr) {
        auto* raw = attempted.get();
        list.push_back(std::move(attempted));
        raw->addChildren(parent, list, random);
    }
}

bool Knight::isOutOfRange(Piece const& parent, int nx, int nz, int range) {
    return std::abs(nx - parent.bbox.minX) > range || std::abs(nz - parent.bbox.minZ) > range;
}

void Knight::placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb) {
    if (x == 0 || x == getXSize()) {
        fill(world, sbb, x, y, z - 2, x, y + 3, z + 2, *d().fenceState, air(), false);
        fillAir(world, sbb, x, y, z - 1, x, y + 3, z + 1);
    } else {
        fill(world, sbb, x - 2, y, z, x + 2, y + 3, z, *d().fenceState, air(), false);
        fillAir(world, sbb, x - 1, y, z, x + 1, y + 3, z);
    }
}

int Knight::getXSize() const {
    switch (orientation) {
    case Orientation::West:
    case Orientation::East: return bbox.maxZ - bbox.minZ;
    default: return bbox.maxX - bbox.minX;
    }
}

void Knight::placeSmallDoorwayAt(Region& world, int facing, int x, int y, int z, BlockBox const& sbb) {
    if (facing == 0 || facing == 2) fill(world, sbb, x - 1, y, z, x + 1, y + 1, z, J("minecraft:cobblestone_wall"), air(), true);
    else fill(world, sbb, x, y, z - 1, x, y + 1, z + 1, J("minecraft:cobblestone_wall"), air(), true);
    fillAir(world, sbb, x, y, z, x, y + 1, z);
}

void Knight::placeCornerStatue(Region& world, int x, int y, int z, int facing, BlockBox const& sbb) {
    // set offsets and stair metas
    int         ox  = 1;
    int         oz  = 1;
    Orientation smx = Orientation::East;
    Orientation smz = Orientation::South;
    switch (facing) {
    case 0: break; // already set up
    case 1:
        oz  = -1;
        smz = Orientation::South;
        break;
    case 2:
        ox  = -1;
        smx = Orientation::West;
        break;
    case 3:
        ox  = -1;
        oz  = -1;
        smx = Orientation::West;
        smz = Orientation::North;
        break;
    default: break;
    }
    auto const& stair = *d().stairState;
    // the center is always the same
    for (int sy = 0; sy < 5; sy++) placeBlock(world, *d().pillarState, x, y + sy, z, sbb);
    // antlers
    placeBlock(world, J("minecraft:oak_fence"), x, y + 4, z + oz, sbb);
    placeBlock(world, J("minecraft:oak_fence"), x + ox, y + 4, z, sbb);
    // arms
    placeBlock(world, getStairState(stair, smz, false), x, y + 3, z + oz, sbb);
    placeBlock(world, getStairState(stair, smx, false), x + ox, y + 3, z, sbb);
    placeBlock(world, getStairState(stair, smz, true), x, y + 2, z + oz, sbb);
    placeBlock(world, getStairState(stair, smx, true), x + ox, y + 2, z, sbb);
    placeBlock(world, getStairState(stair, smx, true), x + ox, y + 2, z + oz, sbb);
    // sword
    placeBlock(world, J("minecraft:cobblestone_wall"), x + ox, y, z + oz, sbb);
    placeBlock(world, J("minecraft:cobblestone_wall"), x + ox, y + 1, z + oz, sbb);
    // feet
    placeBlock(world, getStairState(stair, smz, false), x, y, z + oz, sbb);
    placeBlock(world, getStairState(stair, smx, false), x + ox, y, z, sbb);
}

void Knight::placeWallStatue(Region& world, int x, int y, int z, Rotation facing, BlockBox const& sbb) {
    int  ox    = 1;
    int  oz    = 1;
    auto st    = [&](Rotation r, bool top) -> Block const& { return getStairState(*d().stairState, W(rot(facing, r)), top); };
    auto fence = [&]() -> Block const& { return J("minecraft:oak_fence"); };
    auto wall  = [&]() -> Block const& { return J("minecraft:cobblestone_wall"); };
    // the center is always the same
    for (int sy = 0; sy < 5; sy++) placeBlock(world, *d().pillarState, x, y + sy, z, sbb);
    if (facing == Rotation::None || facing == Rotation::Clockwise180) {
        if (facing == Rotation::Clockwise180) {
            ox = -ox;
            oz = -oz;
        }
        // antlers
        placeBlock(world, fence(), x - ox, y + 4, z, sbb);
        placeBlock(world, fence(), x + ox, y + 4, z, sbb);
        // arms
        placeBlock(world, st(Rotation::None, false), x - ox, y + 3, z, sbb);
        placeBlock(world, st(Rotation::Clockwise180, false), x + ox, y + 3, z, sbb);
        placeBlock(world, st(Rotation::Clockwise90, false), x - ox, y + 3, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, false), x + ox, y + 3, z - oz, sbb);

        placeBlock(world, st(Rotation::None, true), x - ox, y + 2, z, sbb);
        placeBlock(world, st(Rotation::Clockwise180, true), x + ox, y + 2, z, sbb);
        placeBlock(world, st(Rotation::Clockwise90, true), x, y + 2, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, true), x - ox, y + 2, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, true), x + ox, y + 2, z - oz, sbb);
        // sword
        placeBlock(world, wall(), x, y, z - oz, sbb);
        placeBlock(world, wall(), x, y + 1, z - oz, sbb);
        // feet
        placeBlock(world, st(Rotation::None, false), x - ox, y, z, sbb);
        placeBlock(world, st(Rotation::Clockwise180, false), x + ox, y, z, sbb);
    } else {
        if (facing == Rotation::Counterclockwise90) {
            oz = -oz;
            ox = -ox;
        }
        // antlers
        placeBlock(world, fence(), x, y + 4, z - oz, sbb);
        placeBlock(world, fence(), x, y + 4, z + oz, sbb);
        // arms
        placeBlock(world, st(Rotation::None, false), x, y + 3, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise180, false), x, y + 3, z + oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, false), x + ox, y + 3, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, false), x + ox, y + 3, z + oz, sbb);

        placeBlock(world, st(Rotation::None, true), x, y + 2, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise180, true), x, y + 2, z + oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, true), x + oz, y + 2, z, sbb); // sic: x + oz
        placeBlock(world, st(Rotation::Clockwise90, true), x + ox, y + 2, z - oz, sbb);
        placeBlock(world, st(Rotation::Clockwise90, true), x + ox, y + 2, z + oz, sbb);
        // sword
        placeBlock(world, wall(), x + ox, y, z, sbb);
        placeBlock(world, wall(), x + ox, y + 1, z, sbb);
        // feet
        placeBlock(world, st(Rotation::None, false), x, y, z - ox, sbb);
        placeBlock(world, st(Rotation::Clockwise180, false), x, y, z + ox, sbb);
    }
}

bool Knight::attemptToBreakIn(int wx, int wy, int wz) {
    if (!isValidBreakInPoint(wx, wy, wz)) return false;
    addDoor(getRelativeX(wx, wz), getRelativeY(wy), getRelativeZ(wx, wz));
    return true;
}

void Knight::addDoorwayTo(int dx, int dy, int dz, Rotation facing) {
    switch (facing) {
    case Rotation::None: addDoor(dx, dy, dz - 1); break;
    case Rotation::Clockwise90: addDoor(dx + 1, dy, dz); break;
    case Rotation::Clockwise180: addDoor(dx, dy, dz + 1); break;
    case Rotation::Counterclockwise90: addDoor(dx - 1, dy, dz); break;
    }
}

void Knight::addDoor(int dx, int dy, int dz) { doors.emplace_back(dx, dy, dz); }

bool Knight::isValidBreakInPoint(int wx, int wy, int wz) {
    if (wy < bbox.minY || wy > bbox.maxY) return false;
    if (wx == bbox.minX || wx == bbox.maxX) return wz > bbox.minZ && wz < bbox.maxZ;
    if (wz == bbox.minZ || wz == bbox.maxZ) return wx > bbox.minX && wx < bbox.maxX;
    return false;
}

int Knight::getRelativeX(int x, int z) const {
    switch (orientation) {
    case Orientation::South: return x - bbox.minX;
    case Orientation::North: return bbox.maxX - x;
    case Orientation::West: return z - bbox.minZ;
    case Orientation::East: return bbox.maxZ - z;
    default: return x;
    }
}

int Knight::getRelativeY(int y) const { return y - bbox.minY; }

int Knight::getRelativeZ(int x, int z) const {
    switch (orientation) {
    case Orientation::South: return z - bbox.minZ;
    case Orientation::North: return bbox.maxZ - z;
    case Orientation::West: return bbox.maxX - x;
    case Orientation::East: return x - bbox.minX;
    default: return z;
    }
}

void Knight::placeDoors(Region& world, BlockBox const& sbb) {
    for (auto const& door : doors) placeDoorwayAt(world, door.x, door.y, door.z, sbb);
}

void Knight::placeStrongholdWalls(Region& world, BlockBox const& sbb, int sx, int sy, int sz, int dx, int dy, int dz, RandomSource& rand,
                                  ::gf::structure::BlockSelector& randomBlocks) {
    for (int y = sy; y <= dy; ++y)
        for (int x = sx; x <= dx; ++x)
            for (int z = sz; z <= dz; ++z) {
                bool         wall    = y == sy || y == dy || x == sx || x == dx || z == sz || z == dz;
                Block const& blockID = getBlock(world, x, y, z, sbb);
                if (blocks::is(blockID, "minecraft:air")) {
                    // cobblestone to "fill in holes"
                    if (wall) placeBlock(world, J("minecraft:cobblestone"), x, y, z, sbb);
                } else if (y == sy || y == dy) {
                    // do stronghold bricks for floor/ceiling
                    placeBlock(world, getStrongholdStones().next(rand, x, y, z, wall), x, y, z, sbb);
                } else if (!wall || !blocks::is(blockID, "minecraft:dirt")) { // leave dirt there
                    // and use decorator (with presumably underbricks) for walls
                    placeBlock(world, randomBlocks.next(rand, x, y, z, wall), x, y, z, sbb);
                }
            }
}

void Knight::placeUpperStrongholdWalls(Region& world, BlockBox const& sbb, int sx, int sy, int sz, int dx, int dy, int dz, RandomSource& rand,
                                       ::gf::structure::BlockSelector& randomBlocks) {
    static char const* const kKnightStones[]     = {"gloamingforest:cracked_underbrick", "gloamingforest:mossy_underbrick", "gloamingforest:underbrick"};
    static char const* const kStrongholdStones[] = {"minecraft:cracked_stone_bricks", "minecraft:mossy_stone_bricks", "minecraft:infested_stone_bricks",
                                                    "minecraft:stone_bricks"};
    for (int y = sy; y <= dy; ++y)
        for (int x = sx; x <= dx; ++x)
            for (int z = sz; z <= dz; ++z) {
                bool         wall       = y == sy || y == dy || x == sx || x == dx || z == sz || z == dz;
                Block const& state      = getBlock(world, x, y, z, sbb);
                Block const& stateBelow = getBlock(world, x, y - 1, z, sbb);
                bool         isKnightStone = std::any_of(std::begin(kKnightStones), std::end(kKnightStones), [&](char const* id) { return blocks::is(stateBelow, id); });
                bool         isStrongholdStone =
                    std::any_of(std::begin(kStrongholdStones), std::end(kStrongholdStones), [&](char const* id) { return blocks::is(stateBelow, id); });
                bool air              = state.isAir();
                bool isValidSurface   = !air && (blocks::inTag(state, "minecraft:base_stone_overworld") || blocks::inTag(state, "minecraft:dirt"));
                bool isAirWithChance  = air && rand.nextInt(3) == 0;
                bool isStoneBelow     = isKnightStone || isStrongholdStone;
                if (isValidSurface || (isAirWithChance && isStoneBelow)) {
                    if (y == sy || y == dy) {
                        // do stronghold bricks for floor/ceiling
                        placeBlock(world, getStrongholdStones().next(rand, x, y, z, wall), x, y, z, sbb);
                    } else {
                        // and use decorator (with presumably underbricks) for walls
                        placeBlock(world, randomBlocks.next(rand, x, y, z, wall), x, y, z, sbb);
                    }
                }
            }
}

TerrainAdjustment Knight::getTerrainAdjustment() const { return isComponentProtected() ? TerrainAdjustment::Bury : TerrainAdjustment::None; }

// ==== StrongholdPieces ============================================================================================

namespace {
template <class T>
StrongholdPieces::Factory factoryOf() {
    return [](int i, Orientation f, int x, int y, int z) -> std::unique_ptr<Knight> { return std::make_unique<T>(i, f, x, y, z); };
}
} // namespace

void StrongholdPieces::resetLastPieceMade() { tLastPieceMade.reset(); }

void StrongholdPieces::prepareStructurePieces() {
    // pieceWeightArray, each recounted (a fresh StrongholdPieceWeight)
    auto w = [](Factory f, int weight, int limit, int minDepth = 0) {
        auto p            = std::make_shared<Weight>();
        p->factory        = std::move(f);
        p->pieceWeight    = weight;
        p->instancesLimit = limit;
        p->minimumDepth   = minDepth;
        return p;
    };
    pieceList = {
        w(factoryOf<SmallHallway>(), 40, 0),
        w(factoryOf<LeftTurn>(), 20, 0),
        w(factoryOf<Crossing>(), 10, 4),
        w(factoryOf<RightTurn>(), 20, 0),
        w(factoryOf<DeadEnd>(), 5, 0),
        w(factoryOf<BalconyRoom>(), 10, 3, 2),
        w(factoryOf<TrainingRoom>(), 10, 2),
        w(factoryOf<SmallStairs>(), 10, 0),
        w(factoryOf<TreasureCorridor>(), 5, 0),
        w(factoryOf<Atrium>(), 5, 2, 3),
        w(factoryOf<Foundry>(), 5, 1, 4),
        w(factoryOf<TreasureRoom>(), 5, 1, 4),
        w(factoryOf<BossRoom>(), 15, 1, 4),
    };
}

void StrongholdPieces::markBossRoomUsed() {
    // let's assume the boss room is the last one on the list
    if (!pieceList.empty()) pieceList.pop_back();
}

bool StrongholdPieces::hasMoreLimitedPieces() {
    bool flag   = false;
    totalWeight = 0;
    for (auto const& piece : pieceList) {
        totalWeight += piece->pieceWeight;
        if (piece->instancesLimit > 0 && piece->instancesSpawned < piece->instancesLimit) flag = true;
    }
    return flag;
}

std::unique_ptr<Knight> StrongholdPieces::getNextComponent(Piece&, PieceList& list, RandomSource& random, int index, Orientation facing, int x, int y,
                                                           int z) {
    if (!hasMoreLimitedPieces()) return nullptr;
    // repeat up to 5 times if we're not getting the right thing
    for (int i = 0; i < 5; i++) {
        int counter = random.nextInt(totalWeight);
        for (std::size_t k = 0; k < pieceList.size(); ++k) {
            auto piece = pieceList[k];
            counter -= piece->pieceWeight;
            if (counter < 0) {
                if (!piece->isDeepEnough(index) || piece == tLastPieceMade) break;
                // we're here!
                auto component = piece->factory(index, facing, x, y, z);
                if (::gf::structure::bases::findCollisionPiece(list, component->bbox) == nullptr) {
                    ++piece->instancesSpawned;
                    if (!piece->canSpawnMoreStructures()) pieceList.erase(pieceList.begin() + static_cast<std::ptrdiff_t>(k));
                    tLastPieceMade = piece;
                    return component;
                }
            }
        }
    }
    // dead end?
    auto deadEnd = std::make_unique<DeadEnd>(index, facing, x, y, z);
    if (::gf::structure::bases::findCollisionPiece(list, deadEnd->bbox) == nullptr) return deadEnd;
    return nullptr;
}

// ==== StrongholdEntranceComponent =================================================================================

Entrance::Entrance() : Knight(kEntrance) {}

Entrance::Entrance(int i, int x, int y, int z) : Knight(kEntrance) { init(i, Orientation::South, x, y - 10, z); }

BlockBox Entrance::generateBoundingBox(Orientation facing, int x, int y, int z) { return orientBox(x, y, z, -1, -1, 0, 18, 7, 18, facing); }

namespace {
bool listContainsBossRoom(PieceList const& list) {
    return std::any_of(list.begin(), list.end(), [](auto const& p) { return p && p->typeId == kBossRoom; });
}
} // namespace

void Entrance::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // make a random component in each direction
    lowerPieces.prepareStructurePieces();
    addNewComponent(parent, list, random, Rotation::None, 4, 1, 18);
    lowerPieces.prepareStructurePieces();
    if (listContainsBossRoom(list)) lowerPieces.markBossRoomUsed();
    addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 13);
    lowerPieces.prepareStructurePieces();
    if (listContainsBossRoom(list)) lowerPieces.markBossRoomUsed();
    addNewComponent(parent, list, random, Rotation::Clockwise180, 13, 1, -1);
    lowerPieces.prepareStructurePieces();
    if (listContainsBossRoom(list)) lowerPieces.markBossRoomUsed();
    addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 1, 4);
    if (!listContainsBossRoom(list)) gf::log().error("Did not find boss room from exit 3 - EPIC FAIL");
    // add the upper stronghold
    auto  accessChamber = std::make_unique<AccessChamber>(2, orientation, bbox.minX + 8, bbox.minY + 7, bbox.minZ + 4);
    auto* raw           = accessChamber.get();
    list.push_back(std::move(accessChamber));
    raw->addChildren(*this, list, random);
}

void Entrance::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 6, 17, rand, randomBlocks());
    // statues
    placeCornerStatue(world, 5, 1, 5, 0, sbb);
    placeCornerStatue(world, 5, 1, 12, 1, sbb);
    placeCornerStatue(world, 12, 1, 5, 2, sbb);
    placeCornerStatue(world, 12, 1, 12, 3, sbb);
    // statues
    placeWallStatue(world, 9, 1, 16, Rotation::None, sbb);
    placeWallStatue(world, 1, 1, 9, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 8, 1, 1, Rotation::Clockwise180, sbb);
    placeWallStatue(world, 16, 1, 8, Rotation::Counterclockwise90, sbb);
    // doors
    placeDoors(world, sbb);
}

// ==== StrongholdAccessChamberComponent ============================================================================

AccessChamber::AccessChamber() : Knight(kAccessChamber) {}
AccessChamber::AccessChamber(int i, Orientation facing, int x, int y, int z) : Knight(kAccessChamber) { init(i, facing, x, y, z); }

BlockBox AccessChamber::generateBoundingBox(Orientation facing, int x, int y, int z) { return orientBox(x, y, z, -4, 1, 0, 9, 5, 9, facing); }

void AccessChamber::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // make a random component in each direction
    addNewUpperComponent(parent, list, random, Rotation::None, 4, 1, 9);
    addNewUpperComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 4);
    addNewUpperComponent(parent, list, random, Rotation::Clockwise180, 4, 1, -1);
    addNewUpperComponent(parent, list, random, Rotation::Counterclockwise90, 9, 1, 4);
}

void AccessChamber::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    randomFill(world, sbb, 0, 0, 0, 8, 4, 8, true, rand, randomBlocks());
    // doors
    placeSmallDoorwayAt(world, 0, 4, 1, 8, sbb);
    placeSmallDoorwayAt(world, 1, 0, 1, 4, sbb);
    placeSmallDoorwayAt(world, 2, 4, 1, 0, sbb);
    placeSmallDoorwayAt(world, 3, 8, 1, 4, sbb);
    // shaft down
    fill(world, sbb, 2, -2, 2, 6, 0, 6, J("minecraft:mossy_stone_bricks"), air(), false);
    fillAir(world, sbb, 3, -2, 3, 5, 2, 5);
    // stairs surrounding shaft
    auto const& stair = *d().stairState;
    fill(world, sbb, 2, 0, 3, 2, 0, 6, getStairState(stair, W(Rotation::Clockwise180), false), air(), false);
    fill(world, sbb, 6, 0, 2, 6, 0, 6, getStairState(stair, W(Rotation::None), false), air(), false);
    fill(world, sbb, 3, 0, 2, 5, 0, 2, getStairState(stair, W(Rotation::Counterclockwise90), false), air(), false);
    fill(world, sbb, 3, 0, 6, 5, 0, 6, getStairState(stair, W(Rotation::Clockwise90), false), air(), false);
    // pillar
    placeBlock(world, *d().pillarState, 2, 0, 2, sbb);
    // pedestal
    placeBlock(world, J("gloamingforest:trophy_pedestal"), 2, 1, 2, sbb);
    // block point
    fill(world, sbb, 2, -1, 2, 6, -1, 6, J("gloamingforest:stronghold_shield"), air(), false);
}

// ==== the simple lower pieces =======================================================================================

SmallHallway::SmallHallway() : Knight(kSmallHallway) {}
SmallHallway::SmallHallway(int i, Orientation facing, int x, int y, int z) : Knight(kSmallHallway) { init(i, facing, x, y, z); }
BlockBox SmallHallway::generateBoundingBox(Orientation facing, int x, int y, int z) {
    return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 9, 7, 18, facing);
}
void SmallHallway::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // entrance
    addDoor(4, 1, 0);
    // make a random component at the end
    addNewComponent(parent, list, random, Rotation::None, 4, 1, 18);
}
void SmallHallway::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 17, rand, randomBlocks());
    // statues
    placeWallStatue(world, 1, 1, 9, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 7, 1, 9, Rotation::Counterclockwise90, sbb);
    // doors
    placeDoors(world, sbb);
}

LeftTurn::LeftTurn() : Knight(kLeftTurn) {}
LeftTurn::LeftTurn(int i, Orientation facing, int x, int y, int z) : Knight(kLeftTurn) { init(i, facing, x, y, z); }
BlockBox LeftTurn::generateBoundingBox(Orientation facing, int x, int y, int z) { return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 9, 7, 9, facing); }
void     LeftTurn::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // entrance
    addDoor(4, 1, 0);
    // make a random component to the left
    addNewComponent(parent, list, random, Rotation::Counterclockwise90, 9, 1, 4);
}
void LeftTurn::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 8, rand, randomBlocks());
    // clear inside
    fillAir(world, sbb, 1, 1, 1, 7, 5, 7);
    // statue
    placeCornerStatue(world, 2, 1, 6, 1, sbb);
    // doors
    placeDoors(world, sbb);
}

RightTurn::RightTurn() : Knight(kRightTurn) {}
RightTurn::RightTurn(int i, Orientation facing, int x, int y, int z) : Knight(kRightTurn) { init(i, facing, x, y, z); }
BlockBox RightTurn::generateBoundingBox(Orientation facing, int x, int y, int z) { return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 9, 7, 9, facing); }
void     RightTurn::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // entrance
    addDoor(4, 1, 0);
    // make a random component to the right
    addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 4);
}
void RightTurn::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 8, rand, randomBlocks());
    // clear inside
    fillAir(world, sbb, 1, 1, 1, 7, 5, 7);
    // statue
    placeCornerStatue(world, 6, 1, 6, 3, sbb);
    // doors
    placeDoors(world, sbb);
}

Crossing::Crossing() : Knight(kCrossing) {}
Crossing::Crossing(int i, Orientation facing, int x, int y, int z) : Knight(kCrossing) { init(i, facing, x, y, z); }
BlockBox Crossing::generateBoundingBox(Orientation facing, int x, int y, int z) { return getComponentToAddBoundingBox(x, y, z, -13, -1, 0, 18, 7, 18, facing); }
void     Crossing::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    addDoor(13, 1, 0);
    addNewComponent(parent, list, random, Rotation::None, 4, 1, 18);
    addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 13);
    addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 1, 4);
}
void Crossing::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 6, 17, rand, randomBlocks());
    // statues
    placeCornerStatue(world, 2, 1, 2, 0, sbb);
    placeCornerStatue(world, 15, 1, 15, 3, sbb);
    // center pillar
    fillBlocksRotated(world, sbb, 8, 1, 8, 9, 5, 9, *d().pillarState, Rotation::None);
    // statues
    placeWallStatue(world, 8, 1, 7, Rotation::None, sbb);
    placeWallStatue(world, 7, 1, 9, Rotation::Counterclockwise90, sbb);
    placeWallStatue(world, 9, 1, 10, Rotation::Clockwise180, sbb);
    placeWallStatue(world, 10, 1, 8, Rotation::Clockwise90, sbb);
    // tables
    placeTableAndChairs(world, sbb, Rotation::None);
    placeTableAndChairs(world, sbb, Rotation::Clockwise90);
    placeTableAndChairs(world, sbb, Rotation::Clockwise180);
    placeTableAndChairs(world, sbb, Rotation::Counterclockwise90);
    // doors
    placeDoors(world, sbb);
}
void Crossing::placeTableAndChairs(Region& world, BlockBox const& sbb, Rotation rotation) {
    // table
    auto const& oakStairs = J("minecraft:oak_stairs");
    setBlockStateRotated(world, getStairState(oakStairs, W(Rotation::None), true), 5, 1, 3, rotation, sbb);
    setBlockStateRotated(world, getStairState(oakStairs, W(Rotation::Counterclockwise90), true), 5, 1, 4, rotation, sbb);
    setBlockStateRotated(world, getStairState(oakStairs, W(Rotation::Clockwise90), true), 6, 1, 3, rotation, sbb);
    setBlockStateRotated(world, getStairState(oakStairs, W(Rotation::Clockwise180), true), 6, 1, 4, rotation, sbb);
    // chairs
    auto const& spruce = J("minecraft:spruce_stairs");
    setBlockStateRotated(world, getStairState(spruce, W(Rotation::Counterclockwise90), false), 5, 1, 2, rotation, sbb);
    setBlockStateRotated(world, getStairState(spruce, W(Rotation::None), false), 7, 1, 3, rotation, sbb);
    setBlockStateRotated(world, getStairState(spruce, W(Rotation::Clockwise90), false), 6, 1, 5, rotation, sbb);
    setBlockStateRotated(world, getStairState(spruce, W(Rotation::Clockwise180), false), 4, 1, 4, rotation, sbb);
}

// ==== StrongholdDeadEndComponent ===================================================================================

DeadEnd::DeadEnd() : Knight(kDeadEnd) {}
DeadEnd::DeadEnd(int i, Orientation facing, int x, int y, int z) : Knight(kDeadEnd) { init(i, facing, x, y, z); }
Json DeadEnd::save() const {
    Json j            = Knight::save();
    j["chestTrapped"] = chestTrapped;
    return j;
}
void DeadEnd::load(Json const& data) {
    Knight::load(data);
    chestTrapped = data.value("chestTrapped", false);
}
BlockBox DeadEnd::generateBoundingBox(Orientation facing, int x, int y, int z) { return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 9, 7, 9, facing); }
void     DeadEnd::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // entrance
    addDoor(4, 1, 0);
    chestTrapped = random.nextInt(3) == 0;
}
void DeadEnd::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 8, rand, randomBlocks());
    // statues
    placeWallStatue(world, 1, 1, 4, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 7, 1, 4, Rotation::Counterclockwise90, sbb);
    placeWallStatue(world, 4, 1, 7, Rotation::None, sbb);
    // doors
    placeDoors(world, sbb);
    // treasure
    manualTreaurePlacement(world, 4, 1, 3, Orientation::South, kLootCache, chestTrapped, sbb);
    if (chestTrapped) placeBlock(world, J("minecraft:tnt"), 4, 0, 3, sbb);
    auto const& stair = *d().stairState;
    for (int z = 2; z < 5; z++) {
        placeBlock(world, getStairState(stair, Orientation::West, false), 3, 1, z, sbb);
        placeBlock(world, getStairState(stair, Orientation::East, false), 5, 1, z, sbb);
    }
    placeBlock(world, getStairState(stair, Orientation::North, false), 4, 1, 2, sbb);
    placeBlock(world, getStairState(stair, Orientation::South, false), 4, 1, 4, sbb);
    placeBlock(world, getStairState(stair, Orientation::North, false), 4, 2, 3, sbb);
}

// ==== StrongholdBalconyRoomComponent ===============================================================================

namespace {
/// The "FIXME" entry rule of the two-level pieces: enter on the top above y -15, at the bottom
/// below -21, else by the parity of z.
bool enterBottomAt(int y, int z) {
    if (y > -15) return false;
    if (y < -21) return true;
    return (z & 1) == 0;
}
} // namespace

BalconyRoom::BalconyRoom() : Knight(kBalconyRoom) {}
BalconyRoom::BalconyRoom(int i, Orientation facing, int x, int y, int z) : Knight(kBalconyRoom) { init(i, facing, x, y, z); }
Json BalconyRoom::save() const {
    Json j           = Knight::save();
    j["enterBottom"] = enterBottom;
    return j;
}
void BalconyRoom::load(Json const& data) {
    Knight::load(data);
    enterBottom = data.value("enterBottom", false);
}
BlockBox BalconyRoom::generateBoundingBox(Orientation facing, int x, int y, int z) {
    enterBottom = enterBottomAt(y, z);
    if (enterBottom) return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 18, 14, 27, facing);
    // enter on the top
    return getComponentToAddBoundingBox(x, y, z, -13, -8, 0, 18, 14, 27, facing);
}
void BalconyRoom::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // lower left exit
    addNewComponent(parent, list, random, Rotation::None, 13, 1, 27);
    // lower middle right exit
    addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 13);
    // lower middle left exit
    addNewComponent(parent, list, random, Rotation::Clockwise180, 18, 1, 13);
    // upper left exit
    addNewComponent(parent, list, random, Rotation::None, 4, 8, 27);
    // upper close right exit
    addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 8, 4);
    // upper far left exit
    addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 8, 22);
    if (enterBottom) {
        addDoor(4, 1, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise180, 13, 8, -1);
    } else {
        addDoor(13, 8, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise180, 4, 1, -1);
    }
}
void BalconyRoom::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 13, 26, rand, randomBlocks());
    // balcony
    randomFill(world, sbb, 1, 6, 1, 16, 7, 25, false, rand, randomBlocks());
    fill(world, sbb, 4, 8, 4, 13, 8, 22, *d().fenceState, air(), false);
    fillAir(world, sbb, 5, 6, 5, 12, 8, 21);
    // stairs & pillars
    placeStairsAndPillars(world, sbb, Rotation::None);
    placeStairsAndPillars(world, sbb, Rotation::Clockwise180);
    // doors
    placeDoors(world, sbb);
}
void BalconyRoom::placeStairsAndPillars(Region& world, BlockBox const& sbb, Rotation rotation) {
    auto const& stair = *d().stairState;
    auto        st    = [&](Rotation r, bool top) -> Block const& { return getStairState(stair, W(r), top); };
    fillBlocksRotated(world, sbb, 4, 1, 4, 4, 12, 4, *d().pillarState, rotation);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 4, 1, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, false), 5, 1, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 4, 5, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 5, 5, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 4, 12, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 5, 12, 4, rotation, sbb);

    fillBlocksRotated(world, sbb, 13, 1, 4, 13, 12, 4, *d().pillarState, rotation);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 13, 1, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::None, false), 12, 1, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 13, 5, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::None, true), 12, 5, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 13, 12, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::None, true), 12, 12, 4, rotation, sbb);

    fillBlocksRotated(world, sbb, 13, 1, 8, 13, 12, 8, *d().pillarState, rotation);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 13, 1, 9, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise90, false), 13, 1, 7, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::None, false), 12, 1, 8, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 13, 5, 9, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise90, true), 13, 5, 7, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 13, 12, 9, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise90, true), 13, 12, 7, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::None, true), 12, 12, 8, rotation, sbb);

    for (int y = 1; y < 8; y++)
        for (int z = 5; z < 8; z++) {
            setBlockStateRotated(world, air(), y + 6, y + 1, z, rotation, sbb);
            setBlockStateRotated(world, st(Rotation::None, false), y + 6, y, z, rotation, sbb);
            setBlockStateRotated(world, *d().blockState, y + 6, y - 1, z, rotation, sbb);
        }
}

// ==== StrongholdTrainingRoomComponent ==============================================================================

TrainingRoom::TrainingRoom() : Knight(kTrainingRoom) {}
TrainingRoom::TrainingRoom(int i, Orientation facing, int x, int y, int z) : Knight(kTrainingRoom) { init(i, facing, x, y, z); }
BlockBox TrainingRoom::generateBoundingBox(Orientation facing, int x, int y, int z) {
    return getComponentToAddBoundingBox(x, y, z, -13, -1, 0, 18, 7, 18, facing);
}
void TrainingRoom::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    addDoor(13, 1, 0);
    addNewComponent(parent, list, random, Rotation::None, 4, 1, 18);
}
void TrainingRoom::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 6, 17, rand, randomBlocks());
    // statues
    placeCornerStatue(world, 2, 1, 2, 0, sbb);
    placeCornerStatue(world, 15, 1, 15, 3, sbb);
    // sand floor
    auto const& sand = J("minecraft:sand");
    generateMaybeBox(world, sbb, rand, 0.7F, 4, 0, 4, 8, 0, 8, sand, sand, false, false);
    generateMaybeBox(world, sbb, rand, 0.7F, 9, 0, 4, 13, 0, 8, sand, sand, false, false);
    generateMaybeBox(world, sbb, rand, 0.7F, 9, 0, 9, 13, 0, 13, sand, sand, false, false);
    // training dummies
    placeTrainingDummy(world, sbb, Rotation::None);
    placeTrainingDummy(world, sbb, Rotation::Clockwise90);
    placeTrainingDummy(world, sbb, Rotation::Clockwise180);
    // anvil pad
    auto const& cobble = J("minecraft:cobblestone");
    fill(world, sbb, 5, 0, 10, 7, 0, 12, cobble, cobble, false);
    auto const& stair = *d().stairState;
    placeBlock(world, *d().pillarState, 5, 1, 12, sbb);
    placeBlock(world, *d().pillarState, 5, 2, 12, sbb);
    placeBlock(world, *d().pillarState, 6, 1, 12, sbb);
    placeBlock(world, getStairState(stair, Orientation::East, false), 6, 2, 12, sbb);
    placeBlock(world, getStairState(stair, Orientation::East, false), 7, 1, 12, sbb);
    placeBlock(world, *d().pillarState, 5, 1, 11, sbb);
    placeBlock(world, getStairState(stair, Orientation::North, false), 5, 2, 11, sbb);
    placeBlock(world, getStairState(stair, Orientation::North, false), 5, 1, 10, sbb);
    placeBlock(world, J("minecraft:anvil"), 6, 1, 11, sbb);
    // doors
    placeDoors(world, sbb);
}
void TrainingRoom::placeTrainingDummy(Region& world, BlockBox const& sbb, Rotation rotation) {
    fillBlocksRotated(world, sbb, 5, 0, 5, 7, 0, 7, J("minecraft:sand"), rotation);
    setBlockStateRotated(world, *d().fenceState, 6, 1, 6, rotation, sbb);
    setBlockStateRotated(world, J("minecraft:birch_planks"), 6, 2, 6, rotation, sbb);
    setBlockStateRotated(world, J("minecraft:oak_fence"), 5, 2, 6, rotation, sbb);
    setBlockStateRotated(world, J("minecraft:oak_fence"), 7, 2, 6, rotation, sbb);
    setBlockStateRotated(world, J("minecraft:carved_pumpkin", Json{{"facing", blocks::directionName(getStructureRelativeRotation(rotation))}}), 6, 3, 6,
                         rotation, sbb);
}

// ==== StrongholdSmallStairsComponent ===============================================================================

SmallStairs::SmallStairs() : Knight(kSmallStairs) {}
SmallStairs::SmallStairs(int i, Orientation facing, int x, int y, int z) : Knight(kSmallStairs) { init(i, facing, x, y, z); }
Json SmallStairs::save() const {
    Json j            = Knight::save();
    j["enterBottom"]  = enterBottom;
    j["hasTreasure"]  = hasTreasure;
    j["chestTrapped"] = chestTrapped;
    return j;
}
void SmallStairs::load(Json const& data) {
    Knight::load(data);
    enterBottom  = data.value("enterBottom", false);
    hasTreasure  = data.value("hasTreasure", false);
    chestTrapped = data.value("chestTrapped", false);
}
BlockBox SmallStairs::generateBoundingBox(Orientation facing, int x, int y, int z) {
    enterBottom = enterBottomAt(y, z);
    if (enterBottom) return orientBox(x, y, z, -4, -1, 0, 9, 14, 9, facing);
    // enter on the top
    return orientBox(x, y, z, -4, -8, 0, 9, 14, 9, facing);
}
void SmallStairs::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    if (enterBottom) {
        addDoor(4, 1, 0);
        addNewComponent(parent, list, random, Rotation::None, 4, 8, 9);
    } else {
        addDoor(4, 8, 0);
        addNewComponent(parent, list, random, Rotation::None, 4, 1, 9);
    }
    hasTreasure  = random.nextBoolean();
    chestTrapped = random.nextInt(3) == 0;
}
void SmallStairs::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 13, 8, rand, randomBlocks());
    // railing
    fill(world, sbb, 1, 7, 1, 7, 7, 7, *d().platformState, air(), false);
    fillAir(world, sbb, 2, 7, 2, 6, 7, 6);
    Rotation    rotation = enterBottom ? Rotation::None : Rotation::Clockwise180;
    auto const& stair    = *d().stairState;
    // stairs
    for (int y = 1; y < 8; y++)
        for (int x = 3; x < 6; x++) {
            setBlockStateRotated(world, air(), x, y + 1, y, rotation, sbb);
            setBlockStateRotated(world, getStairState(stair, Orientation::North, false), x, y, y, rotation, sbb);
            setBlockStateRotated(world, *d().blockState, x, y - 1, y, rotation, sbb);
        }
    // treasure
    if (hasTreasure) {
        placeTreasureRotated(world, 4, 1, 6, ::gf::structure::opposite(orientation), rotation, kLootCache, chestTrapped, sbb);
        if (chestTrapped) setBlockStateRotated(world, J("minecraft:tnt"), 4, 0, 6, rotation, sbb);
        for (int z = 5; z < 8; z++) {
            setBlockStateRotated(world, getStairState(stair, Orientation::West, false), 3, 1, z, rotation, sbb);
            setBlockStateRotated(world, getStairState(stair, Orientation::East, false), 5, 1, z, rotation, sbb);
        }
        setBlockStateRotated(world, getStairState(stair, Orientation::North, false), 4, 1, 5, rotation, sbb);
        setBlockStateRotated(world, getStairState(stair, Orientation::South, false), 4, 1, 7, rotation, sbb);
        setBlockStateRotated(world, getStairState(stair, Orientation::North, false), 4, 2, 6, rotation, sbb);
    }
    if (enterBottom) placeWallStatue(world, 4, 8, 1, Rotation::Clockwise180, sbb);
    else placeWallStatue(world, 4, 8, 7, Rotation::None, sbb);
    // doors
    placeDoors(world, sbb);
}

// ==== StrongholdTreasureCorridorComponent ==========================================================================

TreasureCorridor::TreasureCorridor() : Knight(kTreasureCorridor) {}
TreasureCorridor::TreasureCorridor(int i, Orientation facing, int x, int y, int z) : Knight(kTreasureCorridor) { init(i, facing, x, y, z); }
BlockBox TreasureCorridor::generateBoundingBox(Orientation facing, int x, int y, int z) {
    return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 9, 7, 27, facing);
}
void TreasureCorridor::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // entrance
    addDoor(4, 1, 0);
    // make a random component at the end
    addNewComponent(parent, list, random, Rotation::None, 4, 1, 27);
}
void TreasureCorridor::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 26, rand, randomBlocks());
    // statues
    placeWallStatue(world, 1, 1, 9, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 1, 1, 17, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 7, 1, 9, Rotation::Counterclockwise90, sbb);
    placeWallStatue(world, 7, 1, 17, Rotation::Counterclockwise90, sbb);
    Rotation rotation = (bbox.minX ^ bbox.minZ) % 2 == 0 ? Rotation::None : Rotation::Clockwise180;
    // treasure!
    placeTreasureRotated(world, 8, 2, 13, rotation == Rotation::None ? ::gf::structure::clockwise(orientation) : ccw(orientation), rotation, kLootCache, sbb);
    // niche!
    auto const& stair = *d().stairState;
    setBlockStateRotated(world, getStairState(stair, Orientation::South, true), 8, 3, 12, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, Orientation::West, true), 8, 3, 13, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, Orientation::North, true), 8, 3, 14, rotation, sbb);
    setBlockStateRotated(world, *d().fenceState, 8, 2, 12, rotation, sbb);
    setBlockStateRotated(world, *d().fenceState, 8, 2, 14, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, Orientation::South, false), 7, 1, 12, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, Orientation::West, false), 7, 1, 13, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, Orientation::North, false), 7, 1, 14, rotation, sbb);
    // doors
    placeDoors(world, sbb);
}

// ==== StrongholdAtriumComponent ====================================================================================

Atrium::Atrium() : Knight(kAtrium) {}
Atrium::Atrium(int i, Orientation facing, int x, int y, int z) : Knight(kAtrium) { init(i, facing, x, y, z); }
Json Atrium::save() const {
    Json j           = Knight::save();
    j["enterBottom"] = enterBottom;
    return j;
}
void Atrium::load(Json const& data) {
    Knight::load(data);
    enterBottom = data.value("enterBottom", false);
}
BlockBox Atrium::generateBoundingBox(Orientation facing, int x, int y, int z) {
    enterBottom = enterBottomAt(y, z);
    if (enterBottom) return getComponentToAddBoundingBox(x, y, z, -4, -1, 0, 18, 14, 18, facing);
    // enter on the top
    return getComponentToAddBoundingBox(x, y, z, -13, -8, 0, 18, 14, 18, facing);
}
void Atrium::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    if (enterBottom) {
        addDoor(4, 1, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise180, 13, 8, -1);
    } else {
        addDoor(13, 8, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise180, 4, 1, -1);
    }
    addNewComponent(parent, list, random, Rotation::None, 13, 1, 18);
    addNewComponent(parent, list, random, Rotation::None, 4, 8, 18);
}
void Atrium::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 13, 17, rand, randomBlocks());
    // balcony
    randomFill(world, sbb, 1, 6, 1, 16, 7, 16, false, rand, randomBlocks());
    fill(world, sbb, 5, 8, 5, 12, 8, 12, *d().fenceState, air(), false);
    fillAir(world, sbb, 6, 6, 6, 11, 8, 11);
    // balcony pillars
    placeBalconyPillar(world, sbb, Rotation::None);
    placeBalconyPillar(world, sbb, Rotation::Clockwise90);
    placeBalconyPillar(world, sbb, Rotation::Clockwise180);
    placeBalconyPillar(world, sbb, Rotation::Counterclockwise90);
    // corner pillars
    randomFill(world, sbb, 1, 1, 1, 1, 12, 2, false, rand, randomBlocks());
    randomFill(world, sbb, 2, 1, 1, 2, 12, 1, false, rand, randomBlocks());
    randomFill(world, sbb, 16, 1, 1, 16, 12, 2, false, rand, randomBlocks());
    randomFill(world, sbb, 15, 1, 1, 15, 12, 1, false, rand, randomBlocks());
    randomFill(world, sbb, 1, 1, 15, 1, 12, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 2, 1, 16, 2, 12, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 16, 1, 15, 16, 12, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 15, 1, 16, 15, 12, 16, false, rand, randomBlocks());
    // grass
    auto const& grass = J("minecraft:grass_block");
    generateMaybeBox(world, sbb, rand, 0.5F, 6, 0, 6, 11, 0, 11, grass, grass, false, false);
    fill(world, sbb, 7, 0, 7, 10, 0, 10, grass, air(), false);
    // tree
    spawnATree(world, rand.nextInt(5), 8, 1, 8, sbb);
    // statues
    placeCornerStatue(world, 2, 8, 2, 0, sbb);
    placeCornerStatue(world, 2, 1, 15, 1, sbb);
    placeCornerStatue(world, 15, 1, 2, 2, sbb);
    placeCornerStatue(world, 15, 8, 15, 3, sbb);
    // doors
    placeDoors(world, sbb);
}
void Atrium::spawnATree(Region& world, int treeNum, int x, int y, int z, BlockBox const& sbb) {
    BlockPos pos = getBlockPosWithOffset(x, y, z);
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    char const* treeGen = nullptr; // vanilla trees go inline
    switch (treeNum) {
    case 3: treeGen = "gloamingforest:tree/gloaming_oak_tree"; break;
    case 4: treeGen = "gloamingforest:tree/rainbow_oak"; break;
    default: break;
    }
    // grow a tree with world.getRandom() (the region's random, not the piece's)
    auto& random = ::gf::structure::ext::worldRandom(world);
    for (int i = 0; i < 100; i++) {
        bool grown = treeGen ? ::gf::features::place(treeGen, world, pos, random) : ::gf::features::placeJson(vanillaTree(treeNum), world, pos, random);
        if (grown) break;
    }
}
void Atrium::placeBalconyPillar(Region& world, BlockBox const& sbb, Rotation rotation) {
    auto const& stair = *d().stairState;
    auto        st    = [&](Rotation r, bool top) -> Block const& { return getStairState(stair, W(r), top); };
    fillBlocksRotated(world, sbb, 5, 1, 5, 5, 12, 5, *d().pillarState, rotation);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 5, 1, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, false), 6, 1, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 5, 5, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 6, 5, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 5, 12, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 6, 12, 5, rotation, sbb);
}

// ==== StrongholdFoundryComponent ===================================================================================

Foundry::Foundry() : Knight(kFoundry) {}
Foundry::Foundry(int i, Orientation facing, int x, int y, int z) : Knight(kFoundry) { init(i, facing, x, y, z); }
Json Foundry::save() const {
    Json j             = Knight::save();
    j["entranceLevel"] = entranceLevel;
    return j;
}
void Foundry::load(Json const& data) {
    Knight::load(data);
    entranceLevel = data.value("entranceLevel", 0);
}
BlockBox Foundry::generateBoundingBox(Orientation facing, int x, int y, int z) {
    if (y > -15) {
        entranceLevel = 3;
        return getComponentToAddBoundingBox(x, y, z, -4, -20, 0, 18, 26, 18, facing);
    }
    if (y < -21) {
        entranceLevel = 1;
        return getComponentToAddBoundingBox(x, y, z, -4, -6, 0, 18, 26, 18, facing);
    }
    entranceLevel = 2;
    return getComponentToAddBoundingBox(x, y, z, -4, -13, 0, 18, 26, 18, facing);
}
void Foundry::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    switch (entranceLevel) {
    case 1:
        addDoor(4, 6, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 13, 13);
        addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 13, 4);
        addNewComponent(parent, list, random, Rotation::None, 13, 20, 18);
        break;
    case 2:
        addDoor(4, 13, 0);
        addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 6, 13);
        addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 20, 4);
        addNewComponent(parent, list, random, Rotation::None, 13, 13, 18);
        break;
    case 3:
        addDoor(4, 20, 0);
        addNewComponent(parent, list, random, Rotation::None, 13, 6, 18);
        addNewComponent(parent, list, random, Rotation::Clockwise90, -1, 13, 13);
        addNewComponent(parent, list, random, Rotation::Counterclockwise90, 18, 13, 4);
        break;
    default: break;
    }
}
void Foundry::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 17, 25, 17, rand, randomBlocks());
    // lava bottom
    auto const& lava = J("minecraft:lava");
    fill(world, sbb, 1, 0, 1, 16, 4, 16, lava, lava, false);
    // top ledge
    randomFill(world, sbb, 1, 19, 1, 16, 19, 16, false, rand, randomBlocks());
    fillAir(world, sbb, 2, 19, 2, 15, 19, 15);
    // middle ledge
    randomFill(world, sbb, 1, 12, 1, 16, 12, 16, false, rand, randomBlocks());
    fillAir(world, sbb, 2, 12, 2, 15, 12, 15);
    // bottom ledge
    randomFill(world, sbb, 1, 5, 1, 16, 5, 16, false, rand, randomBlocks());
    fillAir(world, sbb, 2, 5, 2, 15, 5, 15);
    // corner pillars
    randomFill(world, sbb, 1, 1, 1, 1, 24, 2, false, rand, randomBlocks());
    randomFill(world, sbb, 2, 1, 1, 2, 24, 1, false, rand, randomBlocks());
    randomFill(world, sbb, 16, 1, 1, 16, 24, 2, false, rand, randomBlocks());
    randomFill(world, sbb, 15, 1, 1, 15, 24, 1, false, rand, randomBlocks());
    randomFill(world, sbb, 1, 1, 15, 1, 24, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 2, 1, 16, 2, 24, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 16, 1, 15, 16, 24, 16, false, rand, randomBlocks());
    randomFill(world, sbb, 15, 1, 16, 15, 24, 16, false, rand, randomBlocks());
    // suspended mass
    core::jrand::LegacyRandom massRandom(rand.nextLong());
    auto const&               stone = J("minecraft:stone");
    for (int x = 4; x < 14; x++)
        for (int z = 4; z < 14; z++)
            for (int y = 8; y < 23; y++) {
                float c  = std::abs(static_cast<float>(x) - 8.5F) + std::abs(static_cast<float>(z) - 8.5F) + std::abs(static_cast<float>(y) - 18.0F);
                float f1 = massRandom.nextFloat();
                float f2 = massRandom.nextFloat();
                float r  = 5.5F + ((f1 - f2) * 3.5F);
                if (c < r) placeBlock(world, stone, x, y, z, sbb);
            }
    // drips
    for (int i = 0; i < 400; i++) {
        int dx = massRandom.nextInt(9) + 5;
        int dz = massRandom.nextInt(9) + 5;
        int dy = massRandom.nextInt(13) + 10;
        if (!blocks::is(getBlock(world, dx, dy, dz, sbb), "minecraft:air"))
            for (int y = 0; y < 3; y++) placeBlock(world, stone, dx, dy - y, dz, sbb);
    }
    // ores: redstone 8, iron 8, gold 6, glowstone 2, emerald 2, diamond 4, copper 6
    struct Ore {
        char const* id;
        int         count;
    };
    for (auto const& ore : {Ore{"minecraft:redstone_ore", 8}, Ore{"minecraft:iron_ore", 8}, Ore{"minecraft:gold_ore", 6}, Ore{"minecraft:glowstone", 2},
                            Ore{"minecraft:emerald_ore", 2}, Ore{"minecraft:diamond_ore", 4}, Ore{"minecraft:copper_ore", 6}})
        for (int i = 0; i < ore.count; i++) addOreToMass(world, sbb, massRandom, J(ore.id));
    // doors
    placeDoors(world, sbb);
}
void Foundry::addOreToMass(Region& world, BlockBox const& sbb, RandomSource& massRandom, Block const& state) {
    for (int i = 0; i < 10; i++) {
        int dx = massRandom.nextInt(9) + 5;
        int dz = massRandom.nextInt(9) + 5;
        int dy = massRandom.nextInt(13) + 10;
        if (!getBlock(world, dx, dy, dz, sbb).isAir()) {
            placeBlock(world, state, dx, dy, dz, sbb);
            // we have succeeded, stop looping
            break;
        }
    }
}

// ==== StrongholdTreasureRoomComponent ==============================================================================

TreasureRoom::TreasureRoom() : Knight(kTreasureRoom) {}
TreasureRoom::TreasureRoom(int i, Orientation facing, int x, int y, int z) : Knight(kTreasureRoom) { init(i, facing, x, y, z); }
Json TreasureRoom::save() const {
    Json j           = Knight::save();
    j["enterBottom"] = enterBottom;
    return j;
}
void TreasureRoom::load(Json const& data) {
    Knight::load(data);
    enterBottom = data.value("enterBottom", false);
}
BlockBox TreasureRoom::generateBoundingBox(Orientation facing, int x, int y, int z) { return orientBox(x, y, z, -4, -1, 0, 9, 7, 18, facing); }
void     TreasureRoom::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    addDoor(4, 1, 0);
}
void TreasureRoom::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 8, 6, 17, rand, randomBlocks());
    // statues
    placeWallStatue(world, 1, 1, 4, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 1, 1, 13, Rotation::Clockwise90, sbb);
    placeWallStatue(world, 7, 1, 4, Rotation::Counterclockwise90, sbb);
    placeWallStatue(world, 7, 1, 13, Rotation::Counterclockwise90, sbb);
    placeWallStatue(world, 4, 1, 16, Rotation::None, sbb);
    randomFill(world, sbb, 1, 1, 8, 7, 5, 9, false, rand, randomBlocks());
    auto const& bars = J("minecraft:iron_bars");
    fill(world, sbb, 3, 1, 8, 5, 4, 9, bars, bars, false);
    // spawners
    setSpawner(world, 4, 1, 4, sbb, "gloamingforest:helmet_crab");
    setSpawner(world, 4, 4, 15, sbb, "gloamingforest:helmet_crab");
    // treasure!
    manualTreaurePlacement(world, 2, 4, 13, Orientation::West, kLootRoom, false, sbb);
    manualTreaurePlacement(world, 6, 4, 13, Orientation::East, kLootRoom, false, sbb);
    // doors
    placeDoors(world, sbb);
}
void TreasureRoom::placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb) {
    auto const& bars = J("minecraft:iron_bars");
    if (x == 0 || x == getXSize()) fill(world, sbb, x, y, z - 1, x, y + 3, z + 1, bars, air(), false);
    else fill(world, sbb, x - 1, y, z, x + 1, y + 3, z, bars, air(), false);
}

// ==== StrongholdBossRoomComponent ==================================================================================

BossRoom::BossRoom() : Knight(kBossRoom) {}
BossRoom::BossRoom(int i, Orientation facing, int x, int y, int z) : Knight(kBossRoom) {
    init(i, facing, x, y, z);
    spawnListIndex = INT_MAX;
}
BlockBox BossRoom::generateBoundingBox(Orientation facing, int x, int y, int z) { return getComponentToAddBoundingBox(x, y, z, -13, -1, 0, 27, 7, 27, facing); }
void     BossRoom::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    addDoor(13, 1, 0);
}
void BossRoom::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeStrongholdWalls(world, sbb, 0, 0, 0, 26, 6, 26, rand, randomBlocks());
    // inner walls
    randomFill(world, sbb, 1, 1, 1, 3, 5, 25, false, rand, randomBlocks());
    randomFill(world, sbb, 23, 1, 1, 25, 5, 25, false, rand, randomBlocks());
    randomFill(world, sbb, 4, 1, 1, 22, 5, 3, false, rand, randomBlocks());
    randomFill(world, sbb, 4, 1, 23, 22, 5, 25, false, rand, randomBlocks());
    // obsidian filler
    auto const& obsidian = J("minecraft:obsidian");
    fill(world, sbb, 1, 1, 1, 2, 5, 25, obsidian, obsidian, false);
    fill(world, sbb, 24, 1, 1, 25, 5, 25, obsidian, obsidian, false);
    fill(world, sbb, 4, 1, 1, 22, 5, 2, obsidian, obsidian, false);
    fill(world, sbb, 4, 1, 24, 22, 5, 25, obsidian, obsidian, false);
    // corner pillars
    randomFill(world, sbb, 4, 1, 4, 4, 5, 7, false, rand, randomBlocks());
    randomFill(world, sbb, 5, 1, 4, 5, 5, 5, false, rand, randomBlocks());
    randomFill(world, sbb, 6, 1, 4, 7, 5, 4, false, rand, randomBlocks());
    randomFill(world, sbb, 4, 1, 19, 4, 5, 22, false, rand, randomBlocks());
    randomFill(world, sbb, 5, 1, 21, 5, 5, 22, false, rand, randomBlocks());
    randomFill(world, sbb, 6, 1, 22, 7, 5, 22, false, rand, randomBlocks());
    randomFill(world, sbb, 22, 1, 4, 22, 5, 7, false, rand, randomBlocks());
    randomFill(world, sbb, 21, 1, 4, 21, 5, 5, false, rand, randomBlocks());
    randomFill(world, sbb, 19, 1, 4, 20, 5, 4, false, rand, randomBlocks());
    randomFill(world, sbb, 22, 1, 19, 22, 5, 22, false, rand, randomBlocks());
    randomFill(world, sbb, 21, 1, 21, 21, 5, 22, false, rand, randomBlocks());
    randomFill(world, sbb, 19, 1, 22, 20, 5, 22, false, rand, randomBlocks());
    // pillar decorations (stairs)
    placePillarDecorations(world, sbb, Rotation::None);
    placePillarDecorations(world, sbb, Rotation::Clockwise90);
    placePillarDecorations(world, sbb, Rotation::Clockwise180);
    placePillarDecorations(world, sbb, Rotation::Counterclockwise90);
    // sarcophagi
    placeSarcophagus(world, sbb, 8, 1, 8, Rotation::None);
    placeSarcophagus(world, sbb, 13, 1, 8, Rotation::None);
    placeSarcophagus(world, sbb, 18, 1, 8, Rotation::None);
    placeSarcophagus(world, sbb, 8, 1, 15, Rotation::None);
    placeSarcophagus(world, sbb, 13, 1, 15, Rotation::None);
    placeSarcophagus(world, sbb, 18, 1, 15, Rotation::None);
    // doorway
    fillAir(world, sbb, 12, 1, 1, 14, 4, 2);
    auto const& bars = J("minecraft:iron_bars");
    fill(world, sbb, 12, 1, 3, 14, 4, 3, bars, bars, false);
    // spawner
    placeBlock(world, J("gloamingforest:knight_phantom_boss_spawner"), 13, 2, 13, sbb);
    // doors
    placeDoors(world, sbb);
}
void BossRoom::placeSarcophagus(Region& world, BlockBox const& sbb, int x, int y, int z, Rotation rotation) {
    auto const& pillar = *d().pillarState;
    setBlockStateRotated(world, pillar, x - 1, y, z, rotation, sbb);
    setBlockStateRotated(world, pillar, x + 1, y, z + 3, rotation, sbb);
    setBlockStateRotated(world, pillar, x + 1, y, z, rotation, sbb);
    setBlockStateRotated(world, pillar, x - 1, y, z + 3, rotation, sbb);
    // make either torches or fence posts (world.getRandom(): the region's random)
    auto&       worldRandom = ::gf::structure::ext::worldRandom(world);
    auto const& torch       = J("minecraft:torch");
    setBlockStateRotated(world, worldRandom.nextInt(7) == 0 ? torch : *d().fenceState, x + 1, y + 1, z, rotation, sbb);
    setBlockStateRotated(world, worldRandom.nextInt(7) == 0 ? torch : *d().fenceState, x - 1, y + 1, z, rotation, sbb);
    setBlockStateRotated(world, worldRandom.nextInt(7) == 0 ? torch : *d().fenceState, x + 1, y + 1, z + 3, rotation, sbb);
    setBlockStateRotated(world, worldRandom.nextInt(7) == 0 ? torch : *d().fenceState, x - 1, y + 1, z + 3, rotation, sbb);
    auto const& stair = *d().stairState;
    setBlockStateRotated(world, getStairState(stair, W(Rotation::Clockwise90), false), x, y, z, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, W(Rotation::Counterclockwise90), false), x, y, z + 3, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, W(Rotation::Clockwise180), false), x + 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, W(Rotation::Clockwise180), false), x + 1, y, z + 2, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, W(Rotation::None), false), x - 1, y, z + 1, rotation, sbb);
    setBlockStateRotated(world, getStairState(stair, W(Rotation::None), false), x - 1, y, z + 2, rotation, sbb);
    auto const& slab = J("minecraft:smooth_stone_slab");
    setBlockStateRotated(world, slab, x, y + 1, z + 1, rotation, sbb);
    setBlockStateRotated(world, slab, x, y + 1, z + 2, rotation, sbb);
}
void BossRoom::placePillarDecorations(Region& world, BlockBox const& sbb, Rotation rotation) {
    auto const& stair = *d().stairState;
    auto        st    = [&](Rotation r, bool top) -> Block const& { return getStairState(stair, W(r), top); };
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 4, 1, 8, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, false), 8, 1, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 4, 5, 8, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 8, 5, 4, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, false), 5, 1, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, false), 6, 1, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, false), 6, 1, 5, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Counterclockwise90, true), 5, 5, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 6, 5, 6, rotation, sbb);
    setBlockStateRotated(world, st(Rotation::Clockwise180, true), 6, 5, 5, rotation, sbb);
}
void BossRoom::placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb) {
    auto const& bars = J("minecraft:iron_bars");
    if (x == 0 || x == getXSize()) fill(world, sbb, x, y, z - 1, x, y + 3, z + 1, bars, air(), false);
    else fill(world, sbb, x - 1, y, z, x + 1, y + 3, z, bars, air(), false);
}

// ==== the upper ruins ================================================================================================

UpperAscender::UpperAscender() : Knight(kUpperAscender) {}
UpperAscender::UpperAscender(int i, Orientation facing, int x, int y, int z) : Knight(kUpperAscender) { init(i, facing, x, y, z); }
Json UpperAscender::save() const {
    Json j       = Knight::save();
    j["exitTop"] = exitTop;
    return j;
}
void UpperAscender::load(Json const& data) {
    Knight::load(data);
    exitTop = data.value("exitTop", false);
}
BlockBox UpperAscender::generateBoundingBox(Orientation facing, int x, int y, int z) {
    // TFDimensionData.SEALEVEL (0) + 5
    if (y < 5) {
        exitTop = true;
        return orientBox(x, y, z, -2, -1, 0, 5, 10, 10, facing);
    }
    exitTop = false;
    return orientBox(x, y, z, -2, -6, 0, 5, 10, 10, facing);
}
void UpperAscender::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    // make a random component on the other side
    addNewUpperComponent(parent, list, random, Rotation::None, 2, exitTop ? 6 : 1, 10);
}
void UpperAscender::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    placeUpperStrongholdWalls(world, sbb, 0, 0, 0, 4, 9, 9, rand, randomBlocks());
    // entrance doorway
    placeSmallDoorwayAt(world, 2, 2, exitTop ? 1 : 6, 0, sbb);
    // exit doorway
    placeSmallDoorwayAt(world, 0, 2, exitTop ? 6 : 1, 9, sbb);
    // steps!
    if (exitTop) {
        makeStairsAt(world, 1, 3, Orientation::North, sbb);
        makeStairsAt(world, 2, 4, Orientation::North, sbb);
        makeStairsAt(world, 3, 5, Orientation::North, sbb);
        makeStairsAt(world, 4, 6, Orientation::North, sbb);
        makeStairsAt(world, 5, 7, Orientation::North, sbb);
        makePlatformAt(world, 5, 8, sbb);
    } else {
        makeStairsAt(world, 1, 6, Orientation::South, sbb);
        makeStairsAt(world, 2, 5, Orientation::South, sbb);
        makeStairsAt(world, 3, 4, Orientation::South, sbb);
        makeStairsAt(world, 4, 3, Orientation::South, sbb);
        makeStairsAt(world, 5, 2, Orientation::South, sbb);
        makePlatformAt(world, 5, 1, sbb);
    }
}
void UpperAscender::makeStairsAt(Region& world, int y, int z, Orientation facing, BlockBox const& sbb) {
    // check walls
    if (!getBlock(world, 0, y, z, sbb).isAir() || !getBlock(world, 4, y, z, sbb).isAir())
        for (int x = 1; x < 4; x++) placeBlock(world, getStairState(J("minecraft:stone_brick_stairs"), facing, false), x, y, z, sbb);
}
void UpperAscender::makePlatformAt(Region& world, int y, int z, BlockBox const& sbb) {
    // check walls
    if (!getBlock(world, 0, y, z, sbb).isAir() || !getBlock(world, 4, y, z, sbb).isAir())
        for (int x = 1; x < 4; x++) placeBlock(world, J("minecraft:stone_bricks"), x, y, z, sbb);
}

UpperSmall::UpperSmall(std::string typeId) : Knight(std::move(typeId)) {}
UpperSmall::UpperSmall(std::string typeId, int i, Orientation facing, int x, int y, int z) : Knight(std::move(typeId)) { init(i, facing, x, y, z); }
BlockBox UpperSmall::generateBoundingBox(Orientation facing, int x, int y, int z) {
    if (typeId == kUpperCorridor) return orientBox(x, y, z, -2, -1, 0, 5, 5, 9, facing);
    return orientBox(x, y, z, -2, -1, 0, 5, 5, 5, facing);
}
void UpperSmall::addChildren(Piece& parent, PieceList& list, RandomSource& random) {
    Knight::addChildren(parent, list, random);
    if (typeId == kUpperCorridor) {
        // make a random component at the end
        addNewUpperComponent(parent, list, random, Rotation::None, 2, 1, 9);
    } else if (typeId == kUpperLeftTurn) {
        // make a random component to the left
        addNewUpperComponent(parent, list, random, Rotation::Counterclockwise90, 5, 1, 2);
    } else if (typeId == kUpperRightTurn) {
        // make a random component to the right
        addNewUpperComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 2);
    } else {
        addNewUpperComponent(parent, list, random, Rotation::Counterclockwise90, 5, 1, 2);
        addNewUpperComponent(parent, list, random, Rotation::Clockwise90, -1, 1, 2);
    }
}
void UpperSmall::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    if (typeId == kUpperCorridor) {
        placeUpperStrongholdWalls(world, sbb, 0, 0, 0, 4, 4, 8, rand, randomBlocks());
        // entrance doorway
        placeSmallDoorwayAt(world, 2, 2, 1, 0, sbb);
        // end
        placeSmallDoorwayAt(world, 2, 2, 1, 8, sbb);
        return;
    }
    placeUpperStrongholdWalls(world, sbb, 0, 0, 0, 4, 4, 4, rand, randomBlocks());
    // entrance doorway
    placeSmallDoorwayAt(world, 2, 2, 1, 0, sbb);
    // left turn doorway
    if (typeId == kUpperLeftTurn || typeId == kUpperTIntersection) placeSmallDoorwayAt(world, 3, 4, 1, 2, sbb);
    // right turn doorway
    if (typeId == kUpperRightTurn || typeId == kUpperTIntersection) placeSmallDoorwayAt(world, 1, 0, 1, 2, sbb);
}

// ==== StrongholdShieldStructure =====================================================================================

ShieldStructure::ShieldStructure() : Knight(kShield) { spawnListIndex = -1; }

void ShieldStructure::load(Json const& data) {
    Knight::load(data);
    spawnListIndex = -1;
}

void ShieldStructure::postProcess(Region& world, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    auto shield = [](char const* f) -> Block const& { return J("gloamingforest:stronghold_shield", Json{{"facing", f}}); };
    auto const& down  = shield("down");
    auto const& up    = shield("up");
    auto const& north = shield("north");
    auto const& south = shield("south");
    auto const& west  = shield("west");
    auto const& east  = shield("east");
    int         xs    = bbox.maxX - bbox.minX + 1;
    int         ys    = bbox.maxY - bbox.minY + 1;
    int         zs    = bbox.maxZ - bbox.minZ + 1;
    // +x
    fill(world, sbb, xs, 0, 0, xs, ys, zs, west, west, false);
    // -x
    fill(world, sbb, 0, 0, 0, 0, ys, zs, east, east, false);
    // +z
    fill(world, sbb, 0, 0, zs, xs, ys, zs, north, north, false);
    // -z
    fill(world, sbb, 0, 0, 0, xs, ys, 0, south, south, false);
    // top
    fill(world, sbb, 0, 0, 0, xs, 0, zs, up, up, false);
    // bottom
    fill(world, sbb, 0, ys, 0, xs, ys, zs, down, down, false);
}

// ==== the structure =====================================================================================================

void layout(RandomSource& firstPieceRandom, RandomSource& contextRandom, int x, int y, int z, PieceList& pieces) {
    // new StrongholdEntranceComponent(0, x, y + random.nextInt(3) == 0 ? 5 : 1, z): Java's precedence
    // compares (y + rand(3)) with 0
    int  entranceY = (y + firstPieceRandom.nextInt(3)) == 0 ? 5 : 1;
    auto entrance  = std::make_unique<Entrance>(0, x, entranceY, z);
    auto* raw      = entrance.get();
    pieces.push_back(std::move(entrance));
    raw->addChildren(*raw, pieces, contextRandom);
}

void registerPieces() {
    static bool done = false;
    if (done) return;
    done = true;
    ::gf::structure::bases::registerDecorator("DecoStronghold", [] { return strongholdDecorator(); });
    auto reg = [](char const* type, auto make) {
        ::gf::structure::registerPieceType(type, [make](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = make();
            p->load(saved);
            return p;
        });
    };
    reg(kEntrance, [] { return std::make_unique<Entrance>(); });
    reg(kAccessChamber, [] { return std::make_unique<AccessChamber>(); });
    reg(kSmallHallway, [] { return std::make_unique<SmallHallway>(); });
    reg(kLeftTurn, [] { return std::make_unique<LeftTurn>(); });
    reg(kRightTurn, [] { return std::make_unique<RightTurn>(); });
    reg(kCrossing, [] { return std::make_unique<Crossing>(); });
    reg(kDeadEnd, [] { return std::make_unique<DeadEnd>(); });
    reg(kBalconyRoom, [] { return std::make_unique<BalconyRoom>(); });
    reg(kTrainingRoom, [] { return std::make_unique<TrainingRoom>(); });
    reg(kSmallStairs, [] { return std::make_unique<SmallStairs>(); });
    reg(kTreasureCorridor, [] { return std::make_unique<TreasureCorridor>(); });
    reg(kAtrium, [] { return std::make_unique<Atrium>(); });
    reg(kFoundry, [] { return std::make_unique<Foundry>(); });
    reg(kTreasureRoom, [] { return std::make_unique<TreasureRoom>(); });
    reg(kBossRoom, [] { return std::make_unique<BossRoom>(); });
    reg(kUpperAscender, [] { return std::make_unique<UpperAscender>(); });
    reg(kUpperCorridor, [] { return std::make_unique<UpperSmall>(kUpperCorridor); });
    reg(kUpperLeftTurn, [] { return std::make_unique<UpperSmall>(kUpperLeftTurn); });
    reg(kUpperRightTurn, [] { return std::make_unique<UpperSmall>(kUpperRightTurn); });
    reg(kUpperTIntersection, [] { return std::make_unique<UpperSmall>(kUpperTIntersection); });
    reg(kShield, [] { return std::make_unique<ShieldStructure>(); });
}

} // namespace gf::feature::stronghold::pieces
