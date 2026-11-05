// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the Naga courtyard's maze and layout (U20, src/core/naga/Courtyard.h), translated from
// StructureMazeGenerator (TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "core/jrand/JRand.h"
#include "core/naga/Courtyard.h"

using namespace gf::core;
using namespace gf::core::naga;

namespace {

struct Generated {
    Maze           maze{};
    CornerClipping clipping{};
};

Generated generate(std::int64_t seed) {
    Generated g;
    jrand::LegacyRandom r(seed);
    generateMaze(g.maze, g.clipping, r);
    return g;
}

} // namespace

TEST(NagaCourtyard, Constants) {
    EXPECT_EQ(kRadius, 44);
    EXPECT_EQ(kMazeSize, 7);
    auto box = mainBox(100, 64, 200);
    EXPECT_EQ(box, (BlockBox{78, 63, 178, 122, 73, 222}));
    auto sc = sizeConstraints(100, 64, 200);
    EXPECT_EQ(sc, (BlockBox{56, 63, 156, 144, 73, 244}));
    EXPECT_STREQ(pieceTypeId(PieceKind::HedgeCap), "twilightforest:TFNCCp");
    EXPECT_STREQ(pieceTypeId(PieceKind::WallCornerInner), "twilightforest:TFNCWA");
}

TEST(NagaCourtyard, MazeInvariants) {
    for (std::int64_t seed : {0LL, 1LL, 42LL, -7LL, 123456789LL, 9001LL}) {
        auto g = generate(seed);
        // the center is excluded and its neighbours do not connect toward it
        EXPECT_EQ(g.maze[3][3], kExcluded) << seed;
        EXPECT_EQ(g.maze[4][3] & kWest, 0) << seed;
        EXPECT_EQ(g.maze[2][3] & kEast, 0) << seed;
        EXPECT_EQ(g.maze[3][4] & kNorth, 0) << seed;
        EXPECT_EQ(g.maze[3][2] & kSouth, 0) << seed;
        for (auto const& c : g.clipping) {
            EXPECT_GE(c[0], 1);
            EXPECT_LE(c[0], 2);
            EXPECT_GE(c[1], 1);
            EXPECT_LE(c[1], 2);
        }
        // the four corners are clipped (at least one cell each)
        EXPECT_TRUE(g.maze[0][0] & kExcluded) << seed;
        EXPECT_TRUE(g.maze[6][0] & kExcluded) << seed;
        EXPECT_TRUE(g.maze[0][6] & kExcluded) << seed;
        EXPECT_TRUE(g.maze[6][6] & kExcluded) << seed;
        for (auto const& row : g.maze)
            for (int v : row) EXPECT_EQ(v & ~0b11111, 0) << seed;
    }
}

TEST(NagaCourtyard, DrawsAsJava) {
    // generateMaze draws 49 nextInt(4) and then 8 nextInt(2): the random is left where Java leaves it
    jrand::LegacyRandom a(77), b(77);
    Maze                m{};
    CornerClipping      c{};
    generateMaze(m, c, a);
    for (int i = 0; i < 49; i++) (void)b.nextInt(4);
    int clips[8];
    for (int& v : clips) v = b.nextInt(2) + 1;
    for (int d = 0; d < 4; d++) {
        EXPECT_EQ(c[d][0], clips[d * 2]);
        EXPECT_EQ(c[d][1], clips[d * 2 + 1]);
    }
    EXPECT_EQ(a.nextLong(), b.nextLong());
}

TEST(NagaCourtyard, LayoutIsDeterministicAndComplete) {
    auto g  = generate(12345);
    auto sc = sizeConstraints(0, 64, 0);
    jrand::LegacyRandom r1(99), r2(99);
    auto a = layoutPieces(g.maze, g.clipping, sc, r1);
    auto b = layoutPieces(g.maze, g.clipping, sc, r2);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); i++) {
        EXPECT_EQ(a[i].kind, b[i].kind);
        EXPECT_EQ(a[i].x, b[i].x);
        EXPECT_EQ(a[i].z, b[i].z);
        EXPECT_EQ(a[i].rotation, b[i].rotation);
        EXPECT_EQ(a[i].terraceTemplate, b[i].terraceTemplate);
    }
    std::map<PieceKind, int> counts;
    for (auto const& p : a) counts[p.kind]++;
    // one center path at the exact center, outer walls on the four sides, 4 x 2 outer corners
    // and 4 inner corners
    EXPECT_EQ(counts[PieceKind::WallCornerOuter], 8);
    EXPECT_EQ(counts[PieceKind::WallCornerInner], 4);
    EXPECT_GT(counts[PieceKind::Wall], 8);
    EXPECT_GT(counts[PieceKind::Path], 10);
    bool center = false;
    for (auto const& p : a)
        if (p.kind == PieceKind::Path && p.x == sc.minX + 3 * 12 + 6 - 1 && p.z == sc.minZ + 3 * 12 + 6 - 1) center = true;
    EXPECT_TRUE(center);
    // every hedge cell piece sits on the 12-block grid at offset 6, 1 above the constraints' floor
    for (auto const& p : a) {
        switch (p.kind) {
        case PieceKind::HedgeCap:
        case PieceKind::HedgeCapPillar:
        case PieceKind::HedgeCorner:
        case PieceKind::HedgeT:
        case PieceKind::HedgeIntersection:
            EXPECT_EQ((p.x - sc.minX - 6) % 12, 0);
            EXPECT_EQ((p.z - sc.minZ - 6) % 12, 0);
            EXPECT_EQ(p.y, sc.minY + 1);
            EXPECT_GE(p.rotation, 0);
            EXPECT_LE(p.rotation, 3);
            break;
        case PieceKind::Terrace:
        case PieceKind::TerraceHedge:
            EXPECT_EQ(p.y, sc.minY + 1 - 3);
            EXPECT_FALSE(p.terraceTemplate.empty());
            break;
        default:
            break;
        }
    }
}

TEST(NagaCourtyard, CellPiecesFollowTheirConnections) {
    // the cell piece of each non-excluded cell matches its connection bits (rotation table)
    for (std::int64_t seed : {3LL, 4LL, 5LL}) {
        auto                g  = generate(seed);
        auto                sc = sizeConstraints(0, 0, 0);
        jrand::LegacyRandom r(seed);
        auto                pieces = layoutPieces(g.maze, g.clipping, sc, r);
        std::map<std::pair<int, int>, PieceRecord> cells;
        for (auto const& p : pieces)
            if (p.kind == PieceKind::HedgeCap || p.kind == PieceKind::HedgeCapPillar || p.kind == PieceKind::HedgeCorner || p.kind == PieceKind::HedgeT ||
                p.kind == PieceKind::HedgeIntersection || ((p.kind == PieceKind::Terrace || p.kind == PieceKind::TerraceHedge)))
                cells.emplace(std::make_pair(p.x, p.z), p);
        for (int x = 0; x < kMazeSize; x++)
            for (int y = 0; y < kMazeSize; y++) {
                int  m       = g.maze[x][y];
                bool center  = x == 3 || y == 3;
                if (!center && (m & kExcluded)) continue;
                if (x == 3 && y == 3) continue;
                int xBB = sc.minX + x * 12 + 6, zBB = sc.minZ + y * 12 + 6;
                int bits = m & 0b1111;
                if (bits == 0) {
                    auto it = cells.find({xBB - 6, zBB - 6});
                    ASSERT_NE(it, cells.end()) << "terrace at " << x << "," << y;
                    continue;
                }
                if (bits == 0b0101 || bits == 0b1010) continue; // a line: the connector lines share the kind
                auto it = cells.find({xBB, zBB});
                ASSERT_NE(it, cells.end()) << "cell " << x << "," << y << " bits " << bits;
                int n = __builtin_popcount(static_cast<unsigned>(bits));
                if (n == 1) EXPECT_TRUE(it->second.kind == PieceKind::HedgeCap || it->second.kind == PieceKind::HedgeCapPillar);
                if (n == 2) EXPECT_EQ(it->second.kind, PieceKind::HedgeCorner);
                if (n == 3) EXPECT_EQ(it->second.kind, PieceKind::HedgeT);
                if (n == 4) EXPECT_EQ(it->second.kind, PieceKind::HedgeIntersection);
            }
    }
}

TEST(NagaCourtyard, FacingStrings) {
    EXPECT_EQ(facingString(0b1111), "─┼─");
    EXPECT_EQ(facingString(0), " • ");
}
