// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/courtyard/StructureMazeGenerator.java
//   src/main/java/gloamingforest/world/components/structures/courtyard/CourtyardMain.java
//   src/main/java/gloamingforest/enums/Diagonals.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The Naga courtyard's maze and piece layout (U20), pure: StructureMazeGenerator#generateMaze and
// #addChildren (processInnerWallsAndFloor, processOuterWalls) as a list of piece records in Java's
// order. src/feature/naga/Courtyard.cpp turns each record into its template piece.

#include <array>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"

namespace gf::core::naga {

/// CourtyardMain.ROW_OF_CELLS and RADIUS.
inline constexpr int kRowOfCells = 8;
inline constexpr int kRadius     = static_cast<int>((((kRowOfCells - 2) / 2.0F) * 12.0F) + 8); // 44
/// The connectome is one cell smaller than the cell grid.
inline constexpr int kMazeSize = kRowOfCells - 1;

/// WallFacing bits.
inline constexpr int kEast     = 0b0001;
inline constexpr int kSouth    = 0b0010;
inline constexpr int kWest     = 0b0100;
inline constexpr int kNorth    = 0b1000;
inline constexpr int kExcluded = 0b10000;

using Maze = std::array<std::array<int, kMazeSize>, kMazeSize>; ///< maze[x][y]
/// Diagonals (TOP_RIGHT, BOTTOM_RIGHT, BOTTOM_LEFT, TOP_LEFT) x {z cells, x cells}.
using CornerClipping = std::array<std::array<int, 2>, 4>;

/// StructureMazeGenerator#generateMaze(maze, cornerClippings, random, 8, 8, 2).
void generateMaze(Maze& maze, CornerClipping& clipping, jrand::RandomSource& random);

/// The piece kinds of the courtyard (their StructurePieceType ids in pieceTypeId).
enum class PieceKind {
    HedgeCap,          ///< TFNCCp  NagaCourtyardHedgeCapComponent
    HedgeCapPillar,    ///< TFNCCpP NagaCourtyardHedgeCapPillarComponent
    HedgeCorner,       ///< TFNCCr
    HedgeT,            ///< TFNCT
    HedgeLine,         ///< TFNCLn
    HedgeIntersection, ///< TFNCIs
    HedgePadder,       ///< TFNCPd
    Terrace,           ///< TFNCTe CourtyardTerrace
    TerraceHedge,      ///< TFNCHe CourtyardTerraceHedge
    Path,              ///< TFNCPa CourtyardPathPiece
    Wall,              ///< TFNCWl CourtyardWall
    WallPadder,        ///< TFNCWP CourtyardWallPadder
    WallCornerOuter,   ///< TFNCWC CourtyardWallCornerOuter
    WallCornerInner,   ///< TFNCWA CourtyardWallCornerInner
};
[[nodiscard]] char const* pieceTypeId(PieceKind kind);

/// One piece StructureMazeGenerator#addChildren adds: the constructor arguments (i, x, y, z,
/// rotation as Rotation.values() index) and, for terraces, the template.
struct PieceRecord {
    PieceKind   kind     = PieceKind::HedgeLine;
    int         genDepth = 0;
    int         x = 0, y = 0, z = 0;
    int         rotation = 0;
    std::string terraceTemplate; ///< "gloamingforest:courtyard/terrace_*" (Terrace only)
};

/// CourtyardMain's boxes for a start at (x, y, z) (the piece's own coordinates: upstream's x + 1, z + 1).
[[nodiscard]] BlockBox mainBox(int x, int y, int z);
[[nodiscard]] BlockBox sizeConstraints(int x, int y, int z);

/// StructureMazeGenerator#addChildren: processInnerWallsAndFloor then processOuterWalls, drawing
/// from `random` (Structure.GenerationContext#random) in Java's order.
[[nodiscard]] std::vector<PieceRecord> layoutPieces(Maze const& maze, CornerClipping const& clipping, BlockBox const& constraints,
                                                    jrand::RandomSource& random);

/// The box-drawing string of a maze cell (StructureMazeGenerator#getStringFromFacings), for logs.
[[nodiscard]] std::string facingString(int directions);

} // namespace gf::core::naga
