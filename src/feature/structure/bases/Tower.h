// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerWingComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerBridgeComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerOutbuildingComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerBeardComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerBeardAttachedComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofAttachedSlabComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofFenceComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofGableForwardsComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofPointyComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofPointyOverhangComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofSlabComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofSlabForwardsComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofStairsComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerRoofStairsOverhangComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18). The 16
// classes of upstream's old lich tower ("TFLT*" piece types): the wing with its openings, stairs,
// windows, floors and decorations, its main / bridge / outbuilding variants, the beards under it
// and the roofs on it. They are the base of the dark tower (U26), the aurora palace and the
// mushroom tower (U28); those units subclass them and override the virtual members, as in Java.
// Pieces own nothing across ticks; children go into the start's PieceList.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "feature/structure/bases/ComponentOld.h"

namespace gf::structure::bases {

class TowerRoof;

/// TowerWingComponent ("gloamingforest:TFLTWin").
class TowerWing : public ComponentOld {
public:
    /// TowerWingComponent(type, i, x, y, z, pSize, pHeight, direction): a size x height x size wing.
    TowerWing(std::string typeId, int genDepth, int x, int y, int z, int size, int height, Orientation direction);

    int size = 0;

    void addChildren(Piece& start, PieceList& pieces, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    /// "towerSize", "towerHeight", "doorInts", "highestOpening", "openingTowards0..3".
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    /// makeTowerWing: a child wing at opening (x, y, z) facing `rotation`; false when it does not fit.
    virtual bool makeTowerWing(PieceList& pieces, RandomSource& random, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation);
    /// addOpening: a door at (dx, dy, dz) towards `direction` (highestOpening, openingTowards).
    virtual void addOpening(int dx, int dy, int dz, Rotation direction);
    void         addOpening(int dx, int dy, int dz, Orientation facing);
    virtual void makeABeard(Piece& parent, PieceList& pieces, RandomSource& random);
    virtual void makeARoof(Piece& parent, PieceList& pieces, RandomSource& random);
    /// isDeadEnd: no opening but the entrance.
    [[nodiscard]] virtual bool isDeadEnd() const;
    /// getValidOpening: a random opening position {x, y, z} on the wall facing `direction`.
    [[nodiscard]] virtual std::array<int, 3> getValidOpening(RandomSource& random, Rotation direction);
    [[nodiscard]] int                        getGroundLevelDelta() const override;

protected:
    TowerWing(std::string typeId, int genDepth, int x, int y, int z);

    virtual bool makeBridge(PieceList& pieces, RandomSource& random, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation);
    virtual void makeAttachedRoof(PieceList& pieces, RandomSource& random);
    /// tryToFitRoof: adds `roof` (and its children) when it fits, and remembers its type.
    virtual void tryToFitRoof(PieceList& pieces, RandomSource& random, std::unique_ptr<TowerRoof> roof);
    virtual void makeFreestandingRoof(PieceList& pieces, RandomSource& random);
    virtual void makeOpeningMarkers(Region& region, RandomSource& random, int numMarkers, BlockBox const& sbb);

    // ---- decoration (decorateThisTower picks one per floor) --------------------------------------
    virtual void decorateThisTower(Region& region, BlockBox const& sbb);
    virtual void decorateDeadEnd(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void decorateFloor(Region& region, RandomSource& random, int floor, int bottom, int top, std::optional<Rotation> ladderUpDir,
                               std::optional<Rotation> ladderDownDir, BlockBox const& sbb);
    virtual void decorateWell(Region& region, RandomSource& random, int bottom, BlockBox const& sbb);
    virtual void decorateSkeletonRoom(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                      BlockBox const& sbb);
    virtual void decorateZombieRoom(Region& region, RandomSource& random, int bottom, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb);
    virtual void decorateCactusRoom(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                    BlockBox const& sbb);
    virtual void decorateTreasureChest(Region& region, int bottom, int top, BlockBox const& sbb);
    virtual void decorateSpiderWebs(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                    BlockBox const& sbb);
    virtual void decorateFurniture(Region& region, RandomSource& random, int bottom, int freeSpace, BlockBox const& sbb);
    virtual void decorateSolidRock(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                   BlockBox const& sbb);
    virtual void decorateLibrary(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                 BlockBox const& sbb);
    virtual void decorateLibraryTreasure(Region& region, RandomSource& random, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb);
    virtual void decorateFullLibrary(Region& region, RandomSource& random, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                     BlockBox const& sbb);
    virtual void decorateTrap(Region& region, int bottom, int top, BlockBox const& sbb);
    virtual void decorateStairTower(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void decorateStairFloor(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void decorateChandelier(Region& region, RandomSource& random, int decoTop, BlockBox const& sbb);
    virtual void decorateHangingChains(Region& region, RandomSource& random, int decoTop, BlockBox const& sbb);
    [[nodiscard]] virtual bool chainCollides(BlockPos const& coords, std::vector<BlockPos> const& list) const;
    virtual void decorateOneChain(Region& region, RandomSource& random, int dx, int decoTop, int length, int dz, BlockBox const& sbb);
    virtual void decorateFloatingBooks(Region& region, RandomSource& random, int decoTop, BlockBox const& sbb);
    virtual void decorateFloatingVines(Region& region, RandomSource& random, int decoTop, BlockBox const& sbb);
    virtual void decoratePlanter(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void decorateStairWell(Region& region, RandomSource& random, BlockBox const& sbb);

    // ---- layout helpers ------------------------------------------------------------------------
    [[nodiscard]] virtual bool isWindowPos(int x, int z) const;
    [[nodiscard]] virtual bool isWindowPos(int x, int y, int z) const;
    [[nodiscard]] virtual bool isOpeningPos(int x, int y, int z) const;
    [[nodiscard]] virtual bool isLadderPos(int x, int z, Rotation ladderUpDir, Rotation ladderDownDir) const;
    [[nodiscard]] virtual int  getLadderX(Rotation ladderDir) const;
    [[nodiscard]] virtual int  getLadderZ(Rotation ladderDir) const;
    virtual void               makeOpenings(Region& region, BlockBox const& sbb);
    virtual void               makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb);
    /// getYByStairs: the y of an opening at wall position `rx` that the stairs reach.
    [[nodiscard]] virtual int getYByStairs(int rx, RandomSource& random, Rotation direction);
    virtual void              makeWindows(Region& region, BlockBox const& sbb, bool real);
    virtual void              makeWindowBlock(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb, bool realWindows);
    virtual void              makeWindowBase(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb);
    /// makeStairs: the stairs of the tower's size (5, 7, 9 or 15); false when the size has none.
    virtual bool makeStairs(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual bool makeStairs5(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeStairs5flight(Region& region, BlockBox const& sbb, int height, Rotation rotation, bool useBirchWood);
    virtual bool makeStairs7(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeStairs7flight(Region& region, BlockBox const& sbb, int height, Rotation rotation, bool useBirchWood);
    virtual bool makeStairs9(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeStairs9flight(Region& region, BlockBox const& sbb, int height, Rotation rotation, bool useBirchWood);
    virtual bool makeStairs15(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeStairs15flight(Region& region, RandomSource& random, BlockBox const& sbb, int height, Rotation rotation, bool useBirchWood);
    virtual void generatePaintingsOnWall(Region& region, RandomSource& random, int howMany, int floorLevel, Orientation direction, int minSize,
                                         BlockBox const& sbb);
    /// getRandomWallSpot: a free wall position for a painting (nullopt: none found).
    [[nodiscard]] virtual std::optional<BlockPos> getRandomWallSpot(RandomSource& random, int floorLevel, Orientation direction, BlockBox const& sbb);
    virtual void makeGlyphBranches(Region& region, RandomSource& random, Block const& colour, BlockBox const& sbb);

    int                   height = 0;
    std::optional<std::string> roofType; ///< the piece type of the roof that fit (Java's roofType class)
    std::vector<BlockPos> openings;
    int                   highestOpening = 0;
    std::array<bool, 4>   openingTowards{false, false, true, false};
};

/// TowerMainComponent ("gloamingforest:TFLTMai"): the lich tower's main wing with the lich room.
class TowerMain : public TowerWing {
public:
    TowerMain(RandomSource& random, int genDepth, int x, int y, int z);

    void addChildren(Piece& start, PieceList& pieces, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] std::array<int, 3> getOutbuildingOpening(RandomSource& random, Rotation rotation);
    bool makeTowerOutbuilding(PieceList& pieces, RandomSource& random, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation);
    [[nodiscard]] TerrainAdjustment getTerrainAdjustment() const override;

protected:
    virtual void makeStairwayCrossings(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeStairCrossing(Region& region, RandomSource& random, int flight, BlockBox const& sbb);
    virtual void makeLichRoom(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeTowerPaintings(Region& region, RandomSource& random, BlockBox const& sbb);
    virtual void makeLichFloor(Region& region, int floorLevel, Rotation rotation, BlockBox const& sbb);
    virtual void decorateLichChandelier(Region& region, int floorLevel, BlockBox const& sbb);
    virtual void decoratePaintings(Region& region, RandomSource& random, int floorLevel, BlockBox const& sbb);
    virtual void decorateTorches(Region& region, RandomSource& random, int floorLevel, BlockBox const& sbb);
};

/// TowerBridgeComponent ("gloamingforest:TFLTBri"): a short bridge that leads to a new wing.
class TowerBridge : public TowerWing {
public:
    TowerBridge(int genDepth, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    void addChildren(Piece& start, PieceList& pieces, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    /// getWingBB: the box of the wing at the far end.
    [[nodiscard]] BlockBox getWingBB() const;

protected:
    int dSize   = 0; ///< the destination wing's size
    int dHeight = 0; ///< the destination wing's height
};

/// TowerOutbuildingComponent ("gloamingforest:TFLTOut"): a ground-level outbuilding wing.
class TowerOutbuilding : public TowerWing {
public:
    TowerOutbuilding(int genDepth, int x, int y, int z, int pSize, int pHeight, Orientation direction);

    void makeABeard(Piece& parent, PieceList& pieces, RandomSource& random) override;
    bool makeTowerWing(PieceList& pieces, RandomSource& random, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation direction) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// TowerBeardComponent ("gloamingforest:TFLTBea"): the stepped pyramid under a freestanding wing.
class TowerBeard : public ComponentOld {
public:
    TowerBeard(std::string typeId, int genDepth, TowerWing const& wing, int x, int y, int z);

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    /// "beardSize", "beardHeight".
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

protected:
    virtual void makePyramidBeard(Region& region, RandomSource& random, BlockBox const& sbb);

    int size   = 0; ///< wing size - 2
    int height = 0; ///< size / 2
};

/// TowerBeardAttachedComponent ("gloamingforest:TFLTBA"): the beard under a wing attached to a wall.
class TowerBeardAttached : public TowerBeard {
public:
    TowerBeardAttached(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;

protected:
    virtual void makeAttachedBeard(Region& region, RandomSource& random, BlockBox const& sbb);
};

/// TowerRoofComponent ("gloamingforest:TFLTRoo"): the roof base (no blocks of its own).
class TowerRoof : public ComponentOld {
public:
    TowerRoof(std::string typeId, int genDepth, int x, int y, int z);

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    /// fits: the roof intersects nothing but its tower.
    [[nodiscard]] virtual bool fits(TowerWing const& parent, PieceList const& pieces) const;
    /// "roofSize", "roofHeight".
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

protected:
    /// makeAttachedOverhangBB: hangs forwards off the wing (attached roofs).
    void makeAttachedOverhangBB(TowerWing const& wing);
    /// makeCapBB: sits on top of the wing.
    void makeCapBB(TowerWing const& wing);
    /// makeOverhangBB: overhangs every side by one block (freestanding roofs).
    void makeOverhangBB(TowerWing const& wing);

    int size   = 0;
    int height = 0;
};

/// TowerRoofFenceComponent ("gloamingforest:TFLTRF"): a fence ring, the last-resort roof.
class TowerRoofFence : public TowerRoof {
public:
    TowerRoofFence(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// TowerRoofGableForwardsComponent ("gloamingforest:TFLTRGF").
class TowerRoofGableForwards : public TowerRoof {
public:
    TowerRoofGableForwards(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] virtual int slopeChangeForSize() const;
};

/// TowerRoofPointyComponent ("gloamingforest:TFLTRP").
class TowerRoofPointy : public TowerRoof {
public:
    TowerRoofPointy(std::string typeId, int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] virtual int slopeChangeForSize() const;
};

/// TowerRoofPointyOverhangComponent ("gloamingforest:TFLTRPO").
class TowerRoofPointyOverhang : public TowerRoofPointy {
public:
    TowerRoofPointyOverhang(int genDepth, TowerWing const& wing, int x, int y, int z);
};

/// TowerRoofSlabComponent ("gloamingforest:TFLTRS").
class TowerRoofSlab : public TowerRoof {
public:
    TowerRoofSlab(std::string typeId, int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;

protected:
    virtual void makePyramidCap(Region& region, Block const& slabType, Block const& woodType, BlockBox const& sbb);
    virtual void makeConnectedCap(Region& region, Block const& slabType, Block const& woodType, BlockBox const& sbb);
};

/// TowerRoofAttachedSlabComponent ("gloamingforest:TFLTRAS").
class TowerRoofAttachedSlab : public TowerRoofSlab {
public:
    TowerRoofAttachedSlab(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// TowerRoofSlabForwardsComponent ("gloamingforest:TFLTRSF").
class TowerRoofSlabForwards : public TowerRoofSlab {
public:
    TowerRoofSlabForwards(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// TowerRoofStairsComponent ("gloamingforest:TFLTRSt").
class TowerRoofStairs : public TowerRoof {
public:
    TowerRoofStairs(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// TowerRoofStairsOverhangComponent ("gloamingforest:TFLTRStO").
class TowerRoofStairsOverhang : public TowerRoof {
public:
    TowerRoofStairsOverhang(int genDepth, TowerWing const& wing, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

} // namespace gf::structure::bases
