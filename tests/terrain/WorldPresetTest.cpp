// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: the world preset choice and the chunk records it reads (core/terrain/WorldPreset.h).
#include <gtest/gtest.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#include <string>

#include "core/terrain/WorldPreset.h"

namespace gf::tests::terrain {
namespace {

namespace wp = core::terrain;

void putInt(std::string& s, std::int32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
}

std::string chunkKey(
    std::int32_t                 x,
    std::int32_t                 z,
    std::optional<std::int32_t>  dim,
    unsigned char                tag,
    std::optional<unsigned char> sub = std::nullopt
) {
    std::string k;
    putInt(k, x);
    putInt(k, z);
    if (dim) putInt(k, *dim);
    k.push_back(static_cast<char>(tag));
    if (sub) k.push_back(static_cast<char>(*sub));
    return k;
}

TEST(TerrainWorldPreset, VersionRecordsGiveTheirDimension) {
    EXPECT_EQ(wp::chunkVersionKeyDimension(chunkKey(3, -7, std::nullopt, 44)), 0);
    EXPECT_EQ(wp::chunkVersionKeyDimension(chunkKey(-1, 0, std::nullopt, 118)), 0);
    EXPECT_EQ(wp::chunkVersionKeyDimension(chunkKey(125000, -99999, 3, 44)), 3);
    EXPECT_EQ(wp::chunkVersionKeyDimension(chunkKey(0, 0, 1, 118)), 1);
    EXPECT_EQ(wp::chunkVersionKeyDimension(chunkKey(-2, 5, 7, 44)), 7);
}

TEST(TerrainWorldPreset, OtherRecordsAreNoChunks) {
    // the chunk's other records: subchunks (47 + index), data 3D (43), block entities (49), finalized state (54)
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(1, 1, 3, 47, 0)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(1, 1, std::nullopt, 47, 2)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(1, 1, 3, 43)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(1, 1, 3, 49)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(1, 1, std::nullopt, 54)));
    // the world's string keys of the same lengths
    for (char const* s :
         {"Overworld", "mobevents", "BiomeData", "~local_player", "scoreboard", "gf:terrain:skylight", "portals", ""})
        EXPECT_FALSE(wp::chunkVersionKeyDimension(s)) << s;
    // a 13-byte key whose dimension field is not a dimension id
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, 0, 44)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, -4, 44)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, 0x01000000, 44)));
}

TEST(TerrainWorldPreset, VersionKeysAreTheRecordsTheScanAccepts) {
    for (int dim : {0, 1, 2, 3, 7}) {
        auto keys   = wp::chunkVersionKeys(-37, 125000, dim);
        auto dimArg = dim == 0 ? std::optional<std::int32_t>{} : std::optional<std::int32_t>{dim};
        EXPECT_EQ(keys[0], chunkKey(-37, 125000, dimArg, 44));
        EXPECT_EQ(keys[1], chunkKey(-37, 125000, dimArg, 118));
        for (auto const& k : keys) EXPECT_EQ(wp::chunkVersionKeyDimension(k), dim);
    }
}

TEST(TerrainWorldPreset, SavedChunksAreReadOnlyWhenTheyChangeTheAnswer) {
    for (int provisional : {-1, 0, 1})
        for (bool configured : {false, true}) {
            bool matters = wp::savedChunksMatter(std::nullopt, provisional, configured);
            // they matter exactly when the answer with and without saved chunks differs
            bool differs = wp::choosePreset(std::nullopt, true, provisional, configured).skylight
                        != wp::choosePreset(std::nullopt, false, provisional, configured).skylight;
            EXPECT_EQ(matters, differs) << provisional << " " << configured;
            EXPECT_FALSE(wp::savedChunksMatter(true, provisional, configured));
            EXPECT_FALSE(wp::savedChunksMatter(false, provisional, configured));
        }
}

TEST(TerrainWorldPreset, StoredChoiceWins) {
    for (bool saved : {false, true})
        for (int provisional : {-1, 0, 1})
            for (bool configured : {false, true}) {
                auto a = wp::choosePreset(true, saved, provisional, configured);
                EXPECT_TRUE(a.skylight);
                EXPECT_EQ(a.source, wp::PresetSource::Stored);
                auto b = wp::choosePreset(false, saved, provisional, configured);
                EXPECT_FALSE(b.skylight);
                EXPECT_EQ(b.source, wp::PresetSource::Stored);
            }
}

TEST(TerrainWorldPreset, AGeneratedTwilightForestStaysTwilight) {
    for (int provisional : {-1, 0, 1}) {
        auto c = wp::choosePreset(std::nullopt, true, provisional, true);
        EXPECT_FALSE(c.skylight);
        EXPECT_EQ(c.source, wp::PresetSource::SavedChunks);
    }
}

TEST(TerrainWorldPreset, ANewWorldTakesTheProvisionalThenTheConfig) {
    auto p = wp::choosePreset(std::nullopt, false, 1, false);
    EXPECT_TRUE(p.skylight);
    EXPECT_EQ(p.source, wp::PresetSource::Provisional);
    p = wp::choosePreset(std::nullopt, false, 0, true);
    EXPECT_FALSE(p.skylight);
    EXPECT_EQ(p.source, wp::PresetSource::Provisional);
    auto c = wp::choosePreset(std::nullopt, false, -1, true);
    EXPECT_TRUE(c.skylight);
    EXPECT_EQ(c.source, wp::PresetSource::Config);
    c = wp::choosePreset(std::nullopt, false, -1, false);
    EXPECT_FALSE(c.skylight);
    EXPECT_EQ(c.source, wp::PresetSource::Config);
}

TEST(TerrainWorldPreset, RingProbeVisitsEachPositionOnceNearestFirst) {
    std::array<std::array<int, 2>, 3> centres{{{0, 0}, {0, 0}, {1, 0}}};
    std::vector<std::pair<int, int>>  order;
    auto r = wp::probeRings(centres, 4, [&](int x, int z) {
        order.emplace_back(x, z);
        return false;
    });
    EXPECT_FALSE(r.found);
    std::set<std::pair<int, int>> distinct(order.begin(), order.end());
    EXPECT_EQ(distinct.size(), order.size()); // no position twice
    // the union of two 9 x 9 squares one apart: 9 x 10
    EXPECT_EQ(r.probed, 90u);
    EXPECT_EQ(order.size(), 90u);
    // nearest first: the Chebyshev distance to the nearer centre never decreases by more than a ring
    int last = 0;
    for (auto const& [x, z] : order) {
        int d = std::min(std::max(std::abs(x), std::abs(z)), std::max(std::abs(x - 1), std::abs(z)));
        EXPECT_LE(d, last + 1);
        last = std::max(last, d);
    }
}

TEST(TerrainWorldPreset, RingProbeStopsAtTheFirstSavedChunk) {
    std::array<std::array<int, 2>, 1> centre{{{10, -10}}};
    auto r = wp::probeRings(centre, 16, [](int x, int z) { return x == 13 && z == -8; });
    EXPECT_TRUE(r.found);
    EXPECT_LE(r.probed, 7u * 7u); // within ring 3
    auto none = wp::probeRings(centre, 16, [](int, int) { return false; });
    EXPECT_FALSE(none.found);
    EXPECT_EQ(none.probed, 33u * 33u);
}

} // namespace
} // namespace gf::tests::terrain
