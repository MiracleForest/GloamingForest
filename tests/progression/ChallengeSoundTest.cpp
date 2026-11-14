// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U14: the challenge toast's sound (core::progression::kChallengeSound), GloamingForest's original
// stand-in for Mojang's ui.toast.challenge_complete: the resource pack defines it (category ui) on
// the committed .ogg, which is a valid mono Vorbis file as long as the recipe's render, and the
// recipe (tools/sounds/recipes/progression.json) renders to an audible sound with the synth core.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../golden/GoldenRepo.h"
#include "core/progression/Rules.h"
#include "core/synth/Json.h"
#include "core/synth/Patch.h"
#include "core/synth/Sequencer.h"
#include "core/synth/Vorbis.h"

namespace {

namespace fs = std::filesystem;
using gf::core::synth::Json;

std::string readText(fs::path const& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

TEST(ProgressionChallengeSound, PackDefinesTheOriginalSound) {
    auto const root = gf::tests::golden::repoDir();
    ASSERT_FALSE(root.empty()) << "repository not found (set GF_REPO_DIR)";
    auto const defs = Json::parse(readText(root / "assets/pack/rp/sounds.d/progression.json"));
    auto const* def = defs.find(gf::core::progression::kChallengeSound);
    ASSERT_NE(def, nullptr) << "sounds.d/progression.json defines " << gf::core::progression::kChallengeSound;
    EXPECT_EQ(def->str("category", ""), "ui");
    auto const& sounds = (*def)["sounds"];
    ASSERT_EQ(sounds.size(), 1u);
    auto const name = sounds.items().front().str("name", "");
    EXPECT_EQ(name, "sounds/progression/challenge_complete");

    auto const bytesText = readText(root / "assets/pack/rp" / (name + ".ogg"));
    std::vector<std::uint8_t> bytes(bytesText.begin(), bytesText.end());
    auto const info = gf::core::synth::probeOggVorbis(bytes);
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_EQ(info.channels, 1);
    EXPECT_EQ(info.sampleRate, 44100);

    // the recipe it was rendered from: same length, audible, over well before the toast's 5 s
    auto const recipe = Json::parse(readText(root / "tools/sounds/recipes/progression.json"));
    auto const& track = recipe["files"]["ui/challenge_complete"]["track"];
    ASSERT_TRUE(track.isObject());
    gf::core::synth::PatchLibrary lib;
    for (auto const& entry : fs::recursive_directory_iterator(root / "tools/sounds/patches")) {
        if (entry.path().extension() != ".json") continue;
        auto rel = fs::relative(entry.path(), root / "tools/sounds/patches").replace_extension().generic_string();
        lib.add(rel, Json::parse(readText(entry.path())));
    }
    auto const pcm = gf::core::synth::renderTrack(track, 44100, &lib);
    EXPECT_EQ(static_cast<std::int64_t>(pcm.size()), info.samples);
    EXPECT_LE(pcm.size(), std::size_t(3 * 44100));
    float peak = 0;
    for (float x : pcm) peak = std::max(peak, std::fabs(x));
    EXPECT_GT(peak, 0.2f);
    EXPECT_LE(peak, 1.0f);
}
