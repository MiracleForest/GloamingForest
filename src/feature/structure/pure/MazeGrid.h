// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/TFMaze.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Pure C++ (no mc/ll includes; tests/structure gtests it against Java goldens): the cell grid of
// TFMaze. bases::Maze keeps the storage (header-mandated layout) and forwards here. Cells sit at
// odd raw coordinates, walls at even ones; 0 is a wall.

#include <climits>
#include <cstdio>

#include "core/jrand/JRand.h"

namespace gf::structure::pure::maze {

using core::jrand::RandomSource;

inline constexpr int OUT_OF_BOUNDS = INT_MIN;
inline constexpr int OOB           = OUT_OF_BOUNDS;
inline constexpr int ROOM          = 5;
inline constexpr int DOOR          = 6;

/// A view on a maze's raw storage (rawWidth * rawDepth ints, z-major).
struct Grid {
    int* storage  = nullptr;
    int  rawWidth = 0;
    int  rawDepth = 0;
};

inline void putRaw(Grid g, int rawx, int rawz, int value) {
    if (rawx >= 0 && rawx < g.rawWidth && rawz >= 0 && rawz < g.rawDepth) g.storage[rawz * g.rawWidth + rawx] = value;
}
[[nodiscard]] inline int getRaw(Grid g, int rawx, int rawz) {
    if (rawx < 0 || rawx >= g.rawWidth || rawz < 0 || rawz >= g.rawDepth) return OUT_OF_BOUNDS;
    return g.storage[rawz * g.rawWidth + rawx];
}
[[nodiscard]] inline int getCell(Grid g, int x, int z) { return getRaw(g, x * 2 + 1, z * 2 + 1); }
inline void              putCell(Grid g, int x, int z, int value) { putRaw(g, x * 2 + 1, z * 2 + 1, value); }
[[nodiscard]] inline bool cellEquals(Grid g, int x, int z, int value) { return getCell(g, x, z) == value; }

/// The wall value, or OUT_OF_BOUNDS if the area is out of bounds or the cells are not adjacent.
[[nodiscard]] inline int getWall(Grid g, int sx, int sz, int dx, int dz) {
    if (dx == sx + 1 && dz == sz) return getRaw(g, sx * 2 + 2, sz * 2 + 1);
    if (dx == sx - 1 && dz == sz) return getRaw(g, sx * 2, sz * 2 + 1);
    if (dx == sx && dz == sz + 1) return getRaw(g, sx * 2 + 1, sz * 2 + 2);
    if (dx == sx && dz == sz - 1) return getRaw(g, sx * 2 + 1, sz * 2);
    return OUT_OF_BOUNDS; // upstream logs "Wall check out of bounds" here
}
inline void putWall(Grid g, int sx, int sz, int dx, int dz, int value) {
    if (dx == sx + 1 && dz == sz) putRaw(g, sx * 2 + 2, sz * 2 + 1, value);
    if (dx == sx - 1 && dz == sz) putRaw(g, sx * 2, sz * 2 + 1, value);
    if (dx == sx && dz == sz + 1) putRaw(g, sx * 2 + 1, sz * 2 + 2, value);
    if (dx == sx && dz == sz - 1) putRaw(g, sx * 2 + 1, sz * 2, value);
}
[[nodiscard]] inline bool isWall(Grid g, int sx, int sz, int dx, int dz) { return getWall(g, sx, sz, dx, dz) == 0; }

[[nodiscard]] inline bool allCellsNonZero(Grid g, int width, int depth) {
    for (int x = 0; x < width; x++)
        for (int z = 0; z < depth; z++)
            if (getCell(g, x, z) == 0) return false;
    return true;
}

/// Should we put a torch here? (the in-between spots where x and z are even)
[[nodiscard]] inline bool shouldTorch(Grid g, int rx, int rz, RandomSource& rand, float torchRarity) {
    if (getRaw(g, rx + 1, rz) == OOB || getRaw(g, rx - 1, rz) == OOB || getRaw(g, rx, rz + 1) == OOB || getRaw(g, rx, rz - 1) == OOB) return false;
    if ((getRaw(g, rx + 1, rz) == 0 && getRaw(g, rx - 1, rz) == 0) || (getRaw(g, rx, rz + 1) == 0 && getRaw(g, rx, rz - 1) == 0)) return false;
    return rand.nextFloat() <= torchRarity;
}

/// Should we put a pillar here? (`pillarIsAir`: no pillar block defined)
[[nodiscard]] inline bool shouldPillar(Grid g, int rx, int rz, bool pillarIsAir) {
    if (pillarIsAir) return false;
    if (getRaw(g, rx + 1, rz) == OOB || getRaw(g, rx - 1, rz) == OOB || getRaw(g, rx, rz + 1) == OOB || getRaw(g, rx, rz - 1) == OOB) return false;
    return (getRaw(g, rx + 1, rz) != 0 || getRaw(g, rx - 1, rz) != 0) && (getRaw(g, rx, rz + 1) != 0 || getRaw(g, rx, rz - 1) != 0);
}

/// Should we put a tree instead of a post? (corners and exits, else 1 in 50)
[[nodiscard]] inline bool shouldTree(Grid g, int rx, int rz, RandomSource& rand) {
    if ((rx == 0 || rx == g.rawWidth - 1) && (getRaw(g, rx, rz + 1) != 0 || getRaw(g, rx, rz - 1) != 0)) return true;
    if ((rz == 0 || rz == g.rawDepth - 1) && (getRaw(g, rx + 1, rz) != 0 || getRaw(g, rx - 1, rz) != 0)) return true;
    return rand.nextInt(50) == 0;
}

/// A 3x3 cell room with exits in every direction (upstream's putCell(rx, ...) quirk kept: the exit
/// cells are addressed with raw coordinates, as in Java).
inline void carveRoom1(Grid g, int cx, int cz) {
    int rx = cx * 2 + 1;
    int rz = cz * 2 + 1;
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++) putRaw(g, rx + i, rz + j, ROOM);
    putCell(g, rx, rz + 1, 0);
    putCell(g, rx, rz - 1, 0);
    putCell(g, rx + 1, rz, 0);
    putCell(g, rx - 1, rz, 0);
    if (getRaw(g, rx, rz + 4) != OUT_OF_BOUNDS) putRaw(g, rx, rz + 3, ROOM);
    if (getRaw(g, rx, rz - 4) != OUT_OF_BOUNDS) putRaw(g, rx, rz - 3, ROOM);
    if (getRaw(g, rx + 4, rz) != OUT_OF_BOUNDS) putRaw(g, rx + 3, rz, ROOM);
    if (getRaw(g, rx - 4, rz) != OUT_OF_BOUNDS) putRaw(g, rx - 3, rz, ROOM);
}

/// Four exits in the middle of each side.
inline void add4Exits(Grid g) {
    int hx = g.rawWidth / 2 + 1;
    int hz = g.rawDepth / 2 + 1;
    putRaw(g, hx, 0, ROOM);
    putRaw(g, hx, g.rawDepth - 1, ROOM);
    putRaw(g, 0, hz, ROOM);
    putRaw(g, g.rawWidth - 1, hz, ROOM);
}

/// Recursive backtracker: marks the cell visited, carves to a random unvisited neighbour and
/// recurses, then retries this cell twice (as upstream does).
inline void rbGen(Grid g, int sx, int sz, RandomSource& rand, float doorRarity) {
    putCell(g, sx, sz, 1);

    int unvisited = 0;
    if (cellEquals(g, sx + 1, sz, 0)) unvisited++;
    if (cellEquals(g, sx - 1, sz, 0)) unvisited++;
    if (cellEquals(g, sx, sz + 1, 0)) unvisited++;
    if (cellEquals(g, sx, sz - 1, 0)) unvisited++;
    if (unvisited == 0) return;

    int rn = rand.nextInt(unvisited);
    int dx = 0, dz = 0;
    if (cellEquals(g, sx + 1, sz, 0)) {
        if (rn == 0) {
            dx = sx + 1;
            dz = sz;
        }
        rn--;
    }
    if (cellEquals(g, sx - 1, sz, 0)) {
        if (rn == 0) {
            dx = sx - 1;
            dz = sz;
        }
        rn--;
    }
    if (cellEquals(g, sx, sz + 1, 0)) {
        if (rn == 0) {
            dx = sx;
            dz = sz + 1;
        }
        rn--;
    }
    if (cellEquals(g, sx, sz - 1, 0)) {
        if (rn == 0) {
            dx = sx;
            dz = sz - 1;
        }
    }

    if (rand.nextFloat() <= doorRarity) putWall(g, sx, sz, dx, dz, DOOR);
    else putWall(g, sx, sz, dx, dz, 2);

    rbGen(g, dx, dz, rand, doorRarity);
    rbGen(g, sx, sz, rand, doorRarity);
    rbGen(g, sx, sz, rand, doorRarity);
}

} // namespace gf::structure::pure::maze
