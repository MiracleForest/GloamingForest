// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// The world's terrain preset (TF's twilight_noise_gen or skylight_noise_gen), chosen once per world
// like TF's world preset and kept in gf:terrain:skylight; and the chunk records of Bedrock's world
// DB that tell whether a dimension already holds generated chunks (a world whose Twilight Forest
// was generated before the preset was kept stays twilight_noise_gen). Pure: no game headers.

#include <optional>
#include <string_view>

namespace gf::core::terrain {

/// The dimension of a chunk's version record in Bedrock's LevelDB (key: int32 x, int32 z, [int32
/// dimension when not the overworld], the tag 44 "Version" or the legacy 118), 0 for the
/// overworld; nullopt for every other key (subchunks, block entities, string keys, ...). Every
/// saved chunk has exactly one version record.
[[nodiscard]] std::optional<int> chunkVersionKeyDimension(std::string_view key);

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
[[nodiscard]] PresetChoice choosePreset(std::optional<bool> stored, bool savedTwilightChunks, int provisional, bool configured);

} // namespace gf::core::terrain
