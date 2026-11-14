// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of the landmark decoration seed (F6, core/landmark/Decoration): the random Minecraft 1.21.1's
// ChunkGenerator#applyBiomeDecoration places a structure's pieces with. The structure slots follow
// Java's Structure registry (Minecraft's 34 structures and upstream 4.8's 21, the steps of their
// worldgen/structure JSON: upstream's from assets/data/landmarks); the seeds are checked against the
// random goldens the F13 oracle dumped from Java (tests/golden/rng.json, WorldgenRandom over a
// XoroshiroRandomSource: setDecorationSeed, then setFeatureSeed(decorationSeed, index, step)).
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/landmark/Decoration.h"
#include "TestRepo.h"

namespace {

namespace fs  = std::filesystem;
namespace dec = gf::core::landmark::decoration;
using gf::tests::landmark::readJson;
using gf::tests::landmark::repoDir;
using nlohmann::json;

/// Java's Structure registry: Minecraft's structures plus upstream's (assets/data/landmarks: the key and
/// the "step" of upstream's worldgen/structure JSON).
std::vector<dec::Structure> javaRegistry() {
    auto            registry = dec::vanillaStructures();
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(repoDir() / "assets/data/landmarks/gloamingforest", ec)) {
        if (e.path().extension() != ".json") continue;
        auto j = readJson(e.path());
        if (!j.is_object()) continue;
        auto step = j.value("structure", json::object()).value("step", std::string{});
        registry.push_back({j.value("key", std::string{}), dec::stepIndex(step)});
    }
    return registry;
}

constexpr int kUnderground = 3, kSurface = 4, kTopLayer = 10;

} // namespace

TEST(LandmarkDecoration, StepNames) {
    EXPECT_EQ(dec::stepIndex("raw_generation"), 0);
    EXPECT_EQ(dec::stepIndex("underground_structures"), kUnderground);
    EXPECT_EQ(dec::stepIndex("surface_structures"), kSurface);
    EXPECT_EQ(dec::stepIndex("strongholds"), 5);
    EXPECT_EQ(dec::stepIndex("top_layer_modification"), kTopLayer);
    EXPECT_EQ(dec::stepIndex("nope"), -1);
    for (int i = 0; i < dec::kSteps; ++i) EXPECT_EQ(dec::stepIndex(dec::stepName(i)), i);
    EXPECT_TRUE(dec::stepName(11).empty());
}

TEST(LandmarkDecoration, RegistryOrderIsPathThenNamespace) {
    // ResourceLocation#compareTo over "worldgen/structure/<path>.json": the path first
    EXPECT_TRUE(dec::registryBefore("gloamingforest:aurora_palace", "minecraft:bastion_remnant"));
    EXPECT_TRUE(dec::registryBefore("minecraft:bastion_remnant", "gloamingforest:camp"));
    // the ".json" suffix: "x.json" < "x_y.json" ('.' 0x2E < '_' 0x5F)
    EXPECT_TRUE(dec::registryBefore("minecraft:ruined_portal", "minecraft:ruined_portal_desert"));
    EXPECT_TRUE(dec::registryBefore("minecraft:shipwreck", "minecraft:shipwreck_beached"));
    // the same path: the namespace decides
    EXPECT_TRUE(dec::registryBefore("minecraft:stronghold", "othermod:stronghold"));
    EXPECT_FALSE(dec::registryBefore("othermod:stronghold", "minecraft:stronghold"));
    EXPECT_TRUE(dec::registryBefore("stronghold", "othermod:stronghold")); // no namespace: minecraft
}

TEST(LandmarkDecoration, GloamingStructureSlotsFollowJavasRegistry) {
    ASSERT_FALSE(repoDir().empty()) << "repository not found (set GF_REPO_DIR)";
    auto registry = javaRegistry();
    ASSERT_EQ(registry.size(), 34u + 21u) << "Minecraft's 34 structures and upstream's 21";
    std::map<int, int> perStep;
    for (auto const& s : registry) ++perStep[s.step];
    EXPECT_EQ(perStep[kUnderground], 8);
    EXPECT_EQ(perStep[kSurface], 43);
    EXPECT_EQ(perStep[7], 3); // underground_decoration: ancient_city, fortress, nether_fossil
    EXPECT_EQ(perStep[kTopLayer], 1);

    // applyBiomeDecoration's index of every upstream structure: the structures of its step before it in the
    // registry (worked out by hand from the file paths, Minecraft's structures included)
    std::map<std::string, dec::Slot> const expected = {
        {"gloamingforest:knight_stronghold", {kUnderground, 1}}, // after buried_treasure
        {"gloamingforest:labyrinth", {kUnderground, 2}},
        {"gloamingforest:troll_cave", {kUnderground, 7}}, // after mineshaft(_mesa), trail_ruins, trial_chambers
        {"gloamingforest:aurora_palace", {kSurface, 0}},
        {"gloamingforest:camp", {kSurface, 2}}, // after bastion_remnant
        {"gloamingforest:dark_tower", {kSurface, 3}},
        {"gloamingforest:fallen_trunk", {kSurface, 6}}, // after desert_pyramid, end_city
        {"gloamingforest:final_castle", {kSurface, 7}},
        {"gloamingforest:hedge_maze", {kSurface, 8}},
        {"gloamingforest:hollow_tree", {kSurface, 9}},
        {"gloamingforest:hydra_lair", {kSurface, 10}},
        {"gloamingforest:large_hollow_hill", {kSurface, 13}}, // after igloo, jungle_pyramid
        {"gloamingforest:lich_tower", {kSurface, 14}},
        {"gloamingforest:medium_hollow_hill", {kSurface, 16}}, // after mansion
        {"gloamingforest:mushroom_tower", {kSurface, 18}},     // after monument
        {"gloamingforest:naga_courtyard", {kSurface, 19}},
        {"gloamingforest:quest_grove", {kSurface, 23}}, // after ocean_ruin_*, pillager_outpost
        {"gloamingforest:small_hollow_hill", {kSurface, 33}}, // after the 7 ruined portals and 2 shipwrecks
        {"gloamingforest:swamp_hollow_tree", {kSurface, 35}}, // after stronghold (surface_structures in 1.21.1)
        {"gloamingforest:yeti_cave", {kSurface, 42}},         // after swamp_hut and the 5 villages: the last one
        {"gloamingforest:giant_house", {kTopLayer, 0}},
    };
    ASSERT_EQ(expected.size(), 21u);
    for (auto const& s : registry) {
        auto slot = dec::slotOf(s.id, s.step, registry);
        if (auto it = expected.find(s.id); it != expected.end()) {
            EXPECT_EQ(slot, it->second) << s.id << ": got " << slot.step << "/" << slot.index;
        } else {
            EXPECT_TRUE(s.id.starts_with("minecraft:")) << "unexpected structure " << s.id;
        }
    }
    // a few of Minecraft's own, which shift upstream's indices
    EXPECT_EQ(dec::slotOf("minecraft:buried_treasure", kUnderground, registry), (dec::Slot{kUnderground, 0}));
    EXPECT_EQ(dec::slotOf("minecraft:monument", kSurface, registry), (dec::Slot{kSurface, 17}));
    EXPECT_EQ(dec::slotOf("minecraft:stronghold", kSurface, registry), (dec::Slot{kSurface, 34}));
    EXPECT_EQ(dec::slotOf("minecraft:ancient_city", 7, registry), (dec::Slot{7, 0}));
    // a structure that is not listed takes the place it would sort to
    EXPECT_EQ(dec::slotOf("gf_test:landmark_fixture", kSurface, registry), (dec::Slot{kSurface, 13}));
}

TEST(LandmarkDecoration, SeedsMatchJava) {
    ASSERT_FALSE(repoDir().empty()) << "repository not found (set GF_REPO_DIR)";
    auto g = readJson(repoDir() / "tests/golden/rng.json");
    ASSERT_TRUE(g.is_object() && g.contains("worldgen")) << "tests/golden/rng.json";
    int checked = 0;
    for (auto const& e : g.at("worldgen")) {
        if (e.at("source") != "xoroshiro") continue; // applyBiomeDecoration's WorldgenRandom is a Xoroshiro one
        std::int64_t levelSeed = e.at("levelSeed");
        SCOPED_TRACE("seed " + std::to_string(levelSeed));
        for (auto const& d : e.at("decoration")) {
            int minX = d.at("minBlockX"), minZ = d.at("minBlockZ");
            if (minX % 16 != 0 || minZ % 16 != 0) continue; // a chunk's section origin only
            std::int64_t ds = dec::decorationSeed(levelSeed, minX / 16, minZ / 16);
            EXPECT_EQ(ds, d.at("decorationSeed").get<std::int64_t>()) << "chunk " << minX / 16 << " " << minZ / 16;
            for (auto const& f : d.at("features")) {
                dec::Slot slot{f.at("step").get<int>(), f.at("index").get<int>()};
                auto      random = dec::structureRandom(ds, slot);
                for (auto const& v : f.at("nextInt16")) EXPECT_EQ(random->nextInt(16), v.get<int>()) << "slot " << slot.step << "/" << slot.index;
                ++checked;
            }
        }
    }
    EXPECT_GE(checked, 9) << "the xoroshiro decoration goldens";
}

TEST(LandmarkDecoration, FeatureSeedIsDecorationSeedPlusIndexPlusTenThousandSteps) {
    // setFeatureSeed(decorationSeed, index, step) = setSeed(decorationSeed + index + 10000 * step):
    // the lich tower (surface_structures, index 14) of a chunk is the 40014th seed after the
    // decoration seed, and a landmark with another slot draws another sequence
    for (std::int64_t levelSeed : {0LL, 123456789LL, -8346138912345678901LL}) {
        for (auto [cx, cz] : {std::pair{0, 0}, std::pair{-37, 512}, std::pair{1874999, -1874999}}) {
            auto ds   = dec::decorationSeed(levelSeed, cx, cz);
            auto lich = dec::structureRandom(ds, {kSurface, 14});
            gf::core::jrand::WorldgenRandom same(std::make_unique<gf::core::jrand::Xoroshiro128pp>(0));
            same.setSeed(static_cast<std::int64_t>(static_cast<std::uint64_t>(ds) + 14u + 40000u));
            auto other = dec::structureRandom(ds, {kUnderground, 2});
            auto a = lich->nextLong(), b = same.nextLong(), c = other->nextLong();
            EXPECT_EQ(a, b);
            EXPECT_NE(a, c);
        }
    }
}
