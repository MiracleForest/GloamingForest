// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleFoundation48Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof48CrenellatedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBossGazeboComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleStairTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleLargeTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleMazeTower13Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDamagedTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleWreckedTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleFoundation13Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleFoundation13ComponentThorns.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBellFoundation21Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleEntranceTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleEntranceSideTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleEntranceBottomTowerComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleEntranceStairsComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBellTower21Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13ConicalComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13CrenellatedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13PeakedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof9CrenellatedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonStepsComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonRoom31Component.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonExitComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonForgeRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleMuralComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/StructureTFDecoratorCastle.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U30 (finalcastle): the final castle's piece types on F7's bases (rule R19: procedural pieces
// translated exactly; the gazebo, large tower and bell tower interiors are original .gft templates,
// assets/structures/finalcastle). Unit-internal: the castle module and its selftests use it.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/finalcastle/Castle.h"
#include "feature/structure/bases/Tower.h"

namespace gf::feature::finalcastle::castle {

using ::gf::structure::BlockBox;
using ::gf::structure::Json;
using ::gf::structure::Orientation;
using ::gf::structure::Piece;
using ::gf::structure::RandomSource;
using ::gf::structure::Region;
using ::gf::structure::Rotation;
using ::gf::structure::bases::ComponentOld;
using ::gf::structure::bases::Decorator;
using ::gf::structure::bases::PieceList;
using ::gf::structure::bases::TowerWing;

inline constexpr char kFinalCastle[] = "gloamingforest:final_castle";

namespace type {
inline constexpr char Main[]            = "gloamingforest:GloamingFCMain";
inline constexpr char Foundation48[]    = "gloamingforest:GloamingFCToF48";
inline constexpr char Roof48[]          = "gloamingforest:GloamingFCRo48Cr";
inline constexpr char Gazebo[]          = "gloamingforest:GloamingFCBoGaz";
inline constexpr char StairTower[]      = "gloamingforest:GloamingFCStTo";
inline constexpr char LargeTower[]      = "gloamingforest:GloamingFCLaTo";
inline constexpr char MazeTower[]       = "gloamingforest:GloamingFCSiTo";
inline constexpr char Bridge[]          = "gloamingforest:GloamingFCBri";
inline constexpr char Damaged[]         = "gloamingforest:GloamingFCDamT";
inline constexpr char Wrecked[]         = "gloamingforest:GloamingFCWrT";
inline constexpr char Foundation13[]    = "gloamingforest:GloamingFCToF13";
inline constexpr char Thorns[]          = "gloamingforest:GloamingFCFTh21";
inline constexpr char EntranceTower[]   = "gloamingforest:GloamingFCEnTo";
inline constexpr char EntranceSide[]    = "gloamingforest:GloamingFCEnSiTo";
inline constexpr char EntranceBottom[]  = "gloamingforest:GloamingFCEnBoTo";
inline constexpr char EntranceStairs[]  = "gloamingforest:GloamingFCEnSt";
inline constexpr char BellTower[]       = "gloamingforest:GloamingFCBelTo";
inline constexpr char BellFoundation[]  = "gloamingforest:GloamingFCBeF21";
inline constexpr char Roof13Conical[]   = "gloamingforest:GloamingFCRo13Con";
inline constexpr char Roof13Cren[]      = "gloamingforest:GloamingFCRo13Cr";
inline constexpr char Roof13Peaked[]    = "gloamingforest:GloamingFCRo13Pk";
inline constexpr char Roof9Cren[]       = "gloamingforest:GloamingFCRo9Cr";
inline constexpr char DungeonSteps[]    = "gloamingforest:GloamingFCDunSt";
inline constexpr char DungeonEntrance[] = "gloamingforest:GloamingFCDunEn";
inline constexpr char DungeonRoom[]     = "gloamingforest:GloamingFCDunR31";
inline constexpr char DungeonExit[]     = "gloamingforest:GloamingFCDunEx";
inline constexpr char ForgeRoom[]       = "gloamingforest:GloamingFCDunBoR";
inline constexpr char Mural[]           = "gloamingforest:GloamingFCMur";
} // namespace type

namespace id {
inline constexpr char YellowRune[]  = "gloamingforest:yellow_castle_rune_brick";
inline constexpr char BlueRune[]    = "gloamingforest:blue_castle_rune_brick";
inline constexpr char PinkRune[]    = "gloamingforest:pink_castle_rune_brick";
inline constexpr char VioletRune[]  = "gloamingforest:violet_castle_rune_brick";
inline constexpr char YellowDoor[]  = "gloamingforest:yellow_castle_door";
inline constexpr char BlueDoor[]    = "gloamingforest:blue_castle_door";
inline constexpr char PinkDoor[]    = "gloamingforest:pink_castle_door";
inline constexpr char VioletDoor[]  = "gloamingforest:violet_castle_door";
inline constexpr char PinkField[]   = "gloamingforest:pink_force_field";
inline constexpr char BlueField[]   = "gloamingforest:blue_force_field";
inline constexpr char GreenField[]  = "gloamingforest:green_force_field";
inline constexpr char VioletField[] = "gloamingforest:violet_force_field";
inline constexpr char Deadrock[]    = "gloamingforest:deadrock";
inline constexpr char GazeboPool[]     = "gloamingforest:final_castle/temp/gazebo";
inline constexpr char LargeTowerPool[] = "gloamingforest:final_castle/temp/large_tower";
inline constexpr char BellTowerPool[]  = "gloamingforest:final_castle/temp/bell_tower";
/// The jigsaw names the hosts select (upstream: "gloamingforest:final_castle/final_boss", ".../large_tower",
/// ".../room"; GloamingForest names them in its own namespace, as assemble refuses gloamingforest:
/// strings that are no registry id).
inline constexpr char FinalBossJigsaw[]  = "gf:final_castle/final_boss";
inline constexpr char LargeTowerJigsaw[] = "gf:final_castle/large_tower";
inline constexpr char RoomJigsaw[]       = "gf:final_castle/room";
} // namespace id

/// A Java block state as a Bedrock block (structure::blocks::java).
[[nodiscard]] Block const& J(char const* javaId, Json const& props = Json::object());
/// StructureTFDecoratorCastle ("DecoCastle").
[[nodiscard]] std::unique_ptr<Decorator> castleDecorator();
/// `this.deco = parent.deco` (Java shares the object; the castle never changes a shared one after
/// handing it on, so a copy is the same).
[[nodiscard]] std::unique_ptr<Decorator> copyDecorator(Decorator const* deco);
/// The decorator of a piece if it is a GloamingStructureComponentOld (Java's `parent instanceof
/// GloamingStructureComponentOld`).
[[nodiscard]] Decorator const* decoOf(Piece const& piece);
/// BoundingBoxUtils.getComponentToAddBoundingBox(x, y, z, ..., dir, false).
[[nodiscard]] BlockBox componentBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir);
/// RandomSource.create(world.getSeed() + (minX * 321534781L) ^ (minZ * 756839L)).
[[nodiscard]] std::int64_t decoSeed(Region& region, BlockBox const& box);

// ==== the keep ===================================================================================================

/// FinalCastleMainComponent ("GloamingFCMain").
class Main : public ComponentOld {
public:
    /// FinalCastleMainComponent(i, x, y, z). `overworldSeed` is WorldUtil.getOverworldSeed() for the
    /// murals.
    Main(int i, int x, int y, int z, std::int64_t overworldSeed);
    Main();

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    [[nodiscard]] BlockPos offsetTowerCCoords(int x, int y, int z, int howFar, Orientation direction) const override;

private:
    void buildTowerMaze(PieceList& list, RandomSource& rand, int x, int y, int z, int howFar, Orientation direction, char const* type, BlockPos const& dest);
    [[nodiscard]] static bool isMazeComplete(PieceList const& list, char const* type);
    void makeSmallTowerStairs(Region& region, BlockBox const& sbb, Rotation rotation);
    void makeLargeTowerStairs(Region& region, BlockBox const& sbb, Rotation rotation);
    void makeMezzTopStairs(Region& region, BlockBox const& sbb, int y, int z, Orientation stairMeta);
    void makeHalfPillarBase(Region& region, BlockBox const& sbb, Rotation rotation, int y, int z, bool isFlipped);
    void makePillarBase(Region& region, BlockBox const& sbb, int x, int z, int y, bool isFlipped);

    std::int64_t mOverworldSeed = 0; ///< not saved (only the layout uses it)
};

/// FinalCastleFoundation48Component ("GloamingFCToF48").
class Foundation48 : public ComponentOld {
public:
    Foundation48(int i, ComponentOld const& keep, int x, int y, int z);
    Foundation48();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void makePiling(Region& region, BlockBox const& sbb, int mid, Rotation rotation, int i);
};

/// FinalCastleRoof48CrenellatedComponent ("GloamingFCRo48Cr").
class Roof48 : public ComponentOld {
public:
    Roof48(int i, ComponentOld const& keep, int x, int y, int z);
    Roof48();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// FinalCastleBossGazeboComponent ("GloamingFCBoGaz"): its blocks come from the gazebo template.
class Gazebo : public ComponentOld {
public:
    Gazebo(int i, ComponentOld const& keep, int x, int y, int z);
    Gazebo();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// FinalCastleStairTowerComponent ("GloamingFCStTo").
class StairTower : public TowerWing {
public:
    StairTower(int i, int x, int y, int z, Orientation rotation);
    StairTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] virtual Block const& getGlyphMeta() const;
};

/// FinalCastleLargeTowerComponent ("GloamingFCLaTo").
class LargeTower : public TowerWing {
public:
    LargeTower(int i, int x, int y, int z, Orientation rotation);
    LargeTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] virtual Block const& getGlyphMeta() const;
};

/// FinalCastleMuralComponent ("GloamingFCMur").
class MuralPiece : public ComponentOld {
public:
    MuralPiece(int i, int x, int y, int z, int width, int height, Orientation direction, std::int64_t overworldSeed);
    MuralPiece();
    void               postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// "muralWidth", "muralHeight", "muralBytes" plus ComponentOld's.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    [[nodiscard]] core::finalcastle::Mural const& mural() const { return mMural; }

private:
    core::finalcastle::Mural mMural;
};

// ==== the tower mazes ============================================================================================

/// FinalCastleMazeTower13Component ("GloamingFCSiTo" and its subclasses).
class MazeTower13 : public TowerWing {
public:
    static constexpr int LOWEST_DOOR  = 108;
    static constexpr int HIGHEST_DOOR = 186;

    /// FinalCastleMazeTower13Component(piece, rand, i, x, y, z, color, direction): 2-4 floors.
    MazeTower13(std::string typeId, RandomSource& rand, int i, int x, int y, int z, char const* color, Orientation direction);
    /// FinalCastleMazeTower13Component(piece, i, x, y, z, floors, entranceFloor, color, direction).
    MazeTower13(std::string typeId, int i, int x, int y, int z, int floors, int entranceFloor, char const* color, Orientation direction);
    /// For loading.
    explicit MazeTower13(std::string typeId);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// "color" ({"Name": rune brick id}) plus TowerWing's.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    /// buildTowards: the roof and foundation, then more towers towards `dest`.
    void buildTowards(Piece& parent, PieceList& list, RandomSource& rand, BlockPos const& dest);
    /// getValidOpeningCC: a random opening on the wall facing `facing`, on a floor.
    [[nodiscard]] virtual BlockPos getValidOpeningCC(RandomSource& rand, Orientation facing);
    [[nodiscard]] virtual Block const& getGlyphMeta() const;
    [[nodiscard]] Block const&         doorColor() const;
    [[nodiscard]] std::string const&   color() const { return mColor; }
    [[nodiscard]] int                  towerHeight() const { return height; }
    [[nodiscard]] std::vector<BlockPos> const& doors() const { return openings; }

protected:
    [[nodiscard]] BlockPos offsetTowerCCoords(int x, int y, int z, int howFar, Orientation direction) const override;
    void makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) override;
    virtual void addFloors(Region& region, BlockBox const& sbb);
    [[nodiscard]] virtual bool hasAccessibleRoof() const;
    void buildNonCriticalTowers(PieceList& list, RandomSource& rand);
    bool buildDamagedTower(PieceList& list, RandomSource& rand, Orientation facing);
    [[nodiscard]] virtual std::unique_ptr<MazeTower13> makeNewDamagedTower(RandomSource& rand, Orientation facing, BlockPos const& tc);
    [[nodiscard]] Orientation ori() const { return getOrientation().value_or(Orientation::North); }

    std::string mColor;

private:
    [[nodiscard]] Orientation findBestDirectionTowards(BlockPos const& dest) const;
    [[nodiscard]] Orientation findSecondDirectionTowards(BlockPos const& dest) const;
    [[nodiscard]] Orientation findThirdDirectionTowards(BlockPos const& dest) const;
    bool buildContinueTowerTowards(PieceList& list, RandomSource& rand, BlockPos const& dest, Orientation facing, int howFar);
    [[nodiscard]] int adjustOpening(int posY, BlockPos const& dest) const;
    bool buildEndTowerTowards(PieceList& list, RandomSource& rand, BlockPos const& dest, Orientation facing, int howFar);
    void addStairsDown(Region& region, BlockBox const& sbb, Rotation rotation, int y);
};

/// FinalCastleDamagedTowerComponent ("GloamingFCDamT").
class DamagedTower : public MazeTower13 {
public:
    DamagedTower(std::string typeId, RandomSource& rand, int i, int x, int y, int z, Orientation direction);
    explicit DamagedTower(std::string typeId = type::Damaged);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    [[nodiscard]] std::unique_ptr<MazeTower13> makeNewDamagedTower(RandomSource& rand, Orientation facing, BlockPos const& tc) override;
    void destroyTower(Region& region, RandomSource& rand, BlockBox const& sbb);
    [[nodiscard]] virtual std::vector<std::optional<core::finalcastle::DestroyArea>> makeInitialDestroyList(RandomSource& rand);
    virtual void determineBlockDestroyed(Region& region, std::vector<std::optional<core::finalcastle::DestroyArea>> const& areas, int y, int x, int z);
};

/// FinalCastleWreckedTowerComponent ("GloamingFCWrT").
class WreckedTower : public DamagedTower {
public:
    WreckedTower(RandomSource& rand, int i, int x, int y, int z, Orientation direction);
    WreckedTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    [[nodiscard]] Block const& getGlyphMeta() const override;

protected:
    [[nodiscard]] std::vector<std::optional<core::finalcastle::DestroyArea>> makeInitialDestroyList(RandomSource& rand) override;
    void determineBlockDestroyed(Region& region, std::vector<std::optional<core::finalcastle::DestroyArea>> const& areas, int y, int x, int z) override;
};

/// FinalCastleEntranceTowerComponent ("GloamingFCEnTo").
class EntranceTower : public MazeTower13 {
public:
    EntranceTower(int i, int x, int y, int z, Orientation direction);
    EntranceTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    [[nodiscard]] BlockPos getValidOpeningCC(RandomSource& rand, Orientation facing) override;

private:
    bool buildSideTower(PieceList& list, RandomSource& rand, int middleFloors, Orientation facing, int howFar);
};

/// FinalCastleEntranceSideTowerComponent ("GloamingFCEnSiTo").
class EntranceSideTower : public MazeTower13 {
public:
    EntranceSideTower(int i, int x, int y, int z, int floors, int entranceFloor, Orientation direction);
    EntranceSideTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
};

/// FinalCastleEntranceBottomTowerComponent ("GloamingFCEnBoTo").
class EntranceBottomTower : public MazeTower13 {
public:
    EntranceBottomTower(int i, int x, int y, int z, int floors, int entranceFloor, Orientation direction);
    EntranceBottomTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;

protected:
    [[nodiscard]] bool hasAccessibleRoof() const override { return false; }

private:
    bool addStairs(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation);
};

/// FinalCastleEntranceStairsComponent ("GloamingFCEnSt").
class EntranceStairs : public ComponentOld {
public:
    EntranceStairs(int index, int x, int y, int z, Orientation direction);
    EntranceStairs();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void placeStairs(Region& region, BlockBox const& sbb, int x, int y, int z, Orientation facing);
};

/// FinalCastleBellTower21Component ("GloamingFCBelTo").
class BellTower : public MazeTower13 {
public:
    BellTower(int i, int x, int y, int z, Orientation direction);
    BellTower();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    void addFloors(Region&, BlockBox const&) override {} // NO-OP, let the Jigsaw handle it
};

/// FinalCastleBridgeComponent ("GloamingFCBri").
class Bridge : public ComponentOld {
public:
    Bridge(int i, int x, int y, int z, int length, Orientation direction);
    Bridge();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// FinalCastleFoundation13Component ("GloamingFCToF13" and its subclasses).
class Foundation13 : public ComponentOld {
public:
    Foundation13(std::string typeId, int i, ComponentOld const& sideTower, int x, int y, int z);
    explicit Foundation13(std::string typeId = type::Foundation13);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    int groundLevel = -1; ///< not saved (Java)
};

/// FinalCastleFoundation13ComponentThorns ("GloamingFCFTh21").
class ThornFoundation : public Foundation13 {
public:
    ThornFoundation(int i, ComponentOld const& sideTower, int x, int y, int z);
    ThornFoundation();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void makeThornVine(Region& region, RandomSource& decoRNG, Rotation rotation, BlockBox const& sbb);
    void makeThornBranch(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
};

/// FinalCastleBellFoundation21Component ("GloamingFCBeF21").
class BellFoundation : public Foundation13 {
public:
    BellFoundation(int i, ComponentOld const& sideTower, int x, int y, int z);
    BellFoundation();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

// ==== roofs ======================================================================================================

/// FinalCastleRoof13ConicalComponent ("GloamingFCRo13Con").
class Roof13Conical : public ComponentOld {
public:
    Roof13Conical(RandomSource& rand, int i, ComponentOld const& sideTower, int x, int y, int z);
    Roof13Conical();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    /// "slope" plus ComponentOld's.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    int                slope = 0;
};

/// The roofs with a fixed height above the tower (crenellated 13, peaked 13, crenellated 9).
class SimpleRoof : public ComponentOld {
public:
    SimpleRoof(std::string typeId, int i, ComponentOld const& sideTower, int height);
    explicit SimpleRoof(std::string typeId);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
};

/// FinalCastleRoof13CrenellatedComponent ("GloamingFCRo13Cr").
class Roof13Cren : public SimpleRoof {
public:
    Roof13Cren(int i, ComponentOld const& sideTower) : SimpleRoof(type::Roof13Cren, i, sideTower, 5) {}
    Roof13Cren() : SimpleRoof(type::Roof13Cren) {}
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// FinalCastleRoof13PeakedComponent ("GloamingFCRo13Pk").
class Roof13Peaked : public SimpleRoof {
public:
    Roof13Peaked(int i, ComponentOld const& sideTower) : SimpleRoof(type::Roof13Peaked, i, sideTower, 18) {}
    Roof13Peaked() : SimpleRoof(type::Roof13Peaked) {}
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// FinalCastleRoof9CrenellatedComponent ("GloamingFCRo9Cr").
class Roof9Cren : public SimpleRoof {
public:
    Roof9Cren(int i, ComponentOld const& sideTower) : SimpleRoof(type::Roof9Cren, i, sideTower, 5) {}
    Roof9Cren() : SimpleRoof(type::Roof9Cren) {}
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

// ==== the dungeon ================================================================================================

class DungeonEntrance;
class ForgeRoom;

/// FinalCastleDungeonStepsComponent ("GloamingFCDunSt").
class DungeonSteps : public ComponentOld {
public:
    DungeonSteps(int i, int x, int y, int z, Orientation rotation);
    DungeonSteps();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

    DungeonSteps*    buildMoreStepsTowards(PieceList& list, RandomSource& rand, Rotation rotation);
    DungeonEntrance* buildLevelUnder(PieceList& list, RandomSource& rand, int level);
    ForgeRoom*       buildBossRoomUnder(PieceList& list, RandomSource& rand);
};

/// FinalCastleDungeonRoom31Component ("GloamingFCDunR31" and its subclasses).
class DungeonRoom31 : public TowerWing {
public:
    DungeonRoom31(std::string typeId, int i, int x, int y, int z, Orientation direction, int level);
    explicit DungeonRoom31(std::string typeId = type::DungeonRoom);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] ::gf::structure::bases::TerrainAdjustment getTerrainAdjustment() const override;
    [[nodiscard]] int                                       getGroundLevelDelta() const override { return 0; }

    int level = 0; ///< not saved (Java: only used while building)

protected:
    [[nodiscard]] virtual Block const& getRuneColor(Block const& forceFieldColor) const;
    [[nodiscard]] virtual Block const& getForceFieldColor(RandomSource& decoRNG) const;

private:
    [[nodiscard]] static bool isExitBuildForLevel(Piece const& parent);
    static void               setExitBuiltForLevel(Piece& parent, bool exit);
    bool addDungeonRoom(Piece& parent, PieceList& list, RandomSource& rand, Rotation rotation, int level);
    bool addDungeonExit(PieceList& list, RandomSource& rand, Rotation rotation);
    [[nodiscard]] BlockPos getNewRoomCoords(RandomSource& rand, Rotation rotation) const;
};

/// FinalCastleDungeonEntranceComponent ("GloamingFCDunEn").
class DungeonEntrance : public DungeonRoom31 {
public:
    DungeonEntrance(int i, int x, int y, int z, Orientation direction, int level);
    DungeonEntrance();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

    bool hasExit = false; ///< not saved (Java)

protected:
    [[nodiscard]] Block const& getRuneColor(Block const& forceFieldColor) const override;
    [[nodiscard]] Block const& getForceFieldColor(RandomSource& decoRNG) const override;
};

/// FinalCastleDungeonExitComponent ("GloamingFCDunEx").
class DungeonExit : public DungeonRoom31 {
public:
    DungeonExit(int i, int x, int y, int z, Orientation direction, int level);
    DungeonExit();
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] Rotation findStairDirectionTowards(int x, int z) const;

protected:
    [[nodiscard]] Block const& getRuneColor(Block const& forceFieldColor) const override;
    [[nodiscard]] Block const& getForceFieldColor(RandomSource& decoRNG) const override;
};

/// FinalCastleDungeonForgeRoomComponent ("GloamingFCDunBoR").
class ForgeRoom : public ComponentOld {
public:
    ForgeRoom(int i, int x, int y, int z, Orientation direction);
    ForgeRoom();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

// ==== layout =====================================================================================================

/// FinalCastleStructure#getFirstPiece + generateFromStartingPiece: the keep at (x, y, z), its children
/// drawn from `contextRandom` (Structure.GenerationContext#random()).
void layout(RandomSource& contextRandom, int x, int y, int z, std::int64_t overworldSeed, PieceList& pieces);

/// Registers the piece types and the decorator (castle install).
void registerPieces();

} // namespace gf::feature::finalcastle::castle
