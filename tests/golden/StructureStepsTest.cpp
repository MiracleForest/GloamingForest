// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Golden tests of the Java oracle (unit F13) for tests/golden/structure_steps.json: Java's Structure
// registry grouped by GenerationStep.Decoration, each group in registry stream order, the way
// Minecraft 1.21.1's ChunkGenerator#applyBiomeDecoration walks it (a structure's index in its group is
// the salt of setFeatureSeed(decorationSeed, index, step)). Compared with F6's pure decoration-step
// table: core/landmark/Decoration (Minecraft's 34 structures, the registry order, slotOf) plus upstream's
// structures with the steps of their worldgen/structure JSON (assets/data/landmarks/gloamingforest).
// Also compared: F5's built-in structure table (src/feature/worldgen/Structures.cpp kVanilla /
// kGloaming), which worldgen::ext::structuresByStep / structureSlot sort into the runtime registry the
// landmark starts are seeded with first.

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/landmark/Decoration.h"

#include "GoldenRepo.h"

namespace {

namespace fs  = std::filesystem;
namespace dec = gf::core::landmark::decoration;
using gf::tests::golden::readFile;
using gf::tests::golden::readJson;
using gf::tests::golden::repoDir;
using nlohmann::json;

json const& golden() {
    static json const g = readJson(repoDir() / "tests/golden/structure_steps.json");
    return g;
}

// upstream's structures: the key and the "step" of upstream's worldgen/structure JSON (F6's landmark records).
// A record without a key or with an unknown step fails here, by file name, and is left out.
std::vector<dec::Structure> gloamingStructures() {
    std::vector<dec::Structure> out;
    std::error_code             ec;
    fs::path const              dir = repoDir() / "assets/data/landmarks/gloamingforest";
    for (auto const& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".json") continue;
        auto const file = e.path().filename().string();
        json       j    = readJson(e.path());
        if (!j.is_object()) {
            ADD_FAILURE() << dir.generic_string() << "/" << file << " is not a JSON object";
            continue;
        }
        auto key  = j.value("key", std::string{});
        auto name = j.value("structure", json::object()).value("step", std::string{});
        auto step = dec::stepIndex(name);
        if (key.empty() || step < 0) {
            ADD_FAILURE() << file << ": landmark record without a key (\"" << key << "\") or with an unknown structure.step (\"" << name << "\")";
            continue;
        }
        out.push_back({std::move(key), step});
    }
    EXPECT_FALSE(ec) << dir.generic_string() << ": " << ec.message();
    return out;
}

// CamelCase enumerator -> Java's serialized name (UndergroundStructures -> underground_structures).
std::string snakeCase(std::string const& camel) {
    std::string out;
    for (char c : camel) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            if (!out.empty()) out += '_';
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            out += c;
        }
    }
    return out;
}

// F5's built-in table: the {"<id>", DecorationStep::<Step>} rows of kVanilla and kGloaming in
// src/feature/worldgen/Structures.cpp (feature code the gtests cannot link), steps by their
// serialized names. Empty when the file or the arrays are not found.
std::map<std::string, std::vector<std::pair<std::string, std::string>>> worldgenBuiltins() {
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> out;
    std::string const src = readFile(repoDir() / "src/feature/worldgen/Structures.cpp");
    for (char const* table : {"kVanilla", "kGloaming"}) {
        auto start = src.find(std::string("Builtin ") + table + "[]");
        if (start == std::string::npos) continue;
        auto end = src.find("};", start);
        std::string const body = src.substr(start, end == std::string::npos ? std::string::npos : end - start);
        static std::regex const row(R"re(\{\s*"([^"]+)"\s*,\s*DecorationStep::(\w+)\s*\})re");
        auto& rows = out[table];
        for (std::sregex_iterator it(body.begin(), body.end(), row), last; it != last; ++it) rows.emplace_back((*it)[1].str(), snakeCase((*it)[2].str()));
    }
    return out;
}

// F6's table of Java's Structure registry: Minecraft's structures and upstream's.
std::vector<dec::Structure> landmarkRegistry() {
    auto registry = dec::vanillaStructures();
    for (auto& s : gloamingStructures()) registry.push_back(std::move(s));
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

TEST(OracleStructureSteps, CoversMinecraftAndGloamingForest) {
    REQUIRE_GOLDEN();
    std::map<std::string, int> stepOf;
    auto                       steps = goldenSteps();
    for (int s = 0; s < static_cast<int>(steps.size()); ++s)
        for (auto const& id : steps[s]) stepOf[id] = s;

    // every vanilla structure and every upstream landmark, at the step F6 has for it, and nothing else
    auto const tf = gloamingStructures();
    ASSERT_EQ(tf.size(), 21u) << "upstream 4.8's 21 structures (assets/data/landmarks/gloamingforest)";
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
    EXPECT_EQ(stepOf.size(), dec::vanillaStructures().size() + tf.size()) << "Minecraft 1.21.1's 34 structures and upstream 4.8's 21";
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

// F5's built-in table, which worldgen::ext::structuresByStep() / structureSlot() sort into the runtime
// registry (path, then namespace: F6's registryBefore, checked against the golden's order above; the
// selftest landmark.decoration compares structuresByStep() with F6's table entry by entry): every
// structure of the golden once, at Java's step, and F5's DecorationStep enumerators at Java's ordinals.
TEST(OracleStructureSteps, MatchWorldgenBuiltinTable) {
    REQUIRE_GOLDEN();
    // enum class DecorationStep (src/feature/ext/worldgen/Generation.h): static_cast<int> is the step
    std::string const header = readFile(repoDir() / "src/feature/ext/worldgen/Generation.h");
    std::smatch       decl;
    ASSERT_TRUE(std::regex_search(header, decl, std::regex(R"(enum class DecorationStep\s*\{([^}]*)\})")))
        << "enum class DecorationStep not found in src/feature/ext/worldgen/Generation.h";
    // the enumerators without comments (a comment's words are not enumerators)
    std::string const        list = std::regex_replace(std::regex_replace(decl[1].str(), std::regex(R"(//[^\n]*)"), ""), std::regex(R"(/\*[\s\S]*?\*/)"), "");
    std::vector<std::string> enumerators;
    static std::regex const  word(R"(\w+)");
    for (std::sregex_iterator it(list.begin(), list.end(), word), last; it != last; ++it) enumerators.push_back(it->str());
    ASSERT_EQ(enumerators.size(), static_cast<std::size_t>(dec::kSteps)) << "DecorationStep has GenerationStep.Decoration's 11 values";
    for (int i = 0; i < dec::kSteps; ++i) EXPECT_EQ(snakeCase(enumerators[static_cast<std::size_t>(i)]), dec::stepName(i)) << "DecorationStep ordinal " << i;

    std::map<std::string, int> stepOf;
    auto                       steps = goldenSteps();
    for (int s = 0; s < static_cast<int>(steps.size()); ++s)
        for (auto const& id : steps[s]) stepOf[id] = s;

    auto builtins = worldgenBuiltins();
    ASSERT_EQ(builtins["kVanilla"].size(), dec::vanillaStructures().size()) << "kVanilla rows in src/feature/worldgen/Structures.cpp: Minecraft 1.21.1's 34 structures";
    ASSERT_EQ(builtins["kGloaming"].size(), 21u) << "kGloaming rows in src/feature/worldgen/Structures.cpp: upstream 4.8's 21 structures";
    std::set<std::string> seen;
    for (auto const& [table, rows] : builtins) {
        std::string const ns = table == "kVanilla" ? "minecraft:" : "gloamingforest:";
        for (auto const& [id, stepName] : rows) {
            EXPECT_TRUE(seen.insert(id).second) << id << " twice in F5's table";
            EXPECT_EQ(id.rfind(ns, 0), 0u) << id << " in " << table;
            auto it = stepOf.find(id);
            if (it == stepOf.end()) {
                ADD_FAILURE() << id << " (" << table << ") is not in Java's registry";
                continue;
            }
            EXPECT_EQ(dec::stepIndex(stepName), it->second) << id << ": Java's step " << dec::stepName(it->second) << ", F5's " << stepName;
        }
    }
    for (auto const& [id, s] : stepOf) EXPECT_TRUE(seen.contains(id)) << id << " (step " << dec::stepName(s) << ") is in Java's registry but not in F5's table";
}
