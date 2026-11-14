// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of U26's pure dark tower rules (src/core/darktower), translated from The Gloaming Forest
// 4.8 (upstream commit 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <tuple>

#include "core/darktower/Devices.h"

using namespace gf::core::darktower;
using gf::core::jrand::LegacyRandom;

TEST(DarkTowerBuilder, PitchPicksUpDownOrHorizontal) {
    EXPECT_EQ(builderFacing(-90.0f, Facing::North), Facing::Up);   // looking straight up
    EXPECT_EQ(builderFacing(-46.0f, Facing::North), Facing::Up);
    EXPECT_EQ(builderFacing(-44.0f, Facing::North), Facing::North);
    EXPECT_EQ(builderFacing(0.0f, Facing::East), Facing::East);
    EXPECT_EQ(builderFacing(44.0f, Facing::West), Facing::West);
    EXPECT_EQ(builderFacing(46.0f, Facing::West), Facing::Down);
    EXPECT_EQ(builderFacing(90.0f, Facing::South), Facing::Down);
}

TEST(DarkTowerBuilder, HorizontalFacingFollowsJava) {
    EXPECT_EQ(horizontalFacing(0.0f), Facing::South);
    EXPECT_EQ(horizontalFacing(90.0f), Facing::West);
    EXPECT_EQ(horizontalFacing(180.0f), Facing::North);
    EXPECT_EQ(horizontalFacing(-90.0f), Facing::East);
    EXPECT_EQ(horizontalFacing(44.0f), Facing::South);
    EXPECT_EQ(horizontalFacing(46.0f), Facing::West);
}

TEST(DarkTowerBuilder, BuildsSeventeenBlocksThenStops) {
    BuilderState s;
    s.startBuilding({0, 0, 0});
    std::set<std::tuple<int, int, int>> built;
    int  placed = 0;
    bool stopped = false;
    for (int t = 0; t < 400 && !stopped; ++t) {
        auto r = tickBuilder(
            s,
            [] { return true; },
            [](Facing& f) {
                f = Facing::East;
                return true;
            },
            [&](std::array<int, 3> const& p) { return !built.contains({p[0], p[1], p[2]}); }
        );
        if (r.placed) {
            ++placed;
            built.insert({r.at[0], r.at[1], r.at[2]});
            EXPECT_EQ(r.at[0], placed) << "the cursor advances one block east per placement";
            EXPECT_EQ(s.ticksRunning % 10, 0);
        }
        stopped = !s.makingBlocks;
    }
    EXPECT_EQ(placed, 17) << "blocksMade <= RANGE (16) builds 17 blocks";
    EXPECT_TRUE(stopped);
    // not making blocks: the timeout comes after 60 ticks
    int timeoutAt = -1;
    for (int t = 1; t <= 80; ++t) {
        auto r = tickBuilder(s, [] { return true; }, [](Facing&) { return false; }, [](auto const&) { return true; });
        if (r.timeout && timeoutAt < 0) timeoutAt = t;
    }
    EXPECT_EQ(timeoutAt, 60);
}

TEST(DarkTowerBuilder, BlockedPositionStopsTheRun) {
    BuilderState s;
    s.startBuilding({0, 0, 0});
    int placed = 0;
    for (int t = 0; t < 100; ++t) {
        auto r = tickBuilder(
            s,
            [] { return true; },
            [](Facing& f) {
                f = Facing::Up;
                return true;
            },
            [](std::array<int, 3> const& p) { return p[1] < 3; }
        );
        placed += r.placed;
    }
    EXPECT_EQ(placed, 2);
    EXPECT_FALSE(s.makingBlocks);
}

TEST(DarkTowerSlider, TickDelay) {
    EXPECT_EQ(sliderTickDelay(0, 0), 80);
    EXPECT_EQ(sliderTickDelay(79, 0), 1);
    EXPECT_EQ(sliderTickDelay(100, 1), 80);
    EXPECT_EQ(sliderTickDelay(130, 3), 10);
    EXPECT_EQ(sliderTickDelay(30, 3), 80 - (-30 % 80)) << "Java's truncating remainder: 110";
}

TEST(DarkTowerReactor, FakeBlocksCoverTheShell) {
    auto const& k = reactorFakeBlocks();
    std::set<std::tuple<int, int, int>> seen;
    int diamonds = 0;
    for (auto const& f : k) {
        EXPECT_FALSE(f.dx == 0 && f.dy == 0 && f.dz == 0);
        seen.insert({f.dx, f.dy, f.dz});
        diamonds += f.diamond;
    }
    EXPECT_EQ(seen.size(), 26u);
    EXPECT_EQ(diamonds, 14);
}

TEST(DarkTowerReactor, BlobRadiusZeroIsEmptyAndShellsAreSymmetricInCount) {
    EXPECT_TRUE(reactorBlob(0, 0).empty());
    auto r1 = reactorBlob(1, 0);
    EXPECT_FALSE(r1.empty());
    for (auto const& b : r1) {
        EXPECT_LE(std::abs(b.dx), 1);
        EXPECT_LE(std::abs(b.dy), 1);
        EXPECT_LE(std::abs(b.dz), 1);
    }
    // fuzz picks the octant: fuzz 1 mirrors z of the dx = 0 column
    auto f1 = reactorBlob(2, 1);
    bool negZ = false;
    for (auto const& b : f1)
        if (b.dx == 0 && b.dz < 0) negZ = true;
    EXPECT_TRUE(negZ);
}

TEST(DarkTowerReactor, BlobCallsFollowTheCounter) {
    EXPECT_TRUE(reactorBlobCalls(7).empty());
    auto c5 = reactorBlobCalls(5);
    ASSERT_EQ(c5.size(), 1u);
    EXPECT_TRUE(c5[0].debris);
    EXPECT_EQ(c5[0].rad, (5 - 80) / 40); // Java truncation: -1, no blocks
    auto c130 = reactorBlobCalls(130);   // primary 50, secondary 10
    int prim = 0, sec = 0;
    for (auto const& c : c130) (c.centre == 0 ? prim : sec)++;
    EXPECT_EQ(prim, 2);
    EXPECT_EQ(sec, 2);
    auto c345 = reactorBlobCalls(345); // primary 265: nothing; tertiary 185: nothing
    EXPECT_TRUE(c345.empty());
}

TEST(DarkTowerReactor, BurstsNeverCoincide) {
    for (int seed = 0; seed < 200; ++seed) {
        LegacyRandom r(seed);
        auto         b = reactorBursts(r);
        EXPECT_NE(b.secondary, b.tertiary);
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(std::abs(b.secondary[i]), 3);
            EXPECT_EQ(std::abs(b.tertiary[i]), 3);
        }
    }
}

namespace {
struct Grid {
    std::map<int, AntiBlock> cells;
    AntiBlock                read(int i) const {
        auto it = cells.find(i);
        return it == cells.end() ? AntiBlock{0, true, false, false, false} : it->second;
    }
};
AntiBlock stone() { return {1, false, true, false, false}; }
AntiBlock antibuilt() { return {2, false, true, false, false}; }
AntiBlock torch() { return {3, false, false, false, false}; }
} // namespace

TEST(DarkTowerAntibuilder, IndexOrderIsXThenYThenZ) {
    EXPECT_EQ(antibuilderOffset(0), (std::array<int, 3>{-4, -4, -4}));
    EXPECT_EQ(antibuilderOffset(1), (std::array<int, 3>{-4, -4, -3}));
    EXPECT_EQ(antibuilderOffset(9), (std::array<int, 3>{-4, -3, -4}));
    EXPECT_EQ(antibuilderOffset(81), (std::array<int, 3>{-3, -4, -4}));
    EXPECT_EQ(antibuilderOffset(728), (std::array<int, 3>{4, 4, 4}));
}

TEST(DarkTowerAntibuilder, RefillsBrokenBlockAndStaysInFastScan) {
    Grid g;
    g.cells[10] = stone();
    AntibuilderState s;
    LegacyRandom     r(1);
    auto             rd = [&](int i) { return g.read(i); };
    tickAntibuilder(s, true, true, rd, r);
    EXPECT_TRUE(s.slowScan);
    g.cells.erase(10); // broken: air over a motion-blocking snapshot
    bool refilled = false;
    for (int t = 0; t < 400 && !refilled; ++t) {
        for (auto const& rev : tickAntibuilder(s, true, true, rd, r)) {
            EXPECT_EQ(rev.index, 10);
            EXPECT_TRUE(rev.toAntibuilt);
            EXPECT_TRUE(rev.currentAir);
            g.cells[10] = antibuilt();
            refilled    = true;
        }
    }
    EXPECT_TRUE(refilled);
    // the antibuilt block never matches the snapshot: fast scan for as long as a player stays
    for (int t = 0; t < 100; ++t) tickAntibuilder(s, true, true, rd, r);
    EXPECT_FALSE(s.slowScan);
    // everyone leaves: the snapshot is dropped; the next capture absorbs the antibuilt block
    tickAntibuilder(s, false, true, rd, r);
    EXPECT_TRUE(s.snapshot.empty());
    tickAntibuilder(s, true, true, rd, r);
    EXPECT_TRUE(s.slowScan);
}

TEST(DarkTowerAntibuilder, PlacedBlockIsDestroyedAndSlowScanResumes) {
    Grid             g;
    AntibuilderState s;
    LegacyRandom     r(7);
    auto             rd = [&](int i) { return g.read(i); };
    tickAntibuilder(s, true, true, rd, r);
    g.cells[300] = stone(); // placed into snapshot air
    bool destroyed = false;
    for (int t = 0; t < 400 && !destroyed; ++t) {
        for (auto const& rev : tickAntibuilder(s, true, true, rd, r)) {
            EXPECT_FALSE(rev.toAntibuilt);
            EXPECT_FALSE(rev.currentAir);
            g.cells.erase(300);
            destroyed = true;
        }
    }
    ASSERT_TRUE(destroyed);
    EXPECT_FALSE(s.slowScan);
    for (int t = 0; t < 21; ++t) tickAntibuilder(s, true, true, rd, r);
    EXPECT_TRUE(s.slowScan) << "21 change-free scans switch back to slow scan";
}

TEST(DarkTowerAntibuilder, NonRevertibleChangesAreAbsorbed) {
    Grid g;
    g.cells[5] = torch();
    AntibuilderState s;
    LegacyRandom     r(3);
    auto             rd = [&](int i) { return g.read(i); };
    tickAntibuilder(s, true, true, rd, r);
    g.cells.erase(5); // air over a non-motion-blocking snapshot: not revertible
    for (int t = 0; t < 40; ++t) EXPECT_TRUE(tickAntibuilder(s, true, true, rd, r).empty());
    EXPECT_TRUE(s.snapshot[5].air);
    EXPECT_TRUE(s.slowScan);
}

TEST(DarkTowerUrGhast, PhaseTogglesAfterEighteen) {
    UrGhastPhase p; // the constructor's setInTantrum(false) resets the threshold to 18
    EXPECT_FLOAT_EQ(p.damageUntilNextPhase, 18.0f);
    EXPECT_FALSE(p.afterHurt(10, true, false, false));
    EXPECT_FALSE(p.afterHurt(6, false, false, false)) << "i-frame top-ups do not count";
    EXPECT_FALSE(p.afterHurt(7, true, false, false));
    EXPECT_TRUE(p.afterHurt(1, true, false, false));
    EXPECT_TRUE(p.tantrum);
    EXPECT_FLOAT_EQ(p.damageUntilNextPhase, 18.0f);
    EXPECT_FLOAT_EQ(p.incoming(20.0f), 2.0f);
    EXPECT_FALSE(p.afterHurt(17, true, false, false));
    EXPECT_FALSE(p.afterHurt(5, true, false, true)) << "trapped hits do not count";
    EXPECT_TRUE(p.afterHurt(1, true, false, false));
    EXPECT_FALSE(p.tantrum);
}

TEST(DarkTowerGhast, SquashMatchesRenderer) {
    auto idle = ghastSquash(8.0f, 0.0f);
    EXPECT_FLOAT_EQ(idle.xz, 4.5f);
    EXPECT_FLOAT_EQ(idle.y, 4.5f);
    auto full = ghastSquash(24.0f, 20.0f); // v = 1/3
    EXPECT_FLOAT_EQ(full.y, (24.0f + 1.0f / 3.0f) / 2.0f);
    EXPECT_FLOAT_EQ(full.xz, (24.0f + 3.0f) / 2.0f);
    auto neg = ghastSquash(8.0f, -40.0f);
    EXPECT_FLOAT_EQ(neg.xz, 4.5f);
}
