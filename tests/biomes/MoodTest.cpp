// U1 biomes: the biomes' mood sound (BiomeAmbientSoundsHandler with TF's LEGACY_CAVE_SETTINGS).

#include <gtest/gtest.h>

#include <cmath>

#include "core/biomes/Mood.h"

namespace m = gf::core::biomes::mood;

TEST(BiomeMood, DarknessPlaysAfterTheTickDelay) {
    // no sky light and no block light: +1/6000 per tick, the sound at 1.0 (float steps: about 6000 ticks)
    float moodiness = 0.0F;
    int   ticks     = 0;
    while (!m::step(moodiness, 0, 0)) {
        ++ticks;
        ASSERT_LT(ticks, 7000);
    }
    EXPECT_NEAR(ticks, 6000, 5);
    EXPECT_EQ(moodiness, 0.0F); // reset when played
}

TEST(BiomeMood, LightKeepsItAway) {
    float moodiness = 0.5F;
    // sky light takes sky / 15 * 0.001 per tick
    EXPECT_FALSE(m::step(moodiness, 15, 0));
    EXPECT_NEAR(moodiness, 0.499F, 1e-6);
    // block light 1 without sky: no change; block light 8: -7 / 6000
    EXPECT_FALSE(m::step(moodiness, 0, 1));
    EXPECT_NEAR(moodiness, 0.499F, 1e-6);
    EXPECT_FALSE(m::step(moodiness, 0, 8));
    EXPECT_NEAR(moodiness, 0.499F - 7.0F / 6000.0F, 1e-6);
    // never below 0
    float low = 0.0F;
    EXPECT_FALSE(m::step(low, 15, 0));
    EXPECT_EQ(low, 0.0F);
}

TEST(BiomeMood, SoundComesFromBeyondTheSampleBlock) {
    m::Vec eye{0.5, 65.62, 0.5};
    // a block 4 east of the eyes' block, same height band: the sound 2 blocks further in that direction
    auto p = m::soundPosition(eye, 4, 65, 0);
    double d0 = 4.5 - 0.5, d1 = 65.5 - 65.62, d2 = 0.0;
    double d3 = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    EXPECT_NEAR(p.x, 0.5 + d0 / d3 * (d3 + 2.0), 1e-9);
    EXPECT_NEAR(p.y, 65.62 + d1 / d3 * (d3 + 2.0), 1e-9);
    EXPECT_NEAR(p.z, 0.5, 1e-9);
    double dist = std::sqrt((p.x - eye.x) * (p.x - eye.x) + (p.y - eye.y) * (p.y - eye.y) + (p.z - eye.z) * (p.z - eye.z));
    EXPECT_NEAR(dist, d3 + 2.0, 1e-9);
}
