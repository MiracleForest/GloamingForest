// Tests of U15's atmosphere maths (src/core/atmosphere): TF's TwilightForestRenderInfo,
// TFSkyRenderer, FogHandler, TFWeatherRenderer, IntervalUtils and the aurora shader.

#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include "core/atmosphere/Atmosphere.h"

using namespace gf::core::atmosphere;
using gf::core::BlockBox;

TEST(Atmosphere, FixedTimeAndDaylight) {
    // DimensionType#timeOfDay(13000): d0 = 13000 / 24000 - 0.25, (2 d0 + 0.5 - cos(d0 pi) / 2) / 3
    float tod = timeOfDay(kFixedTime);
    EXPECT_NEAR(tod, 0.2596f, 0.0005f);
    float d = daylight(tod);
    EXPECT_NEAR(d, 0.379f, 0.002f);
    EXPECT_NEAR(daylight(timeOfDay(6000)), 1.0f, 1e-6f); // noon
    EXPECT_NEAR(daylight(timeOfDay(18000)), 0.0f, 1e-6f); // midnight
}

TEST(Atmosphere, FogColour) {
    auto c = brightnessDependentFogColor({1, 1, 1}, 0.5f);
    EXPECT_NEAR(c.r, 0.53f, 1e-6f);
    EXPECT_NEAR(c.g, 0.53f, 1e-6f);
    EXPECT_NEAR(c.b, 0.545f, 1e-6f);
    // 32 chunks: f = 1 - 1^0.25 = 0, no sky blend
    auto f = fogColor(rgbOf(0xC0FFD8), rgbOf(0x20224A), 1.0f, 32);
    EXPECT_NEAR(f.g, 1.0f, 1e-5f);
    // fewer chunks pull the fog towards the (darker) sky
    auto g = fogColor(rgbOf(0xC0FFD8), rgbOf(0x20224A), 1.0f, 2);
    EXPECT_LT(g.g, f.g);
    EXPECT_EQ(rgbOf(0x827391), (Rgb{0x82 / 255.f, 0x73 / 255.f, 0x91 / 255.f}));
}

TEST(Atmosphere, BiomeLooks) {
    EXPECT_EQ(biomeLooks().size(), 22u);
    auto spooky = biomeLook("twilightforest:spooky_forest");
    ASSERT_TRUE(spooky);
    EXPECT_EQ(spooky->fog, 0x827391u);
    EXPECT_EQ(biomeLook("twilightforest:glacier")->sky, 0x130D28u);
    EXPECT_FALSE(biomeLook("minecraft:plains"));
}

TEST(Atmosphere, NearFog) {
    EXPECT_TRUE(isFoggyAt("twilightforest:spooky_forest", 100));
    EXPECT_TRUE(isFoggyAt("twilightforest:dark_forest", 20));
    EXPECT_FALSE(isFoggyAt("twilightforest:dark_forest", 20.5));
    EXPECT_TRUE(isFoggyAt("twilightforest:dark_forest_center", 5));
    EXPECT_FALSE(isFoggyAt("twilightforest:forest", 5));
}

TEST(Atmosphere, VanillaFogPlanes) {
    EXPECT_EQ(vanillaFog(true, 192, false), (FogPlanes{0, 192}));
    EXPECT_EQ(vanillaFog(false, 192, false), (FogPlanes{192 - 19.2f, 192}));
    EXPECT_EQ(vanillaFog(false, 32, false), (FogPlanes{28, 32}));       // clamp 4
    EXPECT_EQ(vanillaFog(false, 1024, false), (FogPlanes{960, 1024}));  // clamp 64
    EXPECT_EQ(vanillaFog(false, 256, true), (FogPlanes{12.8f, 96}));    // near fog: min(256, 192) / 2
    EXPECT_EQ(vanillaFog(true, 256, true), (FogPlanes{0, 256}));        // the sky fog ignores the near fog
}

TEST(Atmosphere, FogHandlerSmoothing) {
    FogSmoother s;
    FogPlanes   v = vanillaFog(false, 256, true);
    // not loaded: vanilla
    EXPECT_FALSE(s.terrain(v, true, false));
    // first loaded frame: set outright, vanilla this frame
    EXPECT_FALSE(s.terrain(v, true, true));
    EXPECT_TRUE(s.terrainLoaded());
    EXPECT_FLOAT_EQ(s.terrainPlanes().end, 48.0f);
    EXPECT_FLOAT_EQ(s.terrainPlanes().start, 36.0f);
    // leaving the spooky forest: far grows by 0.003 of the gap per frame, near by 0.0015
    FogPlanes plain = vanillaFog(false, 256, false);
    auto      p     = s.terrain(plain, false, true);
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->end, 48.0f + 0.003f * (256.0f - 48.0f), 1e-3f);
    EXPECT_NEAR(p->start, 36.0f + 0.0015f * (plain.start - 36.0f), 1e-3f);
    for (int i = 0; i < 5000; ++i) p = s.terrain(plain, false, true);
    EXPECT_NEAR(p->end, 256.0f, 0.5f);
    // entering again: near shrinks twice as fast (0.006)
    FogPlanes before = *p;
    p                = s.terrain(v, true, true);
    float far        = before.end + 0.003f * (48.0f - before.end);
    EXPECT_NEAR(p->end, far, 1e-3f);
    EXPECT_NEAR(p->start, before.start + 0.006f * (48.0f * 0.75f - before.start), 1e-3f);
    // sky fog: spooky near 0, far halved
    EXPECT_FALSE(s.sky(vanillaFog(true, 256, true), true, true));
    EXPECT_FLOAT_EQ(s.skyPlanes().start, 0.0f);
    EXPECT_FLOAT_EQ(s.skyPlanes().end, 128.0f);
    s.reset();
    EXPECT_FALSE(s.skyLoaded());
    EXPECT_FALSE(s.terrainLoaded());
}

TEST(Atmosphere, FogSmoothingByFrames) {
    // two frames at once land where two single frames land (same target)
    FogSmoother a, b;
    FogPlanes   v = vanillaFog(false, 256, true), plain = vanillaFog(false, 256, false);
    (void)a.terrain(v, true, true);
    (void)b.terrain(v, true, true);
    (void)a.terrain(plain, false, true);
    auto one = a.terrain(plain, false, true);
    auto two = b.terrain(plain, false, true, 2.0f);
    ASSERT_TRUE(one && two);
    EXPECT_NEAR(one->end, two->end, 1e-3f);
    EXPECT_NEAR(one->start, two->start, 1e-3f);
}

TEST(Atmosphere, BirthdaySplash) {
    EXPECT_EQ(birthdaySplash(2026), "Happy 15th birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2012), "Happy 1st birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2013), "Happy 2nd birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2014), "Happy 3rd birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2022), "Happy 11th birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2032), "Happy 21st birthday to the Twilight Forest!");
    EXPECT_EQ(birthdaySplash(2123), "Happy 112th birthday to the Twilight Forest!");
}

TEST(Atmosphere, Stars) {
    auto stars = starQuads();
    // 3000 tries, those with 0.01 < |f|^2 < 1 kept (about pi / 6 of them)
    EXPECT_GT(stars.size(), 1400u);
    EXPECT_LT(stars.size(), 1700u);
    EXPECT_EQ(stars.size(), starQuads().size()); // seeded: always the same field
    for (auto const& q : stars) {
        // the last vertex is the star's centre at distance 100 (the four corners sum to zero)
        float r = std::sqrt(q[3].x * q[3].x + q[3].y * q[3].y + q[3].z * q[3].z);
        EXPECT_NEAR(r, 100.0f, 0.01f);
        float side = std::hypot(q[0].x - q[3].x, q[0].y - q[3].y, q[0].z - q[3].z);
        EXPECT_GE(side, 0.15f * std::sqrt(2.0f) - 1e-3f);
        EXPECT_LE(side, 0.25f * std::sqrt(2.0f) + 1e-3f);
    }
    // the sky rotation keeps the distance
    auto v = rotateStar({0, 0, 100}, timeOfDay(kFixedTime));
    EXPECT_NEAR(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), 100.0f, 1e-3f);
    // without the X rotation (time of day 0) only Y by -90 degrees: +z goes to -x, y stays
    auto w = rotateStar({0, 100, 0}, 0.0f);
    EXPECT_NEAR(w.y, 100.0f, 1e-3f);
    auto z = rotateStar({0, 0, 100}, 0.0f);
    EXPECT_NEAR(z.x, -100.0f, 1e-3f);
    EXPECT_TRUE(drawsVoidPlate(-33, -32));
    EXPECT_FALSE(drawsVoidPlate(-31.9, -32));
}

TEST(Atmosphere, WeatherBasics) {
    EXPECT_EQ(columnSeed(1, 0), 3121 + 45238971);
    EXPECT_EQ(columnSeed(0, 1), 418711 + 13761);
    EXPECT_EQ(columnSeed(2, 3), (std::int64_t{2} * 2 * 3121 + 2 * 45238971) ^ (std::int64_t{3} * 3 * 418711 + 3 * 13761));
    auto [rx, rz] = rainOffset(3, 4);
    EXPECT_FLOAT_EQ(rx, -0.4f);
    EXPECT_FLOAT_EQ(rz, 0.3f);
    EXPECT_TRUE(std::isnan(rainOffset(0, 0).first));
    EXPECT_EQ(effectFor("twilightforest:frost", false), Effect::Blizzard);
    EXPECT_EQ(effectFor("twilightforest:hunger", false), Effect::Mosquito);
    EXPECT_EQ(effectFor("twilightforest:fire", false), Effect::Ashes);
    EXPECT_EQ(effectFor("twilightforest:darkness", true), Effect::DarkStream);
    EXPECT_EQ(effectFor("twilightforest:darkness", false), Effect::None);
    EXPECT_EQ(effectFor("twilightforest:acid_rain", false), Effect::BigRain);
    EXPECT_EQ(effectFor("twilightforest:unknown", true), Effect::None);
    EXPECT_STREQ(textureOf(Effect::Blizzard), "textures/twilightforest/environment/blizzard");
    EXPECT_EQ(weatherRange(true), 10);
    EXPECT_EQ(weatherRange(false), 5);
    EXPECT_EQ(lockedSpan(10, 10, -32), (std::pair{0, 20}));
    EXPECT_EQ(lockedSpan(-40, 5, -32), std::nullopt); // both ends at the floor
    EXPECT_EQ(lockedSpan(-35, 5, -32), (std::pair{-32, -30}));
}

TEST(Atmosphere, LockedColumns) {
    ColumnInput in{.x = 3, .z = -2, .minY = 0, .maxY = 20, .ticks = 1000, .partialTicks = 0.5f, .camX = 0.5, .camY = 10.2, .camZ = 0.5, .range = 10};
    auto        b = lockedColumn(in, "twilightforest:frost");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->effect, Effect::Blizzard);
    EXPECT_TRUE(b->fullBright);
    EXPECT_FLOAT_EQ(b->u1 - b->u0, 1.0f);
    EXPECT_NEAR(b->v1 - b->v0, 20 * 0.25f, 1e-3f);
    EXPECT_FLOAT_EQ(b->y0, -10.2f);
    float d = std::sqrt(3.0f * 3.0f + 2.0f * 2.0f) / 10.0f;
    EXPECT_NEAR(b->alpha, (1 - d * d) * 0.3f + 0.5f, 1e-5f);
    // the same column twice is the same column (seeded per column)
    auto b2 = lockedColumn(in, "twilightforest:frost");
    EXPECT_FLOAT_EQ(b->u0, b2->u0);
    // the quad faces the camera: its width is 1 block, perpendicular to the camera direction
    float wx = b->x1 - b->x0, wz = b->z1 - b->z0;
    EXPECT_NEAR(std::sqrt(wx * wx + wz * wz), 1.0f, 1e-4f);
    EXPECT_NEAR(wx * 3.0f + wz * -2.0f, 0.0f, 1e-4f);

    auto m = lockedColumn(in, "twilightforest:hunger");
    ASSERT_TRUE(m);
    EXPECT_FLOAT_EQ(m->alpha, 1.0f);
    EXPECT_LE(m->color.r, 0.3f);
    auto a = lockedColumn(in, "twilightforest:fire");
    ASSERT_TRUE(a);
    EXPECT_GE(a->color.r, 0.8f);
    EXPECT_FLOAT_EQ(a->color.r, a->color.b); // grey
    auto r = lockedColumn(in, "twilightforest:acid_rain");
    ASSERT_TRUE(r);
    EXPECT_FALSE(r->fullBright);
    // darkness: streams in about half the columns, never moving sideways
    int streams = 0;
    for (int x = -10; x <= 10; ++x)
        for (int z = -10; z <= 10; ++z) {
            ColumnInput c = in;
            c.x = x, c.z = z;
            if (auto s = lockedColumn(c, "twilightforest:darkness")) {
                ++streams;
                EXPECT_FLOAT_EQ(s->u0, 0.0f);
            }
        }
    EXPECT_GT(streams, 120);
    EXPECT_LT(streams, 320);
    // the camera's own column draws nothing (NaN in Java)
    ColumnInput own = in;
    own.x = 0, own.z = 0;
    EXPECT_FALSE(lockedColumn(own, "twilightforest:frost"));
    EXPECT_FALSE(lockedColumn(in, "twilightforest:nothing"));
    auto s = sparkleColumn(in);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->effect, Effect::Sparkles);
    EXPECT_LE(s->alpha, 0.8f);
}

TEST(Atmosphere, Intervals) {
    EXPECT_EQ(mergeAndSortIntervals({{5, 8}, {1, 3}, {2, 4}}), (std::vector<Interval>{{1, 4}, {5, 8}}));
    EXPECT_EQ(mergeAndSortIntervals({{1, 3}, {3, 5}}), (std::vector<Interval>{{1, 5}}));
    EXPECT_TRUE(mergeAndSortIntervals({}).empty());
    EXPECT_EQ(subtractIntervals({{0, 10}}, {{2, 3}, {5, 6}}), (std::vector<Interval>{{0, 2}, {3, 5}, {6, 10}}));
    EXPECT_EQ(subtractIntervals({{0, 4}, {6, 9}}, {{3, 7}}), (std::vector<Interval>{{0, 3}, {7, 9}}));
    EXPECT_EQ(subtractIntervals({{0, 4}}, {}), (std::vector<Interval>{{0, 4}}));
    EXPECT_EQ(cameraBox(0, 10, 0, 10), (BlockBox{-10, 0, -10, 10, 30, 10}));
    BlockBox p = cameraBox(0, 10, 0, 10);
    // a protected piece with an unprotected hole in its middle
    std::vector<std::pair<BlockBox, bool>> boxes{{{-5, 0, -5, 5, 20, 5}, true}, {{-1, 8, -1, 1, 11, 1}, false}, {{50, 0, 50, 60, 9, 60}, true}};
    EXPECT_EQ(sparkleIntervals(0, 0, p, boxes), (std::vector<Interval>{{0, 8}, {12, 21}}));
    EXPECT_EQ(sparkleIntervals(4, 4, p, boxes), (std::vector<Interval>{{0, 21}}));
    EXPECT_TRUE(sparkleIntervals(7, 0, p, boxes).empty());
}

TEST(Atmosphere, UrGhastRain) {
    float r = 0;
    for (int i = 0; i < 5; ++i) r = tickUrGhastRain(r, true);
    EXPECT_NEAR(r, 0.5f, 1e-5f);
    for (int i = 0; i < 20; ++i) r = tickUrGhastRain(r, true);
    EXPECT_FLOAT_EQ(r, 1.0f);
    r = tickUrGhastRain(r, false);
    EXPECT_NEAR(r, 0.98f, 1e-6f);
    EXPECT_FLOAT_EQ(rainLevel(0.2f, 0.6f, true), 0.6f);
    EXPECT_FLOAT_EQ(rainLevel(0.2f, 0.6f, false), 0.3f);
    EXPECT_EQ(splashCount(1.0f, false), 100);
    EXPECT_EQ(splashCount(1.0f, true), 50);
    EXPECT_EQ(splashCount(0.5f, false), 25);
}

TEST(Atmosphere, AuroraCounter) {
    int a = 0;
    for (int i = 0; i < 100; ++i) a = tickAurora(a, true, false);
    EXPECT_EQ(a, 60);
    a = tickAurora(a, false, false);
    EXPECT_EQ(a, 59);
    EXPECT_EQ(tickAurora(59, true, true), 0); // empty list disables it
    EXPECT_FLOAT_EQ(auroraAlpha(60, 60, 0.3f), 0.5f);
    EXPECT_FLOAT_EQ(auroraAlpha(0, 60, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(auroraScale(512), 32768.0f);
    EXPECT_FLOAT_EQ(auroraHeight(70.0), 186.0f);
}

TEST(Atmosphere, AuroraShader) {
    EXPECT_GE(auroraSeed(20260521), 0);
    EXPECT_EQ(auroraSeed(20260521), auroraSeed(20260521));
    EXPECT_NE(auroraSeed(1), auroraSeed(2));
    // OpenSimplex2: bounded, continuous, zero on the lattice
    float lo = 0, hi = 0;
    for (int i = 0; i < 2000; ++i) {
        float x = i * 0.0137f, y = i * 0.0291f - 7.0f, z = i * 0.0071f;
        float n = openSimplex2ImproveXY(x, y, z);
        lo      = std::min(lo, n);
        hi      = std::max(hi, n);
        float m = openSimplex2ImproveXY(x + 1e-3f, y, z);
        EXPECT_NEAR(n, m, 0.05f);
    }
    EXPECT_GT(hi, 0.3f);
    EXPECT_LT(lo, -0.3f);
    EXPECT_LE(hi, 1.2f);
    EXPECT_GE(lo, -1.2f);
    EXPECT_NEAR(openSimplex2ImproveXY(0, 0, 0), 0.0f, 1e-5f);
    // fog fade
    EXPECT_FLOAT_EQ(linearFogFade(10, 20, 100), 1.0f);
    EXPECT_FLOAT_EQ(linearFogFade(100, 20, 100), 0.0f);
    EXPECT_NEAR(linearFogFade(60, 20, 100), 0.5f, 1e-5f);
    // fragments: green-cyan to blue, alpha from the ribbons, nothing beyond the fog
    AuroraUniforms u{.seed = auroraSeed(20260521), .camX = 100, .camY = 70, .camZ = -40, .gameTime = 0.25f, .fogStart = 150, .fogEnd = 192};
    int            lit = 0;
    for (int i = -40; i <= 40; ++i)
        for (int j = -40; j <= 40; ++j) {
            auto f = auroraFragment(i * 12.0f, j * 12.0f, u);
            EXPECT_GE(f.alpha, 0.0f);
            EXPECT_LE(f.alpha, 3.0f);
            EXPECT_GE(f.color.g, 0.0f);
            EXPECT_LE(f.color.g, 1.0f);
            if (f.alpha > 0.05f) ++lit;
        }
    EXPECT_GT(lit, 0);
    EXPECT_FLOAT_EQ(auroraFragment(2000, 0, u).alpha, 0.0f);
}
