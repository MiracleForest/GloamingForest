// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerMainBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/TowerRoofMushroomComponent.java
//   src/main/java/twilightforest/world/components/structures/mushroomtower/MushroomTowerDecorator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The mushroom tower's pieces ("twilightforest:TFMT*") on F7's tower base.

#include <memory>

#include "feature/structure/bases/Tower.h"

namespace gf::feature::aurora {

/// MushroomTowerDecorator ("DecoMushroomTower").
[[nodiscard]] std::unique_ptr<structure::bases::Decorator> mushroomTowerDecorator();

/// MushroomTowerWingComponent ("twilightforest:TFMTWin").
class MushroomTowerWing : public structure::bases::TowerWing {
public:
    static constexpr int RANGE        = 200;
    static constexpr int FLOOR_HEIGHT = 4;
    static constexpr int MAIN_SIZE    = 15;

    MushroomTowerWing(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, structure::Orientation direction);

    bool hasBase    = false;
    bool isAscender = false;

    void addChildren(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;
    void postProcess(structure::Region& region, structure::RandomSource& rand, structure::BlockBox const& sbb, BlockPos const& pivot) override;
    bool makeTowerWing(structure::bases::PieceList& list, structure::RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       structure::Rotation rotation) override;
    void makeARoof(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;
    [[nodiscard]] std::array<int, 3> getValidOpening(structure::RandomSource& rand, structure::Rotation direction) override;
    [[nodiscard]] structure::Json    save() const override;
    void                             load(structure::Json const& data) override;

protected:
    [[nodiscard]] bool isBridge() const;
    std::array<int, 3> adjustCoordinates(int x, int y, int z, int wingSize, structure::Orientation direction, structure::bases::PieceList const& list) const;
    bool makeBridge(structure::bases::PieceList& list, structure::RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                    structure::Rotation rotation) override;
    bool makeBridge(structure::bases::PieceList& list, structure::RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                    structure::Rotation rotation, bool ascender);
    [[nodiscard]] int getYByStairs(int rx, structure::RandomSource& rand, structure::Rotation direction) override;
    void              makeDoorOpening(structure::Region& region, int dx, int dy, int dz, structure::BlockBox const& sbb) override;
    void              decorateFloor(structure::Region& region, structure::RandomSource& rand, int floor, int bottom, int top, std::optional<structure::Rotation> ladderUpDir,
                                    std::optional<structure::Rotation> ladderDownDir, structure::BlockBox const& sbb) override;

private:
    [[nodiscard]] bool isHighest(structure::BlockBox const& box, int size, structure::bases::PieceList const& list) const;
    bool makeMainBridge(structure::bases::PieceList& list, structure::RandomSource& rand, int index, int x, int y, int z, int wingHeight, structure::Rotation rotation);
    void makeTrunk(structure::Region& region, structure::BlockBox const& sbb);
    void makeFloorsForTower(structure::Region& region, structure::BlockBox const& sbb);
    void placeFloor(structure::Region& region, int dy, structure::BlockBox const& sbb);
};

/// MushroomTowerMainComponent ("twilightforest:TFMTMai").
class MushroomTowerMain : public MushroomTowerWing {
public:
    /// MushroomTowerMainComponent(rand, index, x, y, z): the tower at (x + 15, y + 4, z + 15), NORTH.
    MushroomTowerMain(structure::RandomSource& rand, int index, int x, int y, int z);
    MushroomTowerMain(int index, int x, int y, int z, int pSize, int pHeight, structure::Orientation direction);

    void addChildren(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;
    void makeARoof(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;

protected:
    void makeDoorOpening(structure::Region& region, int dx, int dy, int dz, structure::BlockBox const& sbb) override;

private:
    std::optional<structure::Rotation> makeAscenderTower(structure::bases::PieceList& list, structure::RandomSource& rand);
};

/// MushroomTowerBridgeComponent ("twilightforest:TFMTBri").
class MushroomTowerBridge : public MushroomTowerWing {
public:
    MushroomTowerBridge(std::string typeId, int index, int x, int y, int z, int pSize, int pHeight, structure::Orientation direction);

    void addChildren(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;
    void postProcess(structure::Region& region, structure::RandomSource& rand, structure::BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] structure::BlockBox getWingBB() const;
    [[nodiscard]] structure::Json     save() const override;
    void                              load(structure::Json const& data) override;

protected:
    int dSize   = 0;
    int dHeight = 0;
};

/// MushroomTowerMainBridgeComponent ("twilightforest:TFMTMB").
class MushroomTowerMainBridge : public MushroomTowerBridge {
public:
    MushroomTowerMainBridge(int index, int x, int y, int z, int pHeight, structure::Orientation direction);
    bool makeTowerWing(structure::bases::PieceList& list, structure::RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight,
                       structure::Rotation rotation) override;
};

/// TowerRoofMushroomComponent ("twilightforest:TFMTRoofMush").
class TowerRoofMushroom : public structure::bases::TowerRoof {
public:
    TowerRoofMushroom(int index, structure::bases::TowerWing const& wing, float pHang, int x, int y, int z);
    /// The load form.
    TowerRoofMushroom();
    void addChildren(structure::Piece& parent, structure::bases::PieceList& list, structure::RandomSource& rand) override;
    void postProcess(structure::Region& region, structure::RandomSource& rand, structure::BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void makeCircle(structure::Region& region, int y, int radius, int hollow, structure::BlockBox const& sbb);
};

/// Registers the TFMT* piece types and the decorator.
void registerMushroomTowerPieces();

} // namespace gf::feature::aurora
