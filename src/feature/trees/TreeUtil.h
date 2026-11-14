// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/features/FeatureLogic.java
//   src/main/java/gloamingforest/util/features/FeaturePlacers.java
//   src/main/java/gloamingforest/util/features/FeatureUtil.java
//   src/main/java/gloamingforest/util/iterators/VoxelBresenhamIterator.java
//   src/main/java/gloamingforest/util/RootPlacer.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U3 trees: the feature helpers upstream's tree features, placers and decorators share (FeatureLogic,
// FeaturePlacers, FeatureUtil, VoxelBresenhamIterator), over a worldgen::Region. Every helper draws
// from the given random source in Java's order. Thread-safe (features run on worker threads).

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "feature/features/Features.h"

class Block;

namespace gf::feature::trees {

using Json         = nlohmann::json;
using Region       = gf::worldgen::Region;
using RandomSource = gf::worldgen::RandomSource;

/// A Java BiConsumer<BlockPos, BlockState> (trunk / leaves / decoration setters).
using Placer = std::function<void(BlockPos const& pos, Block const& block)>;

[[nodiscard]] inline BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }
[[nodiscard]] inline BlockPos above(BlockPos const& p, int n = 1) { return {p.x, p.y + n, p.z}; }
[[nodiscard]] inline BlockPos below(BlockPos const& p, int n = 1) { return {p.x, p.y - n, p.z}; }

/// Java's Direction (ordinal order DOWN, UP, NORTH, SOUTH, WEST, EAST).
enum class Dir { Down, Up, North, South, West, East };
[[nodiscard]] BlockPos    step(Dir d);
[[nodiscard]] BlockPos    rel(BlockPos const& p, Dir d, int n = 1);
[[nodiscard]] char const* dirName(Dir d);
[[nodiscard]] Dir         clockWise(Dir d);
[[nodiscard]] Dir         counterClockWise(Dir d);
/// Direction.getRandom: VALUES[nextInt(6)].
[[nodiscard]] Dir randomDir(RandomSource& random);
/// Direction.Plane.HORIZONTAL.getRandomDirection: {NORTH, EAST, SOUTH, WEST}[nextInt(4)].
[[nodiscard]] Dir randomHorizontal(RandomSource& random);
inline constexpr Dir kHorizontal[4] = {Dir::North, Dir::East, Dir::South, Dir::West};
inline constexpr Dir kAll[6]        = {Dir::Down, Dir::Up, Dir::North, Dir::South, Dir::West, Dir::East};

/// VoxelBresenhamIterator: the voxels of the line from `from` to `to` (both included), lazily.
class Bresenham {
public:
    Bresenham(BlockPos const& from, BlockPos const& to);
    /// The next voxel (false when the line is done).
    bool next(BlockPos& out);

private:
    int      mXInc, mYInc, mZInc, mDx2, mDy2, mDz2, mLength;
    int      mAxis; ///< 0 x, 1 y, 2 z
    int      mI = 0, mErr1, mErr2;
    BlockPos mVoxel;
};

/// FeatureLogic.translate: moves `distance` along (angle 0..1 of a turn, tilt 0 up .. 1 down).
[[nodiscard]] BlockPos translate(BlockPos const& pos, double distance, double angle, double tilt);

/// The block a provider gives at `pos` (nullptr: none).
[[nodiscard]] Block const* provide(Json const& provider, RandomSource& random, BlockPos const& pos);

/// TreeFeature.validTreePos: air or #minecraft:replaceable_by_trees.
[[nodiscard]] bool treeFeatureValidPos(Region& region, BlockPos const& pos);
/// FeaturePlacers.validTreePos: TreeFeature.validTreePos or #minecraft:flowers.
[[nodiscard]] bool validTreePos(Region& region, BlockPos const& pos);
/// BlockState#canBeReplaced || isAir (FeatureLogic.IS_REPLACEABLE_AIR).
[[nodiscard]] bool replaceableOrAir(Region& region, BlockPos const& pos);
/// FeatureLogic.isReplaceable.
[[nodiscard]] bool isReplaceable(Block const& block, bool includeFlowers);
[[nodiscard]] bool hasEmptyNeighborExceptBelow(Region& region, BlockPos const& pos);
[[nodiscard]] bool hasSolidNeighbor(Region& region, BlockPos const& pos);
[[nodiscard]] bool canRootGrowIn(Region& region, BlockPos const& pos);
/// FeatureUtil.hasAirAround: an air neighbour other than below.
[[nodiscard]] bool hasAirAround(Region& region, BlockPos const& pos);
/// FeatureUtil.anyBelowMatch: `pos` and the `depth` blocks below it.
[[nodiscard]] bool anyBelowMatch(BlockPos const& pos, int depth, std::function<bool(BlockPos const&)> const& predicate);
/// BlockState#isSolidRender (a full opaque cube), approximated by Bedrock's material.
[[nodiscard]] bool isSolidRender(Block const& block);
/// SaplingBlock#mayPlaceOn (canSustainPlant of a sapling): #minecraft:dirt or farmland.
[[nodiscard]] bool saplingSoil(Block const& block);
/// LevelHeightAccessor#isOutsideBuildHeight.
[[nodiscard]] inline bool outsideBuildHeight(Region& region, int y) { return y < region.minY() || y >= region.maxY(); }

/// RootPlacer: a setter and the root penetrability.
struct RootPlacer {
    Placer placer;
    int    penetrability = 1;
};

/// FeaturePlacers.placeProvidedBlock / placeIfValidTreePos.
void placeProvided(Region& region, Placer const& placer, bool (*predicate)(Region&, BlockPos const&), BlockPos const& pos, Json const& provider, RandomSource& random);
bool placeIfValidTreePos(Region& region, Placer const& placer, RandomSource& random, BlockPos const& pos, Json const& provider);
bool placeIfValidRootPos(Region& region, RootPlacer const& placer, RandomSource& random, BlockPos const& pos, Json const& provider);
/// FeaturePlacers.drawBresenhamBranch / drawBresenhamTree.
void drawBresenhamBranch(Region& region, Placer const& placer, RandomSource& random, BlockPos const& from, BlockPos const& to, Json const& provider);
/// FeaturePlacers.placeCircleOdd / placeCircleEven (with the legacy distance when asked).
void placeCircleOdd(Region& region, Placer const& placer, RandomSource& random, BlockPos const& center, float radius, Json const& provider, bool legacyDistance = false);
void placeCircleEven(Region& region, Placer const& placer, RandomSource& random, BlockPos const& center, float radius, Json const& provider, bool legacyDistance = false);
/// FeaturePlacers.placeSpheroid with a vertical bias (FoliageSetter variant: no random edges).
void placeSpheroidBiased(Region& region, Placer const& placer, RandomSource& random, BlockPos const& center, float xzRadius, float yRadius, float verticalBias, Json const& provider);
/// FeaturePlacers.placeSpheroid without bias (random edge blocks).
void placeSpheroid(Region& region, Placer const& placer, RandomSource& random, BlockPos const& center, float xzRadius, float yRadius, Json const& provider);
/// FeaturePlacers.traceRoot / traceExposedRoot over a line in progress.
void traceRoot(Region& region, RootPlacer const& placer, RandomSource& random, Json const& dirtRoot, Bresenham& line);
void traceExposedRoot(Region& region, RootPlacer const& placer, RandomSource& random, Json const& exposedRoot, Json const& dirtRoot, Bresenham& line);
/// FeaturePlacers.buildRoot.
void buildRoot(Region& region, RootPlacer const& placer, RandomSource& random, BlockPos const& start, double offset, int b, Json const& provider);

/// Sets a block through the region (flags of Block.UPDATE_KNOWN_SHAPE | UPDATE_ALL).
void setBlock(Region& region, BlockPos const& pos, Block const& block);

/// A position list that keeps Java's set semantics (no duplicates) cheaply.
class PosSet {
public:
    bool add(BlockPos const& p);
    [[nodiscard]] bool contains(BlockPos const& p) const;
    std::vector<BlockPos> list;

private:
    std::unordered_set<std::int64_t> mKeys; ///< the hash index (O(1) per insert / lookup)
};

} // namespace gf::feature::trees
