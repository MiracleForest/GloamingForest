// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): deterministic random numbers for the sound synthesizer.
#pragma once

#include <cstdint>
#include <string_view>

namespace gf::core::synth {

/// SplitMix64 step (seeding and hashing).
[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z               = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// FNV-1a of a string, mixed: stable seeds from names ("mob/naga/hiss1").
[[nodiscard]] constexpr std::uint64_t hashString(std::string_view s, std::uint64_t seed = 0) {
    std::uint64_t h = 0xCBF29CE484222325ull ^ seed;
    for (char c : s) {
        h ^= std::uint8_t(c);
        h *= 0x100000001B3ull;
    }
    std::uint64_t st = h;
    return splitMix64(st);
}

/// xoshiro256** generator: the same sequence on every platform.
class Random {
public:
    explicit Random(std::uint64_t seed = 0) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        std::uint64_t st = seed;
        for (auto& s : mS) s = splitMix64(st);
    }

    std::uint64_t next() {
        std::uint64_t const result  = rotl(mS[1] * 5, 7) * 9;
        std::uint64_t const t       = mS[1] << 17;
        mS[2]                      ^= mS[0];
        mS[3]                      ^= mS[1];
        mS[1]                      ^= mS[2];
        mS[0]                      ^= mS[3];
        mS[2]                      ^= t;
        mS[3]                       = rotl(mS[3], 45);
        return result;
    }

    /// Uniform in [0, 1).
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    /// Uniform in [lo, hi).
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    /// Uniform in [-1, 1).
    double bipolar() { return uniform() * 2.0 - 1.0; }
    /// Integer in [0, n).
    int below(int n) { return n <= 1 ? 0 : int(next() % std::uint64_t(n)); }
    /// Approximately normal (sum of 4 uniforms, variance 1).
    double gaussian() {
        double s = uniform() + uniform() + uniform() + uniform() - 2.0;
        return s * 1.7320508075688772; // sqrt(3)
    }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::uint64_t                  mS[4]{};
};

} // namespace gf::core::synth
