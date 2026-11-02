// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the landmark grid (F6) against the Java oracle goldens (F13, tests/golden/landmarks):
// the facts TF 4.8 (TeamTwilight/twilightforest @ 9e40a757, LGPL-2.1-or-later) computed on a
// NeoForge 1.21.1 server for three seeds. Every center and sampled chunk is checked with the C++
// translation of LegacyLandmarkPlacements (getNearestCenterXZ, chunkHasLandmarkCenter,
// manhattanDistanceFromLandmarkCenter, pickVarietyLandmark, pickLandmarkForChunk), and the
// structures that started at each center with LandmarkGridPlacement#isStructureChunk of their
// structure set (assets/data/landmarks).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/jrand/JRand.h"
#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "core/landmark/Placement.h"
#include "TestRepo.h"

namespace {

namespace fs  = std::filesystem;
namespace cl  = gf::core::landmark;
namespace plc = gf::core::landmark::placement;
using nlohmann::json;

using gf::tests::landmark::readJson;
using gf::tests::landmark::repoDir;

/// tests/golden/landmarks/*.json.
std::vector<fs::path> landmarkGoldens() {
    std::vector<fs::path> out;
    std::error_code       ec;
    auto                  dir = repoDir() / "tests/golden/landmarks";
    for (auto const& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

/// The structures that started in a center chunk ("starts": null, one id or a list).
std::set<std::string> startsOf(json const& center) {
    std::set<std::string> out;
    auto const&           s = center.contains("starts") ? center["starts"] : json();
    if (s.is_string()) out.insert(s.get<std::string>());
    else if (s.is_array())
        for (auto const& e : s)
            if (e.is_string()) out.insert(e.get<std::string>());
    return out;
}

/// The landmark grid structure sets of the descriptors: structure key -> placement.
std::map<std::string, plc::Placement> gridSets() {
    std::map<std::string, plc::Placement> out;
    std::error_code                       ec;
    for (auto const& e : fs::directory_iterator(repoDir() / "assets/data/landmarks/twilightforest", ec)) {
        if (e.path().extension() != ".json") continue;
        auto j = readJson(e.path());
        if (!j.is_object() || !j.contains("structure_set")) continue;
        auto p = plc::Placement::fromJson(j["structure_set"].value("placement", json::object()));
        if (p.kind != plc::Kind::LandmarkGrid) continue;
        for (auto const& s : j["structure_set"].value("structures", json::array())) out[s.value("structure", std::string{})] = p;
    }
    return out;
}

#define REQUIRE_ORACLE()                                                                                               \
    do {                                                                                                               \
        ASSERT_FALSE(repoDir().empty()) << "tests/golden not found (set GF_REPO_DIR)";                                \
        ASSERT_FALSE(landmarkGoldens().empty()) << "no tests/golden/landmarks/*.json";                               \
    } while (0)

} // namespace

TEST(LandmarkOracle, CentersMatchTheJavaGrid) {
    REQUIRE_ORACLE();
    int checked = 0;
    for (auto const& file : landmarkGoldens()) {
        SCOPED_TRACE(file.filename().string());
        auto g = readJson(file);
        ASSERT_TRUE(g.is_object());
        std::int64_t seed = g.at("seed").get<std::int64_t>();
        for (auto const& c : g.at("centers")) {
            int rx = c.at("regionX"), rz = c.at("regionZ"), cx = c.at("chunkX"), cz = c.at("chunkZ");
            // getNearestCenterXZ of the region (any chunk of it), and its chunk
            auto center = cl::nearestCenter(rx * 16, rz * 16);
            EXPECT_EQ(center.x, c.at("x").get<int>()) << c.dump();
            EXPECT_EQ(center.z, c.at("z").get<int>()) << c.dump();
            EXPECT_EQ(cx, center.x >> 4) << c.dump();
            EXPECT_EQ(cz, center.z >> 4) << c.dump();
            EXPECT_EQ(cl::nearestCenter(cx, cz), center) << c.dump();
            EXPECT_EQ(cl::chunkHasCenter(cx, cz), c.at("hasCenter").get<bool>()) << c.dump();
            EXPECT_EQ(cl::manhattanDistanceFromCenter(cx, cz), 0) << c.dump();
            // pickVarietyLandmark(seed, chunkX, chunkZ)
            EXPECT_EQ(cl::pickVariety(seed, cx, cz), c.at("variety").get<std::string>()) << c.dump();
            // pickLandmarkForChunk: the biome Java read at (pickBiomeX, pickBiomeZ)
            int  bx = c.at("pickBiomeX"), bz = c.at("pickBiomeZ");
            auto biome   = c.at("pickBiome").get<std::string>();
            int  sampled = 0;
            auto pick    = cl::pickForChunk(seed, cx, cz, [&](int x, int z) {
                ++sampled;
                EXPECT_EQ(x, bx) << c.dump();
                EXPECT_EQ(z, bz) << c.dump();
                return biome;
            });
            EXPECT_EQ(sampled, 1) << c.dump();
            EXPECT_EQ(pick, c.at("pick").get<std::string>()) << c.dump();
            EXPECT_EQ(cl::biomeSampleColumn(cx, cz), (cl::ColumnXZ{bx, bz})) << c.dump();
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3 * 256);
}

TEST(LandmarkOracle, ChunksMatchTheJavaGrid) {
    REQUIRE_ORACLE();
    int checked = 0;
    for (auto const& file : landmarkGoldens()) {
        SCOPED_TRACE(file.filename().string());
        auto         g    = readJson(file);
        std::int64_t seed = g.at("seed").get<std::int64_t>();
        for (auto const& c : g.at("chunks")) {
            int  cx = c.at("chunkX"), cz = c.at("chunkZ");
            auto center = cl::nearestCenter(cx, cz);
            EXPECT_EQ(center.x, c.at("x").get<int>()) << c.dump();
            EXPECT_EQ(center.z, c.at("z").get<int>()) << c.dump();
            EXPECT_EQ(cl::chunkHasCenter(cx, cz), c.at("hasCenter").get<bool>()) << c.dump();
            EXPECT_EQ(cl::manhattanDistanceFromCenter(cx, cz), c.at("manhattan").get<int>()) << c.dump();
            EXPECT_EQ(cl::pickVariety(seed, cx, cz), c.at("variety").get<std::string>()) << c.dump();
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3 * 1024);
}

TEST(LandmarkOracle, VarietyWeightsAreTheOracles) {
    REQUIRE_ORACLE();
    for (auto const& file : landmarkGoldens()) {
        auto                                     g = readJson(file);
        std::vector<std::pair<std::string, int>> weights;
        for (auto const& w : g.at("varietyWeights")) weights.emplace_back(w.at(0).get<std::string>(), w.at(1).get<int>());
        EXPECT_EQ(weights, cl::varietyLandmarks()) << file.filename().string();
    }
}

TEST(LandmarkOracle, GridStartsFollowLandmarkGridPlacement) {
    // LandmarkGridPlacement#isStructureChunk decides the placement half of every landmark start (the
    // other half is the structure's biome tag at the center): a landmark grid structure started at a
    // center only where its set's placement accepts the center chunk
    REQUIRE_ORACLE();
    auto sets = gridSets();
    ASSERT_EQ(sets.size(), 16u) << "the 16 landmark grid structures of TF";
    int starts = 0, refused = 0;
    for (auto const& file : landmarkGoldens()) {
        SCOPED_TRACE(file.filename().string());
        auto         g       = readJson(file);
        std::int64_t seed    = g.at("seed").get<std::int64_t>();
        auto         variety = [seed](int x, int z) { return cl::pickVariety(seed, x, z); };
        plc::SetLookup none  = [](std::string const&) -> plc::Placement const* { return nullptr; };
        for (auto const& c : g.at("centers")) {
            int  cx = c.at("chunkX"), cz = c.at("chunkZ");
            auto started = startsOf(c);
            for (auto const& [key, placement] : sets) {
                bool accepted = plc::isStructureChunk(placement, seed, cx, cz, variety, none);
                if (started.contains(key)) {
                    EXPECT_TRUE(accepted) << key << " started at " << c.dump();
                    ++starts;
                } else if (!accepted) {
                    ++refused;
                }
            }
        }
    }
    EXPECT_GT(starts, 0);
    EXPECT_GT(refused, 0);
}

namespace {

/// The members of TF's structure biome tags (BiomeTagGenerator#addTags, TF 4.8 @ 9e40a757,
/// LGPL-2.1-or-later; data/twilightforest/tags/worldgen/biome/valid_*_biomes.json).
std::map<std::string, std::set<std::string>> const& validBiomeTags() {
    static std::map<std::string, std::set<std::string>> const tags = [] {
        std::set<std::string> const landmarkForests = {
            "twilightforest:clearing",        "twilightforest:dense_forest", "twilightforest:dense_mushroom_forest", "twilightforest:firefly_forest",
            "twilightforest:forest",          "twilightforest:mushroom_forest", "twilightforest:oak_savannah",      "twilightforest:spooky_forest",
        };
        return std::map<std::string, std::set<std::string>>{
            {"#twilightforest:valid_quest_grove_biomes", {"twilightforest:enchanted_forest"}},
            {"#twilightforest:valid_mushroom_tower_biomes", {"twilightforest:dense_mushroom_forest"}},
            {"#twilightforest:valid_hedge_maze_biomes", landmarkForests},
            {"#twilightforest:valid_hollow_hill_biomes", landmarkForests},
            {"#twilightforest:valid_naga_courtyard_biomes", landmarkForests},
            {"#twilightforest:valid_lich_tower_biomes", landmarkForests},
            {"#twilightforest:valid_labyrinth_biomes", {"twilightforest:swamp"}},
            {"#twilightforest:valid_hydra_lair_biomes", {"twilightforest:fire_swamp"}},
            {"#twilightforest:valid_knight_stronghold_biomes", {"twilightforest:dark_forest"}},
            {"#twilightforest:valid_dark_tower_biomes", {"twilightforest:dark_forest_center"}},
            {"#twilightforest:valid_yeti_cave_biomes", {"twilightforest:snowy_forest"}},
            {"#twilightforest:valid_aurora_palace_biomes", {"twilightforest:glacier"}},
            {"#twilightforest:valid_troll_cave_biomes", {"twilightforest:highlands"}},
            {"#twilightforest:valid_giant_house_biomes", {"twilightforest:highlands"}},
            {"#twilightforest:valid_final_castle_biomes", {"twilightforest:final_plateau"}},
        };
    }();
    return tags;
}

/// The "biomes" of each landmark grid structure (assets/data/landmarks): structure key -> tag.
std::map<std::string, std::string> gridBiomes() {
    std::map<std::string, std::string> out;
    std::error_code                    ec;
    for (auto const& e : fs::directory_iterator(repoDir() / "assets/data/landmarks/twilightforest", ec)) {
        if (e.path().extension() != ".json") continue;
        auto j = readJson(e.path());
        if (!j.is_object() || !j.contains("structure") || !j.contains("structure_set")) continue;
        auto p = plc::Placement::fromJson(j["structure_set"].value("placement", json::object()));
        if (p.kind != plc::Kind::LandmarkGrid) continue;
        for (auto const& s : j["structure_set"].value("structures", json::array()))
            out[s.value("structure", std::string{})] = j["structure"].value("biomes", std::string{});
    }
    return out;
}

} // namespace

TEST(LandmarkOracle, AcceptedGridStartsWithAValidBiomeStarted) {
    // the converse: where a landmark grid set accepts the center chunk and the structure's biome tag
    // holds for the tile's main biome (LandmarkStructure#findValidGenerationPoint), Java started it.
    // The biome counts as known where the oracle read the same biome at the biome-grid tile's sample
    // column (pickBiome) and at the center (mainBiome)
    REQUIRE_ORACLE();
    auto sets   = gridSets();
    auto biomes = gridBiomes();
    ASSERT_EQ(biomes.size(), sets.size());
    for (auto const& [key, tag] : biomes) ASSERT_TRUE(validBiomeTags().contains(tag)) << key << ": " << tag;
    int checked = 0, unknown = 0;
    for (auto const& file : landmarkGoldens()) {
        SCOPED_TRACE(file.filename().string());
        auto         g       = readJson(file);
        std::int64_t seed    = g.at("seed").get<std::int64_t>();
        auto         variety = [seed](int x, int z) { return cl::pickVariety(seed, x, z); };
        plc::SetLookup none  = [](std::string const&) -> plc::Placement const* { return nullptr; };
        for (auto const& c : g.at("centers")) {
            int  cx = c.at("chunkX"), cz = c.at("chunkZ");
            auto main = c.value("mainBiome", std::string{});
            if (main.empty() || main != c.value("pickBiome", std::string{})) {
                ++unknown;
                continue;
            }
            auto started = startsOf(c);
            for (auto const& [key, placement] : sets) {
                if (!plc::isStructureChunk(placement, seed, cx, cz, variety, none)) continue;
                bool valid = validBiomeTags().at(biomes.at(key)).contains(main);
                EXPECT_EQ(started.contains(key), valid) << key << " (accepted, biome " << main << ") at " << c.dump();
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 3 * 200);
    EXPECT_LT(unknown, 3 * 16);
}

TEST(LandmarkOracle, JavaRandomMatchesJrandLegacyRandom) {
    // the grid's JavaRandom and F5's jrand::LegacyRandom are the same source: once jrand is real (not
    // F0's stub), they must agree draw for draw, so the grid can switch to it (Grid.h)
    gf::core::jrand::LegacyRandom probe(42);
    if (probe.nextInt() == 0) GTEST_SKIP() << "core/jrand is still the F0 contract stub";
    for (std::int64_t seed : {0LL, 42LL, -1LL, 123456789LL, static_cast<long long>(0x8000000000000000ULL)}) {
        cl::JavaRandom               a(seed);
        gf::core::jrand::LegacyRandom b(seed);
        for (int i = 0; i < 64; ++i) {
            ASSERT_EQ(a.nextInt(), b.nextInt()) << "seed " << seed;
            ASSERT_EQ(a.nextInt(37), b.nextInt(37)) << "seed " << seed;
            ASSERT_EQ(a.nextLong(), b.nextLong()) << "seed " << seed;
            ASSERT_EQ(a.nextFloat(), b.nextFloat()) << "seed " << seed;
            ASSERT_EQ(a.nextDouble(), b.nextDouble()) << "seed " << seed;
        }
    }
}
