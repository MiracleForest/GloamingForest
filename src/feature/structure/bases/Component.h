// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponent.java
//   src/main/java/twilightforest/world/components/structures/TFStructureDecorator.java
//   src/main/java/twilightforest/world/components/structures/UtilityPiece.java
//   src/main/java/twilightforest/world/components/structures/SpawnIndexProvider.java
//   src/main/java/twilightforest/world/components/structures/util/SortablePiece.java
//   src/main/java/twilightforest/world/components/structures/util/ProgressionPiece.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18). The root of
// TF's piece hierarchy: TFStructureComponent (spawn index, decorator, progression protection), the
// decorator block sets and the utility piece. Declarations only: the behaviour is F7's, translated
// from the Java files above (bases/Bases.cpp holds the neutral stubs until then).
//
// Java -> C++: WorldGenLevel -> worldgen::Region&, BoundingBox -> BlockBox, BlockState -> Block const&,
// Direction (horizontal) -> Orientation, EntityType / ResourceKey<LootTable> -> id strings,
// StructurePieceAccessor -> PieceList&, CompoundTag save data -> Json (Piece::save / load).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "feature/structure/Structure.h"

namespace gf::structure::bases {

/// The pieces of a structure start (Java's StructurePieceAccessor / StructurePiecesBuilder).
using PieceList = std::vector<std::unique_ptr<Piece>>;
/// StructurePieceAccessor#findCollisionPiece: the first piece of `pieces` whose box intersects `box`.
[[nodiscard]] Piece* findCollisionPiece(PieceList const& pieces, BlockBox const& box);

/// TFStructureDecorator: the block set a structure family builds with. W1 units register their
/// subclasses (StructureDecoratorDarkTower "DecoDarkTower", IceTowerDecorator "DecoIceTower",
/// MushroomTowerDecorator "DecoMushroomTower", StrongholdDecorator "DecoStronghold",
/// StructureTFDecoratorCastle "DecoCastle") by their save string.
struct Decorator {
    virtual ~Decorator() = default;
    Block const* blockState    = nullptr; ///< minecraft:stone
    Block const* accentState   = nullptr; ///< minecraft:cobblestone
    Block const* stairState    = nullptr; ///< minecraft:stone_stairs
    Block const* fenceState    = nullptr; ///< minecraft:oak_fence
    Block const* pillarState   = nullptr; ///< minecraft:stone_bricks
    Block const* platformState = nullptr; ///< minecraft:stone_slab
    Block const* floorState    = nullptr; ///< minecraft:stone_bricks
    Block const* roofState     = nullptr; ///< minecraft:stone_bricks
    /// StructurePiece.BlockSelector randomBlocks (default: the stronghold stones selector).
    BlockSelector* randomBlocks = nullptr;
    /// getDecoString: the save string of this decorator ("" for the default one).
    [[nodiscard]] virtual std::string decoString() const { return {}; }
};
/// The default decorator (TFStructureDecorator's field initialisers).
[[nodiscard]] std::unique_ptr<Decorator> defaultDecorator();
/// TFStructureDecorator.getDecoFor: a registered decorator by save string, else the default one.
[[nodiscard]] std::unique_ptr<Decorator> decoratorFor(std::string const& decoString);
void registerDecorator(std::string const& decoString, std::function<std::unique_ptr<Decorator>()> factory);

/// TFStructureComponent: StructurePiece + SpawnIndexProvider + ProgressionPiece.
class Component : public Piece {
public:
    /// TFStructureComponent(type, i, boundingBox).
    Component(std::string typeId, int genDepth, BlockBox const& box);
    /// TFStructureComponent(type, i, x, y, z): a one-block box at (x, y, z).
    Component(std::string typeId, int genDepth, int x, int y, int z);

    std::unique_ptr<Decorator> deco;           ///< saved as "deco" (decoString)
    int                        spawnListIndex = 0; ///< saved as "si"

    /// SpawnIndexProvider#getSpawnIndex: the controlled-spawn list of this piece (-1: none).
    [[nodiscard]] virtual int getSpawnIndex() const { return spawnListIndex; }
    /// ProgressionPiece#isComponentProtected: under block protection while progression is locked.
    [[nodiscard]] virtual bool isComponentProtected() const { return true; }

    /// "si", "deco", "rot" plus Piece's fields.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

protected:
    /// TFStructureComponent#placeBlock: also schedules the post-processing TF's list of blocks needs
    /// (fences, torches, ladders, stairs, chests, liquids, force fields, thorns, gravel ...).
    void placeBlockScheduled(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb);
    /// setInvisibleTextEntity (debug text displays; no-op unless debugging).
    void setInvisibleTextEntity(Region& region, int x, int y, int z, BlockBox const& sbb, std::string const& text, bool forcePlace, float yOffset);
};

/// SpawnIndexProvider.Deny: a piece without controlled spawns.
class NoSpawnIndex {
public:
    virtual ~NoSpawnIndex() = default;
    [[nodiscard]] virtual int getSpawnIndex() const { return -1; }
};

/// SortablePiece: 0 is the default; lower keys generate sooner, so later pieces overwrite them.
class SortablePiece {
public:
    virtual ~SortablePiece() = default;
    [[nodiscard]] virtual int getSortKey() const = 0;
};

/// UtilityPiece ("twilightforest:TFUtilityPiece"): an empty piece that only reserves a box;
/// `allowFeatures` (saved "allow_features") lets decoration generate inside it.
class UtilityPiece : public Piece {
public:
    UtilityPiece(int genDepth, BlockBox const& box, bool allowFeatures = true);
    bool allowFeatures = true;
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
};

} // namespace gf::structure::bases
