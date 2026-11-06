// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of U30's pure mob and WIP-text rules (src/core/finalcastle/Mobs.h), translated from The
// Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later).
#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <tuple>

#include "core/finalcastle/Mobs.h"

using namespace gf::core::finalcastle;

namespace {
using Key = std::tuple<int, int, int>;
Key key(IPos p) { return {p.x, p.y, p.z}; }
} // namespace

TEST(FinalCastleRovingCube, SymbolNeedsAllSixNeighbours) {
    std::set<Key> wires;
    IPos          c{3, 64, -2};
    auto          loaded = [](IPos) { return true; };
    auto          empty  = [&](IPos p) { return !wires.contains(key(p)); };
    auto          wire   = [&](IPos p) { return wires.contains(key(p)); };
    // the "8 redstone dust around a blank square" ring of TF's comment: never a symbol
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            if (dx || dz) wires.insert({c.x + dx, c.y, c.z + dz});
    EXPECT_FALSE(isRedstoneSymbol(c, loaded, empty, wire));
    // wire on all six sides (down, up, and the four horizontal ones)
    wires.insert({c.x, c.y - 1, c.z});
    EXPECT_FALSE(isRedstoneSymbol(c, loaded, empty, wire)); // five
    wires.insert({c.x, c.y + 1, c.z});
    EXPECT_TRUE(isRedstoneSymbol(c, loaded, empty, wire));
    // not loaded, or not empty
    EXPECT_FALSE(isRedstoneSymbol(c, [](IPos) { return false; }, empty, wire));
    wires.insert(key(c));
    EXPECT_FALSE(isRedstoneSymbol(c, loaded, empty, wire));
}

TEST(FinalCastleRovingCube, SearchOrderAndRange) {
    IPos cube{100, 70, 100};
    // two symbols: the lower x wins (x is the outermost loop)
    std::set<Key> symbols{{cube.x + 5, cube.y, cube.z - 16}, {cube.x - 3, cube.y + 4, cube.z + 15}};
    auto          found = searchForRedstoneSymbol(cube, [&](IPos p) { return symbols.contains(key(p)); });
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, (IPos{cube.x - 3, cube.y + 4, cube.z + 15}));
    // the box is x / z -16..15, y -5..4: x + 16, z + 16 and y + 5 are outside
    for (Key k : {Key{cube.x + 16, cube.y, cube.z}, Key{cube.x, cube.y, cube.z + 16}, Key{cube.x, cube.y + 5, cube.z}, Key{cube.x - 17, cube.y, cube.z}}) {
        std::set<Key> one{k};
        EXPECT_FALSE(searchForRedstoneSymbol(cube, [&](IPos p) { return one.contains(key(p)); }));
    }
    std::set<Key> corner{{cube.x - 16, cube.y - 5, cube.z - 16}};
    EXPECT_TRUE(searchForRedstoneSymbol(cube, [&](IPos p) { return corner.contains(key(p)); }));
    // within z the y loop runs first: (x, z) equal, the lowest y wins
    std::set<Key> column{{cube.x, cube.y + 3, cube.z}, {cube.x, cube.y - 2, cube.z}};
    EXPECT_EQ(*searchForRedstoneSymbol(cube, [&](IPos p) { return column.contains(key(p)); }), (IPos{cube.x, cube.y - 2, cube.z}));
}

TEST(FinalCastleRovingCube, CenteringDistance) {
    IPos s{10, 64, 20};
    // symbolX - x + 0.5: the distance to the symbol block's centre column, at its floor
    EXPECT_NEAR(distanceFromSymbol(s, 10.5, 64.0, 20.5), 0.0, 1e-12);
    EXPECT_NEAR(distanceFromSymbol(s, 11.5, 64.0, 20.5), 1.0, 1e-12);
    EXPECT_NEAR(distanceFromSymbol(s, 10.5, 66.0, 20.5), 2.0, 1e-12);
    EXPECT_NEAR(distanceFromSymbol(s, 13.5, 64.0, 24.5), 5.0, 1e-12);
    EXPECT_FALSE(isCloseToSymbol(0.25));
    EXPECT_TRUE(isCloseToSymbol(0.26));
    EXPECT_TRUE(isCloseToSymbol(9.99));
    EXPECT_FALSE(isCloseToSymbol(10.0));
    EXPECT_TRUE(keepsCentering(0.11));
    EXPECT_FALSE(keepsCentering(0.1));
    EXPECT_FALSE(keepsCentering(100.0));
    auto t = centerTarget(s);
    EXPECT_DOUBLE_EQ(t.x, 10.5);
    EXPECT_DOUBLE_EQ(t.y, 64.0);
    EXPECT_DOUBLE_EQ(t.z, 20.5);
}

TEST(FinalCastleAdherent, ShotAimsAtTheEyesMinusOnePointOne) {
    // a target 10 blocks east, eye height 1.62, the bolt starting at 1.77 (eye 1.87 - 0.1)
    auto s = adherentShot(DVec{0.0, 64.0, 0.0}, 64.0 + 1.77, DVec{10.0, 64.0, 0.0}, 1.62, 2);
    EXPECT_DOUBLE_EQ(s.direction.x, 10.0);
    EXPECT_DOUBLE_EQ(s.direction.z, 0.0);
    double d0 = 64.0 + 1.62 - 1.100000023841858;
    EXPECT_NEAR(s.direction.y, d0 - (64.0 + 1.77) + 10.0F * 0.2F, 1e-6);
    EXPECT_FLOAT_EQ(s.velocity, 0.6F);
    EXPECT_FLOAT_EQ(s.inaccuracy, 2.0F); // normal: 10 - 2 * 4
    EXPECT_FLOAT_EQ(adherentShot({}, 0, {1, 0, 0}, 0, 1).inaccuracy, 6.0F);
    EXPECT_FLOAT_EQ(adherentShot({}, 0, {1, 0, 0}, 0, 3).inaccuracy, -2.0F);
}

TEST(FinalCastleAdherent, RangedAttackGoalTimer) {
    // RangedAttackGoal(adherent, 1.0, 60, 10): min == max, always 60
    EXPECT_EQ(rangedIntervalAfterShot(25.0, 10.0F, 60, 60), 60);
    EXPECT_EQ(rangedIntervalOnReset(81.0, 10.0F, 60, 60), 60);
    // the general form
    EXPECT_EQ(rangedIntervalAfterShot(25.0, 10.0F, 20, 60), 40);
    EXPECT_EQ(rangedIntervalOnReset(100.0, 10.0F, 20, 60), 60);
}

TEST(FinalCastleWip, TextPositionsAndPunchBox) {
    IPos b{-7, 120, 33};
    auto pos = wipTextPositions(b);
    ASSERT_EQ(pos.size(), 4u);
    EXPECT_STREQ(kWipLines[0].text, "Final Castle WIP.");
    EXPECT_STREQ(kWipLines[3].text, "discord.experiment115.com");
    EXPECT_DOUBLE_EQ(pos[0].x, -6.5);
    EXPECT_DOUBLE_EQ(pos[0].z, 33.5);
    EXPECT_DOUBLE_EQ(pos[0].y, 120 + static_cast<double>(2.3F));
    EXPECT_DOUBLE_EQ(pos[1].y, 120 + static_cast<double>(1.3F));
    EXPECT_DOUBLE_EQ(pos[2].y, 121.0);
    EXPECT_DOUBLE_EQ(pos[3].y, 120 + static_cast<double>(0.7F));
    auto punch = punchFor(pos);
    ASSERT_TRUE(punch);
    EXPECT_DOUBLE_EQ(punch->width, 2.0);
    EXPECT_NEAR(punch->height, 3.6, 1e-6);
    EXPECT_DOUBLE_EQ(punch->pos.x, -6.5);
    EXPECT_DOUBLE_EQ(punch->pos.z, 33.5);
    EXPECT_NEAR(punch->pos.y, 119.7, 1e-6);
    auto box = punch->box();
    EXPECT_DOUBLE_EQ(box.minX, -7.5);
    EXPECT_DOUBLE_EQ(box.maxX, -5.5);
    EXPECT_NEAR(box.maxY, 123.3, 1e-6);
    for (auto const& p : pos) EXPECT_TRUE(box.contains(p));
    EXPECT_FALSE(box.contains(DVec{-6.5, 123.4, 33.5}));
    EXPECT_FALSE(punchFor({}));
}
