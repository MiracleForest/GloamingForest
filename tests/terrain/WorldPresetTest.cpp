// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: the world preset choice and the chunk records it reads (core/terrain/WorldPreset.h).
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/terrain/WorldPreset.h"

namespace gf::tests::terrain {
namespace {

namespace wp = core::terrain;

void putInt(std::string& s, std::int32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
}

std::string chunkKey(std::int32_t x, std::int32_t z, std::optional<std::int32_t> dim, unsigned char tag, std::optional<unsigned char> sub = std::nullopt) {
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
    for (char const* s : {"Overworld", "mobevents", "BiomeData", "~local_player", "scoreboard", "gf:terrain:skylight", "portals", ""})
        EXPECT_FALSE(wp::chunkVersionKeyDimension(s)) << s;
    // a 13-byte key whose dimension field is not a dimension id
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, 0, 44)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, -4, 44)));
    EXPECT_FALSE(wp::chunkVersionKeyDimension(chunkKey(0, 0, 0x01000000, 44)));
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

} // namespace
} // namespace gf::tests::terrain
