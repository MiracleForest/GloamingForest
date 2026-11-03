// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Golden tests of the Java oracle (unit F13) for tests/golden/structure_steps.json: Java's Structure
// registry grouped by GenerationStep.Decoration, each group in registry stream order, the way
// Minecraft 1.21.1's ChunkGenerator#applyBiomeDecoration walks it (a structure's index in its group is
// the salt of setFeatureSeed(decorationSeed, index, step)). Compared with F6's pure decoration-step
// table: core/landmark/Decoration (Minecraft's 34 structures, the registry order, slotOf) plus TF's
// structures with the steps of their worldgen/structure JSON (assets/data/landmarks/twilightforest).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS // std::getenv (GF_REPO_DIR)
#endif

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/landmark/Decoration.h"

namespace {

namespace fs  = std::filesystem;
namespace dec = gf::core::landmark::decoration;
using nlohmann::json;

// The repository root: $GF_REPO_DIR, or found walking up from this file / the working directory
// (xmake runs the tests from the build directory) to tests/golden/manifest.json. Empty: not found.
fs::path repoDir() {
    static fs::path const dir = [] {
        std::error_code ec;
        auto isRoot = [&](fs::path const& p) { return fs::exists(p / "tests/golden/manifest.json", ec); };
        if (char const* env = std::getenv("GF_REPO_DIR"); env && *env && isRoot(env)) return fs::path(env);
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

json readJson(fs::path const& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return json::parse(ss.str(), nullptr, false);
}

json const& golden() {
    static json const g = readJson(repoDir() / "tests/golden/structure_steps.json");
    return g;
}

// TF's structures: the key and the "step" of TF's worldgen/structure JSON (F6's landmark records).
std::vector<dec::Structure> twilightStructures() {
    std::vector<dec::Structure> out;
    std::error_code             ec;
    for (auto const& e : fs::directory_iterator(repoDir() / "assets/data/landmarks/twilightforest", ec)) {
        if (e.path().extension() != ".json") continue;
        json j = readJson(e.path());
        if (!j.is_object()) continue;
        auto step = j.value("structure", json::object()).value("step", std::string{});
        out.push_back({j.value("key", std::string{}), dec::stepIndex(step)});
    }
    return out;
}

// F6's table of Java's Structure registry: Minecraft's structures and TF's.
std::vector<dec::Structure> landmarkRegistry() {
    auto registry = dec::vanillaStructures();
    for (auto& s : twilightStructures()) registry.push_back(std::move(s));
    return registry;
}

// steps of the golden as [step][index] -> id
std::vector<std::vector<std::string>> goldenSteps() {
    std::vector<std::vector<std::string>> out;
    for (json const& group : golden().at("steps")) out.push_back(group.get<std::vector<std::string>>());
    return out;
}

#define REQUIRE_GOLDEN()                                                                                               \
    do {                                                                                                               \
        ASSERT_FALSE(repoDir().empty()) << "repository not found (set GF_REPO_DIR)";                                   \
        ASSERT_TRUE(golden().is_object()) << "tests/golden/structure_steps.json missing or not JSON";                  \
    } while (0)

} // namespace

TEST(OracleStructureSteps, IsWellFormed) {
    REQUIRE_GOLDEN();
    json const& g = golden();
    EXPECT_EQ(g.value("format", 0), 1);
    EXPECT_EQ(g.value("kind", ""), "structure_steps");
    ASSERT_FALSE(g.contains("seed")) << "structure_steps is seed-independent";

    // one group per GenerationStep.Decoration, named as Java serializes it
    auto const& names = g.at("stepNames");
    ASSERT_EQ(names.size(), static_cast<std::size_t>(dec::kSteps));
    for (int i = 0; i < dec::kSteps; ++i) EXPECT_EQ(names.at(i).get<std::string>(), dec::stepName(i)) << "step " << i;
    auto steps = goldenSteps();
    ASSERT_EQ(steps.size(), static_cast<std::size_t>(dec::kSteps));

    // the groups partition the registry, each one in stream order
    auto order = g.at("order").get<std::vector<std::string>>();
    EXPECT_EQ(order.size(), g.at("count").get<std::size_t>());
    std::map<std::string, std::size_t> position;
    for (std::size_t i = 0; i < order.size(); ++i) EXPECT_TRUE(position.emplace(order[i], i).second) << order[i] << " twice in order";
    std::size_t grouped = 0;
    for (int s = 0; s < dec::kSteps; ++s) {
        std::size_t last = 0;
        for (std::size_t i = 0; i < steps[s].size(); ++i) {
            auto it = position.find(steps[s][i]);
            ASSERT_NE(it, position.end()) << steps[s][i] << " (step " << s << ") is not in order";
            if (i > 0) EXPECT_GT(it->second, last) << steps[s][i] << " (step " << s << ") out of stream order";
            last = it->second;
            ++grouped;
        }
    }
    EXPECT_EQ(grouped, order.size()) << "every structure in exactly one step";
}

TEST(OracleStructureSteps, CoversMinecraftAndTwilightForest) {
    REQUIRE_GOLDEN();
    std::map<std::string, int> stepOf;
    auto                       steps = goldenSteps();
    for (int s = 0; s < static_cast<int>(steps.size()); ++s)
        for (auto const& id : steps[s]) stepOf[id] = s;

    // every vanilla structure and every TF landmark, at the step F6 has for it, and nothing else
    auto const tf = twilightStructures();
    ASSERT_EQ(tf.size(), 21u) << "TF 4.8's 21 structures (assets/data/landmarks/twilightforest)";
    std::set<std::string> expected;
    for (auto const* list : {&dec::vanillaStructures(), &tf}) {
        for (auto const& s : *list) {
            expected.insert(s.id);
            auto it = stepOf.find(s.id);
            if (it == stepOf.end()) {
                ADD_FAILURE() << s.id << " is not in the golden";
                continue;
            }
            EXPECT_EQ(it->second, s.step) << s.id << ": Java's step " << dec::stepName(it->second) << ", F6's " << dec::stepName(s.step);
        }
    }
    for (auto const& [id, s] : stepOf) EXPECT_TRUE(expected.contains(id)) << id << " (step " << dec::stepName(s) << ") is in Java's registry but not in F6's table";
    EXPECT_EQ(stepOf.size(), dec::vanillaStructures().size() + tf.size()) << "Minecraft 1.21.1's 34 structures and TF 4.8's 21";
    EXPECT_EQ(stepOf.size(), golden().at("count").get<std::size_t>());
}

TEST(OracleStructureSteps, MatchLandmarkDecorationSlots) {
    REQUIRE_GOLDEN();
    auto const registry = landmarkRegistry();
    // applyBiomeDecoration's index of every structure: its position in its step's group
    auto        steps   = goldenSteps();
    std::size_t checked = 0;
    for (int s = 0; s < static_cast<int>(steps.size()); ++s) {
        for (int i = 0; i < static_cast<int>(steps[s].size()); ++i) {
            auto slot = dec::slotOf(steps[s][i], s, registry);
            EXPECT_EQ(slot, (dec::Slot{s, i})) << steps[s][i] << ": F6 has " << slot.step << "/" << slot.index << ", Java " << s << "/" << i;
            ++checked;
        }
    }
    // the registry order itself: F6's comparator (file path, then namespace) sorts the stream
    auto order = golden().at("order").get<std::vector<std::string>>();
    EXPECT_EQ(checked, order.size()) << "every structure has a slot";
    for (std::size_t i = 1; i < order.size(); ++i) {
        EXPECT_TRUE(dec::registryBefore(order[i - 1], order[i])) << order[i - 1] << " comes before " << order[i] << " in Java's registry";
    }
}
