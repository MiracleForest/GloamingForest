// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponentOld.java
//   src/main/java/twilightforest/loot/TFLootTables.java (generateChest, generateChestContents)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/bases/ComponentOld.h"

#include <algorithm>
#include <climits>

#include "mc/world/level/block/Block.h"

#include "feature/structure/Internal.h"
#include "game/Blocks.h"

namespace gf::structure::bases {

namespace {

/// The piece's orientation, rotation and mirror, restored after a "fake base mode" call.
struct SavedMode {
    Orientation orientation;
    Rotation    rotation;
    Mirror      mirror;
};

/// TFLootTables.generateChestContents seed: level.getSeed() * x + y ^ z.
std::int64_t chestSeed(Region& region, BlockPos const& pos) {
    auto s = static_cast<std::uint64_t>(region.seed()) * static_cast<std::uint64_t>(static_cast<std::int64_t>(pos.x)) +
             static_cast<std::uint64_t>(static_cast<std::int64_t>(pos.y));
    return static_cast<std::int64_t>(s) ^ static_cast<std::int64_t>(pos.z);
}

} // namespace

ComponentOld::ComponentOld(std::string typeId, int depth, int x, int y, int z) : Component(std::move(typeId), depth, BlockBox{x, y, z, x, y, z}) {}
ComponentOld::ComponentOld(std::string typeId, int depth, BlockBox const& box) : Component(std::move(typeId), depth, box) {}

void ComponentOld::setOrientation(std::optional<Orientation> facing) {
    orientation = facing.value_or(kNoOrientation);
    mirror      = Mirror::None;
    if (!facing) {
        rotation = Rotation::None;
        return;
    }
    switch (*facing) {
    case Orientation::South:
        rotation = Rotation::Clockwise180;
        break;
    case Orientation::West:
        rotation = Rotation::Counterclockwise90;
        break;
    case Orientation::East:
        rotation = Rotation::Clockwise90;
        break;
    default:
        rotation = Rotation::None;
    }
}

std::optional<Orientation> ComponentOld::getOrientation() const {
    return hasOrientation(orientation) ? std::optional<Orientation>(orientation) : std::nullopt;
}

BlockBox ComponentOld::getComponentToAddBoundingBox2(int x, int y, int z, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Orientation dir) {
    switch (dir) {
    case Orientation::West:
        return BlockBox::of(x - maxZ - minZ, y + minY, z + minX, x - minZ, y + maxY + minY, z + maxX + minX);
    case Orientation::North:
        return BlockBox::of(x - maxX - minX, y + minY, z - maxZ - minZ, x - minX, y + maxY + minY, z - minZ);
    case Orientation::East:
        return BlockBox::of(x + minZ, y + minY, z - maxX, x + maxZ + minZ, y + maxY + minY, z - minX);
    default:
        return BlockBox::of(x + minX, y + minY, z + minZ, x + maxX + minX, y + maxY + minY, z + maxZ + minZ);
    }
}

Piece* ComponentOld::findIntersectingExcluding(PieceList const& list, BlockBox const& box, Piece const* exclude) {
    for (auto const& p : list)
        if (p && p.get() != exclude && p->bbox.intersects(box)) return p.get();
    return nullptr;
}

BlockPos ComponentOld::getBlockPosWithOffset(int x, int y, int z) const { return BlockPos{worldX(x, z), worldY(y), worldZ(x, z)}; }

// ---- the "fake base mode" helpers: turn the orientation, act, restore --------------------------------------

#define GF_FAKE_BASE_MODE(rot)                                                                                                    \
    auto*     self_ = const_cast<ComponentOld*>(this);                                                                          \
    SavedMode saved_{self_->orientation, self_->rotation, self_->mirror};                                                         \
    if (hasOrientation(saved_.orientation)) self_->setOrientation(rotateDir(saved_.orientation, rot));
#define GF_RESTORE_BASE_MODE()                                                                                                    \
    self_->orientation = saved_.orientation;                                                                                      \
    self_->rotation    = saved_.rotation;                                                                                         \
    self_->mirror      = saved_.mirror;

Block const& ComponentOld::getBlockStateFromPosRotated(Region& region, int x, int y, int z, BlockBox const& sbb, Rotation rotationsCW) const {
    GF_FAKE_BASE_MODE(rotationsCW)
    Block const& ret = getBlock(region, x, y, z, sbb);
    GF_RESTORE_BASE_MODE()
    return ret;
}

void ComponentOld::replaceAirAndLiquidDownwardsRotated(Region& region, Block const& block, int x, int y, int z, Rotation rot, BlockBox const& sbb) {
    GF_FAKE_BASE_MODE(rot)
    fillColumnDown(region, block, x, y, z, sbb);
    GF_RESTORE_BASE_MODE()
}

int ComponentOld::getXWithOffsetRotated(int x, int z, Rotation rotationsCW) const {
    GF_FAKE_BASE_MODE(rotationsCW)
    int ret = worldX(x, z);
    GF_RESTORE_BASE_MODE()
    return ret;
}

int ComponentOld::getZWithOffsetRotated(int x, int z, Rotation rotationsCW) const {
    GF_FAKE_BASE_MODE(rotationsCW)
    int ret = worldZ(x, z);
    GF_RESTORE_BASE_MODE()
    return ret;
}

void ComponentOld::setBlockStateRotated(Region& region, Block const& block, int x, int y, int z, Rotation rotationsCW, BlockBox const& sbb) {
    GF_FAKE_BASE_MODE(rotationsCW)
    placeBlock(region, block, x, y, z, sbb);
    GF_RESTORE_BASE_MODE()
}

void ComponentOld::fillBlocksRotated(Region& region, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Block const& block,
                                     Rotation rot) {
    GF_FAKE_BASE_MODE(rot)
    fill(region, sbb, minX, minY, minZ, maxX, maxY, maxZ, block, block, false);
    GF_RESTORE_BASE_MODE()
}

void ComponentOld::randomlyFillBlocksRotated(Region& region, BlockBox const& sbb, RandomSource& random, float chance, int minX, int minY, int minZ, int maxX,
                                             int maxY, int maxZ, Block const& block1, Block const& block2, Rotation rot) {
    GF_FAKE_BASE_MODE(rot)
    generateMaybeBox(region, sbb, random, chance, minX, minY, minZ, maxX, maxY, maxZ, block1, block2, false, true);
    GF_RESTORE_BASE_MODE()
}

void ComponentOld::fillAirRotated(Region& region, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Rotation rot) {
    GF_FAKE_BASE_MODE(rot)
    fillAir(region, sbb, minX, minY, minZ, maxX, maxY, maxZ);
    GF_RESTORE_BASE_MODE()
}

void ComponentOld::setSpawnerRotated(Region& region, int x, int y, int z, Rotation rot, std::string const& entityId, BlockBox const& sbb) {
    GF_FAKE_BASE_MODE(rot)
    setSpawner(region, x, y, z, sbb, entityId);
    GF_RESTORE_BASE_MODE()
}

void ComponentOld::placeTreasureRotated(Region& region, int x, int y, int z, Orientation facing, Rotation rot, std::string const& tableId,
                                        BlockBox const& sbb) {
    placeTreasureRotated(region, x, y, z, facing, rot, tableId, false, sbb);
}

void ComponentOld::placeTreasureRotated(Region& region, int x, int y, int z, Orientation facing, Rotation rot, std::string const& tableId, bool trapped,
                                        BlockBox const& sbb) {
    int      dx = getXWithOffsetRotated(x, z, rot);
    int      dy = worldY(y);
    int      dz = getZWithOffsetRotated(x, z, rot);
    BlockPos pos{dx, dy, dz};
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    if (blocks::is(region.getBlock(pos), trapped ? "minecraft:trapped_chest" : "minecraft:chest")) return;
    blockentities::container(region, pos, trapped ? "minecraft:trapped_chest" : "minecraft:chest", facing, tableId, chestSeed(region, pos));
}

#undef GF_FAKE_BASE_MODE
#undef GF_RESTORE_BASE_MODE

// ---- beardifier --------------------------------------------------------------------------------------------

BlockBox          ComponentOld::getBeardifierBox() const { return bbox; }
TerrainAdjustment ComponentOld::getTerrainAdjustment() const { return TerrainAdjustment::None; }
int               ComponentOld::getGroundLevelDelta() const { return 0; }

// ---- spawners, chests, signs, tripwires ------------------------------------------------------------------

void ComponentOld::setSpawner(Region& region, int x, int y, int z, BlockBox const& sbb, std::string const& entityId,
                              std::function<void(Json&)> const& modifier) {
    setSpawnerInWorld(region, sbb, entityId, modifier, BlockPos{worldX(x, z), worldY(y), worldZ(x, z)});
}

void ComponentOld::setSpawnerInWorld(Region& region, BlockBox const& sbb, std::string const& entityId, std::function<void(Json&)> const& modifier,
                                     BlockPos const& pos) {
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    Json data = Json::object();
    if (modifier) modifier(data);
    blockentities::spawner(region, pos, entityId, data);
}

void ComponentOld::surroundBlockCardinal(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb) {
    placeBlock(region, block, x, y, z - 1, sbb);
    placeBlock(region, block, x, y, z + 1, sbb);
    placeBlock(region, block, x - 1, y, z, sbb);
    placeBlock(region, block, x + 1, y, z, sbb);
}

void ComponentOld::surroundBlockCardinalRotated(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb) {
    placeBlock(region, blocks::withJava(block, Json{{"facing", "north"}}), x, y, z - 1, sbb);
    placeBlock(region, blocks::withJava(block, Json{{"facing", "south"}}), x, y, z + 1, sbb);
    placeBlock(region, blocks::withJava(block, Json{{"facing", "west"}}), x - 1, y, z, sbb);
    placeBlock(region, blocks::withJava(block, Json{{"facing", "east"}}), x + 1, y, z, sbb);
}

void ComponentOld::surroundBlockCorners(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb) {
    placeBlock(region, block, x - 1, y, z - 1, sbb);
    placeBlock(region, block, x - 1, y, z + 1, sbb);
    placeBlock(region, block, x + 1, y, z - 1, sbb);
    placeBlock(region, block, x + 1, y, z + 1, sbb);
}

void ComponentOld::placeTreasureAtCurrentPosition(Region& region, int x, int y, int z, std::string const& tableId, BlockBox const& sbb) {
    placeTreasureAtCurrentPosition(region, x, y, z, tableId, false, sbb);
}

void ComponentOld::placeTreasureAtCurrentPosition(Region& region, int x, int y, int z, std::string const& tableId, bool trapped, BlockBox const& sbb) {
    placeTreasureAtWorldPosition(region, tableId, trapped, sbb, BlockPos{worldX(x, z), worldY(y), worldZ(x, z)});
}

void ComponentOld::placeTreasureAtWorldPosition(Region& region, std::string const& tableId, bool trapped, BlockBox const& sbb, BlockPos const& pos) {
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    if (blocks::is(region.getBlock(pos), trapped ? "minecraft:trapped_chest" : "minecraft:chest")) return;
    // TFLootTables.generateChest(world, pos, getOrientation(), trapped, table)
    blockentities::container(region, pos, trapped ? "minecraft:trapped_chest" : "minecraft:chest", getOrientation().value_or(Orientation::North), tableId,
                             chestSeed(region, pos));
}

void ComponentOld::manualTreaurePlacement(Region& region, int x, int y, int z, Orientation facing, std::string const& tableId, bool trapped,
                                          BlockBox const& sbb) {
    BlockPos lootPos{worldX(x, z), worldY(y), worldZ(x, z)};
    auto const& chest = blocks::java(trapped ? "minecraft:trapped_chest" : "minecraft:chest", Json{{"type", "left"}, {"facing", blocks::directionName(facing)}});
    placeBlock(region, chest, x, y, z, sbb);
    if (sbb.contains(lootPos.x, lootPos.y, lootPos.z)) blockentities::lootTable(region, lootPos, tableId, chestSeed(region, lootPos));
}

void ComponentOld::setDoubleLootChest(Region& region, int x, int y, int z, int otherX, int otherY, int otherZ, std::optional<Orientation> facing,
                                      std::string const& tableId, BlockBox const& sbb, bool trapped) {
    setDoubleLootChest(region, x, y, z, otherX, otherY, otherZ, facing, tableId, tableId, sbb, trapped);
}

void ComponentOld::setDoubleLootChest(Region& region, int x, int y, int z, int otherX, int otherY, int otherZ, std::optional<Orientation> facing,
                                      std::string const& tableId, std::string const& secondaryTableId, BlockBox const& sbb, bool trapped) {
    Orientation dir = facing.value_or(Orientation::North);
    BlockPos    first{worldX(x, z), worldY(y), worldZ(x, z)};
    BlockPos    second{worldX(otherX, otherZ), worldY(otherY), worldZ(otherX, otherZ)};
    // world.getRandom().nextBoolean(): the generator's random is not exposed by the region, so the
    // flip is drawn from a source seeded with the first chest's position
    JavaRandom  flipRandom(pure::mthGetSeed(first.x, first.y, first.z));
    bool        flipContents = flipRandom.nextBoolean();
    char const* id           = trapped ? "minecraft:trapped_chest" : "minecraft:chest";
    placeBlock(region, blocks::java(id, Json{{"type", "left"}, {"facing", blocks::directionName(dir)}}), x, y, z, sbb);
    placeBlock(region, blocks::java(id, Json{{"type", "right"}, {"facing", blocks::directionName(dir)}}), otherX, otherY, otherZ, sbb);
    BlockPos a = flipContents ? second : first;
    BlockPos b = flipContents ? first : second;
    if (sbb.contains(a.x, a.y, a.z)) blockentities::lootTable(region, a, tableId, chestSeed(region, a));
    if (sbb.contains(b.x, b.y, b.z)) blockentities::lootTable(region, b, secondaryTableId, chestSeed(region, b));
}

void ComponentOld::placeTripwire(Region& region, int x, int y, int z, int size, Orientation facing, BlockBox const& sbb) {
    int  dx   = stepX(facing);
    int  dz   = stepZ(facing);
    auto hook = [&](Orientation f) -> Block const& { return blocks::java("minecraft:tripwire_hook", Json{{"facing", blocks::directionName(f)}}); };
    placeBlock(region, hook(opposite(facing)), x, y, z, sbb);
    placeBlock(region, hook(facing), x + dx * size, y, z + dz * size, sbb);
    Block const& tripwire = blocks::java("minecraft:tripwire");
    for (int i = 1; i < size; i++) placeBlock(region, tripwire, x + dx * i, y, z + dz * i, sbb);
}

void ComponentOld::placeSignAtCurrentPosition(Region& region, int x, int y, int z, std::string const& line0, std::string const& line1, BlockBox const& sbb) {
    BlockPos pos{worldX(x, z), worldY(y), worldZ(x, z)};
    if (!sbb.contains(pos.x, pos.y, pos.z) || blocks::is(region.getBlock(pos), "minecraft:oak_sign")) return;
    int rotationValue = data2D(getOrientation().value_or(Orientation::South)) * 4;
    region.setBlock(pos, blocks::java("minecraft:oak_sign", Json{{"rotation", std::to_string(rotationValue)}}), 2);
    // frontText.setMessage(1, line0).setMessage(2, line1)
    blockentities::sign(region, pos, {"", line0, line1, ""});
}

std::array<int, 3> ComponentOld::offsetTowerCoords(int x, int y, int z, int towerSize, Orientation direction) const {
    int dx = worldX(x, z);
    int dy = worldY(y);
    int dz = worldZ(x, z);
    switch (direction) {
    case Orientation::South:
        return {dx + 1, dy - 1, dz - towerSize / 2};
    case Orientation::West:
        return {dx + towerSize / 2, dy - 1, dz + 1};
    case Orientation::North:
        return {dx - 1, dy - 1, dz + towerSize / 2};
    case Orientation::East:
        return {dx - towerSize / 2, dy - 1, dz - 1};
    }
    return {x, y, z};
}

BlockPos ComponentOld::offsetTowerCCoords(int x, int y, int z, int towerSize, Orientation direction) const {
    auto c = offsetTowerCoords(x, y, z, towerSize, direction);
    return BlockPos{c[0], c[1], c[2]};
}

// ---- coordinates -------------------------------------------------------------------------------------------

int ComponentOld::worldX(int x, int z) const {
    if (!hasOrientation(orientation)) return x;
    switch (orientation) {
    case Orientation::South:
        return bbox.minX + x;
    case Orientation::West:
        return bbox.maxX - z;
    case Orientation::North:
        return bbox.maxX - x; // TF: added NORTH
    case Orientation::East:
        return bbox.minX + z;
    }
    return x;
}

int ComponentOld::worldZ(int x, int z) const {
    if (!hasOrientation(orientation)) return z;
    switch (orientation) {
    case Orientation::South:
        return bbox.minZ + z;
    case Orientation::West:
        return bbox.minZ + x;
    case Orientation::North:
        return bbox.maxZ - z;
    case Orientation::East:
        return bbox.maxZ - x;
    }
    return z;
}

Orientation ComponentOld::getStructureRelativeRotation(Rotation rotationsCW) const { return rotateDir(orientation, rotationsCW); }

// ---- fills with predicates -----------------------------------------------------------------------------------

void ComponentOld::fillWithAir(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax,
                               std::function<bool(Block const&)> const& predicate) {
    fillWithBlocks(region, sbb, xMin, yMin, zMin, xMax, yMax, zMax, air(), predicate);
}

void ComponentOld::fillWithBlocks(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax, Block const& block,
                                  std::function<bool(Block const&)> const& predicate) {
    fillWithBlocks(region, sbb, xMin, yMin, zMin, xMax, yMax, zMax, block, block, predicate);
}

void ComponentOld::fillWithBlocks(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax, Block const& border,
                                  Block const& interior, std::function<bool(Block const&)> const& predicate) {
    for (int y = yMin; y <= yMax; ++y)
        for (int x = xMin; x <= xMax; ++x)
            for (int z = zMin; z <= zMax; ++z) {
                if (!predicate(getBlock(region, x, y, z, sbb))) continue;
                bool isBorder = (yMin != yMax && (y == yMin || y == yMax)) || (xMin != xMax && (x == xMin || x == xMax)) ||
                                (zMin != zMax && (z == zMin || z == zMax));
                placeBlock(region, isBorder ? border : interior, x, y, z, sbb);
            }
}

BlockSelector& ComponentOld::getStrongholdStones() { return *selectors::get("twilightforest:stronghold_stones"); }

int ComponentOld::getAverageGroundLevel(Region& region, BlockBox const& box) const {
    int yTotal = 0, count = 0;
    int yStart = std::clamp(kSeaLevel, bbox.minY, bbox.maxY);
    // ChunkGenerator#getSpawnHeight: 64 (the vanilla default TF's generator inherits)
    constexpr int kSpawnHeight = 64;
    for (int z = bbox.minZ; z <= bbox.maxZ; ++z)
        for (int x = bbox.minX; x <= bbox.maxX; ++x) {
            if (!box.contains(x, yStart, z)) continue;
            yTotal += std::max(region.height(worldgen::Heightmap::WorldSurfaceWg, x, z), kSpawnHeight);
            ++count;
        }
    return count == 0 ? INT_MIN : yTotal / count;
}

int ComponentOld::findGroundLevel(Region& region, BlockBox const& sbb, int start, std::function<bool(Block const&)> const& predicate) const {
    BlockPos center = boxCenter(sbb);
    for (int y = start; y > 0; y--)
        if (predicate(region.getBlock(BlockPos{center.x, y, center.z}))) return y;
    return 0;
}

bool ComponentOld::isBoundingBoxOutsideBiomes(Region& region, std::function<bool(std::string const&)> const& predicate, BlockPos const& structurePos) const {
    for (int x = bbox.minX - 1; x <= bbox.maxX + 1; x++)
        for (int z = bbox.minZ - 1; z <= bbox.maxZ + 1; z++) {
            // getUncachedNoiseBiome(QuartPos.fromBlock(x), QuartPos.fromBlock(y), QuartPos.fromBlock(z))
            BlockPos    quart{x & ~3, structurePos.y & ~3, z & ~3};
            std::string biome = worldgen::biomeAt(quart);
            if (biome.empty()) biome = region.biome(quart);
            if (!predicate(biome)) return true;
        }
    return false;
}

Block const& ComponentOld::getStairState(Block const& stair, Orientation direction, bool isTopHalf) {
    return blocks::withJava(stair, Json{{"facing", blocks::directionName(direction)}, {"half", isTopHalf ? "top" : "bottom"}});
}

Block const& ComponentOld::getSlabState(Block const& slab, SlabType half) {
    if (half == SlabType::Double) {
        auto const& id = blocks::id(slab);
        if (id.ends_with("_slab")) {
            if (auto const* d = game::blockWith(id.substr(0, id.size() - 5) + "_double_slab")) return *d;
        }
        return blocks::withJava(slab, Json{{"type", "double"}});
    }
    return blocks::withJava(slab, Json{{"type", half == SlabType::Top ? "top" : "bottom"}});
}

Block const& ComponentOld::air() { return blocks::air(); }

} // namespace gf::structure::bases
