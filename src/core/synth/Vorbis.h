// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): an Ogg Vorbis I encoder for the synthesized sounds.
#pragma once

// A compact, deterministic Vorbis I encoder written from the specification
// (https://xiph.org/vorbis/doc/Vorbis_I_spec.html): mono, long blocks only (2048 samples),
// one floor-1 with 32 fixed posts, one residue-1 with seven partition classes and per-file
// Huffman codebooks built from a first analysis pass. The quality setting trades size for noise
// (0.0 ~ 32 kbit/s, 1.0 ~ 128 kbit/s for dense material). Output decodes with libvorbis, FMOD,
// ffmpeg and stb_vorbis; samples beyond the input length are trimmed through the last granule
// position, so the decoded stream has exactly `pcm.size()` samples.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gf::core::synth {

struct VorbisOptions {
    int                      sampleRate = 44100;
    double                   quality    = 0.5; ///< 0..1
    std::uint32_t            serial     = 0x47460001;
    std::string              vendor     = "GloamingForest synth";
    std::vector<std::string> comments; ///< "KEY=value" user comments
};

/// Encodes mono PCM (nominally -1..1) into a complete .ogg file.
[[nodiscard]] std::vector<std::uint8_t> encodeVorbis(std::span<float const> pcm, VorbisOptions const& options);

/// The Ogg page CRC (polynomial 0x04c11db7, MSB first, no reflection).
[[nodiscard]] std::uint32_t oggCrc(std::uint8_t const* data, std::size_t size);

/// Basic facts from an .ogg file's first pages (validation in tests and selftests).
struct OggVorbisInfo {
    bool         valid      = false;
    int          channels   = 0;
    int          sampleRate = 0;
    std::int64_t samples    = 0; ///< granule position of the last page
    std::string  error;
};
[[nodiscard]] OggVorbisInfo probeOggVorbis(std::span<std::uint8_t const> file);

/// The same facts from the two ends of an .ogg file only: its first page (the Vorbis identification
/// header: channels, sample rate) and its last page (whose granule position is the length in
/// samples), so the length of a long file costs two small reads instead of the whole file. Both
/// pages must pass their CRC and the last one must end the stream at the end of the file; the pages
/// in between are not read (probeOggVorbis validates a whole file).
[[nodiscard]] OggVorbisInfo probeOggVorbisEnds(std::filesystem::path const& file);

} // namespace gf::core::synth
