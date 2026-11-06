// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of src/core/giants (U29): the beanstalk grower, the giant block tiling and the cloud
// precipitation scans.
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <tuple>

#include "core/giants/Giants.h"
#include "core/jrand/JRand.h"

using namespace gf::core::giants;

namespace {

/// An open world with a solid floor below y 64 and an optional obstacle column set.
class MemoryWorld final : public BeanstalkWorld {
public:
    std::map<std::tuple<int, int, int>, Cell> cells;
    std::map<std::tuple<int, int, int>, int>  leafDistance;
    int                                       stalkWrites = 0;

    Cell at(int x, int y, int z) override {
        auto it = cells.find({x, y, z});
        if (it != cells.end()) return it->second;
        return y < 64 ? Cell::Solid : Cell::Air;
    }
    void setStalk(int x, int y, int z) override {
        cells[{x, y, z}] = Cell::Stalk;
        stalkWrites++;
    }
    void setAir(int x, int y, int z) override { cells[{x, y, z}] = Cell::Air; }
    void setLeaves(int x, int y, int z, int distance) override {
        cells[{x, y, z}]        = Cell::Leaves;
        leafDistance[{x, y, z}] = distance;
    }
    [[nodiscard]] int count(Cell c) const {
        int n = 0;
        for (auto const& [k, v] : cells)
            if (v == c) n++;
        return n;
    }
};

} // namespace

TEST(GiantsMth, SinTable) {
    EXPECT_FLOAT_EQ(mthSin(0.0F), 0.0F);
    EXPECT_NEAR(mthSin(1.5707964F), 1.0F, 1e-4F);
    EXPECT_NEAR(mthCos(0.0F), 1.0F, 1e-6F);
    EXPECT_NEAR(mthSin(-1.5707964F), -1.0F, 1e-4F);
    // the table quantisation: Mth.sin(x) == SIN[(int)(x * 10430.378F) & 65535]
    EXPECT_EQ(mthSin(0.0001F), mthSin(0.00005F));
}

TEST(GiantsBeanstalk, RumblesThenGrowsToMaxY) {
    MemoryWorld                  world;
    gf::core::jrand::LegacyRandom random(1234);
    BeanstalkState               s;
    int const                    px = 0, py = 64, pz = 0;
    int                          grew = 0, ticks = 0;
    Step                         last = Step::Waiting;
    while (ticks < 2000) {
        ++ticks;
        last = tickBeanstalk(s, world, random, px, py, pz);
        if (ticks <= 100) EXPECT_EQ(last, Step::Waiting) << "tick " << ticks;
        if (ticks == 1) {
            EXPECT_EQ(s.maxY, 175);
            EXPECT_GE(s.nextLeafY, py + 10);
            EXPECT_LT(s.nextLeafY, py + 20);
            EXPECT_GE(s.cScale, 0.125F);
            EXPECT_LT(s.cScale, 0.375F);
        }
        if (ticks < 110) EXPECT_TRUE(isRumbling(s));
        if (last == Step::Grew) grew++;
        if (last == Step::Finished) break;
    }
    ASSERT_EQ(last, Step::Finished);
    EXPECT_EQ(grew, 175 - py); // one layer per even tick from 102 up to maxY - 1
    EXPECT_EQ(ticks, 102 + 2 * grew);
    EXPECT_FALSE(isRumbling(s));
    // every layer y 64..170 has stalk (the top layers thin out); nothing above maxY + 2
    for (int y = py; y <= 170; ++y) {
        bool any = false;
        for (auto const& [k, v] : world.cells)
            if (std::get<1>(k) == y && v == Cell::Stalk) any = true;
        EXPECT_TRUE(any) << "layer " << y;
    }
    for (auto const& [k, v] : world.cells) EXPECT_LE(std::get<1>(k), 177);
    EXPECT_GT(world.count(Cell::Leaves), 0);
    for (auto const& [k, d] : world.leafDistance) {
        EXPECT_GE(d, 1);
        EXPECT_LE(d, 4);
    }
}

TEST(GiantsBeanstalk, BlockedLayerStops) {
    MemoryWorld world;
    // a solid slab at y 70 over the whole spiral area: 15 skipped blocks stop the growth
    for (int x = -30; x <= 30; ++x)
        for (int z = -30; z <= 30; ++z) world.cells[{x, 70, z}] = Cell::Solid;
    gf::core::jrand::LegacyRandom random(99);
    BeanstalkState               s;
    Step                         last = Step::Waiting;
    for (int t = 0; t < 1000 && last != Step::Finished; ++t) last = tickBeanstalk(s, world, random, 0, 64, 0);
    EXPECT_EQ(last, Step::Finished);
    EXPECT_FALSE(s.isAreaClearEnough);
    EXPECT_EQ(s.layer, 7); // layers 64..69 grew, 70 was blocked (its layer still counts)
}

TEST(GiantsBeanstalk, SaveLoadRoundTrip) {
    BeanstalkState s;
    s.ticker = 150, s.layer = 24, s.isAreaClearEnough = false, s.nextLeafY = 91, s.yOffset = 42, s.cScale = 0.3F, s.rScale = 0.2F, s.maxY = 175,
    s.blocksSkipped = 3;
    auto j = s.toJson();
    EXPECT_EQ(j["beardifierGroundDelta"], 42);
    auto t = BeanstalkState::fromJson(j);
    EXPECT_EQ(t.ticker, 150);
    EXPECT_EQ(t.layer, 24);
    EXPECT_FALSE(t.isAreaClearEnough);
    EXPECT_EQ(t.nextLeafY, 91);
    EXPECT_EQ(t.yOffset, 42);
    EXPECT_FLOAT_EQ(t.cScale, 0.3F);
    EXPECT_EQ(t.maxY, 175);
    EXPECT_EQ(t.blocksSkipped, 3);
}

TEST(GiantsBeanstalk, CloudsAboveY150Cleared) {
    MemoryWorld world;
    // a cloud sheet at y 160..161 everywhere near the stalk
    for (int x = -20; x <= 20; ++x)
        for (int z = -20; z <= 20; ++z) {
            world.cells[{x, 160, z}] = Cell::FluffyCloud;
            world.cells[{x, 161, z}] = Cell::WispyCloud;
        }
    gf::core::jrand::LegacyRandom random(7);
    BeanstalkState               s;
    for (int t = 0; t < 2000; ++t)
        if (tickBeanstalk(s, world, random, 0, 100, 0) == Step::Finished) break;
    EXPECT_EQ(s.maxY, 200);
    // the stalk went through the cloud (fluffy clouds are replaced, the air above cleared)
    bool through = false;
    for (auto const& [k, v] : world.cells)
        if (std::get<1>(k) == 170 && v == Cell::Stalk) through = true;
    EXPECT_TRUE(through);
}

TEST(GiantsCells, StateAndLocal) {
    EXPECT_EQ(cellState(0, 0, 0), 1);
    EXPECT_EQ(cellState(3, 3, 3), 64);
    EXPECT_EQ(cellState(-1, -1, -1), 64); // -1 & 3 == 3
    EXPECT_EQ(cellState(5, 2, 9), 1 + 1 + 8 + 16);
    for (int s = 1; s <= 64; ++s) {
        auto l = cellLocal(s);
        EXPECT_EQ(cellState(l[0], l[1], l[2]), s);
    }
    auto o = cellOrigin(-1, 5, 13);
    EXPECT_EQ(o[0], -4);
    EXPECT_EQ(o[1], 4);
    EXPECT_EQ(o[2], 12);
}

TEST(GiantsCells, FaceUvTilesTheTexture) {
    // every outer face of a cell shows each 4 x 4 sub-square exactly once
    for (int f = 0; f < 6; ++f) {
        auto                            side = static_cast<Face>(f);
        std::set<std::pair<int, int>>   seen;
        int                             faces = 0;
        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 4; ++y)
                for (int z = 0; z < 4; ++z) {
                    if (!giantFaceVisible(side, x, y, z)) continue;
                    faces++;
                    auto uv = giantFaceUv(side, x + 64, y + 32, z - 128); // any aligned cell
                    EXPECT_EQ(uv[0] % 4, 0);
                    EXPECT_GE(uv[0], 0);
                    EXPECT_LE(uv[0], 12);
                    seen.insert({uv[0], uv[1]});
                }
        EXPECT_EQ(faces, 16) << f;
        EXPECT_EQ(seen.size(), 16u) << f;
    }
    // the uv depends on the position in the cell only
    for (int f = 0; f < 6; ++f) EXPECT_EQ(giantFaceUv(static_cast<Face>(f), 1, 2, 3), giantFaceUv(static_cast<Face>(f), 1 - 8, 2 + 4, 3 - 400));
    // Java values: top face of (0, 0, 0): offset (0, 0, -1) -> x % 4 = 0, (1 * -1 + 1) % 4 = 0
    EXPECT_EQ(giantFaceUv(Face::Up, 0, 0, 0), (std::array<int, 2>{0, 0}));
    // north face of (0, 0, 0): (0, 1, 0) -> 0, -1 % 4 = -1 -> 15 % 4 = 3; north flips x: 3
    EXPECT_EQ(giantFaceUv(Face::North, 0, 0, 0), (std::array<int, 2>{12, 12}));
    // up the column the texture goes up
    EXPECT_EQ(giantFaceUv(Face::South, 0, 3, 3), (std::array<int, 2>{0, 0}));
}

TEST(GiantsClouds, HighestRainyBlock) {
    // open air below a cloud at 100, distance 32: y - distance
    auto open = [](int) { return false; };
    EXPECT_EQ(highestRainyBlock(100, 32, open), 68);
    EXPECT_EQ(precipitationStopY(100, 32, open), 69);
    // ground at y 80
    auto ground = [](int y) { return y <= 80; };
    EXPECT_EQ(highestRainyBlock(100, 32, ground), 80);
    EXPECT_EQ(precipitationStopY(100, 32, ground), 81);
    // directly on the ground
    auto under = [](int y) { return y <= 99; };
    EXPECT_EQ(highestRainyBlock(100, 32, under), 99);
    // distance 1: nothing scanned
    EXPECT_EQ(highestRainyBlock(100, 1, open), 99);
}

TEST(GiantsClouds, RainingAbove) {
    auto scan = [](int cloudY, int roofY) {
        return [=](int y) {
            if (y == roofY) return RainScan::Opaque;
            if (y == cloudY) return RainScan::Raining;
            return RainScan::Other;
        };
    };
    EXPECT_TRUE(rainingAbove(70, 32, scan(90, -1000)));
    EXPECT_FALSE(rainingAbove(70, 32, scan(90, 80)));  // a roof between
    EXPECT_FALSE(rainingAbove(70, 20, scan(90, -1000))); // out of range (70 .. 89)
    EXPECT_TRUE(rainingAbove(70, 21, scan(90, -1000)));
    EXPECT_EQ(clientPrecipitationDistance(-1, 32), 32);
    EXPECT_EQ(clientPrecipitationDistance(0, 32), 0);
    EXPECT_EQ(clientPrecipitationDistance(8, 32), 8);
}

TEST(GiantsPick, GroupingCounter) {
    GiantPickMining m;
    m.conversion = 64;
    int giants = 0, none = 0;
    for (int i = 0; i < 64; ++i) (takeGiantConversion(m) ? giants : none)++;
    EXPECT_EQ(giants, 1);
    EXPECT_EQ(none, 63);
    EXPECT_EQ(m.conversion, 0);
    EXPECT_EQ(giantConversionOf("minecraft:cobblestone"), "twilightforest:giant_cobblestone");
    EXPECT_EQ(giantConversionOf("minecraft:obsidian"), "twilightforest:giant_obsidian");
    EXPECT_EQ(giantConversionOf("minecraft:stone"), "");
}
