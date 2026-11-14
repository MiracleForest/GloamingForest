// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/features/FeatureLogic.java
//   src/main/java/gloamingforest/util/iterators/VoxelBresenhamIterator.java
//   src/main/java/gloamingforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/gloamingforest/world/components/structures/type/HollowHillStructure.java
//   src/main/java/gloamingforest/world/components/structures/type/FallenTrunkStructure.java
//   src/main/java/gloamingforest/world/components/structures/fallentrunk/FallenTrunkPiece.java
//   src/main/java/gloamingforest/world/components/structures/fallentrunk/Hole.java
//   src/main/java/gloamingforest/world/components/structures/fallentrunk/TrunkUnderDensityFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/TanhHillFunction.java
//   src/main/java/gloamingforest/world/components/feature/GloamingSmallLakeFeature.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U18 (minorlandmarks), pure C++ (gf-core, gtested in tests/minorlandmarks): the game-independent
// maths of the minor landmarks and decorative features, Java parity (float / double split, Java's
// HashSet orders, Util.shuffle / getRandom draws):
//   - FeatureLogic#translate and VoxelBresenhamIterator (hollow trees, wood roots, hollow stumps);
//   - HedgeMazeComponent#isNearRoom;
//   - HollowHillStructure#canSpawnMob's dome is F6's; its terraformer (getStructureTerraformer) is here;
//   - BoundingBox#orientBox and vanilla StructurePiece#getWorldX/Y/Z (fallen trunks);
//   - the fallen trunk's Hole, its mound bases and TrunkUnderDensityFunction;
//   - the vanilla-style lake shape of GloamingSmallLakeFeature;
//   - Util.shuffle / FeatureLogic#getRotation (ruined foundation walls).

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"

namespace gf::core::minorlandmarks {

using core::BlockBox;
using core::jrand::RandomSource;

struct Pos {
    int x = 0, y = 0, z = 0;
    friend bool operator==(Pos const&, Pos const&) = default;
    [[nodiscard]] Pos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
};

/// RandomSource#nextInt(origin, bound): origin + nextInt(bound - origin) (bound exclusive).
[[nodiscard]] int nextIntRange(RandomSource& random, int origin, int bound);

// ---- FeatureLogic / VoxelBresenhamIterator ------------------------------------------------------------
/// FeatureLogic#translate: `distance` along (angle 0..1 around, tilt 0 up .. 0.5 out .. 1 down).
[[nodiscard]] Pos translate(Pos const& pos, double distance, double angle, double tilt);
/// The voxels VoxelBresenhamIterator yields from `from` to `to`, both included, in order.
[[nodiscard]] std::vector<Pos> bresenham(Pos const& from, Pos const& to);
/// The octagonal distance upstream's trunks use: (int)(max(ax, az) + min(ax, az) * 0.5).
[[nodiscard]] constexpr int octagonalDist(int ax, int az) {
    int mx = ax > az ? ax : az, mn = ax > az ? az : ax;
    return static_cast<int>(mx + mn * 0.5);
}

// ---- hedge maze -------------------------------------------------------------------------------------------
/// HedgeMazeComponent#isNearRoom: (dx, dz) covers the maze origin cell or lies within 2 cells of a
/// room already in `rcoords` (pairs; (0, 0) entries are unset).
[[nodiscard]] bool hedgeMazeIsNearRoom(int dx, int dz, std::vector<int> const& rcoords);

// ---- density functions -------------------------------------------------------------------------------
/// HollowHillFunction.fromPos(pos, radius, heightScale) at a block (float maths).
[[nodiscard]] double hollowHillFunction(Pos const& pos, float radius, float heightScale, int x, int y, int z);
/// FocusedDensityFunction.fromPos(pos, radius, nearValue, farValue) at a block (float maths).
[[nodiscard]] double focusedDensity(Pos const& pos, float radius, float nearValue, float farValue, int x, int y, int z);
/// BoundingBox#getCenter.
[[nodiscard]] Pos boxCenter(BlockBox const& box);
/// HollowHillStructure#getStructureTerraformer for hill size 1..3 and the start's bounding box.
[[nodiscard]] double hollowHillTerraformer(int hillSize, BlockBox const& startBox, int x, int y, int z);

/// TanhHillFunction (the fallen trunk's mounds).
struct TanhHill {
    float centerX = 0, bottomY = 0, centerZ = 0, radius = 1, heightScale = 1;
    float cosAngleBiasDirection = 1, sinAngleBiasDirection = 0;
    bool  isXOriented = false, isOnRightSide = false;
    /// The constructor taking the bias angle (Mth.cos / Mth.sin of it).
    [[nodiscard]] static TanhHill withAngle(float cx, float by, float cz, float radius, float heightScale, float angle, bool xOriented, bool rightSide);
    [[nodiscard]] double compute(int blockX, int blockY, int blockZ) const;
    [[nodiscard]] double compute(float dX, float dY, float dZ) const;
};

// ---- vanilla piece geometry ------------------------------------------------------------------------------
/// Java's horizontal Direction by 2D data value: 0 south, 1 west, 2 north, 3 east.
enum class Dir { South = 0, West = 1, North = 2, East = 3 };
/// Direction.Plane.HORIZONTAL order (NORTH, EAST, SOUTH, WEST) by index.
[[nodiscard]] Dir horizontalByIndex(int index);
[[nodiscard]] Dir clockwise(Dir d);
[[nodiscard]] Dir opposite(Dir d);
/// BoundingBox#orientBox.
[[nodiscard]] BlockBox orientBox(int x, int y, int z, int offX, int offY, int offZ, int width, int height, int depth, Dir facing);
/// Vanilla StructurePiece#getWorldX / getWorldY / getWorldZ for an orientation (never null here).
[[nodiscard]] Pos vanillaWorldPos(BlockBox const& box, Dir orientation, int x, int y, int z);

// ---- fallen trunk ------------------------------------------------------------------------------------------
inline constexpr int kTrunkErodedLength = 2;
inline constexpr int kTrunkTerraformPieceSize = 10;

/// FallenTrunkPiece's geometry (its box, orientation, length, radius) and its Hole.
class FallenTrunk {
public:
    FallenTrunk(BlockBox box, Dir orientation, int length, int radius, std::int64_t holeSeed);

    BlockBox     box;
    Dir          orientation;
    int          length;
    int          radius;
    std::int64_t holeSeed;

    /// getSideLength: radius 1 -> 2, else radius * 2 - 1.
    [[nodiscard]] int sideLength() const { return radius == 1 ? 2 : radius * 2 - 1; }
    [[nodiscard]] Pos worldPos(int x, int y, int z) const { return vanillaWorldPos(box, orientation, x, y, z); }
    /// convertLengthToXY.
    [[nodiscard]] std::array<int, 2> lengthToXY(int length) const;
    /// Hole#isInHole (sizeXY = sideLength * 4, sizeZ = length - 6).
    [[nodiscard]] bool inHole(int xy, int z) const;
    [[nodiscard]] int  holeSizeXY() const { return mSizeXY; }
    [[nodiscard]] int  holeSizeZ() const { return mSizeZ; }
    /// getAllAbsoluteHoleBlockPos in Java's HashSet<BlockPos> iteration order.
    [[nodiscard]] std::vector<Pos> holeBlocks() const;
    [[nodiscard]] bool              isHoleBlock(Pos const& p) const;
    /// getAllPotentialBaseMoundBlockPos(rightSide) / getAllowedBaseMoundBlockPos(rightSide), each in
    /// Java's HashSet iteration order (the array Util.getRandom picks from).
    [[nodiscard]] std::vector<Pos> potentialMoundBases(bool rightSide) const;
    [[nodiscard]] std::vector<Pos> allowedMoundBases(bool rightSide) const;
    /// Whether the hole generation hit upstream's 1000 tries (Java logs an error).
    [[nodiscard]] bool holeGaveUp() const { return mGaveUp; }

private:
    int                            mSizeXY = 0, mSizeZ = 0;
    std::vector<std::vector<bool>> mHole; ///< [xy][z]
    bool                           mGaveUp = false;
};

/// TrunkUnderDensityFunction for a fallen trunk piece (its Rigid: the piece box, BEARD_THIN, delta 0).
class TrunkUnderDensity {
public:
    TrunkUnderDensity(FallenTrunk const& piece, bool isBigTree, int minMounds, int maxMounds);
    [[nodiscard]] double compute(int x, int y, int z) const;
    [[nodiscard]] std::vector<TanhHill> const& mounds() const { return mMounds; }
    /// flatSurroundingTerrain(x, y, z).
    [[nodiscard]] static double flatSurroundingTerrain(double x, double y, double z);

private:
    [[nodiscard]] double moundsContribution(int x, int y, int z) const;
    BlockBox              mBox;
    bool                  mBig;
    bool                  mXOriented;
    std::vector<TanhHill> mMounds;
};

// ---- lakes ------------------------------------------------------------------------------------------------
/// GloamingSmallLakeFeature's shape: 4..7 ellipsoids in a 16 x 8 x 16 box, booleans[(x * 16 + z) * 8 + y].
[[nodiscard]] std::array<bool, 2048> lakeShape(RandomSource& random);
/// Whether (x, y, z) borders the shape (not in it, a face neighbour in it).
[[nodiscard]] bool lakeBorder(std::array<bool, 2048> const& shape, int x, int y, int z);

// ---- foundations ------------------------------------------------------------------------------------------
/// Java's Rotation (NONE, CLOCKWISE_90, CLOCKWISE_180, COUNTERCLOCKWISE_90).
enum class Rot { None = 0, Clockwise90 = 1, Clockwise180 = 2, Counterclockwise90 = 3 };
/// FeatureLogic#wallVolumeRotation (nullopt: not on a wall); draws Util.shuffle then Util.getRandom.
[[nodiscard]] std::optional<Rot> wallVolumeRotation(RandomSource& random, int deltaX, int deltaZ, int xBound, int zBound);

} // namespace gf::core::minorlandmarks
