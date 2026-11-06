// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeRuinsComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeMoundComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeUpperEntranceComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeEntranceShaftComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MinotaurMazeComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeRoomComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeDeadEndComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MazeCorridorComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U23 (labyrinth): the 27 minotaur maze piece types on F7's TFStructureComponentOld and TFMaze
// bases (rule R19: procedural pieces translated exactly). Unit-internal: the structure module and
// its selftests use it.

#include <array>
#include <memory>
#include <string>

#include "feature/structure/bases/ComponentOld.h"
#include "feature/structure/bases/Maze.h"

namespace gf::feature::labyrinth::maze {

using ::gf::structure::BlockBox;
using ::gf::structure::Json;
using ::gf::structure::Orientation;
using ::gf::structure::Piece;
using ::gf::structure::RandomSource;
using ::gf::structure::Region;
using ::gf::structure::Rotation;
using ::gf::structure::bases::ComponentOld;
using ::gf::structure::bases::PieceList;

inline constexpr char kLabyrinth[] = "twilightforest:labyrinth";

inline constexpr char kRuins[]         = "twilightforest:TFMMRuins";
inline constexpr char kMound[]         = "twilightforest:TFMMMound";
inline constexpr char kUpperEntrance[] = "twilightforest:TFMMUE";
inline constexpr char kEntranceShaft[] = "twilightforest:TFMMES";
inline constexpr char kMaze[]          = "twilightforest:TFMMaze";
inline constexpr char kRoom[]          = "twilightforest:TFMMR";
inline constexpr char kRoomExit[]      = "twilightforest:TFMMRE";
inline constexpr char kRoomBoss[]      = "twilightforest:TFMMRB";
inline constexpr char kRoomCollapse[]  = "twilightforest:TFMMRC";
inline constexpr char kMushRoom[]      = "twilightforest:TFMMMR";
inline constexpr char kRoomFountain[]  = "twilightforest:TFMMRF";
inline constexpr char kRoomVault[]     = "twilightforest:TFMMRV";
inline constexpr char kRoomSpawner[]   = "twilightforest:TFMMRSC";
inline constexpr char kDeadEnd[]       = "twilightforest:TFMMDE";
inline constexpr char kDeadEndChest[]  = "twilightforest:TFMMDEC";
inline constexpr char kDeadEndTrap[]   = "twilightforest:TFMMDETrC";
inline constexpr char kDeadEndTrip[]   = "twilightforest:TFMMDETC";
inline constexpr char kDeadEndTorch[]  = "twilightforest:TFMMDET";
inline constexpr char kDeadEndWater[]  = "twilightforest:TFMMDEF";
inline constexpr char kDeadEndLava[]   = "twilightforest:TFMMDEFL";
inline constexpr char kDeadEndPaint[]  = "twilightforest:TFMMDEP";
inline constexpr char kDeadEndRoots[]  = "twilightforest:TFMMDER";
inline constexpr char kDeadEndShroom[] = "twilightforest:TFMMDES";
inline constexpr char kCorridor[]      = "twilightforest:TFMMC";
inline constexpr char kCorridorIron[]  = "twilightforest:TFMMCIF";
inline constexpr char kCorridorRoots[] = "twilightforest:TFMMCR";
inline constexpr char kCorridorShroom[] = "twilightforest:TFMMCS";

inline constexpr char kLootDeadEnd[]  = "twilightforest:labyrinth_dead_end";
inline constexpr char kLootRoom[]     = "twilightforest:labyrinth_room";
inline constexpr char kLootVault[]    = "twilightforest:labyrinth_vault";
inline constexpr char kLootJackpot[]  = "twilightforest:labyrinth_vault_jackpot";
inline constexpr char kBossSpawner[]  = "twilightforest:minoshroom_boss_spawner";
inline constexpr char kMinotaur[]     = "twilightforest:minotaur";

/// TFDimensionData.SEALEVEL / the Twilight Forest generator's sea level.
inline constexpr int kSeaLevel = 0;

/// MazeRuinsComponent ("TFMMRuins"): the empty root piece; its children are the two maze levels,
/// the entrance shaft and the mound with the ruin on top.
class Ruins final : public ComponentOld {
public:
    Ruins(int i, int x, int y, int z);
    void addChildren(Piece& start, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// MazeMoundComponent ("TFMMMound"): 35 x 12 x 35; the mound is the structure's terraformer.
class Mound final : public ComponentOld {
public:
    static constexpr int DIAMETER = 35;
    Mound(int i, RandomSource& rand, int x, int y, int z);
    void addChildren(Piece& start, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// MazeUpperEntranceComponent ("TFMMUE"): the ruin on the mound.
class UpperEntrance final : public ComponentOld {
public:
    UpperEntrance(int i, RandomSource& rand, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// MazeEntranceShaftComponent ("TFMMES"): the brick shaft from the ruin down to the maze.
class EntranceShaft final : public ComponentOld {
public:
    EntranceShaft(int i, RandomSource& rand, int x, int y, int z);
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// MinotaurMazeComponent ("TFMMaze"): one maze level of 22 x 22 cells.
class MinotaurMaze final : public ComponentOld {
public:
    MinotaurMaze(int index, int x, int y, int z, int entranceX, int entranceZ, int level, RandomSource& random);
    MinotaurMaze(int index, int x, int y, int z, int level, RandomSource& random);
    /// The NBT constructor: the maze rebuilt from the box (seed) and the saved room coordinates.
    explicit MinotaurMaze(Json const& saved);

    void addChildren(Piece& start, PieceList& list, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;

    [[nodiscard]] int                       level() const { return mLevel; }
    [[nodiscard]] std::array<int, 14> const& roomCoords() const { return mRcoords; }
    [[nodiscard]] ::gf::structure::bases::Maze const& grid() const { return *mMaze; }

    static constexpr int getMazeSize() { return 22; }
    static constexpr int getRadius() { return static_cast<int>(getMazeSize() * 2.5); }
    static constexpr int getDiameter() { return getMazeSize() * 5; }

private:
    void addRoomsToMaze(int entranceX, int entranceZ, int nrooms, RandomSource& rand);
    void configureMaze();
    [[nodiscard]] long long getSeed() const;
    std::unique_ptr<Piece> makeRoom(RandomSource& random, int i, int dx, int dz) const;
    std::unique_ptr<Piece> makeDeadEnd(RandomSource& random, int dx, int dz, Orientation rotation) const;
    std::unique_ptr<Piece> makeCorridor(RandomSource& random, int dx, int dz, Orientation rotation) const;
    void decorateDeadEndsCorridors(RandomSource& random, PieceList& list);

    std::unique_ptr<::gf::structure::bases::Maze> mMaze;
    std::array<int, 14>                          mRcoords{};
    int                                          mLevel = 1;
};

/// MazeRoomComponent ("TFMMR") and its subclasses (the room kind is the type id).
class Room final : public ComponentOld {
public:
    Room(std::string typeId, int i, RandomSource& rand, int x, int y, int z);
    explicit Room(std::string typeId) : ComponentOld(std::move(typeId), 0, 0, 0, 0) {}
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void roomBase(Region& region, BlockBox const& sbb);
    void exitRoom(Region& region, BlockBox const& sbb);
    void bossRoom(Region& region, RandomSource& rand, BlockBox const& sbb);
    void collapseRoom(Region& region, RandomSource& rand, BlockBox const& sbb);
    void mushRoom(Region& region, RandomSource& rand, BlockBox const& sbb);
    void makeMediumMushroom(Region& region, BlockBox const& sbb, int mx, int my, int mz, bool red);
    void fountainRoom(Region& region, BlockBox const& sbb);
    void vaultRoom(Region& region, RandomSource& rand, BlockBox const& sbb);
    void setVaultDoubleLootChest(Region& region, int x, int z, int x2, int z2, Orientation facing, BlockBox const& sbb, bool jackpot);
    void spawnerChestsRoom(Region& region, BlockBox const& sbb);
    void placePillarEnclosure(Region& region, BlockBox const& sbb, int dx, int dz);
};

/// MazeDeadEndComponent ("TFMMDE") and MazeCorridorComponent ("TFMMC") with their subclasses: the
/// 5 x 5 cell decorations (the kind is the type id).
class Cell final : public ComponentOld {
public:
    Cell(std::string typeId, int i, int x, int y, int z, Orientation rotation);
    explicit Cell(std::string typeId) : ComponentOld(std::move(typeId), 0, 0, 0, 0) {}
    void postProcess(Region& region, RandomSource& random, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void deadEnd(Region& region, BlockBox const& sbb);
    void deadEndChest(Region& region, BlockBox const& sbb, bool trapped);
    void deadEndTripwire(Region& region, BlockBox const& sbb);
    void deadEndTrapped(Region& region, BlockBox const& sbb);
    void deadEndTorches(Region& region, BlockBox const& sbb);
    void deadEndFountain(Region& region, BlockBox const& sbb);
    void deadEndFountainLava(Region& region, BlockBox const& sbb);
    void deadEndPainting(Region& region, BlockBox const& sbb);
    void deadEndRoots(Region& region, RandomSource& rand, BlockBox const& sbb);
    void deadEndShrooms(Region& region, RandomSource& rand, BlockBox const& sbb);
    void corridor(Region& region, BlockBox const& sbb);
    void corridorIronFence(Region& region, BlockBox const& sbb);
    void corridorRoots(Region& region, RandomSource& rand, BlockBox const& sbb);
    void corridorShrooms(Region& region, RandomSource& rand, BlockBox const& sbb);
};

/// Whether `typeId` names a piece whose spawn list index is Integer.MAX_VALUE (no spawns).
[[nodiscard]] bool deniesSpawns(std::string const& typeId);

/// The piece types (structure::registerPieceType; once per process).
void registerPieces();

/// LabyrinthStructure#getFirstPiece (MazeRuinsComponent at (x + 5, y, z + 5)) and
/// LandmarkStructure#generateFromStartingPiece with Structure.GenerationContext#random().
void layout(RandomSource& context, int x, int y, int z, PieceList& pieces);

} // namespace gf::feature::labyrinth::maze
