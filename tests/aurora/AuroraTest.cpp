// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of src/core/aurora (U28): the aurora palace's floor plans and the aurora block colours.
#include <gtest/gtest.h>

#include <set>

#include "core/aurora/AuroraColor.h"
#include "core/aurora/FloorPlan.h"
#include "core/jrand/JRand.h"

using namespace gf::core::aurora;

TEST(AuroraFloorPlan, RotateParts) {
    EXPECT_EQ(rotateClockwise(FloorPart::LeftFront, 1), FloorPart::RightFront);
    EXPECT_EQ(rotateClockwise(FloorPart::LeftFront, 2), FloorPart::RightBack);
    EXPECT_EQ(rotateClockwise(FloorPart::LeftFront, 3), FloorPart::LeftBack);
    EXPECT_EQ(rotateClockwise(FloorPart::LeftFront, 4), FloorPart::LeftFront);
    EXPECT_EQ(rotateClockwise(FloorPart::Left, 1), FloorPart::Front);
    EXPECT_EQ(rotateClockwise(FloorPart::Back, 1), FloorPart::Left);
    EXPECT_EQ(rotateClockwise(FloorPart::Mid, 3), FloorPart::Mid);
    EXPECT_EQ(rotateClockwise(FloorPart::Pillars, 1), FloorPart::Pillars);
    // four turns of a set give it back
    PartSet s = static_cast<PartSet>(bit(FloorPart::Left) | bit(FloorPart::RightBack) | bit(FloorPart::Pillars));
    EXPECT_EQ(rotateSet(rotateSet(s, 2), 2), s);
    EXPECT_EQ(rotateSet(s, 1), static_cast<PartSet>(bit(FloorPart::Front) | bit(FloorPart::LeftBack) | bit(FloorPart::Pillars)));
}

TEST(AuroraFloorPlan, Definitions) {
    EXPECT_FLOAT_EQ(floorDef(FloorType::PillarParkour).weight, 6.0F);
    EXPECT_FLOAT_EQ(floorDef(FloorType::PillarPlatforms).weight, 6.0F);
    EXPECT_FLOAT_EQ(floorDef(FloorType::Top).weight, 0.0F);
    EXPECT_EQ(floorDef(FloorType::Top).required, 0);
    EXPECT_EQ(floorDef(FloorType::Top).blocked, static_cast<PartSet>(bit(FloorPart::Mid) | bit(FloorPart::Pillars)));
    EXPECT_EQ(floorDef(FloorType::WraparoundWallSteps).blocked, bit(FloorPart::RightFront));
}

TEST(AuroraFloorPlan, DoorZones) {
    // the entrance door of a wing at (0, 1, 5) lies in BACK (0..3, 4..6)
    std::vector<Door> doors{{0, 1, 5}};
    EXPECT_EQ(partsBlockedByDoors(doors, 0, 10), bit(FloorPart::Back));
    EXPECT_EQ(partsBlockedByDoors(doors, 10, 20), 0);
    // a door at (10, 21, 2) is in LEFT_FRONT (7..10, 0..3); y bounds are inclusive
    doors.push_back({10, 21, 2});
    EXPECT_EQ(partsBlockedByDoors(doors, 20, 30), bit(FloorPart::LeftFront));
    EXPECT_EQ(partsBlockedByDoors(doors, 11, 21), bit(FloorPart::LeftFront));
    // the middle (4..6, 4..6) is no door zone
    EXPECT_EQ(partsBlockedByDoors({{5, 1, 5}}, 0, 10), 0);
}

TEST(AuroraFloorPlan, PlansFollowTheRules) {
    std::vector<Door> doors{{0, 1, 5}, {10, 11, 3}, {4, 21, 10}};
    for (std::int64_t seed = 0; seed < 200; seed++) {
        for (int floors : {2, 3, 4}) {
            gf::core::jrand::LegacyRandom rng(seed * 7919 + floors);
            int  gaveUp = 0;
            auto plan   = createFloorPlan(floors, doors, rng, [&] { gaveUp++; });
            ASSERT_EQ(static_cast<int>(plan.size()), floors);
            EXPECT_EQ(gaveUp, 0);
            // replay the constraints top down
            PartSet top = static_cast<PartSet>(floorDef(FloorType::Top).blocked | partsBlockedByDoors(doors, floors * 10, (floors + 1) * 10));
            for (int n = 0; n < floors; n++) {
                auto const& f      = plan[static_cast<std::size_t>(n)];
                int         level  = floors - 1 - n; // floor index of this entry
                PartSet     bottom = partsBlockedByDoors(doors, level * 10, (level + 1) * 10);
                EXPECT_NE(f.type, FloorType::Top);
                EXPECT_EQ(rotateSet(floorDef(f.type).required, f.rotation) & top, 0) << "seed " << seed;
                EXPECT_EQ(rotateSet(floorDef(f.type).blocked, f.rotation) & bottom, 0) << "seed " << seed;
                top = static_cast<PartSet>(rotateSet(floorDef(f.type).blocked, f.rotation) | partsBlockedByDoors(doors, level * 10, (level + 1) * 10));
            }
            // deterministic for the same random
            gf::core::jrand::LegacyRandom again(seed * 7919 + floors);
            EXPECT_EQ(createFloorPlan(floors, doors, again), plan);
        }
    }
}

TEST(AuroraFloorPlan, WeightsFavourThePillarFloors) {
    std::vector<Door> doors{{0, 1, 5}};
    int               pillars = 0, total = 0;
    for (std::int64_t seed = 0; seed < 500; seed++) {
        gf::core::jrand::LegacyRandom rng(seed);
        for (auto const& f : createFloorPlan(3, doors, rng)) {
            total++;
            if (f.type == FloorType::PillarParkour || f.type == FloorType::PillarPlatforms) pillars++;
        }
    }
    EXPECT_GT(pillars * 5, total); // weight 6 each; the top floor blocks MID and PILLARS, which both need free
}

TEST(AuroraColor, Simplex) {
    EXPECT_FLOAT_EQ(simplex3(0, 0, 0), 0.0f);
    for (int i = 0; i < 1000; i++) {
        float v = simplex3(static_cast<float>(i) * 0.37f, static_cast<float>(i) * 0.11f - 3.0f, static_cast<float>(i) * -0.23f);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
    }
    // fractal noise of 0 iterations is 0; every iteration adds (n + 1) / 2 in 0..1
    EXPECT_FLOAT_EQ(fractalNoiseSum(0, 48.0f, 1, 2, 3), 0.0f);
    float f = fractalNoiseSum(3, 48.0f, 100, 64, -40);
    EXPECT_GE(f, 0.0f);
    EXPECT_LE(f, 3.0f);
}

TEST(AuroraColor, Variants) {
    std::set<int> seen;
    for (int x = -200; x < 200; x += 3)
        for (int z = -200; z < 200; z += 7) {
            int v = calcVariant(x, 64, z);
            EXPECT_GE(v, 0);
            EXPECT_LT(v, 16);
            seen.insert(v);
        }
    EXPECT_EQ(seen.size(), 16u); // the noise sweeps through every texture
}

TEST(AuroraColor, Hsv) {
    EXPECT_EQ(hsvToRGB(0.0f, 1.0f, 1.0f), 0xFF0000u);
    EXPECT_EQ(hsvToRGB(1.0f / 3.0f, 1.0f, 1.0f), 0x00FF00u);
    EXPECT_EQ(hsvToRGB(1.25f, 1.0f, 1.0f), hsvToRGB(0.25f, 1.0f, 1.0f)); // the hue wraps
    // the item colour: hue 0.45 -> sector 2, f ~0.7: (0, 1, 0.7)
    EXPECT_EQ(auroraBlockItemColor(), 0x00FFB2u); // (int)(0.7F * 255 + 0.5F) is 178 in float
    auto red = rgbToHSV(255, 0, 0);
    EXPECT_FLOAT_EQ(red[0], 0.0f);
    EXPECT_FLOAT_EQ(red[1], 1.0f);
    EXPECT_FLOAT_EQ(red[2], 1.0f);
    auto green = rgbToHSV(0, 255, 0);
    EXPECT_NEAR(green[0], 1.0f / 3.0f, 1e-6);
    auto grey = rgbToHSV(128, 128, 128);
    EXPECT_FLOAT_EQ(grey[0], 0.0f);
    EXPECT_FLOAT_EQ(grey[1], 0.0f);
    // pale: half saturation, value min(v + 0.4, 0.9)
    unsigned pale = auroraPaleColor(0xFF0000u);
    EXPECT_EQ(pale, hsvToRGB(0.0f, 0.5f, 0.9f));
}

TEST(AuroraColor, BlockColourHueRange) {
    // the ripple noise keeps the hue in 0.37..0.67 (green to blue)
    for (int x = -300; x < 300; x += 13)
        for (int z = -300; z < 300; z += 17) {
            float h = rippleFractalNoise(2, 128.0f, x, 192, z, 0.37f, 0.67f, 1.5f);
            EXPECT_GE(h, 0.37f - 1e-5f);
            EXPECT_LE(h, 0.67f + 1e-5f);
            unsigned c = auroraBlockColor(x, 64, z);
            EXPECT_LE(c >> 16, 8u) << x << " " << z; // hue 0.37..0.67: next to no red at full saturation
        }
}
