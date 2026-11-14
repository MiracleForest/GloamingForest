// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/GloamingStructureComponentOld.java
//   src/main/java/gloamingforest/world/components/structures/GloamingJigsawPiece.java
//   src/main/java/gloamingforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerWingComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/KnightStrongholdComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonRoom31Component.java
//   src/main/java/gloamingforest/world/components/structures/courtyard/CourtyardTerrace.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/*.java
//   src/main/java/gloamingforest/init/GloamingStructurePieceTypes.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U2 (terrain), pure: the beardifier of upstream 4.8 as it runs on NeoForge 21.1 (gtested).
//
// NeoForge patches Beardifier#forStructuresInChunk: for every start whose structure has a terrain
// adaptation (StructureManager#startsForStructure, terrainAdaptation != NONE) and every piece of it
// within 12 blocks of the chunk (StructurePiece#isCloseToChunk), a piece that implements
// PieceBeardifierModifier gives its own Rigid (getBeardifierBox, getTerrainAdjustment,
// getGroundLevelDelta; nothing when its adjustment is NONE); any other piece gets a Rigid of its
// bounding box with the structure's adaptation and ground delta 0 (upstream has no
// PoolElementStructurePiece, so no jigsaw junctions). GloamingStructureComponentOld implements the
// interface with NONE, so most of upstream's component pieces add no beard at all; the overrides are
// tabled in gloamingPieceBeard below, per piece type id (GloamingStructurePieceTypes).
//
// Beardifier#compute then sums the Rigids (vanilla's kernel, with the ground level delta and no
// clamp), which rigidDensity reproduces.

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "core/landmark/Clearance.h"

namespace gf::core::terrain::beard {

using core::landmark::TerrainAdjustment;

/// Beardifier.Rigid.
struct Rigid {
    BlockBox          box;
    TerrainAdjustment adjustment       = TerrainAdjustment::None;
    int               groundLevelDelta = 0;
};

/// Beardifier#compute's term of one Rigid at a block position.
[[nodiscard]] double rigidDensity(Rigid const& rigid, int x, int y, int z);
/// Beardifier#compute over the Rigids (their sum, in order; no clamp).
[[nodiscard]] double beardifierDensity(std::vector<Rigid> const& rigids, int x, int y, int z);

/// StructurePiece#isCloseToChunk(chunkPos, distance) for a piece's bounding box.
[[nodiscard]] bool isCloseToChunk(BlockBox const& pieceBox, int chunkX, int chunkZ, int distance = 12);

/// What a piece gives the beardifier.
struct PieceBeard {
    /// Whether the piece is a PieceBeardifierModifier (else it takes the structure's adaptation and
    /// its bounding box with ground delta 0).
    bool              modifier         = false;
    TerrainAdjustment adjustment       = TerrainAdjustment::None;
    BlockBox          box;              ///< getBeardifierBox (modifier pieces)
    int               groundLevelDelta = 0;
};

/// TerrainAdjustment by its Java name ("BURY", "beard_box", ...; anything else: NONE).
[[nodiscard]] TerrainAdjustment adjustmentOf(std::string const& name);

/// upstream 4.8's beardifier answer for a piece of type `typeId` ("gloamingforest:GloamingHedge", the path
/// compared without case as Java's registry lower-cases it) with bounding box `box` and saved tag
/// `tag` (StructurePiece#createTag: GloamingJigsawPiece's "terrain_adaptation" / "ground_offset",
/// LichTowerWingBeard's "gen_ground", LichYardBox's "dirt_mix"). nullopt: not one of upstream's piece
/// types (the caller decides: a registered override, the loaded piece, else a plain piece).
[[nodiscard]] std::optional<PieceBeard> gloamingPieceBeard(std::string const& typeId, BlockBox const& box, nlohmann::json const& tag);

/// Beardifier#forStructuresInChunk's Rigid for one piece of a start whose structure has adaptation
/// `structureAdaptation` (nullopt: the piece adds nothing). `pieceBox` is getBoundingBox.
[[nodiscard]] std::optional<Rigid> rigidOf(PieceBeard const& piece, BlockBox const& pieceBox, TerrainAdjustment structureAdaptation);

} // namespace gf::core::terrain::beard
