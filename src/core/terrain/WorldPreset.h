// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// The world's terrain preset (TF's twilight_noise_gen or skylight_noise_gen), chosen once per world
// like TF's world preset and kept in gf:terrain:skylight; and the chunk records of Bedrock's world
// DB that tell whether a dimension already holds generated chunks (a world whose Twilight Forest
// was generated before the preset was kept stays twilight_noise_gen). Pure: no game headers.

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace gf::core::terrain {

/// The dimension of a chunk's version record in Bedrock's LevelDB (key: int32 x, int32 z, [int32
/// dimension when not the overworld], the tag 44 "Version" or the legacy 118), 0 for the
/// overworld; nullopt for every other key (subchunks, block entities, string keys, ...). Every
/// saved chunk has exactly one version record.
[[nodiscard]] std::optional<int> chunkVersionKeyDimension(std::string_view key);

/// The two version-record keys a saved chunk of dimension `dim` (0: the overworld) at chunk (x, z)
/// can have (tag 44 "Version", then the legacy 118): the keys chunkVersionKeyDimension accepts.
[[nodiscard]] std::array<std::string, 2> chunkVersionKeys(int x, int z, int dim);

struct RingProbe {
    bool        found  = false; ///< `saved` answered true for a position
    std::size_t probed = 0;     ///< distinct positions `saved` was asked about
};
/// Asks `saved` about the chunk positions within `radius` (Chebyshev) of the `centres`, ring by ring
/// (ring 0 of every centre, then ring 1 of every centre, ...), walking each ring's perimeter only and
/// each position once even where the centres' squares overlap; stops at the first true.
[[nodiscard]] RingProbe probeRings(
    std::span<std::array<int, 2> const> centres,
    int                                 radius,
    std::function<bool(int x, int z)> const& saved
);

enum class PresetSource {
    Stored,      ///< gf:terrain:skylight
    SavedChunks, ///< the Twilight Forest already holds saved chunks: twilight_noise_gen
    Provisional, ///< chunks generated in this session before the world was ready used it
    Config,      ///< a new world: the config's choice
};

struct PresetChoice {
    bool         skylight = false;
    PresetSource source   = PresetSource::Config;
};

/// The preset of a world: the stored one wins; else a world whose Twilight Forest has saved chunks
/// stays twilight; else what chunks generated before the world was ready used (`provisional`: -1
/// none, 0 twilight, 1 skylight); else the config's.
[[nodiscard]] PresetChoice
choosePreset(std::optional<bool> stored, bool savedTwilightChunks, int provisional, bool configured);

/// Whether the saved chunks can change choosePreset's answer: only without a stored choice and when
/// the alternative is skylight (the config or the provisional chunks); a twilight answer is the
/// same with or without saved chunks, so the world DB is not read for it.
[[nodiscard]] bool savedChunksMatter(std::optional<bool> stored, int provisional, bool configured);

} // namespace gf::core::terrain
