// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): 16-bit PCM WAV files (previews of synthesized sounds).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace gf::core::synth {

/// A mono 16-bit PCM RIFF/WAVE file of `pcm` (clipped to -1..1, rounded).
[[nodiscard]] std::vector<std::uint8_t> encodeWav(std::span<float const> pcm, int sampleRate);

/// Reads a mono or stereo 16-bit PCM WAV (first channel); empty on failure.
[[nodiscard]] std::vector<float> decodeWav(std::span<std::uint8_t const> file, int* sampleRate = nullptr);

} // namespace gf::core::synth
