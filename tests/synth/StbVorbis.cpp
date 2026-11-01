// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// The reference decoder of the synth tests: stb_vorbis v1.22 by Sean Barrett (public domain / MIT,
// tests/synth/third_party/stb_vorbis.c, unmodified), compiled as C++ into gf-tests only.

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "StbVorbis.h"

namespace gf::tests::synth {

std::vector<float> decodeVorbis(std::vector<std::uint8_t> const& file, int* channels, int* rate) {
    int   err = 0;
    auto* v   = stb_vorbis_open_memory(file.data(), int(file.size()), &err, nullptr);
    if (!v) return {};
    stb_vorbis_info info = stb_vorbis_get_info(v);
    if (channels) *channels = info.channels;
    if (rate) *rate = int(info.sample_rate);
    std::vector<float> out;
    float              buf[4096];
    while (true) {
        int n = stb_vorbis_get_samples_float_interleaved(v, 1, buf, 4096);
        if (n <= 0) break;
        out.insert(out.end(), buf, buf + n);
    }
    stb_vorbis_close(v);
    return out;
}

} // namespace gf::tests::synth
