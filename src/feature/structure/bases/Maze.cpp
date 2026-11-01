// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFMaze.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The grid logic lives in pure/MazeGrid.h (gtested against Java); this file holds the maze's
// block settings and copies it into a piece.
#include "feature/structure/bases/Maze.h"

#include <algorithm>

#include "mc/world/level/block/Block.h"

#include "feature/features/Features.h"
#include "feature/structure/Internal.h"
#include "feature/structure/pure/MazeGrid.h"

namespace gf::structure::bases {

namespace g = pure::maze;

namespace {
#define GF_GRID g::Grid{storage.data(), rawWidth, rawDepth}
bool isEven(int n) { return n % 2 == 0; }
} // namespace

Maze::Maze(int cellsWidth, int cellsDepth, RandomSource&)
: width(cellsWidth),
  depth(cellsDepth),
  rawWidth(cellsWidth * 2 + 1),
  rawDepth(cellsDepth * 2 + 1),
  storage(static_cast<std::size_t>(rawWidth) * static_cast<std::size_t>(rawDepth), 0) {
    wallBlockState   = &blocks::java("twilightforest:cut_mazestone");
    rootBlockState   = &blocks::java("twilightforest:mazestone");
    torchBlockState  = &blocks::java("minecraft:torch");
    pillarBlockState = &blocks::air();
    headBlockState   = &blocks::air();
    doorBlockState   = &blocks::air();
}

int  Maze::getCell(int x, int z) const { return g::getCell(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, x, z); }
void Maze::putWall(int sx, int sz, int dx, int dz, int value) { g::putWall(GF_GRID, sx, sz, dx, dz, value); }
bool Maze::isWall(int sx, int sz, int dx, int dz) const {
    return g::isWall(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, sx, sz, dx, dz);
}
void Maze::putRaw(int rawX, int rawZ, int value) { g::putRaw(GF_GRID, rawX, rawZ, value); }
bool Maze::allCellsNonZero() const { return g::allCellsNonZero(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, width, depth); }
void Maze::resetCells() { std::fill(storage.begin(), storage.end(), 0); }

bool Maze::shouldTorch(int rawX, int rawZ, RandomSource& random) const {
    return g::shouldTorch(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, rawX, rawZ, random, torchRarity);
}
bool Maze::shouldPillar(int rawX, int rawZ) const {
    return g::shouldPillar(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, rawX, rawZ, !pillarBlockState || pillarBlockState->isAir());
}
bool Maze::shouldTree(int rawX, int rawZ, RandomSource& random) const {
    return g::shouldTree(g::Grid{const_cast<int*>(storage.data()), rawWidth, rawDepth}, rawX, rawZ, random);
}
void Maze::carveRoom1(int cx, int cz) { g::carveRoom1(GF_GRID, cx, cz); }
void Maze::add4Exits() { g::add4Exits(GF_GRID); }
void Maze::generateRecursiveBacktracker(int sx, int sz, RandomSource& random) { rbGen(sx, sz, random); }
void Maze::rbGen(int sx, int sz, RandomSource& random) { g::rbGen(GF_GRID, sx, sz, random, doorRarity); }

void Maze::copyToStructure(Region& region, int dx, int dy, int dz, ComponentOld& component, BlockBox const& sbb, RandomSource& rand) {
    auto put = [&](Block const* b, int x, int y, int z) {
        if (b) component.placeBlock(region, *b, x, y, z, sbb);
    };
    auto putWallBlock = [&](int x, int y, int z) {
        // TF draws from world.getRandom() here; the region exposes no random, so the copy's own is used
        if (wallBlocks) component.placeBlock(region, wallBlocks->next(rand, x, y, z, true), x, y, z, sbb);
        else put(wallBlockState, x, y, z);
    };
    auto makeWallThing = [&](int mdx, int mdz, int even, int odd) {
        for (int y = 0; y < head; y++) put(headBlockState, mdx + even, dy + tall + y, mdz + odd);
        for (int y = 0; y < tall; y++) putWallBlock(mdx + even, dy + y, mdz + odd);
        for (int y = 1; y <= roots; y++) put(rootBlockState, mdx + even, dy - y, mdz + odd);
    };
    auto rawAt = [&](int x, int z) { return g::getRaw(g::Grid{storage.data(), rawWidth, rawDepth}, x, z); };

    for (int x = 0; x < rawWidth; x++) {
        for (int z = 0; z < rawDepth; z++) {
            int v = rawAt(x, z);
            if (v == 0) {
                int mdx = dx + (x / 2 * (evenBias + oddBias));
                int mdz = dz + (z / 2 * (evenBias + oddBias));
                if (evenBias > 1) {
                    mdx--;
                    mdz--;
                }
                if (isEven(x) && isEven(z)) {
                    if (type == 4 && shouldTree(x, z, rand)) {
                        // putCanopyTree: only where the middle of the tree is in this chunk
                        BlockPos pos = component.getBlockPosWithOffset(mdx, dy, mdz);
                        if (sbb.contains(pos.x, pos.y, pos.z)) {
                            if (!features::place("twilightforest:tree/canopy_tree", region, pos, rand)) makeWallThing(mdx, mdz, 0, 0);
                        }
                    } else {
                        for (int even = 0; even < evenBias; even++)
                            for (int even2 = 0; even2 < evenBias; even2++) {
                                for (int y = 0; y < head; y++) put(headBlockState, mdx + even, dy + tall + y, mdz + even2);
                                for (int y = 0; y < tall; y++) {
                                    if (shouldPillar(x, z)) put(pillarBlockState, mdx + even, dy + y, mdz + even2);
                                    else putWallBlock(mdx + even, dy + y, mdz + even2);
                                }
                                for (int y = 1; y <= roots; y++) put(rootBlockState, mdx + even, dy - y, mdz + even2);
                            }
                    }
                }
                if (isEven(x) && !isEven(z))
                    for (int even = 0; even < evenBias; even++)
                        for (int odd = 1; odd <= oddBias; odd++) makeWallThing(mdx, mdz, even, odd);
                if (!isEven(x) && isEven(z))
                    for (int even = 0; even < evenBias; even++)
                        for (int odd = 1; odd <= oddBias; odd++) makeWallThing(mdx, mdz, odd, even);
            } else if (v == g::DOOR) {
                int mdx = dx + (x / 2 * (evenBias + oddBias));
                int mdz = dz + (z / 2 * (evenBias + oddBias));
                if (evenBias > 1) {
                    mdx--;
                    mdz--;
                }
                if (isEven(x) && !isEven(z))
                    for (int even = 0; even < evenBias; even++)
                        for (int odd = 1; odd <= oddBias; odd++) {
                            for (int y = 0; y < head; y++) put(headBlockState, mdx + even, dy + tall + y, mdz + odd);
                            for (int y = 0; y < tall; y++) put(doorBlockState, mdx + even, dy + y, mdz + odd);
                            for (int y = 1; y <= roots; y++) put(rootBlockState, mdx + even, dy - y, mdz + odd);
                        }
                if (!isEven(x) && isEven(z))
                    for (int even = 0; even < evenBias; even++)
                        for (int odd = 1; odd <= oddBias; odd++) {
                            for (int y = 0; y < head; y++) put(headBlockState, mdx + odd, dy + tall + y, mdz + even);
                            for (int y = 0; y < tall; y++) put(doorBlockState, mdx + odd, dy + y, mdz + even);
                            for (int y = 1; y <= roots; y++) put(rootBlockState, mdx + odd, dy - y, mdz + even);
                        }
            }
        }
    }

    // torches on the posts
    for (int x = 0; x < rawWidth; x++)
        for (int z = 0; z < rawDepth; z++) {
            if (rawAt(x, z) != 0) continue;
            int mdx = dx + (x / 2 * (evenBias + oddBias));
            int mdy = dy + 1;
            int mdz = dz + (z / 2 * (evenBias + oddBias));
            if (isEven(x) && isEven(z)) {
                if (shouldTorch(x, z, rand) && wallBlockState && blocks::id(component.getBlock(region, mdx, mdy, mdz, sbb)) == blocks::id(*wallBlockState))
                    put(torchBlockState, mdx, mdy, mdz);
            }
        }
}

#undef GF_GRID

} // namespace gf::structure::bases
