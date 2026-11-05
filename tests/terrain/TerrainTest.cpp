// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: TF's density function types, the density graph against the F13 density
// goldens, the noise heights against the height goldens and the surface (rules + blankets) against
// the surface goldens, for the oracle's three seeds.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <sstream>

#include "TerrainHarness.h"

namespace gf::tests::terrain {
namespace {

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// ---- density types -----------------------------------------------------------------------------------

TEST(TerrainDensityTypes, FocusedIsFloatClampedMap) {
    auto f = Focused::fromPos(0, 0, 0, 10.0F, 1.0F, -1.0F);
    EXPECT_EQ(f.compute(0, 0, 0), static_cast<double>(jmth::clampedMap(jmth::sqrt(0.75F), 0.0F, 10.0F, 1.0F, -1.0F)));
    EXPECT_EQ(f.compute(100, 0, 0), -1.0);
    EXPECT_EQ(f.minValue(), 0.0);
    EXPECT_EQ(f.maxValue(), 10.0);
}

TEST(TerrainDensityTypes, HollowHillShapes) {
    auto h = HollowHill::fromPos(0, 0, 0, 20.0F, 1.0F);
    // the center column: height = cos(0) * r / 3 (table cosine), clamped to [-1, 1]
    EXPECT_EQ(h.compute(0, 0, 0), 1.0);
    EXPECT_EQ(h.compute(0, 40, 0), -1.0);
    EXPECT_EQ(h.compute(30, 0, 0), 0.0); // outside the radius
    auto bowl = HollowHill::fromPos(0, 0, 0, -20.0F, 1.0F);
    EXPECT_EQ(bowl.compute(0, -40, 0), 1.0);
    EXPECT_EQ(jmth::cos(0.0F), 1.0F);
    EXPECT_EQ(jmth::sin(jmth::kPi / 2.0F), 1.0F);
    EXPECT_EQ(jmth::f2i(std::nanf("")), 0);
    EXPECT_EQ(jmth::f2i(1e20F), 2147483647);
    EXPECT_EQ(jmth::f2i(-1e20F), -2147483647 - 1);
}

TEST(TerrainDensityTypes, AbsoluteDifferenceAndTanhHill) {
    AbsoluteDifference mn{false, 5.0, 10.0, 20.0}, mx{true, 5.0, 10.0, 20.0};
    EXPECT_EQ(mn.compute(12, 23), 2.0);
    EXPECT_EQ(mx.compute(12, 23), 3.0);
    EXPECT_EQ(mx.compute(100, 23), 5.0);
    auto t = TanhHill::withAngle(0.5F, 0.0F, 0.5F, 4.0F, 3.0F, 0.0F, true, true);
    EXPECT_EQ(t.compute(0, 20, 0), -1.0); // |dY| > 10
    EXPECT_EQ(t.compute(1, 0, 1), 1.0);   // on the mound
    EXPECT_EQ(t.compute(1, 0, -3), -1.0); // wrong side
}

TEST(TerrainDensityTypes, CompiledCustomTypes) {
    json fn = {{"type", "minecraft:add"},
               {"argument1", {{"type", "twilightforest:sqrt"}, {"input", {{"type", "twilightforest:coord_max"}, {"max", 100.0}, {"x_center", 0.0}, {"z_center", 0.0}}}}},
               {"argument2", {{"type", "twilightforest:hollow_hill"}, {"x_center", 0.5}, {"y_bottom", 0.5}, {"z_center", 0.5}, {"radius", 8.0}, {"height_scale", 1.0}}}};
    std::string err;
    auto        d = Density::compile(fn, 0, {}, {}, &err);
    ASSERT_TRUE(d) << err;
    EXPECT_EQ(d->computePoint(16, 0, -9, {}), 4.0);
    EXPECT_EQ(d->computePoint(0, 0, 0, {}), 1.0);
    EXPECT_FALSE(Density::compile(json{{"type", "minecraft:spline"}}, 0, {}, {}, &err));
}

TEST(TerrainDensityTypes, TerrainColumnPicksNearestLayer) {
    auto const* c = terrainGrid().column("twilightforest:clearing");
    ASSERT_NE(c, nullptr) << "biome_grid.json";
    EXPECT_EQ(terrainGrid().size(), 20U);
    EXPECT_EQ(c->biomeAt(0), "twilightforest:clearing");
    EXPECT_EQ(c->biomeAt(-1), "twilightforest:clearing");
    EXPECT_EQ(c->biomeAt(-2), "twilightforest:underground"); // a tie keeps the lower key (-3)
    EXPECT_EQ(c->biomeAt(-3), "twilightforest:underground");
}

// ---- density goldens ------------------------------------------------------------------------------------

TEST(TerrainGolden, DensityFinalAtPoints) {
    ASSERT_FALSE(golden::goldenDir().empty());
    json settings = golden::readJson(imported("terrain") / "worldgen/noise_settings/twilight_noise_gen.json");
    ASSERT_FALSE(settings.is_discarded());
    int files = 0;
    for (auto seed : seeds()) {
        json g = golden::readJson(golden::goldenDir() / "density" / seedFile(seed));
        ASSERT_FALSE(g.is_discarded()) << seedFile(seed);
        ++files;
        auto const& pts   = g.at("points");
        auto const& depth = g.at("terrain").at("depth");
        auto const& scale = g.at("terrain").at("scale");
        std::map<std::pair<int, int>, DensityData> at;
        for (std::size_t i = 0; i < pts.size(); ++i) at[{pts[i][0].get<int>(), pts[i][2].get<int>()}] = DensityData{depth[i].get<double>(), scale[i].get<double>()};
        TerrainSampler sampler = [&](int x, int z) { return at.at({x, z}); };
        for (char const* name : {"final_density", "initial_density_without_jaggedness"}) {
            std::string err;
            auto d = Density::compile(settings.at("noise_router").at(name), seed, densityResolver(), {}, &err);
            ASSERT_TRUE(d) << err;
            auto const& entry = g.at("router").at(name);
            EXPECT_EQ(d->minValue(), entry.at("min").get<double>()) << name;
            EXPECT_EQ(d->maxValue(), entry.at("max").get<double>()) << name;
            auto const& values = entry.at("values");
            int         bad    = 0;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                int    x = pts[i][0], y = pts[i][1], z = pts[i][2];
                double v = d->computePoint(x, y, z, sampler), e = values[i].get<double>();
                if (!sameBits(v, e) && bad++ < 5) ADD_FAILURE() << seedFile(seed) << " " << name << " at (" << x << ", " << y << ", " << z << "): " << v << " != " << e;
            }
            EXPECT_EQ(bad, 0) << seedFile(seed) << " " << name;
        }
    }
    EXPECT_EQ(files, 3);
}

/// The F13 heights grid ([i][j] or [j][i]) at column (x0 + i * step, z0 + j * step).
int gridAt(json const& rows, int i, int j, bool zMajor) { return zMajor ? rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)].get<int>() : rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)].get<int>(); }

TEST(TerrainGolden, SampleTerrainMatchesDensityGoldenWhereGridCovers) {
    for (auto seed : seeds()) {
        auto w = loadWorld(seed);
        ASSERT_TRUE(w->gen) << w->error;
        json g      = golden::readJson(golden::goldenDir() / "density" / seedFile(seed));
        auto pts    = g.at("points");
        int  tested = 0, bad = 0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            int x = pts[i][0], z = pts[i][2];
            if (!w->biomes.samplable(x, z)) continue;
            ++tested;
            auto d = w->sampler()(x, z);
            if (!sameBits(d.depth, g["terrain"]["depth"][i].get<double>()) || !sameBits(d.scale, g["terrain"]["scale"][i].get<double>()))
                if (bad++ < 5) ADD_FAILURE() << seedFile(seed) << " sampleTerrain(" << x << ", " << z << ") = " << d.depth << " / " << d.scale;
        }
        EXPECT_EQ(bad, 0);
        RecordProperty("tested_" + std::to_string(seed), tested);
    }
}

TEST(TerrainGolden, BaseHeightsMatch) {
    for (auto seed : seeds()) {
        json g = golden::readJson(golden::goldenDir() / "heights" / seedFile(seed));
        ASSERT_FALSE(g.is_discarded());
        int x0 = g.at("x0"), z0 = g.at("z0"), step = g.at("step"), width = g.at("width"), height = g.at("height");
        // orientation of the biome rows and of the height arrays: the one that matches
        int best = -1, bestBad = 1 << 30, bestTested = 0;
        for (int variant = 0; variant < 4; ++variant) {
            bool transposed = (variant & 1) != 0, zMajor = (variant & 2) != 0;
            auto w          = loadWorld(seed, "twilight_noise_gen", transposed);
            ASSERT_TRUE(w->gen) << w->error;
            auto sampler = w->sampler();
            int  bad = 0, tested = 0;
            for (int i = 0; i < width && bad < 40; i += 3)
                for (int j = 0; j < height && bad < 40; j += 3) {
                    int x = x0 + i * step, z = z0 + j * step;
                    if (!w->biomes.samplable(x, z)) continue;
                    ++tested;
                    if (w->gen->baseHeight(x, z, false, sampler) != gridAt(g.at("worldSurfaceWg"), i, j, zMajor)) ++bad;
                }
            if (bad < bestBad) best = variant, bestBad = bad, bestTested = tested;
        }
        ASSERT_GE(best, 0);
        bool transposed = (best & 1) != 0, zMajor = (best & 2) != 0;
        auto w          = loadWorld(seed, "twilight_noise_gen", transposed);
        auto sampler    = w->sampler();
        int  bad = 0, tested = 0;
        for (int i = 0; i < width; ++i)
            for (int j = 0; j < height; ++j) {
                int x = x0 + i * step, z = z0 + j * step;
                if (!w->biomes.samplable(x, z)) continue;
                ++tested;
                for (bool ocean : {false, true}) {
                    int got = w->gen->baseHeight(x, z, ocean, sampler);
                    int exp = gridAt(g.at(ocean ? "oceanFloorWg" : "worldSurfaceWg"), i, j, zMajor);
                    if (got != exp && bad++ < 8)
                        ADD_FAILURE() << seedFile(seed) << (ocean ? " oceanFloorWg" : " worldSurfaceWg") << " at (" << x << ", " << z << "): " << got << " != " << exp;
                }
            }
        EXPECT_EQ(bad, 0) << seedFile(seed) << ": " << tested << " columns (variant " << best << ", sample " << bestTested << ")";
        EXPECT_GT(tested, 120 * 120);
    }
}

} // namespace
} // namespace gf::tests::terrain
