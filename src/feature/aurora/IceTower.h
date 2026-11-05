// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBossWingComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerEntranceComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerStairsComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerRoofComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerBeardComponent.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerDecorator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The aurora palace's pieces ("twilightforest:TFIT*") on F7's tower base.

#include <memory>

#include "feature/structure/bases/Tower.h"

namespace gf::feature::aurora {

using structure::BlockBox;
using structure::Orientation;
using structure::Piece;
using structure::Region;
using structure::RandomSource;
using structure::Rotation;
using structure::bases::PieceList;

/// IceTowerDecorator ("DecoIceTower").
[[nodiscard]] std::unique_ptr<structure::bases::Decorator> iceTowerDecorator();

/// IceTowerWingComponent ("twilightforest:TFITWin").
class IceTowerWing : public structure::bases::TowerWing {
public:
    static constexpr int SIZE  = 11;
    static constexpr int RANGE = static_cast<int>(SIZE * 2.5F);

    IceTowerWing(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    bool hasBase       = false;
    int  treasureFloor = -1;

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) override;
    void makeARoof(Piece& parent, PieceList& list, RandomSource& rand) override;
    void makeABeard(Piece& parent, PieceList& list, RandomSource& rand) override;
    [[nodiscard]] structure::Json save() const override;
    void                          load(structure::Json const& data) override;

    bool makeBossTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation);

protected:
    virtual bool shouldHaveBase(PieceList const& list, RandomSource& rand);
    [[nodiscard]] int getYByStairs(int rx, RandomSource& rand, Rotation direction) override;
    void              makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) override;
    virtual void      makeFloorsForTower(Region& region, RandomSource& decoRNG, BlockBox const& sbb);
    virtual void      placeFloor(Region& region, RandomSource& rand, BlockBox const& sbb, int floorHeight, int floor);
    virtual void      decorateTopFloor(Region& region, RandomSource& rand, int floor, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                       BlockBox const& sbb);
    /// decorateFloor(world, rand, floorType, floor, ...) of the regular wings (`type`: a core::aurora::FloorType).
    void decorateFloorOfType(Region& region, RandomSource& rand, int type, int floor, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                             BlockBox const& sbb);
    [[nodiscard]] bool isBossWing() const { return typeId == "twilightforest:TFITBoss"; }

    [[nodiscard]] Orientation getChestDirection(Rotation rotation) const;
    void decoratePillars(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb);
    void decoratePillarsGrid(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb);
    void decoratePillarsCorners(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb);
    void decorateTopFloorTreasure(Region& region, int bottom, Rotation rotation, BlockBox const& sbb);
    void decorateFarWallSteps(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb);
    void decorateWraparoundWallSteps(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb);
    void decorateWraparoundWallStepsPillars(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure,
                                            BlockBox const& sbb);
    void decoratePlatform(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure, BlockBox const& sbb);
    void decorateQuadPillarStairs(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure, BlockBox const& sbb);
    void decoratePillarPlatforms(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb);
    void decoratePillarPlatformsOutside(Region& region, int bottom, int top, Rotation ladderUpDir, bool hasTreasure, BlockBox const& sbb);
    void buildStairway(Region& region, int bottom, int top, Rotation ladderUpDir, BlockBox const& sbb);
    void placeFullBlocks(Region& region, int bottom, int top, BlockBox const& sbb, Rotation rotation);
    void decoratePillarParkour(Region& region, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, bool hasTreasure, BlockBox const& sbb);
};

/// IceTowerMainComponent ("twilightforest:TFITMai").
class IceTowerMain : public IceTowerWing {
public:
    /// IceTowerMainComponent(rand, index, x, y, z): the tower at (x + 11, y + 40, z + 11), NORTH.
    IceTowerMain(RandomSource& rand, int index, int x, int y, int z);
    /// The load form.
    IceTowerMain();

    bool hasBossWing = false;

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    [[nodiscard]] structure::Json save() const override;
    void                          load(structure::Json const& data) override;

private:
    void makeEntranceBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int length, Rotation rotation);
    bool makeEntranceTower(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation);
};

/// IceTowerBossWingComponent ("twilightforest:TFITBoss").
class IceTowerBossWing : public IceTowerWing {
public:
    IceTowerBossWing(int index, int x, int y, int z, int wingSize, int wingHeight, Orientation direction);

protected:
    bool shouldHaveBase(PieceList const& list, RandomSource& rand) override;
    void placeFloor(Region& region, RandomSource& rand, BlockBox const& sbb, int floorHeight, int floor) override;
    void decorateFloor(Region& region, RandomSource& rand, int floor, int bottom, int top, std::optional<Rotation> ladderUpDir,
                       std::optional<Rotation> ladderDownDir, BlockBox const& sbb) override;
    void decorateTopFloor(Region& region, RandomSource& rand, int floor, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                          BlockBox const& sbb) override;

private:
    void placeIceStairs(Region& region, BlockBox const& sbb, RandomSource& rand, int y, Rotation rotation);
};

/// IceTowerEntranceComponent ("twilightforest:TFITEnt").
class IceTowerEntrance : public IceTowerWing {
public:
    IceTowerEntrance(int index, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    bool makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) override;

protected:
    bool shouldHaveBase(PieceList const& list, RandomSource& rand) override;
    void makeFloorsForTower(Region& region, RandomSource& rand, BlockBox const& sbb) override;

private:
    bool addStairs(PieceList& list, RandomSource& rand, int index, int x, int y, int z, Rotation rotation);
    void decoratePillarsCornersHigh(Region& region, int bottom, int top, Rotation rotation, BlockBox const& sbb);
};

/// IceTowerBridgeComponent ("twilightforest:TFITBri").
class IceTowerBridge : public structure::bases::ComponentOld {
public:
    IceTowerBridge(int index, int x, int y, int z, int length, int zLength, Orientation direction);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] structure::Json save() const override;
    void                          load(structure::Json const& data) override;

private:
    int length       = 0;
    int extraZlength = 0;
};

/// IceTowerStairsComponent ("twilightforest:TFITSt").
class IceTowerStairs : public structure::bases::TowerWing {
public:
    static constexpr int stairsLength = 5;
    IceTowerStairs(int index, int x, int y, int z, int size, int height, Orientation direction);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void placeStairs(Region& region, BlockBox const& sbb, int x, int y, int z);
};

/// IceTowerRoofComponent ("twilightforest:TFITRoof").
class IceTowerRoof : public structure::bases::TowerRoof {
public:
    IceTowerRoof(int index, structure::bases::TowerWing const& wing, int x, int y, int z);
    /// The load form.
    IceTowerRoof();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// IceTowerBeardComponent ("twilightforest:TFITBea").
class IceTowerBeard : public structure::bases::ComponentOld {
public:
    IceTowerBeard(int index, structure::bases::TowerWing const& wing, int x, int y, int z);
    /// The load form.
    IceTowerBeard();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] structure::Json save() const override;
    void                          load(structure::Json const& data) override;

private:
    int size   = 0;
    int height = 0;
};

/// Registers the TFIT* piece types and the decorator.
void registerIceTowerPieces();

} // namespace gf::feature::aurora
