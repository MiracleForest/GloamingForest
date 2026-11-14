// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerWingComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerMainBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBossBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBossTrapComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBalconyComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerBeardComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofAntennaComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofCactusComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofFourPostComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerRoofRingsComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/EnumDarkTowerDoor.java
//   src/main/java/gloamingforest/world/components/structures/darktower/StructureDecoratorDarkTower.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U26 (darktower): the dark tower's piece types on F7's tower bases (rule R19: procedural pieces
// translated exactly). Unit-internal: the structure module and its selftests use it.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "feature/structure/bases/Maze.h"
#include "feature/structure/bases/Tower.h"

namespace gf::feature::darktower::tower {

using ::gf::structure::BlockBox;
using ::gf::structure::Json;
using ::gf::structure::Orientation;
using ::gf::structure::Piece;
using ::gf::structure::RandomSource;
using ::gf::structure::Region;
using ::gf::structure::Rotation;
using ::gf::structure::bases::Decorator;
using ::gf::structure::bases::PieceList;

inline constexpr char kWing[]           = "gloamingforest:TFDTWin";
inline constexpr char kMain[]           = "gloamingforest:TFDTMai";
inline constexpr char kBridge[]         = "gloamingforest:TFDTBri";
inline constexpr char kEntranceBridge[] = "gloamingforest:TFDTEB";
inline constexpr char kMainBridge[]     = "gloamingforest:TFDTMB";
inline constexpr char kBossBridge[]     = "gloamingforest:TFDTBB";
inline constexpr char kEntrance[]       = "gloamingforest:TFDTEnt";
inline constexpr char kBossTrap[]       = "gloamingforest:TFDTBT";
inline constexpr char kBalcony[]        = "gloamingforest:TFDTBal";
inline constexpr char kBeard[]          = "gloamingforest:TFDTBea";
inline constexpr char kRoof[]           = "gloamingforest:TFDTRooS";
inline constexpr char kRoofAntenna[]    = "gloamingforest:TFDTRA";
inline constexpr char kRoofCactus[]     = "gloamingforest:TFDTRC";
inline constexpr char kRoofFourPost[]   = "gloamingforest:TFDTRFP";
inline constexpr char kRoofRings[]      = "gloamingforest:TFDTRR";

inline constexpr char kDarkTower[] = "gloamingforest:dark_tower";

/// EnumDarkTowerDoor.
enum class Door { Vanishing, Reappearing, Locked };

/// StructureDecoratorDarkTower ("DecoDarkTower").
[[nodiscard]] std::unique_ptr<Decorator> darkTowerDecorator();

/// DarkTowerWingComponent ("TFDTWin").
class Wing : public ::gf::structure::bases::TowerWing {
public:
    Wing(std::string typeId, int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    using TowerWing::addOpening;
    using TowerWing::placeBlock;

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// "keyTower", "doorTypeInts" plus TowerWing's.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    void makeARoof(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeABeard(Piece& parent, PieceList& list, RandomSource& rand) override;
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       Rotation rotation) override;
    [[nodiscard]] std::array<int, 3> getValidOpening(RandomSource& rand, Rotation direction) override;
    void                             addOpening(int dx, int dy, int dz, Rotation direction) override;
    void                             addOpening(int dx, int dy, int dz, Rotation direction, Door type);
    [[nodiscard]] bool               isDeadEnd() const override;
    /// Records the tower devices it generates so they tick once their chunk is loaded.
    void placeBlock(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox) override;

    [[nodiscard]] bool isKeyTower() const { return keyTower; }
    void               setKeyTower(bool k) { keyTower = k; }
    [[nodiscard]] std::vector<Door> const& doorTypes() const { return openingTypes; }

protected:
    /// DarkTowerEntranceComponent (no balconies on entrance towers).
    [[nodiscard]] virtual bool isEntrance() const { return false; }

    void makeOpenings(Region& region, BlockBox const& sbb) override;
    void makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) override;
    void makeReappearingDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb);
    void makeLockedDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb);

    [[nodiscard]] static int validateChildHeight(int childHeight) { return (childHeight / 4) * 4 + 1; }
    bool makeTowerBalcony(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation);

    void destroyTower(Region& region, RandomSource& decoRNG, int x, int y, int z, int amount, BlockBox const& sbb);
    void netherTransformBlob(Region& region, RandomSource& inRand, int sx, int sy, int sz, int rad, BlockBox const& sbb);
    void testAndChangeToNetherrack(Region& region, RandomSource& rand, int x, int y, int z, BlockBox const& sbb);
    void drawBlob(Region& region, int sx, int sy, int sz, int rad, Block const& state, BlockBox const& sbb);

    void addHalfFloors(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top);
    void makeHalfFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void makeFullFloor(Region& region, BlockBox const& sbb, int y);
    void decorateTreasureRoom(Region& region, BlockBox const& sbb, Rotation rotation, int y, int spacing, Decorator const& myDeco);
    void decorateSpawner(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y);
    void decorateWingLounge(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateReappearingFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateExperimentLamp(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateExperimentPulser(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateWingLibrary(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void makeSmallBookshelf(Region& region, BlockBox const& sbb, Rotation rotation, int y, int bx, int bz);
    void decoratePuzzleChest(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void makePillarFrame(Region& region, BlockBox const& sbb, Decorator const& myDeco, Rotation rotation, int x, int y, int z, bool fenced);
    void makePillarFrame(Region& region, BlockBox const& sbb, Decorator const& myDeco, Rotation rotation, int x, int y, int z, int width,
                         int height, int length, bool fenced);
    void addStairsDown(Region& region, BlockBox const& sbb, Rotation rotation, int y, int sz, int spacing);
    void addSmallTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top);
    void makeTimberFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void makeSmallTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y, bool bottom);
    [[nodiscard]] static int pickBetweenExcluding(int low, int high, RandomSource& rand, int k, int l, int m);
    [[nodiscard]] static int pickFrom(RandomSource& rand, int i, int j, int k);
    [[nodiscard]] bool       checkPost(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void makeEncasedWalls(Region& region, RandomSource& rand, BlockBox const& sbb, int minX, int minY, int minZ, int maxX, int maxY, int maxZ);
    /// setBlockStateRotated for a Java block id (potted plants keep their plant).
    void placeIdRotated(Region& region, std::string const& javaId, int x, int y, int z, Rotation rotation, BlockBox const& sbb);

    bool              keyTower = false;
    std::vector<Door> openingTypes;
};

/// DarkTowerMainComponent ("TFDTMai").
class Main : public Wing {
public:
    /// DarkTowerMainComponent(rand, index, x, y, z): the first main tower at (x + 10, y, z + 10).
    Main(RandomSource& rand, int index, int x, int y, int z);
    Main(RandomSource& rand, int index, int x, int y, int z, Orientation rotation);
    /// For loading (fields come from load()).
    Main();

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeARoof(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    bool makeEntranceTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int childSize, int childHeight, Rotation rotation);
    bool makeNewLargeTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation);
    bool makeBossTrapWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation);

    void addThreeQuarterFloors(Region& region, RandomSource& decoRNG, BlockBox const& sbb, int bottom, int top);
    void addThreeQuarterFloorsDecorateBoss(Region& region, BlockBox const& sbb, int bottom, int top);
    void decorateMainFloor(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTop);
    void makeThreeQuarterFloor(Region& region, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTowerTopFloor);
    void makeLargeStairsUp(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateReappearingMaze(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y);
    void decorateMazeDeadEnds(Region& region, ::gf::structure::bases::Maze const& maze, int y, BlockBox const& sbb);
    void decorateMazeDeadEnd(Region& region, int mx, int y, int mz, int facing, BlockBox const& sbb);
    void decorateUnbuilderMaze(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y);
    void decorateMainLounge(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y);
    void makeDispenserPillar(Region& region, int x, int y, int z, Orientation stairMeta, Rotation rotation, BlockBox const& sbb);
    void decorateBossSpawner(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateExperiment(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void makeWoodPillar(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void placeItemFrameRotated(Region& region, int x, int y, int z, Rotation rotation, Orientation direction, char const* itemId, BlockBox const& sbb);
    void decorateAquarium(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void decorateForge(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y);
    void makeFurnacePillar(Region& region, RandomSource& rand, int x, int y, int z, Orientation direction, Rotation rotation, BlockBox const& sbb);
    void makeStonePillar(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void makeFirePit(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void decorateNetherwart(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y, bool isTop);
    void decorateBotanical(Region& region, RandomSource& decoRNG, BlockBox const& sbb, Rotation rotation, int y);
    void placeTreePlanter(Region& region, int treeNum, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void placeRandomPlant(Region& region, RandomSource& decoRNG, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    void makeBottomEntrance(Region& region, BlockBox const& sbb, Rotation rotation, int y);
    void addTimberMaze(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top);
    void makeTimberBeams(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y, bool isBottom, bool isTop, int top);
    void makeMiniGhastSpawner(Region& region, int y, int sx, int sz, BlockBox const& sbb);
    void addBuilderPlatforms(Region& region, RandomSource& rand, BlockBox const& sbb, int bottom, int top);
    void makeBuilderPlatforms(Region& region, RandomSource& rand, BlockBox const& sbb, Rotation rotation, int y);
    void addTopBuilderPlatform(Region& region, RandomSource& rand, int top, int spacing, BlockBox const& sbb);
    void makeBuilderPlatform(Region& region, RandomSource& rand, Rotation rotation, int y, int z, bool hole, BlockBox const& sbb);
    void makeLampCluster(Region& region, RandomSource& rand, int sx, int y, int sz, Rotation rotation, BlockBox const& sbb);

    bool placedKeys = false; ///< not saved (Java)
};

/// DarkTowerBridgeComponent ("TFDTBri"; extends TowerWingComponent, as in Java).
class Bridge : public ::gf::structure::bases::TowerWing {
public:
    Bridge(std::string typeId, int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       Rotation rotation) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// getWingBB: the box of the wing at the far end.
    [[nodiscard]] BlockBox getWingBB() const;

protected:
    int dSize   = 0; ///< not saved (Java)
    int dHeight = 0;
};

/// DarkTowerEntranceBridgeComponent ("TFDTEB").
class EntranceBridge : public Bridge {
public:
    EntranceBridge(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       Rotation rotation) override;
};

/// DarkTowerMainBridgeComponent ("TFDTMB").
class MainBridge : public Bridge {
public:
    MainBridge(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       Rotation rotation) override;
};

/// DarkTowerBossBridgeComponent ("TFDTBB").
class BossBridge : public Bridge {
public:
    BossBridge(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       Rotation rotation) override;
};

/// DarkTowerEntranceComponent ("TFDTEnt").
class Entrance : public Wing {
public:
    Entrance(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeABeard(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeARoof(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] ::gf::structure::bases::TerrainAdjustment getTerrainAdjustment() const override;

protected:
    [[nodiscard]] bool isEntrance() const override { return true; }
};

/// DarkTowerBossTrapComponent ("TFDTBT").
class BossTrap : public Wing {
public:
    BossTrap(int i, int x, int y, int z, int pSize, int pHeight, Orientation direction);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeARoof(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void addBossTrapFloors(Region& region, BlockBox const& sbb);
};

/// DarkTowerBalconyComponent ("TFDTBal").
class Balcony : public ::gf::structure::bases::TowerWing {
public:
    Balcony(int i, int x, int y, int z, Orientation direction);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// DarkTowerBeardComponent ("TFDTBea").
class Beard : public ::gf::structure::bases::ComponentOld {
public:
    Beard(int i, ::gf::structure::bases::TowerWing const& wing, int x, int y, int z);
    Beard();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// "beardSize", "beardHeight".
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

private:
    void makeDarkBeard(Region& region, BlockBox const& sbb, int minX, int minZ, int maxX, int maxY, int maxZ);
    int  size   = 0;
    int  height = 0;
};

/// DarkTowerRoofComponent ("TFDTRooS") and its four kinds.
class Roof : public ::gf::structure::bases::TowerRoof {
public:
    Roof(std::string typeId, int i, ::gf::structure::bases::TowerWing const& wing, int x, int y, int z);
    explicit Roof(std::string typeId);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

class RoofAntenna : public Roof {
public:
    using Roof::Roof;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

class RoofCactus : public Roof {
public:
    using Roof::Roof;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

class RoofFourPost : public Roof {
public:
    using Roof::Roof;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void makeSmallAntenna(Region& region, BlockBox const& sbb, int height, int x, int z);
};

class RoofRings : public Roof {
public:
    using Roof::Roof;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void makeARing(Region& region, int y, BlockBox const& sbb);
};

/// DarkTowerStructure#getFirstPiece + generateFromStartingPiece: the start piece at (x, y, z) from
/// `firstPieceRandom`, its children drawn from `contextRandom`.
void layout(RandomSource& firstPieceRandom, RandomSource& contextRandom, int x, int y, int z, PieceList& pieces);

/// Registers the piece types and the decorator (structure module install).
void registerPieces();

} // namespace gf::feature::darktower::tower
