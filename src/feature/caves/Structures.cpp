// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveConnectComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveGardenComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollVaultComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudCastleComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudTreeComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCloudComponent.java
//   src/main/java/gloamingforest/world/components/structures/YetiCaveComponent.java
//   src/main/java/gloamingforest/world/components/structures/type/TrollCaveStructure.java
//   src/main/java/gloamingforest/world/components/structures/type/YetiCaveStructure.java
//   src/main/java/gloamingforest/util/HugeMushroomUtil.java
//   src/main/java/gloamingforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The procedural pieces of the troll caves, the cloud castle and the yeti cave (rule R19: exact
// translations), and the two landmark generators for F6.
#include "feature/caves/Structures.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/caves/Caves.h"
#include "core/jrand/JRand.h"
#include "feature/ext/caves/W0Internals.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace st     = gf::structure;
namespace blocks = gf::structure::blocks;
using st::bases::TerrainAdjustment;

namespace {

constexpr char kMain[]       = "gloamingforest:TFTCMai";
constexpr char kConnect[]    = "gloamingforest:TFTCCon";
constexpr char kGarden[]     = "gloamingforest:TFTCGard";
constexpr char kVault[]      = "gloamingforest:TFTCVa";
constexpr char kCastle[]     = "gloamingforest:TFClCa";
constexpr char kCloud[]      = "gloamingforest:TFCloud";
constexpr char kCloudTree[]  = "gloamingforest:TFClTr";
constexpr char kTrollCloud[] = "gloamingforest:TFTCloud";
constexpr char kYeti[]       = "gloamingforest:TFYeti";

/// RotationUtil.ROTATIONS (Rotation.values()).
constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

bool gGardenBiomeCheck = true;

/// The configured features of other units (their feature types): skipped while the selftests run
/// the biome check off, so U27's goldens do not move when those units register their types.
bool externalFeature(std::string const& id) {
    static std::set<std::string> const ids{"gloamingforest:uberous_soil_patch_big", "gloamingforest:uberous_soil_patch_small",
                                           "gloamingforest:troll_cave_dirt", "gloamingforest:troll_cave_mycelium"};
    return ids.contains(id);
}

bool placeFeature(std::string const& id, Region& region, BlockPos const& pos, RandomSource& rand) {
    if (!gGardenBiomeCheck && externalFeature(id)) return false;
    return gf::features::place(id, region, pos, rand);
}

/// BoundingBoxUtils.getComponentToAddBoundingBox(..., centerBounds = false).
BlockBox componentToAddBoundingBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir) {
    switch (dir) {
    case Orientation::West: return BlockBox{x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX};
    case Orientation::North: return BlockBox{x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ};
    case Orientation::East: return BlockBox{x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX};
    default: return BlockBox{x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ};
    }
}

/// TFStructureComponentOld#setOrientation's rotation of an orientation.
Rotation rotationOf(Orientation o) {
    switch (o) {
    case Orientation::South: return Rotation::Clockwise180;
    case Orientation::West: return Rotation::Counterclockwise90;
    case Orientation::East: return Rotation::Clockwise90;
    default: return Rotation::None;
    }
}

/// HugeMushroomBlock's six faces.
struct Faces {
    bool up = false, down = false, north = false, south = false, east = false, west = false;
};

/// HugeMushroomUtil.HugeMushroomType.
enum class MushroomType { Center, North, South, East, West, NorthWest, NorthEast, SouthWest, SouthEast };

Faces facesOf(MushroomType t) {
    switch (t) {
    case MushroomType::Center: return {true, false, false, false, false, false};
    case MushroomType::North: return {true, false, true, false, false, false};
    case MushroomType::South: return {true, false, false, true, false, false};
    case MushroomType::East: return {true, false, false, false, true, false};
    case MushroomType::West: return {true, false, false, false, false, true};
    case MushroomType::NorthWest: return {true, false, true, false, false, true};
    case MushroomType::NorthEast: return {true, false, true, false, true, false};
    case MushroomType::SouthWest: return {true, false, false, true, false, true};
    case MushroomType::SouthEast: return {true, false, false, true, true, false};
    }
    return {};
}

/// HugeMushroomBlock#rotate: the face that pointed d now points rotation.rotate(d).
Faces rotateFaces(Faces f, Rotation rotation) {
    bool        side[4] = {f.north, f.east, f.south, f.west}; // N E S W
    int         turn    = st::rotationIndex(rotation);
    bool        out[4]{};
    for (int i = 0; i < 4; i++) out[(i + turn) & 3] = side[i];
    return {f.up, f.down, out[0], out[2], out[1], out[3]};
}

/// Bedrock's huge_mushroom_bits of a vanilla cap with these faces (the HugeMushroomUtil types and
/// their rotations: one cap skin on top plus up to two adjacent sides).
int capBits(Faces const& f) {
    if (f.up && f.down && f.north && f.south && f.east && f.west) return 14;
    if (!f.up) return 0;
    if (f.north) return f.west ? 1 : (f.east ? 3 : 2);
    if (f.south) return f.west ? 7 : (f.east ? 9 : 8);
    if (f.west) return 4;
    if (f.east) return 6;
    return 5;
}

/// HugeMushroomUtil.getState(type, base) as the Bedrock block, turned by `rotation` (the rotation the
/// piece's placeBlock applies; F7's block rotation does not turn huge mushroom faces).
Block const& mushroomState(std::string const& baseId, MushroomType type, Rotation rotation) {
    Faces f = rotateFaces(facesOf(type), rotation);
    if (baseId == "gloamingforest:huge_mushgloom") {
        if (auto const* b = game::blockWithStates(baseId, Json{{"gloamingforest:up", f.up},
                                                                {"gloamingforest:down", f.down},
                                                                {"gloamingforest:north", f.north},
                                                                {"gloamingforest:south", f.south},
                                                                {"gloamingforest:east", f.east},
                                                                {"gloamingforest:west", f.west}}))
            return *b;
        return blocks::java(baseId);
    }
    if (auto const* b = game::blockWithStates(baseId, Json{{"huge_mushroom_bits", capBits(f)}})) return *b;
    return blocks::java(baseId);
}

/// Java's left-to-right `a.nextInt(n) - a.nextInt(n)`.
int diff(RandomSource& r, int n) {
    int a = r.nextInt(n);
    int b = r.nextInt(n);
    return a - b;
}

} // namespace

void setGardenBiomeCheckForTest(bool enabled) { gGardenBiomeCheck = enabled; }

// ---- TrollCaveMainComponent ------------------------------------------------------------------------------

TrollCaveMain::TrollCaveMain(std::string typeId, int index, int x, int y, int z, std::string config)
: ComponentOld(std::move(typeId), index, x, y, z),
  configId(std::move(config)) {
    setOrientation(Orientation::South); // DEPTH_AVERAGE
    size       = 30;
    height     = 20;
    int radius = size / 2;
    bbox       = componentToAddBoundingBox(x, y, z, -radius, -height, -radius, size, height, size, Orientation::South);
}

Json TrollCaveMain::save() const {
    Json j         = ComponentOld::save();
    j["size"]      = size;
    j["height"]    = height;
    j["config_id"] = configId;
    return j;
}

void TrollCaveMain::load(Json const& data) {
    ComponentOld::load(data);
    size     = data.value("size", size);
    height   = data.value("height", height);
    configId = data.value("config_id", configId);
}

TerrainAdjustment TrollCaveMain::getTerrainAdjustment() const { return TerrainAdjustment::Bury; }

std::int64_t TrollCaveMain::decoSeed(Region& region) const {
    // world.getSeed() + (minX * 321534781L) ^ (minZ * 756839L): + binds tighter than ^
    auto a = static_cast<std::uint64_t>(region.seed()) + static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minX) * 321534781LL);
    auto b = static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minZ) * 756839LL);
    return static_cast<std::int64_t>(a ^ b);
}

void TrollCaveMain::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    // make 4 caves
    for (Rotation caveRotation : kRotations) {
        BlockPos dest = getValidOpening(rand, caveRotation);
        makeSmallerCave(list, rand, genDepth + 1, dest.x, dest.y, dest.z, 18, 15, caveRotation);
    }
    // add vault
    auto  vault = std::make_unique<TrollVault>(genDepth + 1, bbox.minX + ((bbox.maxX - bbox.minX) / 2), bbox.minY, bbox.minZ + ((bbox.maxZ - bbox.minZ) / 2));
    auto* raw   = vault.get();
    list.push_back(std::move(vault));
    raw->addChildren(*this, list, rand);
}

bool TrollCaveMain::makeSmallerCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    BlockPos    dest      = offsetTowerCCoords(x, y, z, caveSize, direction);
    auto        cave      = std::make_unique<TrollCaveConnect>(index, dest.x, dest.y, dest.z, caveSize, caveHeight, direction, configId);
    // check to see if it intersects something already there
    Piece* intersect = st::bases::findCollisionPiece(list, cave->bbox);
    if (intersect == nullptr || intersect == this) {
        auto* raw = cave.get();
        list.push_back(std::move(cave));
        raw->addChildren(*this, list, rand);
        return true;
    }
    return false;
}

void TrollCaveMain::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    core::jrand::LegacyRandom decoRNG(decoSeed(region));
    // clear inside
    hollowCaveMiddle(region, sbb, rand, 0, 0, 0, size - 1, height - 1, size - 1);
    placeSpeleothems(region, rand, sbb, decoRNG);
    // uberous!
    for (int i = 0; i < 32; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        generateAtSurface(region, "gloamingforest:uberous_soil_patch_big", decoRNG, dest.x, dest.z, sbb);
    }
}

void TrollCaveMain::placeSpeleothems(Region& region, RandomSource& rand, BlockBox const& sbb, RandomSource& decoRNG) {
    decoRNG.setSeed(decoSeed(region));
    int  ceilingY = worldY(height);
    int  floorY   = worldY(0);
    auto bounds   = st::intersection(sbb, bbox);
    if (!bounds) return;
    for (BlockPos pos : st::speleothems::latticePositions(st::speleothems::lattice(configId), *bounds, ceilingY)) {
        // stone stalactites!
        if (!blocks::isAir(region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z})) && st::speleothems::shouldDoAStalactite(configId, rand))
            st::speleothems::spike(region, pos, configId, decoRNG, true, std::numeric_limits<int>::max());
        // stone stalagmites!
        if (st::speleothems::shouldDoAStalagmite(configId, rand))
            st::speleothems::spike(region, BlockPos{pos.x, floorY, pos.z}, configId, decoRNG, false, std::numeric_limits<int>::max());
    }
}

BlockPos TrollCaveMain::getCoordsInCave(RandomSource& rand) const {
    int x = rand.nextInt(size - 1);
    int y = rand.nextInt(height - 1);
    int z = rand.nextInt(size - 1);
    return BlockPos{x, y, z};
}

void TrollCaveMain::hollowCaveMiddle(Region& region, BlockBox const& sbb, RandomSource& rand, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    int          threshold = size / 5;
    Block const& trollsteinn = blocks::java("gloamingforest:trollsteinn");
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            for (int z = minZ; z <= maxZ; ++z) {
                double dist = core::caves::hollowDistance(x, y, z, minX, minY, minZ, maxX, maxY, maxZ);
                if (blocks::inTag(getBlock(region, x, y, z, sbb), "gloamingforest:cannot_troll_cave_hollow")) continue;
                if (dist > threshold) {
                    placeBlock(region, blocks::air(), x, y, z, sbb);
                } else if (dist == threshold && rand.nextInt(4) == 0 && blocks::inTag(getBlock(region, x, y, z, sbb), "minecraft:base_stone_overworld")) {
                    placeBlock(region, trollsteinn, x, y, z, sbb);
                }
            }
        }
    }
}

BlockPos TrollCaveMain::getValidOpening(RandomSource& rand, Rotation direction) const {
    int offset  = size / 4;            // wall thickness
    int wLength = size - (offset * 2); // wall length
    // for directions 0 or 2, the wall lies along the z axis
    if (direction == Rotation::None || direction == Rotation::Clockwise180) {
        int rx = direction == Rotation::None ? size - 1 : 0;
        int rz = offset + rand.nextInt(wLength);
        return BlockPos{rx, 0, rz};
    }
    // for directions 1 or 3, the wall lies along the x axis
    int rx = offset + rand.nextInt(wLength);
    int rz = direction == Rotation::Clockwise90 ? size - 1 : 0;
    int ry = diff(rand, offset);
    return BlockPos{rx, ry, rz};
}

BlockPos TrollCaveMain::offsetTowerCCoords(int x, int y, int z, int towerSize, Orientation direction) const {
    int dx = worldX(x, z);
    int dy = worldY(y);
    int dz = worldZ(x, z);
    switch (direction) {
    case Orientation::South: return BlockPos{dx - 1, dy - 1, dz - towerSize / 2};
    case Orientation::West: return BlockPos{dx + towerSize / 2, dy - 1, dz - 1};
    case Orientation::North: return BlockPos{dx + 1, dy - 1, dz + towerSize / 2};
    case Orientation::East: return BlockPos{dx - towerSize / 2, dy - 1, dz + 1};
    }
    // ugh?
    return BlockPos{x, y, z};
}

void TrollCaveMain::generateAtSurface(Region& region, std::string const& feature, RandomSource& rand, int x, int z, BlockBox const& sbb) {
    int      dx = worldX(x, z);
    int      dz = worldZ(x, z);
    BlockPos pos{dx, st::kSeaLevel + 15, dz};
    for (int i = 0; i < 15; i++) {
        pos.y += 1;
        if (sbb.contains(pos.x, pos.y, pos.z) && blocks::isAir(region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z}))) {
            placeFeature(feature, region, pos, rand);
            break;
        }
    }
}

void TrollCaveMain::makeTreasureCrate(Region& region, BlockBox const& sbb) {
    // treasure!
    int          mid      = size / 2;
    Block const& obsidian = blocks::java("minecraft:obsidian");
    fill(region, sbb, mid - 2, 0, mid - 2, mid + 1, 3, mid + 1, obsidian, obsidian, false);
    fillAir(region, sbb, mid - 1, 1, mid - 1, mid, 2, mid);
    placeTreasureAtCurrentPosition(region, mid, 1, mid, "gloamingforest:troll_garden", false, sbb);
}

// ---- TrollCaveConnectComponent -----------------------------------------------------------------------------

TrollCaveConnect::TrollCaveConnect(int index, int x, int y, int z, int caveSize, int caveHeight, Orientation direction, std::string config)
: TrollCaveMain(kConnect, index, x, y, z, std::move(config)) {
    size   = caveSize;
    height = caveHeight;
    setOrientation(direction);
    bbox = componentToAddBoundingBox(x, y, z, 0, 0, 0, size - 1, height - 1, size - 1, direction);
}

Json TrollCaveConnect::save() const {
    Json j = TrollCaveMain::save();
    for (int i = 0; i < 4; i++) j["openingTowards" + std::to_string(i)] = openingTowards[static_cast<std::size_t>(i)];
    return j;
}

void TrollCaveConnect::load(Json const& data) {
    TrollCaveMain::load(data);
    for (int i = 0; i < 4; i++) openingTowards[static_cast<std::size_t>(i)] = data.value("openingTowards" + std::to_string(i), false);
}

void TrollCaveConnect::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    // make 4 caves
    if (genDepth < 3) {
        for (Rotation rotation : kRotations) {
            BlockPos dest = getValidOpening(rand, rotation);
            if (rand.nextBoolean() || !makeGardenCave(list, rand, genDepth + 1, dest.x, dest.y, dest.z, 30, 15, rotation))
                makeSmallerCave(list, rand, genDepth + 1, dest.x, dest.y, dest.z, 20, 15, rotation);
        }
    }
}

void TrollCaveConnect::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    // clear inside
    hollowCaveMiddle(region, sbb, rand, 0, 0, 0, size - 1, height - 1, size - 1);
    core::jrand::LegacyRandom decoRNG(decoSeed(region));
    // wall decorations
    for (Rotation rotation : kRotations)
        if (!openingTowards[static_cast<std::size_t>(st::rotationIndex(rotation))]) decorateWall(region, sbb, decoRNG, rotation);
    placeSpeleothems(region, rand, sbb, decoRNG);
    // possible treasure
    decoRNG.setSeed(decoSeed(region));
    if (countExits() == 1 && decoRNG.nextInt(3) == 0) {
        // treasure!
        makeTreasureCrate(region, sbb);
    } else if (decoRNG.nextInt(3) == 0) {
        // or a monolith!
        makeMonolith(region, decoRNG, sbb);
    }
}

void TrollCaveConnect::makeMonolith(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int          mid      = size / 2;
    int          h        = 7 + rand.nextInt(8);
    Rotation     rotation = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
    Block const& obsidian = blocks::java("minecraft:obsidian");
    fillBlocksRotated(region, sbb, mid - 1, 0, mid - 1, mid - 1, h, mid - 1, obsidian, rotation);
    fillBlocksRotated(region, sbb, mid, 0, mid - 1, mid, h - 2, mid - 1, obsidian, rotation);
    fillBlocksRotated(region, sbb, mid - 1, 0, mid, mid - 1, h - 2, mid, obsidian, rotation);
    fillBlocksRotated(region, sbb, mid, 0, mid, mid, h - 4, mid, obsidian, rotation);
}

int TrollCaveConnect::countExits() const {
    int count = 0;
    for (bool b : openingTowards)
        if (b) count++;
    return count;
}

void TrollCaveConnect::decorateWall(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation) {
    if (decoRNG.nextBoolean()) {
        decorateBracketMushrooms(region, sbb, decoRNG, rotation);
    } else if (decoRNG.nextBoolean()) {
        decorateStoneFormation(region, sbb, decoRNG, rotation);
        decorateStoneFormation(region, sbb, decoRNG, rotation);
    }
    // else: decorateStoneProjection is commented out in upstream
}

void TrollCaveConnect::decorateStoneFormation(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation) {
    int z      = 5 + decoRNG.nextInt(7);
    int startY = 1 + decoRNG.nextInt(2);
    for (int y = startY; y < height; y += 2) {
        int width = 1;
        int depth = 1 + (decoRNG.nextInt(3) == 0 ? 1 : 0);
        makeSingleStoneFormation(region, sbb, decoRNG, rotation, z, y, width, depth);
        // wiggle a little
        z += diff(decoRNG, 4);
        if (z < 5 || z > size - 5) z = 5 + decoRNG.nextInt(7);
    }
}

void TrollCaveConnect::makeSingleStoneFormation(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation, int z, int y, int width, int depth) {
    char const* id = "minecraft:stone";
    if (decoRNG.nextInt(8) == 0) id = "minecraft:obsidian";
    else if (decoRNG.nextInt(4) == 0) id = "gloamingforest:trollsteinn";
    fillBlocksRotated(region, sbb, size - (depth + 1), y - width, z - width, size - 1, y + width, z + width, blocks::java(id), rotation);
}

void TrollCaveConnect::decorateBracketMushrooms(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation) {
    int z      = 5 + decoRNG.nextInt(7);
    int startY = 1 + decoRNG.nextInt(4);
    for (int y = startY; y < height; y += 2) {
        int width = 1 + decoRNG.nextInt(2);
        width += decoRNG.nextInt(2);
        int depth = 1 + decoRNG.nextInt(2);
        depth += decoRNG.nextInt(2);
        char const* mushBlock = "minecraft:red_mushroom_block";
        if (decoRNG.nextInt(3) == 0) mushBlock = "gloamingforest:huge_mushgloom";
        else if (decoRNG.nextBoolean()) mushBlock = "minecraft:brown_mushroom_block";
        makeSingleBracketMushroom(region, sbb, rotation, z, y, width, depth, mushBlock);
        // wiggle a little
        z += diff(decoRNG, 4);
        if (z < 5 || z > size - 5) z = 5 + decoRNG.nextInt(7);
    }
}

void TrollCaveConnect::makeSingleBracketMushroom(Region& region, BlockBox const& sbb, Rotation rotation, int z, int y, int width, int depth, std::string const& mushBlock) {
    // the rotation placeBlock applies while fillBlocksRotated / setBlockStateRotated fake the orientation
    Rotation placed = rotationOf(getStructureRelativeRotation(rotation));
    auto     state  = [&](MushroomType t) -> Block const& { return mushroomState(mushBlock, t, placed); };
    fillBlocksRotated(region, sbb, size - depth, y, z - (width - 1), size - 2, y, z + (width - 1), state(MushroomType::Center), rotation);
    fillBlocksRotated(region, sbb, size - (depth + 1), y, z - (width - 1), size - (depth + 1), y, z + (width - 1), state(MushroomType::East), rotation);
    Block const& northMushroom = state(MushroomType::South);
    for (int d = 0; d < (depth - 1); d++) setBlockStateRotated(region, northMushroom, size - (2 + d), y, z - width, rotation, sbb);
    setBlockStateRotated(region, state(MushroomType::SouthEast), size - (depth + 1), y, z - width, rotation, sbb);
    Block const& southMushroom = state(MushroomType::North);
    for (int d = 0; d < (depth - 1); d++) setBlockStateRotated(region, southMushroom, size - (2 + d), y, z + width, rotation, sbb);
    setBlockStateRotated(region, state(MushroomType::NorthEast), size - (depth + 1), y, z + width, rotation, sbb);
}

bool TrollCaveConnect::makeGardenCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    BlockPos    dest      = offsetTowerCCoords(x, y, z, caveSize, direction);
    auto        cave      = std::make_unique<TrollCaveGarden>(index, dest.x, dest.y, dest.z, caveSize, caveHeight, direction, configId);
    // check to see if it intersects something already there
    Piece* intersect = st::bases::findCollisionPiece(list, cave->bbox);
    // findNearbyGarden: upstream's "large" box is the garden's box moved by -30 on every axis
    BlockBox const& b = cave->bbox;
    BlockBox        largeBox{b.minX - 30, b.minY - 30, b.minZ - 30, b.maxX - 30, b.maxY - 30, b.maxZ - 30};
    Piece*          otherGarden = nullptr;
    for (auto const& p : list)
        if (p->typeId == kGarden && st::intersection(p->bbox, largeBox)) {
            otherGarden = p.get();
            break;
        }
    if ((intersect == nullptr || intersect == this) && otherGarden == nullptr) {
        auto* raw = cave.get();
        list.push_back(std::move(cave));
        raw->addChildren(*this, list, rand);
        openingTowards[static_cast<std::size_t>(st::rotationIndex(rotation))] = true;
        return true;
    }
    return false;
}

bool TrollCaveConnect::makeSmallerCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation) {
    if (TrollCaveMain::makeSmallerCave(list, rand, index, x, y, z, caveSize, caveHeight, rotation)) {
        openingTowards[static_cast<std::size_t>(st::rotationIndex(rotation))] = true;
        return true;
    }
    return false;
}

// ---- TrollCaveGardenComponent -------------------------------------------------------------------------------

TrollCaveGarden::TrollCaveGarden(int index, int x, int y, int z, int caveSize, int caveHeight, Orientation direction, std::string config)
: TrollCaveMain(kGarden, index, x, y, z, std::move(config)) {
    size   = caveSize;
    height = caveHeight;
    setOrientation(direction);
    bbox = componentToAddBoundingBox(x, y, z, 0, 0, 0, size - 1, height - 1, size - 1, direction);
}

void TrollCaveGarden::addChildren(Piece&, PieceList&, RandomSource&) {
    // add a cloud
}

void TrollCaveGarden::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    if (gGardenBiomeCheck) {
        auto highlands = [](std::string const& biome) { return biome == "gloamingforest:highlands" || biome == "gloamingforest:highlands_underground"; };
        if (isBoundingBoxOutsideBiomes(region, highlands, pivot)) return;
    }
    // clear inside
    hollowCaveMiddle(region, sbb, rand, 0, 0, 0, size - 1, height - 1, size - 1);
    core::jrand::LegacyRandom decoRNG(decoSeed(region));
    // treasure!
    makeTreasureCrate(region, sbb);
    // dirt!
    for (int i = 0; i < 24; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        generate(region, "gloamingforest:troll_cave_dirt", decoRNG, dest.x, 0, dest.z, sbb);
    }
    // mycelium!
    for (int i = 0; i < 16; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        generate(region, "gloamingforest:troll_cave_mycelium", decoRNG, dest.x, 0, dest.z, sbb);
    }
    // uberous!
    for (int i = 0; i < 16; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        generate(region, "gloamingforest:uberous_soil_patch_small", decoRNG, dest.x, 0, dest.z, sbb);
        generateAtSurface(region, "gloamingforest:uberous_soil_patch_small", decoRNG, dest.x, dest.z, sbb);
    }
    Block const& mycelium = blocks::java("minecraft:mycelium");
    // mushglooms first
    for (int i = 0; i < 8; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        // upstream passes the piece's own rotation as the extra turn
        setBlockStateRotated(region, mycelium, dest.x, 0, dest.z, rotation, sbb);
        generate(region, "gloamingforest:troll_big_mushglooms", decoRNG, dest.x, 1, dest.z, sbb);
    }
    // mushrooms!
    for (int i = 0; i < 16; i++) {
        BlockPos dest = getCoordsInCave(decoRNG);
        setBlockStateRotated(region, mycelium, dest.x, 0, dest.z, rotation, sbb);
        char const* feature = rand.nextBoolean() ? "gloamingforest:troll_huge_brown_mushrooms" : "gloamingforest:troll_huge_red_mushrooms";
        generate(region, feature, decoRNG, dest.x, 1, dest.z, sbb);
    }
    placeSpeleothems(region, rand, sbb, decoRNG);
}

void TrollCaveGarden::generate(Region& region, std::string const& feature, RandomSource& rand, int x, int y, int z, BlockBox const& sbb) {
    BlockPos pos{worldX(x, z), worldY(y), worldZ(x, z)};
    if (sbb.contains(pos.x, pos.y, pos.z)) placeFeature(feature, region, pos, rand);
}

// ---- TrollVaultComponent -----------------------------------------------------------------------------------

TrollVault::TrollVault(int index, int x, int y, int z) : ComponentOld(kVault, index, x, y, z) {
    setOrientation(Orientation::South);
    // adjust x, y, z
    x = (x >> 2) << 2;
    y = (y / 4) * 4;
    z = (z >> 2) << 2;
    // spawn list!
    spawnListIndex = -1;
    bbox           = componentToAddBoundingBox(x, y, z, -8, 0, -8, 12, 12, 12, Orientation::South);
}

void TrollVault::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& giantObsidian = blocks::java("gloamingforest:giant_obsidian");
    Block const& cobblestone   = blocks::java("minecraft:cobblestone");
    // make walls
    fill(region, sbb, 0, 0, 0, 11, 11, 11, giantObsidian, giantObsidian, false);
    // clear inside
    fillAir(region, sbb, 4, 4, 4, 7, 7, 7);
    // cobblestone platform
    fill(region, sbb, 5, 5, 5, 6, 5, 6, cobblestone, cobblestone, false);
    // chests
    auto facing = st::clockwise(*getOrientation());
    setDoubleLootChest(region, 5, 6, 5, 5, 6, 6, facing, "gloamingforest:troll_vault", "gloamingforest:troll_vault_with_lamp", sbb, false);
    setDoubleLootChest(region, 6, 6, 5, 6, 6, 6, facing, "gloamingforest:troll_garden", sbb, false);
}

// ---- CloudCastleComponent -----------------------------------------------------------------------------------

CloudCastle::CloudCastle(int index, int x, int y, int z) : ComponentOld(kCastle, index, x, y, z) {
    setOrientation(Orientation::South);
    // round to nearest mult of 4
    x &= ~0b11;
    y &= ~0b11;
    z &= ~0b11;
    bbox = componentToAddBoundingBox(x, y, z, -8, -4, -8, 64, 16, 64, Orientation::South);
}

void CloudCastle::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    // the literal cloud
    auto cloud = std::make_unique<Cloud>(genDepth + 1, bbox.minX, bbox.minY + 3, bbox.minZ);
    list.push_back(std::move(cloud));
    // up to two trees: in the x direction
    bool plus   = rand.nextBoolean();
    int  offset = diff(rand, 5);
    list.push_back(std::make_unique<CloudTree>(genDepth + 1, bbox.minX + 8 + (plus ? 32 : -16), 168, bbox.minZ + (offset * 4)));
    // in the z direction
    plus   = rand.nextBoolean();
    offset = diff(rand, 5);
    list.push_back(std::make_unique<CloudTree>(genDepth + 1, bbox.minX + (offset * 4), 168, bbox.minZ + 8 + (plus ? 32 : -16)));
}

void CloudCastle::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& fluffy = blocks::java("gloamingforest:fluffy_cloud");
    Block const& cobble = blocks::java("gloamingforest:giant_cobblestone");
    Block const& log    = blocks::java("gloamingforest:giant_log");
    // make haus
    fill(region, sbb, 8, 0, 8, 23, 3, 23, fluffy, fluffy, false);
    fill(region, sbb, 8, 4, 8, 23, 15, 23, cobble, cobble, false);
    fill(region, sbb, 8, 16, 8, 23, 19, 23, log, log, false);
    // clear inside
    fillAir(region, sbb, 12, 4, 12, 19, 15, 19);
    // clear door
    fillAir(region, sbb, 8, 4, 12, 12, 11, 15);
    // add giants
    placeGiant(region, sbb, 14, 14, "gloamingforest:giant_miner");
    placeGiant(region, sbb, 17, 17, "gloamingforest:armored_giant");
}

void CloudCastle::placeGiant(Region& region, BlockBox const& sbb, int x, int z, char const* entityId) {
    int bx = worldX(x, z);
    int by = worldY(4);
    int bz = worldZ(x, z);
    if (!sbb.contains(bx, by, bz)) return;
    // persistent, finalized as a STRUCTURE spawn
    st::blockentities::entity(region, bx, by, bz, entityId, Json{{"persistent", true}});
}

// ---- CloudComponent ------------------------------------------------------------------------------------------

namespace {
constexpr int kCloudOffset = 16 * -2;
constexpr int kCloudWidth  = 16 * 6;
constexpr int kCloudDepth  = 8;

BlockBox cloudBox(int minX, int maxY, int minZ) {
    return BlockBox{minX & ~0b1111, maxY - kCloudDepth, minZ & ~0b1111, (minX + kCloudWidth) | 0b1111, maxY, (minZ + kCloudWidth) | 0b1111};
}
} // namespace

Cloud::Cloud(int depth, int centerX, int yHeight, int centerZ) : Piece(kCloud) {
    genDepth = depth;
    bbox     = cloudBox((centerX + kCloudOffset) & ~0b1111, yHeight, (centerZ + kCloudOffset) & ~0b1111);
}

Cloud::Cloud() : Piece(kCloud) {}

void Cloud::postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const&) {
    int chunkX     = chunkBox.minX >> 4;
    int chunkZ     = chunkBox.minZ >> 4;
    int genCenterX = bbox.minX - kCloudOffset - (chunkX << 4) + 16;
    int genCenterZ = bbox.minZ - kCloudOffset - (chunkZ << 4) + 16;
    bool         isCenter = genCenterX == 0 && genCenterZ == 0;
    Block const& wispy    = blocks::java("gloamingforest:wispy_cloud");
    Block const& fluffy   = blocks::java("gloamingforest:fluffy_cloud");
    Block const& top      = isCenter ? fluffy : wispy;
    auto         setIfAir = [&](int lx, int y, int lz, Block const& b) {
        BlockPos p{(chunkX << 4) + lx, y, (chunkZ << 4) + lz};
        if (blocks::isAir(region.getBlock(p))) region.setBlock(p, b, 2);
    };
    for (auto const& c : core::caves::generateCloud(chunkX, chunkZ, genCenterX, genCenterZ, bbox.maxY, random)) {
        if (c.full) {
            setIfAir(c.lx, c.y, c.lz, top);
            for (int d = 1; d < c.depth; d++) setIfAir(c.lx, c.y - d, c.lz, fluffy);
            setIfAir(c.lx, c.y - c.depth, c.lz, wispy);
        } else {
            for (int d = 1; d < c.depth; d++) setIfAir(c.lx, c.y - d, c.lz, wispy);
        }
    }
}

// ---- CloudTreeComponent -------------------------------------------------------------------------------------

CloudTree::CloudTree(int index, int x, int y, int z) : ComponentOld(kCloudTree, index, x, y, z) {
    setOrientation(Orientation::South);
    // adjust x, y, z
    x = (x >> 2) << 2;
    y = (y >> 2) << 2;
    z = (z >> 2) << 2;
    bbox = componentToAddBoundingBox(x, y, z, -8, 0, -8, 20, 28, 20, Orientation::South);
}

void CloudTree::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& leaves = blocks::java("gloamingforest:giant_leaves");
    Block const& log    = blocks::java("gloamingforest:giant_log");
    Block const& fluffy = blocks::java("gloamingforest:fluffy_cloud");
    // leaves
    fill(region, sbb, 0, 12, 0, 19, 19, 19, leaves, leaves, false);
    fill(region, sbb, 4, 20, 4, 15, 23, 15, leaves, leaves, false);
    fill(region, sbb, 8, 24, 4, 11, 27, 15, leaves, leaves, false);
    fill(region, sbb, 4, 24, 8, 15, 27, 11, leaves, leaves, false);
    // trunk
    fill(region, sbb, 8, 0, 8, 11, 23, 11, log, log, false);
    // cloud base
    fill(region, sbb, 8, -4, 8, 11, -1, 11, fluffy, fluffy, false);
}

// ---- TrollCloudComponent ------------------------------------------------------------------------------------

TrollCloud::TrollCloud() : ComponentOld(kTrollCloud, 0, 0, 0, 0) {}

Json TrollCloud::save() const {
    Json j      = ComponentOld::save();
    j["size"]   = size;
    j["height"] = height;
    return j;
}

void TrollCloud::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("size", 0);
    height = data.value("height", 0);
}

void TrollCloud::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // placeCloud(world, sbb, 0, 0, 0, size - 1, 6, size - 1)
    int          maxX = size - 1, maxY = 6, maxZ = size - 1;
    Block const& glass  = blocks::java("minecraft:white_stained_glass");
    Block const& quartz = blocks::java("minecraft:quartz_block");
    fill(region, sbb, 0, 0, 0, maxX, maxY, maxZ, glass, glass, false);
    fill(region, sbb, 2, 2, 2, maxX - 2, maxY - 1, maxZ - 2, quartz, quartz, false);
}

// ---- YetiCaveComponent --------------------------------------------------------------------------------------

YetiCave::YetiCave(int index, int x, int y, int z, std::string config) : HollowHill(kYeti, index, 2, x, y, z, std::move(config)) {
    // FIXME Get rid of HollowHillComponent so this ugly hack can be sanitized (upstream)
    bbox.minY -= 1;
}

// FIXME Bandaid for adjusting structure box (upstream)
int YetiCave::worldY(int y) const { return HollowHill::worldY(y + 1); }

void YetiCave::postProcess(Region& region, RandomSource& rand, BlockBox const& writeableBounds, BlockPos const&) {
    constexpr int maxRadius       = 24;
    int           bottomYRelative = worldY(4);
    BlockPos      center          = getLocatorPosition();
    drainWater(writeableBounds, region, 6, blocks::java("minecraft:cave_air"), center.x, center.z, maxRadius + 10, blocks::java("minecraft:packed_ice"),
               bottomYRelative);
    // fill in features
    for (BlockPos dest : st::speleothems::latticePositions(st::speleothems::lattice(speleothemConfigId), writeableBounds, bottomYRelative)) {
        int xDist    = std::abs(dest.x - center.x);
        int zDist    = std::abs(dest.z - center.z);
        int minDelta = std::min(xDist, zDist);
        if (xDist <= maxRadius && zDist <= maxRadius) {
            if (st::speleothems::shouldDoAStalactite(speleothemConfigId, rand))
                generateSpeleothem(region, BlockPos{dest.x, dest.y + 15 - minDelta / 6, dest.z}, writeableBounds, true);
            if (st::speleothems::shouldDoAStalagmite(speleothemConfigId, rand))
                generateSpeleothem(region, BlockPos{dest.x, dest.y - 4 + minDelta / 6, dest.z}, writeableBounds, false);
        }
    }
    // spawn alpha yeti
    setBlockStateRotated(region, blocks::java("gloamingforest:alpha_yeti_boss_spawner"), radius, 1, radius, Rotation::None, writeableBounds);
}

void YetiCave::drainWater(BlockBox const& chunkBox, Region& region, int maxDepth, Block const& airState, int xCenter, int zCenter, double radius,
                          Block const& undergroundBlock, int yStart) {
    int minY = yStart - maxDepth;
    for (int z = chunkBox.minZ; z <= chunkBox.maxZ; z++) {
        int dZ = std::abs(zCenter - z);
        for (int x = chunkBox.minX; x <= chunkBox.maxX; x++) {
            int dX = std::abs(xCenter - x);
            if (std::max(dX, dZ) >= radius) continue;
            int  maxY         = std::min(yStart, region.height(worldgen::Heightmap::OceanFloorWg, x, z) - 1);
            bool crossedFloor = false;
            for (int y = maxY; y >= minY; y--) {
                BlockPos     p{x, y, z};
                Block const& stateAt = region.getBlock(p);
                if (blocks::isWater(stateAt)) region.setBlock(p, airState, 3);
                else crossedFloor = true;
                if (crossedFloor && (blocks::is(stateAt, "minecraft:dirt") || blocks::is(stateAt, "minecraft:snow_block"))) region.setBlock(p, undergroundBlock, 3);
            }
        }
    }
}

// ---- layouts and registration ------------------------------------------------------------------------------

void layoutTrollCave(RandomSource& contextRandom, int x, int y, int z, PieceList& pieces) {
    auto  main = std::make_unique<TrollCaveMain>(kMain, 0, x, y + 11, z);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
}

void layoutYetiCave(int x, int y, int z, PieceList& pieces) { pieces.push_back(std::make_unique<YetiCave>(0, x, y + 4, z)); }

void installStructures() {
    auto loaded = [](std::unique_ptr<Piece> p, Json const& saved) {
        p->load(saved);
        return p;
    };
    st::registerPieceType(kMain, [loaded](Json const& s) { return loaded(std::make_unique<TrollCaveMain>(kMain, 0, 0, 0, 0), s); });
    st::registerPieceType(kConnect, [loaded](Json const& s) { return loaded(std::make_unique<TrollCaveConnect>(0, 0, 0, 0, 18, 15, Orientation::South), s); });
    st::registerPieceType(kGarden, [loaded](Json const& s) { return loaded(std::make_unique<TrollCaveGarden>(0, 0, 0, 0, 30, 15, Orientation::South), s); });
    st::registerPieceType(kVault, [loaded](Json const& s) { return loaded(std::make_unique<TrollVault>(0, 0, 0, 0), s); });
    st::registerPieceType(kCastle, [loaded](Json const& s) { return loaded(std::make_unique<CloudCastle>(0, 0, 0, 0), s); });
    st::registerPieceType(kCloud, [loaded](Json const& s) { return loaded(std::make_unique<Cloud>(), s); });
    st::registerPieceType(kCloudTree, [loaded](Json const& s) { return loaded(std::make_unique<CloudTree>(0, 0, 0, 0), s); });
    st::registerPieceType(kTrollCloud, [loaded](Json const& s) { return loaded(std::make_unique<TrollCloud>(), s); });
    st::registerPieceType(kYeti, [loaded](Json const& s) { return loaded(std::make_unique<YetiCave>(0, 0, 0, 0), s); });

    gf::landmark::registerGenerator(kTrollCave, {.layout = [](gf::landmark::LayoutContext& ctx) {
                                        auto        ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                        auto const& c         = ctx.start.center;
                                        layoutTrollCave(*ctxRandom, c.x, c.y, c.z, ctx.pieces);
                                        return true;
                                    }});
    gf::landmark::registerGenerator(kYetiCave, {
                                                   .layout =
                                                       [](gf::landmark::LayoutContext& ctx) {
                                                           auto        ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
                                                           auto const& c         = ctx.start.center;
                                                           layoutYetiCave(c.x, c.y, c.z, ctx.pieces);
                                                           for (std::size_t i = 0; i < ctx.pieces.size(); i++) ctx.pieces[i]->addChildren(*ctx.pieces[0], ctx.pieces, *ctxRandom);
                                                           return true;
                                                       },
                                                   .terraform =
                                                       [](gf::landmark::Start const& start, int x, int y, int z) {
                                                           auto center = st::boxCenter(start.box);
                                                           return core::caves::yetiCaveTerraformer(center.x, center.z, x, y, z);
                                                       },
                                               });
}

} // namespace gf::feature::caves
