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
#pragma once

// U25 (stronghold): the knight stronghold's piece types on F7's GloamingStructureComponentOld base (rule
// R19: procedural pieces translated exactly). Unit-internal: the structure module and its
// selftests use it.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/structure/bases/ComponentOld.h"

class Block;

namespace gf::feature::stronghold::pieces {

using ::gf::structure::BlockBox;
using ::gf::structure::Json;
using ::gf::structure::Orientation;
using ::gf::structure::Piece;
using ::gf::structure::RandomSource;
using ::gf::structure::Region;
using ::gf::structure::Rotation;
using ::gf::structure::bases::Decorator;
using ::gf::structure::bases::PieceList;

inline constexpr char kStructure[] = "gloamingforest:knight_stronghold";

inline constexpr char kEntrance[]        = "gloamingforest:GloamingSEnter";
inline constexpr char kAccessChamber[]   = "gloamingforest:GloamingSAC";
inline constexpr char kSmallHallway[]    = "gloamingforest:GloamingSSH";
inline constexpr char kLeftTurn[]        = "gloamingforest:GloamingSLT";
inline constexpr char kRightTurn[]       = "gloamingforest:GloamingSRT";
inline constexpr char kCrossing[]        = "gloamingforest:GloamingSCr";
inline constexpr char kDeadEnd[]         = "gloamingforest:GloamingSDE";
inline constexpr char kBalconyRoom[]     = "gloamingforest:GloamingSBalR";
inline constexpr char kTrainingRoom[]    = "gloamingforest:GloamingSTR";
inline constexpr char kSmallStairs[]     = "gloamingforest:GloamingSSS";
inline constexpr char kTreasureCorridor[] = "gloamingforest:GloamingSTC";
inline constexpr char kAtrium[]          = "gloamingforest:GloamingSAt";
inline constexpr char kFoundry[]         = "gloamingforest:GloamingSFo";
inline constexpr char kTreasureRoom[]    = "gloamingforest:GloamingTreaR";
inline constexpr char kBossRoom[]        = "gloamingforest:GloamingSBR";
inline constexpr char kUpperAscender[]   = "gloamingforest:GloamingSUA";
inline constexpr char kUpperCorridor[]   = "gloamingforest:GloamingSUCo";
inline constexpr char kUpperLeftTurn[]   = "gloamingforest:GloamingSULT";
inline constexpr char kUpperRightTurn[]  = "gloamingforest:GloamingSURT";
inline constexpr char kUpperTIntersection[] = "gloamingforest:GloamingSUTI";
inline constexpr char kShield[]          = "gloamingforest:GloamingSShield";

inline constexpr char kLootCache[] = "gloamingforest:stronghold_cache";
inline constexpr char kLootRoom[]  = "gloamingforest:stronghold_room";

/// StrongholdDecorator ("DecoStronghold").
[[nodiscard]] std::unique_ptr<Decorator> strongholdDecorator();

/// BoundingBox.orientBox (vanilla).
[[nodiscard]] BlockBox orientBox(int x, int y, int z, int offX, int offY, int offZ, int sizeX, int sizeY, int sizeZ, Orientation facing);

/// KnightStrongholdComponent.
class Knight : public ::gf::structure::bases::ComponentOld {
public:
    /// The load constructor (the fields come from load()).
    explicit Knight(std::string typeId);

    std::vector<BlockPos> doors;

    /// "doorInts" plus the base's.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    [[nodiscard]] ::gf::structure::bases::TerrainAdjustment getTerrainAdjustment() const override;

    /// KnightStrongholdComponent.getComponentToAddBoundingBox.
    [[nodiscard]] static BlockBox getComponentToAddBoundingBox(int x, int y, int z, int xOff, int yOff, int zOff, int xSize, int ySize, int zSize,
                                                               Orientation facing);

    bool attemptToBreakIn(int wx, int wy, int wz);
    void addDoorwayTo(int dx, int dy, int dz, Rotation facing);
    void addDoor(int dx, int dy, int dz);

protected:
    /// The constructor's tail (Java's KnightStrongholdComponent(type, i, facing, x, y, z)): the box
    /// from generateBoundingBox, the orientation and the spawn index. Called by each subclass
    /// constructor so the virtual calls reach the subclass, as in Java.
    void init(int i, Orientation facing, int x, int y, int z);
    [[nodiscard]] virtual BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) = 0;

    void addNewComponent(Piece& entrance, PieceList& list, RandomSource& random, Rotation facing, int x, int y, int z);
    [[nodiscard]] Piece* findBreakInComponent(PieceList& list, int x, int y, int z);
    void addNewUpperComponent(Piece& parent, PieceList& list, RandomSource& random, Rotation facing, int x, int y, int z);
    [[nodiscard]] static bool isOutOfRange(Piece const& parent, int nx, int nz, int range);

    virtual void placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb);
    [[nodiscard]] int getXSize() const;
    void placeSmallDoorwayAt(Region& world, int facing, int x, int y, int z, BlockBox const& sbb);
    void placeCornerStatue(Region& world, int x, int y, int z, int facing, BlockBox const& sbb);
    void placeWallStatue(Region& world, int x, int y, int z, Rotation facing, BlockBox const& sbb);
    [[nodiscard]] virtual bool isValidBreakInPoint(int wx, int wy, int wz);
    [[nodiscard]] int getRelativeX(int x, int z) const;
    [[nodiscard]] int getRelativeY(int y) const;
    [[nodiscard]] int getRelativeZ(int x, int z) const;
    void placeDoors(Region& world, BlockBox const& sbb);
    void placeStrongholdWalls(Region& world, BlockBox const& sbb, int sx, int sy, int sz, int dx, int dy, int dz, RandomSource& rand,
                              ::gf::structure::BlockSelector& randomBlocks);
    void placeUpperStrongholdWalls(Region& world, BlockBox const& sbb, int sx, int sy, int sz, int dx, int dy, int dz, RandomSource& rand,
                                   ::gf::structure::BlockSelector& randomBlocks);

    [[nodiscard]] Decorator const& d() const { return *deco; }
    [[nodiscard]] ::gf::structure::BlockSelector& randomBlocks() const { return *deco->randomBlocks; }
};

// ---- the lower stronghold ------------------------------------------------------------------------------

/// StrongholdPieceWeight + StrongholdPieces.
class StrongholdPieces {
public:
    using Factory = std::function<std::unique_ptr<Knight>(int i, Orientation facing, int x, int y, int z)>;
    struct Weight {
        Factory     factory;
        int         pieceWeight      = 0;
        int         instancesSpawned = 0;
        int         instancesLimit   = 0;
        int         minimumDepth     = 0;
        [[nodiscard]] bool isDeepEnough(int depth) const { return canSpawnMoreStructures() && depth >= minimumDepth; }
        [[nodiscard]] bool canSpawnMoreStructures() const { return instancesLimit == 0 || instancesSpawned < instancesLimit; }
    };

    void prepareStructurePieces();
    void markBossRoomUsed();
    [[nodiscard]] std::unique_ptr<Knight> getNextComponent(Piece& parent, PieceList& list, RandomSource& random, int index, Orientation facing, int x,
                                                           int y, int z);
    /// The static lastPieceMade (upstream keeps it across starts; it only ever matches a weight of the
    /// current list, which prepareStructurePieces makes anew).
    static void resetLastPieceMade();

private:
    [[nodiscard]] bool hasMoreLimitedPieces();
    std::vector<std::shared_ptr<Weight>> pieceList;
    int                                  totalWeight = 0;
};

class Entrance final : public Knight {
public:
    Entrance();
    Entrance(int i, int x, int y, int z);
    StrongholdPieces lowerPieces;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class AccessChamber final : public Knight {
public:
    AccessChamber();
    AccessChamber(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] bool isComponentProtected() const override { return false; }

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

/// The simple lower pieces: one class per type id, the same shape of code.
class SmallHallway final : public Knight {
public:
    SmallHallway();
    SmallHallway(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class LeftTurn final : public Knight {
public:
    LeftTurn();
    LeftTurn(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class RightTurn final : public Knight {
public:
    RightTurn();
    RightTurn(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class Crossing final : public Knight {
public:
    Crossing();
    Crossing(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void placeTableAndChairs(Region& world, BlockBox const& sbb, Rotation rotation);
};

class DeadEnd final : public Knight {
public:
    DeadEnd();
    DeadEnd(int i, Orientation facing, int x, int y, int z);
    bool chestTrapped = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class BalconyRoom final : public Knight {
public:
    BalconyRoom();
    BalconyRoom(int i, Orientation facing, int x, int y, int z);
    bool enterBottom = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void placeStairsAndPillars(Region& world, BlockBox const& sbb, Rotation rotation);
};

class TrainingRoom final : public Knight {
public:
    TrainingRoom();
    TrainingRoom(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void placeTrainingDummy(Region& world, BlockBox const& sbb, Rotation rotation);
};

class SmallStairs final : public Knight {
public:
    SmallStairs();
    SmallStairs(int i, Orientation facing, int x, int y, int z);
    bool enterBottom  = false;
    bool hasTreasure  = false;
    bool chestTrapped = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class TreasureCorridor final : public Knight {
public:
    TreasureCorridor();
    TreasureCorridor(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

class Atrium final : public Knight {
public:
    Atrium();
    Atrium(int i, Orientation facing, int x, int y, int z);
    bool enterBottom = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void spawnATree(Region& world, int treeNum, int x, int y, int z, BlockBox const& sbb);
    void placeBalconyPillar(Region& world, BlockBox const& sbb, Rotation rotation);
};

class Foundry final : public Knight {
public:
    Foundry();
    Foundry(int i, Orientation facing, int x, int y, int z);
    int entranceLevel = 0;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void addOreToMass(Region& world, BlockBox const& sbb, RandomSource& massRandom, Block const& state);
};

class TreasureRoom final : public Knight {
public:
    TreasureRoom();
    TreasureRoom(int i, Orientation facing, int x, int y, int z);
    bool enterBottom = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
    void     placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb) override;
};

class BossRoom final : public Knight {
public:
    BossRoom();
    BossRoom(int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
    void     placeDoorwayAt(Region& world, int x, int y, int z, BlockBox const& sbb) override;
    bool     isValidBreakInPoint(int, int, int) override { return false; }

private:
    void placeSarcophagus(Region& world, BlockBox const& sbb, int x, int y, int z, Rotation rotation);
    void placePillarDecorations(Region& world, BlockBox const& sbb, Rotation rotation);
};

// ---- the upper ruins -----------------------------------------------------------------------------------

class UpperAscender final : public Knight {
public:
    UpperAscender();
    UpperAscender(int i, Orientation facing, int x, int y, int z);
    bool exitTop = false;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] bool isComponentProtected() const override { return false; }

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;

private:
    void makeStairsAt(Region& world, int y, int z, Orientation facing, BlockBox const& sbb);
    void makePlatformAt(Region& world, int y, int z, BlockBox const& sbb);
};

/// The four small upper pieces (corridor, left turn, right turn, T intersection).
class UpperSmall final : public Knight {
public:
    UpperSmall(std::string typeId);
    UpperSmall(std::string typeId, int i, Orientation facing, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] bool isComponentProtected() const override { return false; }

protected:
    BlockBox generateBoundingBox(Orientation facing, int x, int y, int z) override;
};

/// StrongholdShieldStructure: the legacy piece of old saves (never generated in 4.8).
class ShieldStructure final : public Knight {
public:
    ShieldStructure();
    void load(Json const& data) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    BlockBox generateBoundingBox(Orientation, int, int, int) override { return bbox; }
};

/// KnightStrongholdStructure#getFirstPiece + LandmarkStructure#generateFromStartingPiece: the entrance
/// at (x, y + rand(3) == 0 ? 5 : 1, z) from `firstPieceRandom`, its children from `contextRandom`.
void layout(RandomSource& firstPieceRandom, RandomSource& contextRandom, int x, int y, int z, PieceList& pieces);
/// Registers the piece types and the decorator (once).
void registerPieces();

} // namespace gf::feature::stronghold::pieces
