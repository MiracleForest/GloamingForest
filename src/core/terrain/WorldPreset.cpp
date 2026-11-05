// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#include "core/terrain/WorldPreset.h"

#include <cstdint>

namespace gf::core::terrain {

namespace {

constexpr unsigned char kVersionTag       = 44;  // LevelChunkTag::Version
constexpr unsigned char kLegacyVersionTag = 118; // LevelChunkTag::LegacyVersion

std::int32_t readInt32LE(std::string_view key, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(key[at + static_cast<std::size_t>(i)]);
    return static_cast<std::int32_t>(v);
}

} // namespace

std::optional<int> chunkVersionKeyDimension(std::string_view key) {
    auto isVersion = [](char c) {
        auto t = static_cast<unsigned char>(c);
        return t == kVersionTag || t == kLegacyVersionTag;
    };
    if (key.size() == 9 && isVersion(key[8])) return 0;
    if (key.size() == 13 && isVersion(key[12])) {
        int dim = readInt32LE(key, 8);
        // the overworld never stores its id; Bedrock's dimension ids are small
        if (dim <= 0 || dim > 0xFFFF) return std::nullopt;
        return dim;
    }
    return std::nullopt;
}

PresetChoice choosePreset(std::optional<bool> stored, bool savedTwilightChunks, int provisional, bool configured) {
    if (stored) return {*stored, PresetSource::Stored};
    if (savedTwilightChunks) return {false, PresetSource::SavedChunks};
    if (provisional >= 0) return {provisional == 1, PresetSource::Provisional};
    return {configured, PresetSource::Config};
}

} // namespace gf::core::terrain
