// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: upstream's density function types, the density graph against the F13 density
// goldens, the noise heights against the height goldens and the surface (rules + blankets) against
// the surface goldens, for the oracle's three seeds.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>

#include "TerrainHarness.h"
#include "core/jrand/JRand.h"
#include "core/landmark/Landmark.h"
#include "core/landmark/Placement.h"

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
               {"argument1", {{"type", "gloamingforest:sqrt"}, {"input", {{"type", "gloamingforest:coord_max"}, {"max", 100.0}, {"x_center", 0.0}, {"z_center", 0.0}}}}},
               {"argument2", {{"type", "gloamingforest:hollow_hill"}, {"x_center", 0.5}, {"y_bottom", 0.5}, {"z_center", 0.5}, {"radius", 8.0}, {"height_scale", 1.0}}}};
    std::string err;
    auto        d = Density::compile(fn, 0, {}, {}, &err);
    ASSERT_TRUE(d) << err;
    EXPECT_EQ(d->computePoint(16, 0, -9, {}), 4.0);
    EXPECT_EQ(d->computePoint(0, 0, 0, {}), 1.0);
    EXPECT_FALSE(Density::compile(json{{"type", "minecraft:spline"}}, 0, {}, {}, &err));
}

TEST(TerrainDensityTypes, TerrainColumnPicksNearestLayer) {
    auto const* c = terrainGrid().column("gloamingforest:clearing");
    ASSERT_NE(c, nullptr) << "biome_grid.json";
    EXPECT_EQ(terrainGrid().size(), 20U);
    EXPECT_EQ(c->biomeAt(0), "gloamingforest:clearing");
    EXPECT_EQ(c->biomeAt(-1), "gloamingforest:clearing");
    EXPECT_EQ(c->biomeAt(-2), "gloamingforest:underground"); // a tie keeps the lower key (-3)
    EXPECT_EQ(c->biomeAt(-3), "gloamingforest:underground");
}

// ---- density goldens ------------------------------------------------------------------------------------

/// The Skylight goldens: F13's oracle and dumper run on a world whose Gloaming Forest uses
/// skylight_noise_gen (tests/terrain/oracle/skylight.ps1).
fs::path skylightGoldenDir() { return repo() / "tests/terrain/golden/skylight"; }

/// initial_density_without_jaggedness and final_density of `settingsName` against the density
/// goldens in `dir` (the router's values at the oracle's points, bit for bit).
void checkDensityGolden(std::string const& settingsName, fs::path const& dir) {
    ASSERT_FALSE(golden::goldenDir().empty());
    json settings = golden::readJson(imported("terrain") / "worldgen/noise_settings" / (settingsName + ".json"));
    ASSERT_FALSE(settings.is_discarded());
    int files = 0;
    for (auto seed : seeds()) {
        json g = golden::readJson(dir / "density" / seedFile(seed));
        ASSERT_FALSE(g.is_discarded()) << seedFile(seed);
        ++files;
        auto const& pts   = g.at("points");
        auto const& depth = g.at("terrain").at("depth");
        auto const& scale = g.at("terrain").at("scale");
        std::map<std::pair<int, int>, DensityData> at;
        for (std::size_t i = 0; i < pts.size(); ++i) at[{pts[i][0].get<int>(), pts[i][2].get<int>()}] = DensityData{depth[i].get<double>(), scale[i].get<double>()};
        // RandomState wires the router through mapAll, which turns upstream's biome-driven routers into
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

TEST(TerrainGolden, DensityFinalAtPoints) { checkDensityGolden("gloaming_noise_gen", golden::goldenDir()); }
TEST(TerrainSkylight, DensityFinalAtPointsMatchesOracle) { checkDensityGolden("skylight_noise_gen", skylightGoldenDir()); }

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

/// NoiseBasedChunkGenerator#getBaseHeight (WORLD_SURFACE_WG and OCEAN_FLOOR_WG) of `settingsName`
/// against the heights goldens in `dir`, over the columns the biomes golden covers.
void checkHeightsGolden(std::string const& settingsName, fs::path const& dir) {
    for (auto seed : seeds()) {
        json g = golden::readJson(dir / "heights" / seedFile(seed));
        ASSERT_FALSE(g.is_discarded());
        int x0 = g.at("x0"), z0 = g.at("z0"), step = g.at("step"), width = g.at("width"), height = g.at("height");
        // orientation of the biome rows and of the height arrays: the one that matches
        int best = -1, bestBad = 1 << 30, bestTested = 0;
        for (int variant = 0; variant < 4; ++variant) {
            bool transposed = (variant & 1) != 0, zMajor = (variant & 2) != 0;
            auto w          = loadWorld(seed, settingsName, transposed);
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
        auto w          = loadWorld(seed, settingsName, transposed);
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

TEST(TerrainGolden, BaseHeightsMatch) { checkHeightsGolden("gloaming_noise_gen", golden::goldenDir()); }
TEST(TerrainSkylight, BaseHeightsMatchOracle) { checkHeightsGolden("skylight_noise_gen", skylightGoldenDir()); }

// Every Skylight golden names the settings its dimension used, and none equals the
// gloaming_noise_gen golden of its seed (a datapack that did not apply would dump those).
TEST(TerrainSkylight, GoldensAreSkylights) {
    for (auto seed : seeds()) {
        json density  = golden::readJson(skylightGoldenDir() / "density" / seedFile(seed));
        json heights  = golden::readJson(skylightGoldenDir() / "heights" / seedFile(seed));
        json tDensity = golden::readJson(golden::goldenDir() / "density" / seedFile(seed));
        json tHeights = golden::readJson(golden::goldenDir() / "heights" / seedFile(seed));
        ASSERT_FALSE(
            density.is_discarded() || heights.is_discarded() || tDensity.is_discarded() || tHeights.is_discarded()
        ) << seedFile(seed);
        for (json const* g : {&density, &heights})
            EXPECT_EQ(g->at("source").value("settings", std::string()), "gloamingforest:skylight_noise_gen")
                << seedFile(seed) << " " << g->at("kind");
        EXPECT_NE(
            density.at("router").at("final_density").at("values"),
            tDensity.at("router").at("final_density").at("values")
        ) << seedFile(seed);
        EXPECT_NE(heights.at("worldSurfaceWg"), tHeights.at("worldSurfaceWg")) << seedFile(seed);
        EXPECT_NE(heights.at("oceanFloorWg"), tHeights.at("oceanFloorWg")) << seedFile(seed);
    }
}


constexpr int kLandmarkRadius = 48;

namespace pl = core::landmark::placement;

/// A rectangle of block columns (inclusive) the oracle's chunks may have terraformed.
struct Reach {
    std::string what;
    int         minX, minZ, maxX, maxZ;
    [[nodiscard]] bool holds(int x, int z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
};

json structureSet(std::string const& unit, std::string const& id) { return golden::readJson(imported(unit) / "worldgen/structure_set" / (id + ".json")); }
std::vector<std::string> biomeTag(std::string const& id) {
    json t = golden::readJson(imported("biomes") / "tags/worldgen/biome" / (id + ".json"));
    return t.is_object() ? t.at("values").get<std::vector<std::string>>() : std::vector<std::string>{};
}

/// The structures off the landmark grid that reach the noise terrain of the oracle's surface chunks,
/// computed from their structure sets and Java's generation points (the oracle's dump has no
/// structure list):
/// - gloamingforest:fallen_trunk (avoid_landmark_grid, salt 1275623845, kept a chunk away from
///   hollow_tree start chunks): FallenTrunkStructure#findGenerationPoint (its own
///   RandomSource.create(seed + chunkX * 14413411 + chunkZ * 43387781): position, radius, length,
///   the biome tests of isValidNoiseBiome and hasInvalidNearbyBiome, orientation) gives the trunk's
///   box; its terraformer (a BEARD_THIN Rigid of the box, TrunkUnderDensityFunction's flattening
///   within TERRAFORM_PIECE_SIZE 10 and the mounds on the trunk's sides) reaches at most 12 blocks
///   from the box, and only inside the chunks the start references (terrain adaptation NONE: the
///   chunks of the box inflated by 10, FallenTrunkPiece#addChildren's terraforming UtilityPiece);
/// - gloamingforest:camp (salt 701432212, beard_box jigsaw pieces): CampStructure#findGenerationPoint's
///   center (the Structure.GenerationContext random), tested against #valid_camp_biomes; its pieces
///   stay within 32 blocks of the center, their beard reaches 12 more.
/// hollow_tree has no terraformer (its pieces are placed after the oracle's surface status).
std::vector<Reach> structureReach(World const& w, int x0, int z0, int x1, int z1) {
    std::vector<Reach>                    out;
    std::map<std::string, pl::Placement> sets;
    for (auto const& [unit, id] : std::vector<std::pair<std::string, std::string>>{{"minorlandmarks", "fallen_trunk"}, {"minorlandmarks", "hollow_tree"}, {"questcamps", "camp"}})
        sets["gloamingforest:" + id] = pl::Placement::fromJson(structureSet(unit, id).at("placement"));
    pl::SetLookup lookup = [&](std::string const& id) -> pl::Placement const* {
        auto it = sets.find(id);
        return it == sets.end() ? nullptr : &it->second;
    };
    pl::VarietyFn variety     = [](int, int) { return std::string(); };
    auto          trunkBiomes = biomeTag("valid_hollow_tree_biomes"), campBiomes = biomeTag("valid_camp_biomes");
    auto          in          = [](std::vector<std::string> const& set, std::string const* b) { return b && std::find(set.begin(), set.end(), *b) != set.end(); };
    auto          sampler     = w.sampler();
    // fallen trunks: at most 28 long, so start chunks up to 2 chunks around the grid reach it
    for (int cx = (x0 >> 4) - 3; cx <= (x1 >> 4) + 3; ++cx)
        for (int cz = (z0 >> 4) - 3; cz <= (z1 >> 4) + 3; ++cz) {
            if (!pl::isStructureChunk(sets["gloamingforest:fallen_trunk"], w.seed, cx, cz, variety, lookup)) continue;
            core::jrand::LegacyRandom r(w.seed + cx * 14413411LL + cz * 43387781LL);
            int                  x = cx * 16 + r.nextInt(16), z = cz * 16 + r.nextInt(16);
            static constexpr int kRadiuses[] = {1, 2, 4};
            int                  radius      = kRadiuses[r.nextInt(3)];
            int                  length      = radius == 4 ? r.nextInt(28 - 22 + 1) + 22 : r.nextInt(24 - 17 + 1) + 17;
            bool                 known       = w.biomes.samplable(x, z);
            if (known) {
                int worldY = w.gen->baseHeight(x, z, false, sampler) - 1;
                if (!in(trunkBiomes, w.noiseBiome(x >> 2, worldY >> 2, z >> 2))) continue; // isValidNoiseBiome
                // hasInvalidNearbyBiome: findBiomeHorizontal(x, worldY, z, 24, 1, invalid, findClosest
                // false) walks the square ring of quart radius 6; any hit ends the attempt
                bool invalid = false, outside = false;
                for (int dz = -6; dz <= 6 && !invalid; ++dz)
                    for (int dx = -6; dx <= 6 && !invalid; ++dx) {
                        auto const* b = w.noiseBiome((x >> 2) + dx, worldY >> 2, (z >> 2) + dz);
                        if (!b) outside = true;
                        else if (!in(trunkBiomes, b)) invalid = true;
                    }
                if (invalid) continue;
                known = !outside;
            }
            int dir = r.nextInt(4); // Direction.Plane.HORIZONTAL: NORTH, EAST, SOUTH, WEST
            int xy = radius > 1 ? radius * 2 + 1 : 4, zs = length - 1;
            // BoundingBox#orientBox(x, y, z, 0, 0, 0, xy, xy, zs, dir), horizontally
            int bx0 = x, bx1 = x + xy - 1, bz0 = z, bz1 = z + zs - 1;
            if (dir == 0) bz0 = z - zs + 1, bz1 = z;
            else if (dir == 1) bx0 = x, bx1 = x + zs - 1, bz0 = z, bz1 = z + xy - 1;
            else if (dir == 3) bx0 = x - zs + 1, bx1 = x, bz0 = z, bz1 = z + xy - 1;
            out.push_back({"fallen_trunk at chunk " + std::to_string(cx) + ", " + std::to_string(cz) + (known ? "" : " (biomes partly unknown)"), std::max(bx0 - 12, ((bx0 - 10) >> 4) * 16),
                           std::max(bz0 - 12, ((bz0 - 10) >> 4) * 16), std::min(bx1 + 12, ((bx1 + 10) >> 4) * 16 + 15), std::min(bz1 + 12, ((bz1 + 10) >> 4) * 16 + 15)});
        }
    // camps
    for (int cx = (x0 >> 4) - 4; cx <= (x1 >> 4) + 4; ++cx)
        for (int cz = (z0 >> 4) - 4; cz <= (z1 >> 4) + 4; ++cz) {
            if (!pl::isStructureChunk(sets["gloamingforest:camp"], w.seed, cx, cz, variety, lookup)) continue;
            core::jrand::WorldgenRandom r(std::make_unique<core::jrand::LegacyRandom>(0));
            r.setLargeFeatureSeed(w.seed, cx, cz);
            // Mth.lerpDiscrete(nextFloat, chunk min, chunk max)
            auto lerp = [](float d, int a, int b) { return a + static_cast<int>(std::floor(d * static_cast<float>(b - a - 1))) + (d > 0.0F ? 1 : 0); };
            int  x    = lerp(r.nextFloat(), cx * 16, cx * 16 + 15);
            int  z    = lerp(r.nextFloat(), cz * 16, cz * 16 + 15);
            if (w.biomes.samplable(x, z)) {
                int  y     = w.gen->baseHeight(x, z, false, sampler);
                bool valid = false; // Structure#isValidBiome at the stub (y near the surface)
                for (int qy = (y >> 2) - 1; qy <= (y >> 2) + 1; ++qy) valid = valid || in(campBiomes, w.noiseBiome(x >> 2, qy, z >> 2));
                if (!valid) continue;
            }
            out.push_back({"camp at chunk " + std::to_string(cx) + ", " + std::to_string(cz), x - 44, z - 44, x + 44, z + 44});
        }
    return out;
}

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
        int bad[2][2] = {}, checked = 0, nearLandmark = 0, nearStructure = 0;
        std::vector<std::string> samples[2];
        auto reaches = structureReach(*w, x0, z0, x0 + n * step - 1, z0 + n * step - 1);
        for (auto const& r : reaches) std::cout << "[ terrain  ] " << seedFile(seed) << ": " << r.what << " reaches x " << r.minX << ".." << r.maxX << ", z " << r.minZ << ".." << r.maxZ << "\n";
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
                        if (std::any_of(reaches.begin(), reaches.end(), [&](Reach const& r) { return r.holds(bx, bz); })) {
                            ++nearStructure;
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
        std::cout << "[ terrain  ] surface " << seedFile(seed) << ": " << checked << " cells (" << nearLandmark << " near a landmark, " << nearStructure << " in a structure's reach left out), z-major " << v << ", height mismatches " << bad[v][0] << ", top mismatches " << bad[v][1] << "\n";
        for (auto const& line : samples[v]) std::cout << "             " << line << "\n";
        EXPECT_EQ(bad[v][0] + bad[v][1], 0) << seedFile(seed);
    }
}

// ---- the Skylight preset ----------------------------------------------------------------------------------

/// skylight_noise_gen over the goldens' real biomes: floating islands where the island noise is high,
/// held in a band around the biome terrain's height (sqrt(clamp(interpolated(...))) with flat_cache
/// inside, minus the distance to raw_biome_terrain), with air (the default fluid) everywhere else.
TEST(TerrainSkylight, FloatingIslandsOverTheVoid) {
    int islands = 0, chunks = 0;
    for (auto seed : seeds()) {
        auto w = loadWorld(seed, "skylight_noise_gen");
        ASSERT_TRUE(w->gen) << w->error;
        EXPECT_EQ(w->gen->defaultFluid(), code::Air);
        auto sampler = w->sampler();
        int  minY    = w->gen->shape().minY;
        std::vector<std::pair<int, int>> found;
        int  bx0 = w->biomes.x0 * 4, bz0 = w->biomes.z0 * 4, bx1 = (w->biomes.x0 + w->biomes.width) * 4, bz1 = (w->biomes.z0 + w->biomes.height) * 4;
        int  scanned = 0;
        for (int x = bx0; x < bx1; x += 8)
            for (int z = bz0; z < bz1; z += 8) {
                if (!w->biomes.samplable(x, z)) continue;
                ++scanned;
                if (w->gen->baseHeight(x, z, false, sampler) > minY) found.emplace_back(x, z);
            }
        std::cout << "[ terrain  ] skylight " << seedFile(seed) << ": " << found.size() << " of " << scanned << " columns hold an island\n";
        ASSERT_FALSE(found.empty()) << seedFile(seed) << ": no island in " << scanned << " columns";
        // islands are the exception over the void
        EXPECT_LT(found.size() * 2, static_cast<std::size_t>(scanned)) << seedFile(seed);
        islands += static_cast<int>(found.size());
        for (int k = 0; k < 6; ++k) {
            auto [x, z] = found[found.size() * static_cast<std::size_t>(k) / 6];
            int  cx = x >> 4, cz = z >> 4;
            Grid g(minY, w->gen->shape().height);
            w->gen->fillNoise(cx, cz, sampler, {}, g);
            std::vector<double> d;
            w->gen->density().fillChunk(cx, cz, sampler, d);
            ASSERT_EQ(d.size(), g.cells.size());
            int solid = 0, water = 0, disagree = 0;
            for (std::size_t i = 0; i < g.cells.size(); ++i) {
                auto c = g.cells[i];
                water += c == code::Water || c == code::Lava;
                solid += c == code::DefaultBlock;
                disagree += (c == code::DefaultBlock) != (d[i] > 0.0);
            }
            EXPECT_EQ(water, 0) << "chunk " << cx << ", " << cz;
            EXPECT_GT(solid, 0) << "chunk " << cx << ", " << cz;
            EXPECT_EQ(disagree, 0) << "stone exactly where the density is above 0";
            // the island floats: the column's lowest block is far above the bottom, air below it
            int lx = x & 15, lz = z & 15, lowest = minY - 1;
            for (int y = minY; y < minY + g.height && lowest < minY; ++y)
                if (g.get(lx, y, lz) != code::Air) lowest = y;
            EXPECT_GT(lowest, minY + 8) << "island column (" << x << ", " << z << ")";
            ++chunks;
        }
    }
    EXPECT_GT(islands, 0);
    EXPECT_EQ(chunks, 18);
}

} // namespace
} // namespace gf::tests::terrain
