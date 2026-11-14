// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

#include <cstdint>
#include <vector>

namespace gf::tests::synth {

/// Decodes an .ogg file with stb_vorbis (first channel); empty when stb rejects the stream.
std::vector<float> decodeVorbis(std::vector<std::uint8_t> const& file, int* channels = nullptr, int* rate = nullptr);

} // namespace gf::tests::synth
