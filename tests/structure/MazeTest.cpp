// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MinotaurMazeComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TFMaze generation against Java: MazeGolden.inc is the output of oracle/MazeOracle.java (run on
// a JVM with java.util.Random, which LegacyRandomSource reproduces). The drivers below repeat the
// oracle's (the labyrinth level, the hedge maze, a dark tower door maze) on the C++ grid.

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "feature/structure/pure/JavaRandom.h"
#include "feature/structure/pure/MazeGrid.h"

using gf::structure::pure::JavaRandom;
namespace maze = gf::structure::pure::maze;

namespace {

std::map<std::string, std::string> const& goldens() {
    static std::map<std::string, std::string> const table{
#include "MazeGolden.inc"
    };
    return table;
}

std::string golden(std::string const& name) {
    auto it = goldens().find(name);
    return it == goldens().end() ? std::string("<missing golden ") + name + ">" : it->second;
}

struct TestMaze {
    int              width, depth;
    std::vector<int> storage;
    float            doorRarity  = 0.f;
    float            torchRarity = 0.75f;
    TestMaze(int w, int d) : width(w), depth(d), storage(static_cast<std::size_t>((w * 2 + 1) * (d * 2 + 1)), 0) {}
    [[nodiscard]] maze::Grid grid() { return {storage.data(), width * 2 + 1, depth * 2 + 1}; }

    std::string dump() {
        auto        g = grid();
        std::string s;
        for (int z = 0; z < g.rawDepth; z++) {
            for (int x = 0; x < g.rawWidth; x++) {
                int v = maze::getRaw(g, x, z);
                s += v == 0 ? '#' : v == 1 ? '.' : v == 2 ? ',' : v == maze::ROOM ? 'R' : v == maze::DOOR ? 'D' : '?';
            }
            s += '/';
        }
        return s;
    }
    std::string decorations(JavaRandom& rand) {
        auto        g = grid();
        std::string s;
        for (int x = 0; x < g.rawWidth; x++) {
            for (int z = 0; z < g.rawDepth; z++) {
                if (maze::getRaw(g, x, z) == 0 && x % 2 == 0 && z % 2 == 0) {
                    s += maze::shouldTree(g, x, z, rand) ? 'T' : '-';
                    s += maze::shouldPillar(g, x, z, false) ? 'P' : '-';
                    s += maze::shouldTorch(g, x, z, rand, torchRarity) ? 'L' : '-';
                }
            }
        }
        return s;
    }
};

bool isNearRoom(int dx, int dz, std::vector<int> const& rcoords, int range) {
    if (dx == 1 && dz == 1) return true;
    for (std::size_t i = 0; i < rcoords.size() / 2; i++) {
        int rx = rcoords[i * 2], rz = rcoords[i * 2 + 1];
        if (rx == 0 && rz == 0) continue;
        if (std::abs(dx - rx) < range && std::abs(dz - rz) < range) return true;
    }
    return false;
}

template <class T>
std::string joined(std::vector<T> const& v) {
    std::ostringstream o;
    for (std::size_t i = 0; i < v.size(); i++) o << (i ? " " : "") << v[i];
    return o.str();
}

} // namespace

TEST(StructureJavaRandom, MatchesJavaUtilRandom) {
    JavaRandom r(0);
    std::ostringstream o;
    int  a  = r.nextInt();
    int  b  = r.nextInt(10);
    int  c  = r.nextInt(16);
    int  d  = r.nextInt(1000);
    auto e  = r.nextLong();
    bool f  = r.nextBoolean();
    auto fl = std::bit_cast<std::int32_t>(r.nextFloat());
    auto db = std::bit_cast<std::int64_t>(r.nextDouble());
    int  g  = r.nextInt(2147483647);
    o << a << ' ' << b << ' ' << c << ' ' << d << ' ' << e << ' ' << (f ? "true" : "false") << ' ' << fl << ' ' << db << ' ' << g;
    EXPECT_EQ(o.str(), golden("random0"));

    // nextGaussian: Java's StrictMath (fdlibm) log / sqrt may differ from the C runtime's in the last
    // bit; the structure kit never draws Gaussians, so one ulp is tolerated here
    JavaRandom          g42(42);
    std::istringstream  expected(golden("gaussian42"));
    for (int i = 0; i < 4; i++) {
        std::int64_t want = 0;
        expected >> want;
        auto got = std::bit_cast<std::int64_t>(g42.nextGaussian());
        EXPECT_LE(std::llabs(got - want), 1) << "gaussian " << i;
    }

    JavaRandom       big(-8'123'456'789'012LL);
    std::vector<int> bounded;
    for (int i = 0; i < 16; i++) bounded.push_back(big.nextInt(3 + i * 977));
    EXPECT_EQ(joined(bounded), golden("bounded"));
}

TEST(StructureJavaRandom, MthGetSeed) {
    std::vector<std::int64_t> seeds;
    int const                 ps[][3] = {{0, 0, 0}, {1, 2, 3}, {-1, 64, -1}, {123456, -60, -987654}, {30000000, 319, -30000000}};
    for (auto const& p : ps) seeds.push_back(gf::structure::pure::mthGetSeed(p[0], p[1], p[2]));
    EXPECT_EQ(joined(seeds), golden("mthGetSeed"));
}

TEST(StructureMaze, LabyrinthMatchesJava) {
    for (std::int64_t seed : {12345LL, -4242LL, 90342903LL}) {
        JavaRandom       random(seed);
        TestMaze         m(22, 22);
        int const        nrooms = 7;
        std::vector<int> rcoords(nrooms * 2, 0);
        rcoords[0] = 11;
        rcoords[1] = 11;
        maze::carveRoom1(m.grid(), 11, 11);
        for (int i = 1; i < nrooms; i++) {
            int rx = 0, rz = 0;
            do {
                rx = random.nextInt(22 - 2) + 1;
                rz = random.nextInt(22 - 2) + 1;
            } while (isNearRoom(rx, rz, rcoords, i == 1 ? 7 : 4));
            maze::carveRoom1(m.grid(), rx, rz);
            rcoords[i * 2]     = rx;
            rcoords[i * 2 + 1] = rz;
        }
        maze::rbGen(m.grid(), 0, 0, random, m.doorRarity);
        EXPECT_EQ(m.dump(), golden("labyrinth_" + std::to_string(seed))) << "seed " << seed;
        EXPECT_EQ(m.decorations(random), golden("labyrinth_deco_" + std::to_string(seed))) << "seed " << seed;
    }
}

TEST(StructureMaze, HedgeMazeMatchesJava) {
    for (std::int64_t seed : {7LL, 1234567890123LL}) {
        JavaRandom rand(seed);
        TestMaze   m(16, 16);
        int const  nrooms = 16 / 3;
        while (!maze::allCellsNonZero(m.grid(), m.width, m.depth)) {
            std::fill(m.storage.begin(), m.storage.end(), 0);
            std::vector<int> rcoords(nrooms * 2, 0);
            for (int i = 0; i < nrooms; i++) {
                int rx = 0, rz = 0;
                do {
                    rx = rand.nextInt(16 - 2) + 1;
                    rz = rand.nextInt(16 - 2) + 1;
                } while (isNearRoom(rx, rz, rcoords, 3));
                maze::carveRoom1(m.grid(), rx, rz);
                rcoords[i * 2]     = rx;
                rcoords[i * 2 + 1] = rz;
            }
            maze::rbGen(m.grid(), 0, 0, rand, m.doorRarity);
        }
        maze::add4Exits(m.grid());
        EXPECT_EQ(m.dump(), golden("hedge_" + std::to_string(seed))) << "seed " << seed;
        EXPECT_EQ(m.decorations(rand), golden("hedge_deco_" + std::to_string(seed))) << "seed " << seed;
    }
}

TEST(StructureMaze, DoorsMatchJava) {
    JavaRandom rand(555);
    TestMaze   m(8, 8);
    m.doorRarity = 0.3f;
    maze::rbGen(m.grid(), 0, 5, rand, m.doorRarity);
    EXPECT_EQ(m.dump(), golden("doors_555"));
}

TEST(StructureMaze, WallsAndCells) {
    TestMaze m(3, 2);
    auto     g = m.grid();
    EXPECT_EQ(g.rawWidth, 7);
    EXPECT_EQ(g.rawDepth, 5);
    EXPECT_TRUE(maze::isWall(g, 0, 0, 1, 0));
    maze::putWall(g, 0, 0, 1, 0, 2);
    EXPECT_FALSE(maze::isWall(g, 0, 0, 1, 0));
    EXPECT_EQ(maze::getRaw(g, 2, 1), 2);
    EXPECT_EQ(maze::getWall(g, 0, 0, 2, 0), maze::OUT_OF_BOUNDS);
    EXPECT_EQ(maze::getCell(g, 5, 5), maze::OUT_OF_BOUNDS);
    EXPECT_FALSE(maze::allCellsNonZero(g, 3, 2));
}
