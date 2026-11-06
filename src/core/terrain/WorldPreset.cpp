// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#include "core/terrain/WorldPreset.h"

#include <cstdint>
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace gf::core::terrain {

namespace {

constexpr unsigned char kVersionTag       = 44;  // LevelChunkTag::Version
constexpr unsigned char kLegacyVersionTag = 118; // LevelChunkTag::LegacyVersion

std::int32_t readInt32LE(std::string_view key, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(key[at + static_cast<std::size_t>(i)]);
    return static_cast<std::int32_t>(v);
}

void putInt32LE(std::string& s, std::int32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
}

} // namespace

std::array<std::string, 2> chunkVersionKeys(int x, int z, int dim) {
    std::array<std::string, 2> keys;
    unsigned char const        tags[2] = {kVersionTag, kLegacyVersionTag};
    for (int i = 0; i < 2; ++i) {
        auto& k = keys[static_cast<std::size_t>(i)];
        putInt32LE(k, x);
        putInt32LE(k, z);
        if (dim != 0) putInt32LE(k, dim);
        k.push_back(static_cast<char>(tags[i]));
    }
    return keys;
}

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

RingProbe probeRings(std::span<std::array<int, 2> const> centres, int radius, std::function<bool(int, int)> const& saved) {
    RingProbe                        r;
    std::unordered_set<std::int64_t> seen;
    auto                             visit = [&](int x, int z) {
        auto key = (static_cast<std::int64_t>(x) << 32) | static_cast<std::uint32_t>(z);
        if (!seen.insert(key).second) return false;
        ++r.probed;
        return saved(x, z);
    };
    // the same centre twice probes nothing new: skip repeats up front
    std::vector<std::array<int, 2>> unique;
    for (auto const& c : centres)
        if (std::find(unique.begin(), unique.end(), c) == unique.end()) unique.push_back(c);
    for (int ring = 0; ring <= radius; ++ring)
        for (auto const& [cx, cz] : unique) {
            if (ring == 0) {
                r.found = visit(cx, cz);
                if (r.found) return r;
                continue;
            }
            // the perimeter: the two rows z = cz -+ ring, then the two columns x = cx -+ ring between them
            for (int dx = -ring; dx <= ring; ++dx)
                if ((r.found = visit(cx + dx, cz - ring) || visit(cx + dx, cz + ring))) return r;
            for (int dz = -ring + 1; dz <= ring - 1; ++dz)
                if ((r.found = visit(cx - ring, cz + dz) || visit(cx + ring, cz + dz))) return r;
        }
    return r;
}

PresetChoice choosePreset(std::optional<bool> stored, bool savedTwilightChunks, int provisional, bool configured) {
    if (stored) return {*stored, PresetSource::Stored};
    if (savedTwilightChunks) return {false, PresetSource::SavedChunks};
    if (provisional >= 0) return {provisional == 1, PresetSource::Provisional};
    return {configured, PresetSource::Config};
}

bool savedChunksMatter(std::optional<bool> stored, int provisional, bool configured) {
    if (stored) return false;
    return provisional >= 0 ? provisional == 1 : configured;
}

} // namespace gf::core::terrain
