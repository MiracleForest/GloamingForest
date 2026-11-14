// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/TFMaze.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18). TFMaze: the
// maze generator of the labyrinth (U23), the hedge maze (U18) and the hollow hill mazes. Cells sit
// at odd raw coordinates, walls at even ones (raw size 2 * cells + 1); 0 is a wall. The generation
// (recursive backtracker, rooms, exits) must consume `random` exactly like Java (F7 gtests it).

#include <climits>
#include <vector>

#include "feature/structure/bases/ComponentOld.h"

namespace gf::structure::bases {

class Maze {
public:
    static constexpr int OUT_OF_BOUNDS = INT_MIN;
    static constexpr int OOB           = OUT_OF_BOUNDS;
    static constexpr int ROOM          = 5;
    static constexpr int DOOR          = 6;

    /// TFMaze(cellsWidth, cellsDepth, random): defaults oddBias 3, evenBias 1, tall 3, head 0,
    /// roots 0, walls cut_mazestone, roots mazestone, torches minecraft:torch (rarity 0.75), no
    /// pillars / head / doors (air, door rarity 0).
    Maze(int cellsWidth, int cellsDepth, RandomSource& random);

    int const width;    ///< cells wide (x)
    int const depth;    ///< cells deep (z)
    int       oddBias  = 3; ///< corridor thickness
    int const evenBias = 1; ///< wall thickness (not used by upstream)
    int       tall     = 3; ///< wall blocks tall
    int       head     = 0; ///< blocks placed above the maze
    int       roots    = 0; ///< blocks placed under the maze (hedge mazes)
    int       type     = 0; ///< 1-3: the hollow hill sizes

    BlockSelector* wallBlocks       = nullptr; ///< optional; else wallBlockState
    Block const*   wallBlockState   = nullptr;
    Block const*   headBlockState   = nullptr;
    Block const*   rootBlockState   = nullptr;
    Block const*   pillarBlockState = nullptr;
    Block const*   doorBlockState   = nullptr;
    float          doorRarity       = 0.f;
    Block const*   torchBlockState  = nullptr;
    float          torchRarity      = 0.75f;

    [[nodiscard]] int  getCell(int x, int z) const;
    void               putWall(int sx, int sz, int dx, int dz, int value);
    [[nodiscard]] bool isWall(int sx, int sz, int dx, int dz) const;
    void               putRaw(int rawX, int rawZ, int value);
    [[nodiscard]] bool allCellsNonZero() const;
    void               resetCells();

    /// copyToStructure: draws the walls (and head, roots, pillars, doors, torches, trees) into
    /// `component` at piece-local (dx, dy, dz), clipped to `sbb`.
    void copyToStructure(Region& region, int dx, int dy, int dz, ComponentOld& component, BlockBox const& sbb, RandomSource& random);

    [[nodiscard]] bool shouldTorch(int rawX, int rawZ, RandomSource& random) const;
    [[nodiscard]] bool shouldPillar(int rawX, int rawZ) const;
    [[nodiscard]] bool shouldTree(int rawX, int rawZ, RandomSource& random) const;
    /// carveRoom1: a 3x3-cell room centred on cell (cx, cz) with its exits.
    void carveRoom1(int cx, int cz);
    /// add4Exits: one exit in the middle of each side.
    void add4Exits();
    /// generateRecursiveBacktracker / rbGen: carves the maze from cell (sx, sz).
    void generateRecursiveBacktracker(int sx, int sz, RandomSource& random);
    void rbGen(int sx, int sz, RandomSource& random);

protected:
    int const        rawWidth;
    int const        rawDepth;
    std::vector<int> storage;
};

} // namespace gf::structure::bases
