// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// gtests of src/core/minorlandmarks (U18): translate, the voxel line, the hedge maze rooms, the
// hollow hill terraformer, the fallen trunk geometry, hole and mounds, the lake shape and the
// foundation wall rotations.
#include <gtest/gtest.h>

#include <set>
#include <tuple>

#include "core/jrand/JRand.h"
#include "core/minorlandmarks/MinorLandmarks.h"

using namespace gf::core;
using namespace gf::core::minorlandmarks;

TEST(MinorLandmarks, Translate) {
    EXPECT_EQ(translate({0, 0, 0}, 10, 0.0, 0.5), (Pos{0, 0, 10}));   // angle 0: +z, straight out
    EXPECT_EQ(translate({0, 0, 0}, 10, 0.25, 0.5), (Pos{10, 0, 0}));  // a quarter turn: +x
    EXPECT_EQ(translate({0, 0, 0}, 10, 0.5, 0.5), (Pos{0, 0, -10}));  // half a turn: -z
    EXPECT_EQ(translate({5, 5, 5}, 10, 0.3, 0.0), (Pos{5, 15, 5}));   // tilt 0: straight up
    EXPECT_EQ(translate({5, 5, 5}, 10, 0.3, 1.0), (Pos{5, -5, 5}));   // tilt 1: straight down
    // Math.round: ties go up
    EXPECT_EQ(translate({0, 0, 0}, 1.5, 0.0, 0.5), (Pos{0, 0, 2}));
    EXPECT_EQ(translate({0, 0, 0}, 1.5, 0.5, 0.5), (Pos{0, 0, -1}));
}

TEST(MinorLandmarks, Bresenham) {
    auto line = bresenham({0, 0, 0}, {3, 1, 0});
    ASSERT_EQ(line.size(), 4u);
    EXPECT_EQ(line[0], (Pos{0, 0, 0}));
    EXPECT_EQ(line[1], (Pos{1, 0, 0}));
    EXPECT_EQ(line[2], (Pos{2, 1, 0}));
    EXPECT_EQ(line[3], (Pos{3, 1, 0}));
    auto down = bresenham({0, 10, 0}, {0, 0, 2});
    ASSERT_EQ(down.size(), 11u);
    EXPECT_EQ(down.front(), (Pos{0, 10, 0}));
    EXPECT_EQ(down.back(), (Pos{0, 0, 2}));
    auto single = bresenham({4, 4, 4}, {4, 4, 4});
    ASSERT_EQ(single.size(), 1u);
    // every step moves one block on the major axis
    for (std::size_t i = 1; i < down.size(); i++) EXPECT_EQ(down[i - 1].y - down[i].y, 1);
}

TEST(MinorLandmarks, HedgeMazeRooms) {
    std::vector<int> r(10, 0);
    EXPECT_TRUE(hedgeMazeIsNearRoom(1, 1, r)); // the origin cell is never a room
    EXPECT_FALSE(hedgeMazeIsNearRoom(5, 5, r));
    r[0] = 5;
    r[1] = 5;
    EXPECT_TRUE(hedgeMazeIsNearRoom(7, 7, r));
    EXPECT_TRUE(hedgeMazeIsNearRoom(3, 4, r));
    EXPECT_FALSE(hedgeMazeIsNearRoom(8, 5, r));
    EXPECT_FALSE(hedgeMazeIsNearRoom(2, 5, r));
}

TEST(MinorLandmarks, HollowHillTerraformer) {
    BlockBox box{-18, 0, -18, 18, 9, 18};
    // inside the dome: carved (8 * -1 under the masking sphere)
    EXPECT_DOUBLE_EQ(hollowHillTerraformer(1, box, 0, 3, 0), -8.0);
    // far away: nothing
    EXPECT_DOUBLE_EQ(hollowHillTerraformer(1, box, 200, 3, 0), 0.0);
    // high above the mound: nothing added
    EXPECT_LE(hollowHillTerraformer(1, box, 0, 60, 0), 0.0);
    // the functions
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 50, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 0, 0, 0), 1.0);
    EXPECT_DOUBLE_EQ(hollowHillFunction({0, 0, 0}, 10.0F, 1.0F, 0, 100, 0), -1.0);
    EXPECT_DOUBLE_EQ(focusedDensity({0, 0, 0}, 10.0F, 2.0F, 0.0F, 0, 50, 0), 0.0);
    EXPECT_NEAR(focusedDensity({0, 0, 0}, 10.0F, 2.0F, 0.0F, 0, 0, 0), 2.0 - 2.0 * 0.8660254 / 10.0, 1e-5);
    auto c = boxCenter(box);
    EXPECT_EQ(c, (Pos{0, 5, 0}));
}

TEST(MinorLandmarks, OrientBoxAndWorldPos) {
    for (int d = 0; d < 4; d++) {
        auto     dir = static_cast<Dir>(d);
        BlockBox b   = orientBox(10, 64, 20, 0, 0, 0, 4, 4, 18, dir);
        int      sx = b.maxX - b.minX + 1, sz = b.maxZ - b.minZ + 1;
        if (dir == Dir::North || dir == Dir::South) {
            EXPECT_EQ(sx, 4);
            EXPECT_EQ(sz, 18);
        } else {
            EXPECT_EQ(sx, 18);
            EXPECT_EQ(sz, 4);
        }
        // every local position lands inside the box, all distinct
        std::set<std::tuple<int, int, int>> seen;
        for (int x = 0; x < 4; x++)
            for (int z = 0; z < 18; z++) {
                auto p = vanillaWorldPos(b, dir, x, 0, z);
                EXPECT_TRUE(b.contains(p.x, p.y, p.z));
                seen.insert({p.x, p.y, p.z});
            }
        EXPECT_EQ(seen.size(), 72u);
    }
    EXPECT_EQ(horizontalByIndex(0), Dir::North);
    EXPECT_EQ(horizontalByIndex(3), Dir::West);
    EXPECT_EQ(clockwise(Dir::North), Dir::East);
    EXPECT_EQ(opposite(Dir::West), Dir::East);
}

TEST(MinorLandmarks, FallenTrunkHole) {
    for (int radius : {1, 2}) {
        for (std::int64_t seed : {1LL, 42LL, -7LL, 123456789LL}) {
            int         length = 20;
            int         xy     = radius > 1 ? radius * 2 + 1 : 4;
            BlockBox    box    = orientBox(0, 64, 0, 0, 0, 0, xy, xy, length - 1, Dir::South);
            FallenTrunk t(box, Dir::South, length, radius, seed);
            EXPECT_EQ(t.holeSizeXY(), t.sideLength() * 4);
            EXPECT_EQ(t.holeSizeZ(), length - 6);
            int cells = 0;
            for (int a = 0; a < t.holeSizeXY(); a++)
                for (int z = 0; z < t.holeSizeZ(); z++) cells += t.inHole(a, z) ? 1 : 0;
            EXPECT_GT(cells, 0) << "radius " << radius << " seed " << seed;
            EXPECT_EQ(static_cast<int>(t.holeBlocks().size()), cells);
            // the perimeter walk visits distinct positions
            std::set<std::pair<int, int>> perim;
            for (int a = 0; a < t.holeSizeXY(); a++) {
                auto p = t.lengthToXY(a);
                perim.insert({p[0], p[1]});
            }
            EXPECT_EQ(static_cast<int>(perim.size()), t.holeSizeXY());
            // allowed mound bases are a subset of the potential ones, in the same order
            auto pot = t.potentialMoundBases(true), ok = t.allowedMoundBases(true);
            EXPECT_LE(ok.size(), pot.size());
            std::size_t j = 0;
            for (auto const& p : ok) {
                while (j < pot.size() && !(pot[j] == p)) j++;
                EXPECT_LT(j, pot.size());
            }
            // the hole is deterministic
            FallenTrunk again(box, Dir::South, length, radius, seed);
            EXPECT_EQ(again.holeBlocks(), t.holeBlocks());
        }
    }
}

TEST(MinorLandmarks, TrunkUnderDensity) {
    EXPECT_DOUBLE_EQ(TrunkUnderDensity::flatSurroundingTerrain(0, 5, 0), -1.0);
    EXPECT_DOUBLE_EQ(TrunkUnderDensity::flatSurroundingTerrain(20, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(TrunkUnderDensity::flatSurroundingTerrain(0, 30, 0), 0.0);
    EXPECT_GT(TrunkUnderDensity::flatSurroundingTerrain(2, -3, 0), 0.0); // below ground near the trunk: filled
    BlockBox          box = orientBox(100, 64, 100, 0, 0, 0, 9, 9, 25, Dir::East);
    FallenTrunk       big(box, Dir::East, 26, 4, 99);
    TrunkUnderDensity d(big, true, 1, 2);
    EXPECT_GE(d.mounds().size(), 1u);
    EXPECT_LE(d.mounds().size(), 2u);
    // inside the trunk's cross-section: carved
    auto c = boxCenter(box);
    EXPECT_DOUBLE_EQ(d.compute(c.x, c.y, c.z), -1.0);
    // far below and above: nothing
    EXPECT_DOUBLE_EQ(d.compute(c.x + 40, c.y + 40, c.z), 0.0);
}

TEST(MinorLandmarks, TanhHill) {
    auto h = TanhHill::withAngle(0, 0, 0, 3.0F, 1.5F, 0.0F, true, true);
    EXPECT_DOUBLE_EQ(h.compute(0.0F, 20.0F, 1.0F), -1.0); // too high
    EXPECT_DOUBLE_EQ(h.compute(0.0F, 0.0F, -1.0F), -1.0); // wrong side
    EXPECT_DOUBLE_EQ(h.compute(0.0F, 0.0F, 1.0F), 1.0);   // under the mound
    EXPECT_DOUBLE_EQ(h.compute(0.0F, 0.0F, 50.0F), -1.0); // outside 3 radii
}

TEST(MinorLandmarks, LakeShape) {
    jrand::LegacyRandom a(12345), b(12345);
    auto                s1 = lakeShape(a), s2 = lakeShape(b);
    EXPECT_EQ(s1, s2);
    int in = 0, border = 0;
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = 0; y < 8; y++) {
                bool inside = s1[static_cast<std::size_t>((x * 16 + z) * 8 + y)];
                in += inside ? 1 : 0;
                if (lakeBorder(s1, x, y, z)) {
                    ++border;
                    EXPECT_FALSE(inside);
                }
                if (x == 0 || x == 15 || z == 0 || z == 15 || y == 0 || y == 7) EXPECT_FALSE(inside);
            }
    EXPECT_GT(in, 0);
    EXPECT_GT(border, 0);
}

TEST(MinorLandmarks, WallRotation) {
    jrand::LegacyRandom r(5);
    EXPECT_FALSE(wallVolumeRotation(r, 2, 2, 5, 5).has_value());
    // one wall: Util.shuffle draws nothing, Util.getRandom draws nextInt(1)
    jrand::LegacyRandom x(77), y(77);
    EXPECT_EQ(wallVolumeRotation(x, 2, 0, 5, 5), Rot::None);
    (void)y.nextInt(1);
    EXPECT_EQ(x.nextInt(), y.nextInt());
    jrand::LegacyRandom e(3);
    EXPECT_EQ(wallVolumeRotation(e, 5, 2, 5, 5), Rot::Clockwise90);
    // a corner: one of its two walls
    for (std::int64_t s = 0; s < 20; s++) {
        jrand::LegacyRandom c(s);
        auto                rot = wallVolumeRotation(c, 0, 0, 5, 5);
        ASSERT_TRUE(rot.has_value());
        EXPECT_TRUE(*rot == Rot::None || *rot == Rot::Counterclockwise90);
    }
}

TEST(MinorLandmarks, NextIntRange) {
    jrand::LegacyRandom a(9), b(9);
    EXPECT_EQ(nextIntRange(a, 3, 7), 3 + b.nextInt(4));
}
