// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): 16-bit PCM WAV files (previews of synthesized sounds).
#include "core/synth/Wav.h"

#include <cstring>

#include "core/synth/SynthMath.h"

namespace gf::core::synth {

std::vector<std::uint8_t> encodeWav(std::span<float const> pcm, int sampleRate) {
    std::vector<std::uint8_t> out;
    auto                      put = [&out](std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
    };
    auto                tag       = [&out](char const* s) { out.insert(out.end(), s, s + 4); };
    std::uint32_t const dataBytes = std::uint32_t(pcm.size() * 2);
    tag("RIFF");
    put(36 + dataBytes, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(1, 2); // mono
    put(std::uint32_t(sampleRate), 4);
    put(std::uint32_t(sampleRate) * 2, 4);
    put(2, 2);
    put(16, 2);
    tag("data");
    put(dataBytes, 4);
    for (float s : pcm) {
        double v = math::clamp(double(s), -1.0, 1.0) * 32767.0;
        auto   i = std::int16_t(math::round(v));
        put(std::uint16_t(i), 2);
    }
    return out;
}

std::vector<float> decodeWav(std::span<std::uint8_t const> f, int* sampleRate) {
    auto u32 = [&f](std::size_t p) {
        return std::uint32_t(f[p]) | std::uint32_t(f[p + 1]) << 8 | std::uint32_t(f[p + 2]) << 16
             | std::uint32_t(f[p + 3]) << 24;
    };
    auto u16 = [&f](std::size_t p) { return std::uint16_t(f[p] | f[p + 1] << 8); };
    if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0) return {};
    std::size_t pos      = 12;
    int         channels = 0;
    int         bits     = 0;
    while (pos + 8 <= f.size()) {
        std::uint32_t size = u32(pos + 4);
        if (std::memcmp(f.data() + pos, "fmt ", 4) == 0 && pos + 24 <= f.size()) {
            channels = u16(pos + 10);
            if (sampleRate) *sampleRate = int(u32(pos + 12));
            bits = u16(pos + 22);
        } else if (std::memcmp(f.data() + pos, "data", 4) == 0) {
            if (bits != 16 || channels < 1) return {};
            std::size_t        frames = std::min<std::size_t>(size, f.size() - pos - 8) / (2 * std::size_t(channels));
            std::vector<float> out(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                out[i] = float(std::int16_t(u16(pos + 8 + i * 2 * std::size_t(channels)))) / 32768.0f;
            }
            return out;
        }
        pos += 8 + size + (size & 1);
    }
    return {};
}

} // namespace gf::core::synth
