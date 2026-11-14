// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of U30's pure castle maths (src/core/finalcastle/Castle.h), translated from The Gloaming
// Forest 4.8 (upstream commit 9e40a757, LGPL-2.1-or-later): the rune mural bitmap, the
// decoRNG seeds, the bridge arch and the destroy areas.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/finalcastle/Castle.h"
#include "core/jrand/JRand.h"

using namespace gf::core::finalcastle;

namespace {

std::string render(Mural const& m) {
    std::string s;
    for (int y = m.height() - 1; y >= 0; y--) {
        for (int x = 0; x < m.width(); x++) s += m.get(x, y) > 0 ? '#' : '.';
        s += '\n';
    }
    return s;
}

} // namespace

TEST(FinalCastleSeeds, DecoSeedUsesJavaPrecedence) {
    // (seed + minX * 321534781L) ^ (minZ * 756839L), long arithmetic
    EXPECT_EQ(decoSeed(0, 1, 0), 321534781LL);
    EXPECT_EQ(decoSeed(10, 2, 3), (10LL + 2LL * 321534781LL) ^ (3LL * 756839LL));
    EXPECT_EQ(decoSeed(-5, -3, -7), (-5LL + -3LL * 321534781LL) ^ (-7LL * 756839LL));
    // wraps like a Java long
    std::int64_t big = 0x7fffffffffffff00LL;
    EXPECT_EQ(decoSeed(big, 1000, 0), static_cast<std::int64_t>(static_cast<std::uint64_t>(big) + 321534781000ULL));
}

TEST(FinalCastleSeeds, ThornBranchSeed) {
    // seed + (x * 321534781L) ^ (y * 756839L) + z  =  (seed + x * k1) ^ (y * k2 + z)
    EXPECT_EQ(thornBranchSeed(7, 3, 4, 5), (7LL + 3LL * 321534781LL) ^ (4LL * 756839LL + 5LL));
    EXPECT_EQ(thornBranchSeed(-1, -2, 9, -11), (-1LL + -2LL * 321534781LL) ^ (9LL * 756839LL - 11LL));
}

TEST(FinalCastleBridge, ArchDepth) {
    // l3 - (int) (Mth.cos((l3 - i) / l3 * 1.6F) * l3): deepest at the ends, shallow in the middle
    EXPECT_EQ(bridgeArchDepth(4, 0), 4); // cos(1.6) is about -0.03: (int) -0.12 = 0
    EXPECT_EQ(bridgeArchDepth(4, 3), 1); // cos(0.4) * 4 = 3.68
    EXPECT_EQ(bridgeArchDepth(10, 0), 10);
    EXPECT_EQ(bridgeArchDepth(10, 9), 1); // cos(0.16) * 10 = 9.87
    for (int l3 = 2; l3 <= 10; l3++)
        for (int i = 1; i < l3; i++) EXPECT_LE(bridgeArchDepth(l3, i), bridgeArchDepth(l3, i - 1)) << l3 << " " << i;
}

TEST(FinalCastleMural, StartDotAndShape) {
    Mural m(35, 30, 12345);
    ASSERT_EQ(m.width(), 35);
    ASSERT_EQ(m.height(), 30);
    ASSERT_EQ(m.bytes().size(), 35u * 30u);
    // the 3 x 3 dot at (width / 2 - 1, 2)
    for (int x = 15; x <= 17; x++)
        for (int y = 1; y <= 3; y++) EXPECT_GT(m.get(x, y), 0) << x << "," << y;
    // pixel values: 1 / 4 horizontal (+ / -), 2 / 3 vertical (+ / -)
    for (auto b : m.bytes()) EXPECT_TRUE(b >= 0 && b <= 4);
    // the main tree goes up from the dot
    EXPECT_EQ(m.get(16, 4), 2);
    // the side branches leave the dot to both sides
    EXPECT_GT(m.get(18, 2), 0);
    EXPECT_GT(m.get(14, 2), 0);
    // stripes: some row above height / 3 starts at the left edge
    bool stripe = false;
    for (int y = m.height() / 3 + 1; y <= m.height() - 2; y++) stripe = stripe || m.get(0, y) == 1;
    EXPECT_TRUE(stripe) << render(m);
}

TEST(FinalCastleMural, DeterministicPerSeed) {
    Mural a(19, 12, 99), b(19, 12, 99), c(19, 12, 100);
    EXPECT_EQ(a.bytes(), b.bytes());
    EXPECT_NE(render(a), std::string());
    // a different seed gives (almost always) another mural; these two differ
    Mural d(35, 30, 1), e(35, 30, 2);
    EXPECT_NE(d.bytes(), e.bytes());
    (void)c;
}

TEST(FinalCastleMural, SavedDataRoundTrip) {
    Mural a(35, 30, -42);
    Mural b(a.width(), a.height(), a.bytes());
    EXPECT_EQ(a.bytes(), b.bytes());
    // muralWidth or muralHeight 0: an empty mural (Java: new byte[0])
    Mural z(0, 30, a.bytes());
    EXPECT_TRUE(z.bytes().empty());
    EXPECT_EQ(z.get(3, 3), 0);
}

TEST(FinalCastleDestroyArea, BoxAndIntersection) {
    gf::core::jrand::LegacyRandom rand(5);
    gf::core::BlockBox            tower{100, 64, 200, 112, 104, 212};
    DestroyArea                   a(tower, rand, 90);
    EXPECT_EQ(a.box.maxY, 90);
    EXPECT_EQ(a.box.minY, 80);
    EXPECT_EQ(a.box.maxX - a.box.minX, 4);
    EXPECT_EQ(a.box.maxZ - a.box.minZ, 4);
    EXPECT_GE(a.box.minX, tower.minX - 2);
    EXPECT_LE(a.box.minX, tower.maxX - 2);
    EXPECT_TRUE(a.isEntirelyAbove(79));
    EXPECT_FALSE(a.isEntirelyAbove(80));
    // upstream's argument order: intersects(minX = o.maxX + 1, minZ = o.minX - 1, maxX = o.maxZ + 1, maxZ = o.minZ - 1)
    DestroyArea b = a;
    b.box         = gf::core::BlockBox{0, 0, 0, 4, 10, 4};
    DestroyArea c = a;
    c.box         = gf::core::BlockBox{5, 0, -1, 9, 10, 3};
    // minX 5 <= c.maxX 9, c.minX 5 <= maxX 5, maxZ 3 >= minZ -1, c.minZ -1 <= maxZ -1
    EXPECT_TRUE(c.intersectsWith(b));
    c.box = gf::core::BlockBox{0, 0, 0, 4, 10, 4};
    EXPECT_FALSE(c.intersectsWith(b)); // the same box does not "intersect" under the swapped arguments
    // createNonIntersecting: an empty list takes the first try
    gf::core::jrand::LegacyRandom r1(9), r2(9);
    auto                          first = DestroyArea::createNonIntersecting(tower, r1, 100, {});
    ASSERT_TRUE(first.has_value());
    DestroyArea same(tower, r2, 100);
    EXPECT_EQ(first->box, same.box);
}
