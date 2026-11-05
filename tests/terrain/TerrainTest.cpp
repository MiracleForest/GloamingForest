// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: TF's density function types, the density graph against the F13 density
// goldens, the noise heights against the height goldens and the surface (rules + blankets) against
// the surface goldens, for the oracle's three seeds.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#include "TerrainHarness.h"
#include "core/landmark/Landmark.h"

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
        // RandomState wires the router through mapAll, which turns TF's biome-driven routers into
        // their ChunkCached variants: one 16 x 16 cache keyed by the section-relative column, kept
        // for every compute of the oracle's run (initial_density_without_jaggedness, then
        // final_density), so a point reads the terrain of the first point of its slot
        std::map<int, DensityData> slots;
        TerrainSampler sampler = [&](int x, int z) {
            auto [it, fresh] = slots.try_emplace((z & 15) + ((x & 15) << 4), DensityData{});
            if (fresh) it->second = at.at({x, z});
            return it->second;
        };
        for (char const* name : {"initial_density_without_jaggedness", "final_density"}) {
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
        std::cout << "[ terrain  ] " << seedFile(seed) << ": biome rows transposed " << ((best & 1) != 0) << ", heights z-major " << ((best & 2) != 0) << "\n";
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


constexpr int kLandmarkRadius = 48;

TEST(TerrainGolden, SurfaceGridMatches) {
    for (auto seed : seeds()) {
        json g = golden::readJson(golden::goldenDir() / "surface" / seedFile(seed));
        ASSERT_FALSE(g.is_discarded());
        auto w = loadWorld(seed);
        ASSERT_TRUE(w->gen) << w->error;
        int x0 = g.at("x0"), z0 = g.at("z0"), step = g.at("step"), n = g.at("width");
        auto const& pal   = g.at("palette");
        std::string chars = g.at("paletteChars");
        auto sampler = w->sampler();
        auto biome   = w->blockBiome();
        // [variant][0 = height mismatches, 1 = top mismatches]
        int bad[2][2] = {}, checked = 0, nearLandmark = 0;
        std::vector<std::string> samples[2];
        for (int cx = x0 >> 4; cx < (x0 + n * step) >> 4; ++cx)
            for (int cz = z0 >> 4; cz < (z0 + n * step) >> 4; ++cz) {
                Grid grid(-32, 288);
                w->gen->fillNoise(cx, cz, sampler, {}, grid);
                w->gen->surface().buildSurface(cx, cz, grid, w->table, biome);
                std::vector<std::string> chunkBiomes;
                for (int qx = cx * 4; qx < cx * 4 + 4; ++qx)
                    for (int qz = cz * 4; qz < cz * 4 + 4; ++qz)
                        for (int qy = -8; qy <= 63; ++qy)
                            if (auto b = w->noiseBiome(qx, qy, qz); b && std::find(chunkBiomes.begin(), chunkBiomes.end(), *b) == chunkBiomes.end()) chunkBiomes.push_back(*b);
                w->blankets.apply(cx, cz, grid, w->table, chunkBiomes, biome, [](auto const&) { return false; });
                for (int x = 0; x < 16; x += step)
                    for (int z = 0; z < 16; z += step) {
                        int bx = cx * 16 + x, bz = cz * 16 + z, i = (bx - x0) / step, j = (bz - z0) / step;
                        // the oracle's chunks carry the landmark terraforming (F6 starts, not in gf-core):
                        // leave out the cells near a landmark center
                        auto lc = core::landmark::nearestCenter(cx, cz);
                        if ((bx - lc.x) * (bx - lc.x) + (bz - lc.z) * (bz - lc.z) < kLandmarkRadius * kLandmarkRadius) {
                            ++nearLandmark;
                            continue;
                        }
                        ++checked;
                        int         ws  = grid.worldSurfaceWg(x, z) + 1;
                        auto        c   = grid.get(x, ws - 1, z);
                        std::string top = c == code::DefaultBlock ? "minecraft:stone" : c == code::Water ? "minecraft:water" : (w->table.entry(c) ? w->table.entry(c)->name : "?");
                        for (int v = 0; v < 2; ++v) {
                            int  ei = v ? j : i, ej = v ? i : j;
                            int  eh = g["worldSurfaceWg"][ei][ej].get<int>();
                            char ec = g["top"][ei].get<std::string>()[static_cast<std::size_t>(ej)];
                            std::string et = pal[chars.find(ec)].get<std::string>();
                            if (eh != ws) ++bad[v][0];
                            if (et != top) ++bad[v][1];
                            if ((eh != ws || et != top) && samples[v].size() < 6)
                                samples[v].push_back("(" + std::to_string(bx) + ", " + std::to_string(bz) + ") " + top + "@" + std::to_string(ws) + " vs " + et + "@" + std::to_string(eh));
                        }
                    }
            }
        int v = bad[0][0] + bad[0][1] <= bad[1][0] + bad[1][1] ? 0 : 1;
        std::cout << "[ terrain  ] surface " << seedFile(seed) << ": " << checked << " cells (" << nearLandmark << " near a landmark left out), z-major " << v << ", height mismatches " << bad[v][0] << ", top mismatches " << bad[v][1] << "\n";
        for (auto const& line : samples[v]) std::cout << "             " << line << "\n";
        EXPECT_EQ(bad[v][0] + bad[v][1], 0) << seedFile(seed);
    }
}

} // namespace
} // namespace gf::tests::terrain
