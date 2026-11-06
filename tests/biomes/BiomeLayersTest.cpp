// U1 biomes: the layer stack and the biome source against the F13 oracle goldens
// (tests/golden/biomes/seed-*.json: main and wide main-biome grids, noise biomes, BiomeManager zoom;
// tests/golden/density/seed-*.json: BiomeDensitySource#sampleTerrain).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <thread>

#include "core/biomes/BiomeLayers.h"
#include "core/jrand/BiomeManager.h"

namespace {

namespace fs = std::filesystem;
using gf::core::biomes::BiomeProvider;
using gf::core::biomes::Json;
using gf::core::biomes::LayerStackRegistry;

fs::path repoDir() {
    static fs::path const dir = [] {
        std::error_code ec;
        auto            isRoot = [&](fs::path const& p) { return fs::exists(p / "tests/golden/manifest.json", ec); };
        if (char const* env = std::getenv("GF_REPO_DIR"); env && isRoot(env)) return fs::path(env);
        for (fs::path start : {fs::path(__FILE__).parent_path(), fs::current_path(ec)}) {
            for (fs::path p = start; !p.empty(); p = p.parent_path()) {
                if (isRoot(p)) return p;
                if (p == p.root_path()) break;
            }
        }
        return fs::path();
    }();
    return dir;
}

Json readJson(fs::path const& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return Json::parse(ss.str(), nullptr, false);
}

fs::path dataDir() { return repoDir() / "assets/gen/import/biomes/data/twilightforest/twilight"; }

LayerStackRegistry const& stacks() {
    static LayerStackRegistry const r = [] {
        LayerStackRegistry reg;
        for (auto const& f : fs::directory_iterator(dataDir() / "biome_layer_stack")) {
            reg.entries["twilightforest:" + f.path().stem().string()] = readJson(f.path());
        }
        return reg;
    }();
    return r;
}

Json const& grid() {
    static Json const g = readJson(dataDir() / "biome_terrain_data/biome_grid.json");
    return g;
}

std::vector<fs::path> goldens(char const* kind) {
    std::vector<fs::path> out;
    std::error_code       ec;
    for (auto const& f : fs::directory_iterator(repoDir() / "tests/golden" / kind, ec)) {
        if (f.path().extension() == ".json") out.push_back(f.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

#define REQUIRE_REPO()                                                                                                 \
    if (repoDir().empty() || !fs::exists(dataDir())) GTEST_SKIP() << "repository data not found (set GF_REPO_DIR)"

std::string at(Json const& g, Json const& grid, int i, int j) {
    std::string const& chars = g.at("paletteChars").get_ref<std::string const&>();
    char               c     = grid.at("rows").at(j).get_ref<std::string const&>().at(i);
    return g.at("palette").at(chars.find(c)).get<std::string>();
}

} // namespace

TEST(Biomes, MainGridsMatchOracle) {
    REQUIRE_REPO();
    auto files = goldens("biomes");
    ASSERT_FALSE(files.empty());
    for (auto const& file : files) {
        SCOPED_TRACE(file.filename().string());
        Json          g = readJson(file);
        BiomeProvider p(grid(), stacks(), g.at("seed").get<std::int64_t>());
        for (char const* name : {"main", "wide"}) {
            Json const& m  = g.at(name);
            int         x0 = m.at("x0"), z0 = m.at("z0"), w = m.at("width"), h = m.at("height"), step = m.at("step");
            int         bad = 0;
            for (int j = 0; j < h; ++j) {
                for (int i = 0; i < w; ++i) {
                    std::string expected = at(g, m, i, j);
                    std::string actual   = p.mainBiome(x0 + i * step, z0 + j * step);
                    if (expected != actual && ++bad <= 10)
                        ADD_FAILURE() << name << " (" << x0 + i * step << ", " << z0 + j * step << "): " << actual << " != " << expected;
                }
            }
            EXPECT_EQ(bad, 0) << name;
        }
    }
}

TEST(Biomes, NoiseAndZoomMatchOracle) {
    REQUIRE_REPO();
    for (auto const& file : goldens("biomes")) {
        SCOPED_TRACE(file.filename().string());
        Json          g = readJson(file);
        std::int64_t  seed = g.at("seed");
        BiomeProvider p(grid(), stacks(), seed);
        for (Json const& s : g.at("noise")) EXPECT_EQ(p.noiseBiome(s.at(0), s.at(1), s.at(2)), s.at(3).get<std::string>()) << s.dump();
        // Level#getBiome: BiomeManager's zoom, the quart y clamped to the dimension (ChunkAccess#getNoiseBiome)
        auto zoomSeed = gf::core::biome::obfuscateSeed(seed);
        int  qMin = int(g.at("minY")) >> 2, qMax = (int(g.at("maxY")) - 1) >> 2;
        for (Json const& s : g.at("zoom")) {
            auto q = gf::core::biome::zoom(zoomSeed, s.at(0), s.at(1), s.at(2));
            EXPECT_EQ(p.noiseBiome(q.x, std::clamp(q.y, qMin, qMax), q.z), s.at(3).get<std::string>()) << s.dump();
        }
        // the columns of the golden are the terrain grid's
        for (auto const& [key, layers] : g.at("columns").items()) {
            auto const* col = p.terrainColumn(key);
            ASSERT_NE(col, nullptr) << key;
            ASSERT_EQ(col->layers.size(), layers.size()) << key;
            for (std::size_t i = 0; i < layers.size(); ++i) {
                EXPECT_DOUBLE_EQ(col->layers[i].first, layers.at(i).at(0).get<double>()) << key;
                EXPECT_EQ(col->layers[i].second, layers.at(i).at(1).get<std::string>()) << key;
            }
        }
    }
}

TEST(Biomes, TerrainBlendMatchesOracle) {
    REQUIRE_REPO();
    auto files = goldens("density");
    ASSERT_FALSE(files.empty());
    for (auto const& file : files) {
        SCOPED_TRACE(file.filename().string());
        Json          g = readJson(file);
        BiomeProvider p(grid(), stacks(), g.at("seed").get<std::int64_t>());
        Json const&   points = g.at("points");
        Json const&   depth  = g.at("terrain").at("depth");
        Json const&   scale  = g.at("terrain").at("scale");
        ASSERT_EQ(points.size(), depth.size());
        int exact = 0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            auto d = p.sampleTerrain(points[i].at(0), points[i].at(2));
            // bit exact unless the C runtime's exp differs from HotSpot's in the last ulp
            EXPECT_NEAR(d.depth, depth[i].get<double>(), 1e-12) << points[i].dump();
            EXPECT_NEAR(d.scale, scale[i].get<double>(), 1e-12) << points[i].dump();
            exact += d.depth == depth[i].get<double>() && d.scale == scale[i].get<double>();
        }
        RecordProperty("exact_" + file.stem().string(), exact);
        EXPECT_GE(exact * 10, int(points.size()) * 9) << "most terrain samples should be bit exact";
    }
}

TEST(Biomes, PossibleBiomesInJavaOrder) {
    REQUIRE_REPO();
    BiomeProvider p(grid(), stacks(), 0);
    // from the grid alone (no layer stack built): the columns sorted by id, each column's layers
    auto b = gf::core::biomes::possibleBiomesOf(grid());
    // the columns sorted by path: clearing first, its layers in ascending elevation (underground at -3)
    ASSERT_GE(b.size(), 22u);
    EXPECT_EQ(b.size(), 22u);
    EXPECT_EQ(b[0], "twilightforest:underground");
    EXPECT_EQ(b[1], "twilightforest:clearing");
    EXPECT_EQ(b[2], "twilightforest:dark_forest");
    EXPECT_NE(std::find(b.begin(), b.end(), "twilightforest:highlands_underground"), b.end());
    // the provider's columns are the grid's (the list above is in their order)
    auto cols = gf::core::biomes::terrainColumnsOf(grid());
    ASSERT_EQ(cols.size(), p.columns().size());
    for (std::size_t i = 0; i < cols.size(); ++i) {
        EXPECT_EQ(cols[i].key, p.columns()[i].key);
        EXPECT_EQ(cols[i].layers, p.columns()[i].layers);
        EXPECT_EQ(cols[i].depth, p.columns()[i].depth);
    }
}

TEST(Biomes, KeyBiomeQuadrantsAndCompanions) {
    REQUIRE_REPO();
    BiomeProvider p(grid(), stacks(), 123456789);
    // every 512x512-quart window holds the four key biomes and their companions
    std::set<std::string> seen;
    for (int qz = -256; qz < 256; qz += 2)
        for (int qx = -256; qx < 256; qx += 2) seen.insert(p.mainBiome(qx, qz));
    for (char const* id :
         {"twilightforest:glacier",
          "twilightforest:fire_swamp",
          "twilightforest:dark_forest_center",
          "twilightforest:final_plateau",
          "twilightforest:snowy_forest",
          "twilightforest:swamp",
          "twilightforest:dark_forest",
          "twilightforest:highlands",
          "twilightforest:thornlands",
          "twilightforest:stream"})
        EXPECT_TRUE(seen.contains(id)) << id;
}

TEST(Biomes, ThreadsAgree) {
    REQUIRE_REPO();
    BiomeProvider          p(grid(), stacks(), -8346138912345678901LL);
    BiomeProvider          ref(grid(), stacks(), -8346138912345678901LL);
    std::atomic<int>       mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 20000; ++i) {
                int qx = ((i * 7919 + t * 104729) % 4096) - 2048, qz = ((i * 6271 + t * 7) % 4096) - 2048;
                if (p.mainBiome(qx, qz).empty()) ++mismatches;
            }
        });
    }
    for (auto& th : threads) th.join();
    for (int qz = -2048; qz < 2048; qz += 37)
        for (int qx = -2048; qx < 2048; qx += 41)
            if (p.mainBiome(qx, qz) != ref.mainBiome(qx, qz)) ++mismatches;
    EXPECT_EQ(mismatches.load(), 0);
}
