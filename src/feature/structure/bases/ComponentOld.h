// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/TFStructureComponentOld.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18).
// TFStructureComponentOld: the procedural piece base of the older upstream structures (lich tower wings,
// dark tower, aurora palace, mushroom tower, labyrinth, knight stronghold, final castle, troll
// caves, hollow hills ...): orientation without vanilla's mirror, rotated placement helpers,
// spawners, treasure chests, tripwires, signs, ground level probing and the beardifier box.
// Method names follow the Java ones so translated pieces read like their source.

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "feature/structure/bases/Component.h"

namespace gf::structure::bases {

/// Java's SlabType.
enum class SlabType { Bottom, Top, Double };
/// Java's TerrainAdjustment (beardifier mode of a piece).
enum class TerrainAdjustment { None, Bury, BeardThin, BeardBox, Encapsulate };

class ComponentOld : public Component {
public:
    ComponentOld(std::string typeId, int genDepth, int x, int y, int z);
    ComponentOld(std::string typeId, int genDepth, BlockBox const& box);

    /// setOrientation: orientation without vanilla's mirror (SOUTH -> 180, WEST -> CCW 90,
    /// EAST -> CW 90, NORTH / none -> no rotation).
    virtual void setOrientation(std::optional<Orientation> facing);
    [[nodiscard]] std::optional<Orientation> getOrientation() const;

    /// getComponentToAddBoundingBox2: the box of a child piece at (x, y, z) facing `dir`.
    [[nodiscard]] static BlockBox
    getComponentToAddBoundingBox2(int x, int y, int z, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Orientation dir);
    /// findIntersectingExcluding: the first piece of `list` other than `exclude` intersecting `box`.
    [[nodiscard]] static Piece* findIntersectingExcluding(PieceList const& list, BlockBox const& box, Piece const* exclude);

    [[nodiscard]] BlockPos getBlockPosWithOffset(int x, int y, int z) const;
    [[nodiscard]] Block const& getBlockStateFromPosRotated(Region& region, int x, int y, int z, BlockBox const& sbb, Rotation rotationsCW) const;
    void replaceAirAndLiquidDownwardsRotated(Region& region, Block const& block, int x, int y, int z, Rotation rotation, BlockBox const& sbb);

    /// AIR (minecraft:air).
    [[nodiscard]] static Block const& air();

    // PieceBeardifierModifier
    [[nodiscard]] virtual BlockBox          getBeardifierBox() const;
    [[nodiscard]] virtual TerrainAdjustment getTerrainAdjustment() const;
    [[nodiscard]] virtual int               getGroundLevelDelta() const;

protected:
    /// Spawners (Java's setSpawner; `spawnerData` overrides the spawner's data: delay, count ...).
    void setSpawner(Region& region, int x, int y, int z, BlockBox const& sbb, std::string const& entityId,
                    std::function<void(Json& spawnerData)> const& modifier = {});
    static void setSpawnerInWorld(Region& region, BlockBox const& sbb, std::string const& entityId,
                                  std::function<void(Json& spawnerData)> const& modifier, BlockPos const& pos);
    void setSpawnerRotated(Region& region, int x, int y, int z, Rotation rotation, std::string const& entityId, BlockBox const& sbb);

    void surroundBlockCardinal(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb);
    /// The four sides, each turned to face away from the center (stairs FACING north/south/west/east).
    void surroundBlockCardinalRotated(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb);
    void surroundBlockCorners(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb);

    /// Treasure chests with loot table `tableId` ("gloamingforest:chests/..."), through gf::loot.
    void placeTreasureAtCurrentPosition(Region& region, int x, int y, int z, std::string const& tableId, BlockBox const& sbb);
    void placeTreasureAtCurrentPosition(Region& region, int x, int y, int z, std::string const& tableId, bool trapped, BlockBox const& sbb);
    void placeTreasureAtWorldPosition(Region& region, std::string const& tableId, bool trapped, BlockBox const& sbb, BlockPos const& pos);
    void placeTreasureRotated(Region& region, int x, int y, int z, Orientation facing, Rotation rotation, std::string const& tableId, BlockBox const& sbb);
    void placeTreasureRotated(Region& region, int x, int y, int z, Orientation facing, Rotation rotation, std::string const& tableId, bool trapped,
                              BlockBox const& sbb);
    /// manualTreaurePlacement (sic): a chest facing `facing` without the rotation helpers.
    void manualTreaurePlacement(Region& region, int x, int y, int z, Orientation facing, std::string const& tableId, bool trapped, BlockBox const& sbb);
    void setDoubleLootChest(Region& region, int x, int y, int z, int otherX, int otherY, int otherZ, std::optional<Orientation> facing,
                            std::string const& tableId, BlockBox const& sbb, bool trapped);
    void setDoubleLootChest(Region& region, int x, int y, int z, int otherX, int otherY, int otherZ, std::optional<Orientation> facing,
                            std::string const& tableId, std::string const& secondaryTableId, BlockBox const& sbb, bool trapped);

    /// A tripwire of `size` blocks between two hooks, along `facing`.
    void placeTripwire(Region& region, int x, int y, int z, int size, Orientation facing, BlockBox const& sbb);
    void placeSignAtCurrentPosition(Region& region, int x, int y, int z, std::string const& line0, std::string const& line1, BlockBox const& sbb);

    /// offsetTowerCoords / offsetTowerCCoords: where a tower of `towerSize` attaches at (x, y, z).
    [[nodiscard]] virtual std::array<int, 3> offsetTowerCoords(int x, int y, int z, int towerSize, Orientation direction) const;
    [[nodiscard]] virtual BlockPos           offsetTowerCCoords(int x, int y, int z, int towerSize, Orientation direction) const;

    /// getWorldX / getWorldZ with upstream's own orientation handling (no mirror).
    [[nodiscard]] int worldX(int x, int z) const override;
    [[nodiscard]] int worldZ(int x, int z) const override;
    [[nodiscard]] int getXWithOffsetRotated(int x, int z, Rotation rotationsCW) const;
    [[nodiscard]] int getZWithOffsetRotated(int x, int z, Rotation rotationsCW) const;
    /// getStructureRelativeRotation: this piece's orientation turned by `rotationsCW`.
    [[nodiscard]] Orientation getStructureRelativeRotation(Rotation rotationsCW) const;

    void setBlockStateRotated(Region& region, Block const& block, int x, int y, int z, Rotation rotationsCW, BlockBox const& sbb);
    void fillBlocksRotated(Region& region, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Block const& block, Rotation rotation);
    void randomlyFillBlocksRotated(Region& region, BlockBox const& sbb, RandomSource& random, float chance, int minX, int minY, int minZ, int maxX,
                                   int maxY, int maxZ, Block const& block1, Block const& block2, Rotation rotation);
    void fillAirRotated(Region& region, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ, Rotation rotation);
    /// fillWithAir / fillWithBlocks: only where `predicate(existing block)` holds.
    void fillWithAir(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax,
                     std::function<bool(Block const&)> const& predicate);
    void fillWithBlocks(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax, Block const& block,
                        std::function<bool(Block const&)> const& predicate);
    void fillWithBlocks(Region& region, BlockBox const& sbb, int xMin, int yMin, int zMin, int xMax, int yMax, int zMax, Block const& border,
                        Block const& interior, std::function<bool(Block const&)> const& predicate);

    /// getStrongholdStones: the shared stronghold stones random block selector.
    [[nodiscard]] static BlockSelector& getStrongholdStones();
    /// getAverageGroundLevel: the mean surface height under `box` (-1 when nothing was sampled).
    [[nodiscard]] int getAverageGroundLevel(Region& region, BlockBox const& box) const;
    /// findGroundLevel: the first y going down from `start` whose block satisfies `predicate`.
    [[nodiscard]] int findGroundLevel(Region& region, BlockBox const& sbb, int start, std::function<bool(Block const&)> const& predicate) const;
    /// isBoundingBoxOutsideBiomes: whether a corner of the box lies in a biome `predicate` rejects.
    [[nodiscard]] bool isBoundingBoxOutsideBiomes(Region& region, std::function<bool(std::string const& biomeId)> const& predicate,
                                                  BlockPos const& structurePos) const;

    /// getStairState / getSlabState: a stair facing `direction` (top half or not) / a slab half.
    [[nodiscard]] static Block const& getStairState(Block const& stair, Orientation direction, bool isTopHalf);
    [[nodiscard]] static Block const& getSlabState(Block const& slab, SlabType half);

};

} // namespace gf::structure::bases
