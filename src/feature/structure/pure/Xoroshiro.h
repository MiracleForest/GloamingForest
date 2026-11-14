// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// Pure C++ (no mc/ll includes; the gtests use it too). Minecraft 1.21.1's Xoroshiro128++ random
// source bit for bit (XoroshiroRandomSource, RandomSupport, XoroshiroPositionalRandomFactory,
// MarsagliaPolarGaussian), the MD5 that RandomSupport#seedFromHashOf uses, and the random a
// WorldGenRegion hands its features and pieces (WorldGenRegion#getRandom):
//
//     RandomState(seed).getOrCreateRandomFactory("minecraft:worldgen_region_random")
//                      .at(centerChunk.getWorldPosition())
//
// where the upstream dimension's noise settings use the Xoroshiro source (useLegacyRandomSource false,
// TFDimensionData#makeNoiseSettings). tests/structure/XoroshiroTest.cpp checks every value against
// the output of tests/structure/oracle/XoroshiroOracle.java run on Minecraft 1.21.1 itself.
//
// F5's gf::core::jrand::Xoroshiro128pp (contract "worldgen") was a stub when this was written; it
// is implemented now, and A8 folds the kit onto it in W2b (both are checked against Minecraft's
// own values).

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "core/jrand/JRand.h"
#include "feature/structure/pure/JavaRandom.h"

namespace gf::structure::pure {

namespace md5 {

[[nodiscard]] inline std::uint32_t rotl32(std::uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }

/// RFC 1321 MD5 of the bytes of `text` (Guava's Hashing.md5().hashString(text, UTF_8) for UTF-8 text).
[[nodiscard]] inline std::array<std::uint8_t, 16> digest(std::string_view text) {
    static constexpr std::uint32_t kK[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static constexpr int kS[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                                   14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                                   4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
    std::vector<std::uint8_t> msg(text.begin(), text.end());
    std::uint64_t const       bitLength = static_cast<std::uint64_t>(msg.size()) * 8u;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 0; i < 8; i++) msg.push_back(static_cast<std::uint8_t>(bitLength >> (8 * i)));
    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; i++) {
            auto const* p = &msg[off + static_cast<std::size_t>(i) * 4];
            m[i] = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                   (static_cast<std::uint32_t>(p[3]) << 24);
        }
        std::uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; i++) {
            std::uint32_t f = 0;
            int           g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + kK[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + rotl32(f, kS[i]);
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    std::array<std::uint8_t, 16> out{};
    std::uint32_t const          words[4] = {a0, b0, c0, d0};
    for (int w = 0; w < 4; w++)
        for (int i = 0; i < 4; i++) out[static_cast<std::size_t>(w * 4 + i)] = static_cast<std::uint8_t>(words[w] >> (8 * i));
    return out;
}

} // namespace md5

/// RandomSupport.Seed128bit.
struct Seed128 {
    std::int64_t lo = 0, hi = 0;
};

inline constexpr std::uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ull; ///< RandomSupport.GOLDEN_RATIO_64
inline constexpr std::uint64_t kSilverRatio64 = 0x6A09E667F3BCC909ull; ///< RandomSupport.SILVER_RATIO_64

/// RandomSupport.mixStafford13.
[[nodiscard]] inline std::uint64_t mixStafford13(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// RandomSupport.upgradeSeedTo128bit (the unmixed pair, then mixed).
[[nodiscard]] inline Seed128 upgradeSeedTo128bit(std::int64_t seed) {
    std::uint64_t lo = static_cast<std::uint64_t>(seed) ^ kSilverRatio64;
    std::uint64_t hi = lo + kGoldenRatio64;
    return Seed128{static_cast<std::int64_t>(mixStafford13(lo)), static_cast<std::int64_t>(mixStafford13(hi))};
}

/// RandomSupport.seedFromHashOf: the MD5 of the UTF-8 text as two big-endian longs.
[[nodiscard]] inline Seed128 seedFromHashOf(std::string_view text) {
    auto          d  = md5::digest(text);
    std::uint64_t lo = 0, hi = 0;
    for (int i = 0; i < 8; i++) lo = (lo << 8) | d[static_cast<std::size_t>(i)];
    for (int i = 8; i < 16; i++) hi = (hi << 8) | d[static_cast<std::size_t>(i)];
    return Seed128{static_cast<std::int64_t>(lo), static_cast<std::int64_t>(hi)};
}

/// XoroshiroRandomSource (Xoroshiro128PlusPlus + MarsagliaPolarGaussian).
class Xoroshiro final : public core::jrand::RandomSource {
public:
    /// XoroshiroRandomSource(long seed): the seed upgraded to 128 bits.
    explicit Xoroshiro(std::int64_t seed) { setSeed(seed); }
    /// XoroshiroRandomSource(long seedLo, long seedHi) / (Seed128bit): taken as they are.
    Xoroshiro(std::int64_t seedLo, std::int64_t seedHi) { init(seedLo, seedHi); }

    void setSeed(std::int64_t seed) override {
        auto s = upgradeSeedTo128bit(seed);
        init(s.lo, s.hi);
    }
    /// Xoroshiro128PlusPlus#nextLong.
    [[nodiscard]] std::int64_t nextLong() override {
        std::uint64_t i = mLo, j = mHi;
        std::uint64_t k = rotl(i + j, 17) + i;
        j ^= i;
        mLo = rotl(i, 49) ^ j ^ (j << 21);
        mHi = rotl(j, 28);
        return static_cast<std::int64_t>(k);
    }
    [[nodiscard]] int nextInt() override { return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(nextLong()))); }
    [[nodiscard]] int nextInt(int bound) override {
        if (bound <= 0) return 0; // Java throws IllegalArgumentException; translated callers never pass it
        std::uint64_t i = static_cast<std::uint32_t>(nextInt());
        std::uint64_t j = i * static_cast<std::uint64_t>(bound);
        std::uint64_t k = j & 0xFFFFFFFFull;
        if (k < static_cast<std::uint64_t>(bound)) {
            // Integer.remainderUnsigned(~bound + 1, bound)
            std::uint32_t l = (~static_cast<std::uint32_t>(bound) + 1u) % static_cast<std::uint32_t>(bound);
            while (k < l) {
                i = static_cast<std::uint32_t>(nextInt());
                j = i * static_cast<std::uint64_t>(bound);
                k = j & 0xFFFFFFFFull;
            }
        }
        return static_cast<int>(static_cast<std::int64_t>(j) >> 32);
    }
    [[nodiscard]] bool   nextBoolean() override { return (static_cast<std::uint64_t>(nextLong()) & 1u) != 0; }
    [[nodiscard]] float  nextFloat() override { return static_cast<float>(nextBits(24)) * 5.9604645E-8F; }
    /// (double) nextBits(53) * (double) 1.110223E-16F (Java's DOUBLE_UNIT is a float literal).
    [[nodiscard]] double nextDouble() override { return static_cast<double>(nextBits(53)) * static_cast<double>(1.110223E-16F); }
    [[nodiscard]] double nextGaussian() override {
        if (mHaveGaussian) {
            mHaveGaussian = false;
            return mNextGaussian;
        }
        while (true) {
            double d0 = 2.0 * nextDouble() - 1.0;
            double d1 = 2.0 * nextDouble() - 1.0;
            double d2 = d0 * d0 + d1 * d1;
            if (!(d2 >= 1.0) && d2 != 0.0) {
                double d3     = std::sqrt(-2.0 * std::log(d2) / d2);
                mNextGaussian = d1 * d3;
                mHaveGaussian = true;
                return d0 * d3;
            }
        }
    }
    void consumeCount(int count) override {
        for (int i = 0; i < count; i++) (void)nextLong();
    }
    [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> fork() override {
        auto lo = nextLong();
        auto hi = nextLong();
        return std::make_unique<Xoroshiro>(lo, hi);
    }
    [[nodiscard]] std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override {
        auto lo = nextLong();
        auto hi = nextLong();
        return std::make_unique<Positional>(lo, hi);
    }

    /// XoroshiroRandomSource.XoroshiroPositionalRandomFactory.
    class Positional final : public core::jrand::PositionalRandomFactory {
    public:
        Positional(std::int64_t seedLo, std::int64_t seedHi) : mLo(seedLo), mHi(seedHi) {}
        [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> at(int x, int y, int z) override {
            return std::make_unique<Xoroshiro>(mthGetSeed(x, y, z) ^ mLo, mHi);
        }
        [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> fromHashOf(std::string_view name) override {
            auto s = seedFromHashOf(name);
            return std::make_unique<Xoroshiro>(s.lo ^ mLo, s.hi ^ mHi);
        }

    private:
        std::int64_t mLo, mHi;
    };

private:
    [[nodiscard]] static std::uint64_t rotl(std::uint64_t v, int s) { return (v << s) | (v >> (64 - s)); }
    [[nodiscard]] std::int64_t         nextBits(int bits) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(nextLong()) >> (64 - bits)); }
    void                               init(std::int64_t lo, std::int64_t hi) {
        mLo = static_cast<std::uint64_t>(lo);
        mHi = static_cast<std::uint64_t>(hi);
        // Xoroshiro128PlusPlus: an all-zero state is replaced
        if ((mLo | mHi) == 0) {
            mLo = kGoldenRatio64;
            mHi = kSilverRatio64;
        }
        mHaveGaussian = false;
    }

    std::uint64_t mLo = 0, mHi = 0;
    double        mNextGaussian = 0;
    bool          mHaveGaussian = false;
};

/// WorldGenRegion's random for the region centered on chunk (chunkX, chunkZ) of a level with
/// seed `levelSeed` (Xoroshiro noise settings): RandomState's positional random, forked by
/// "minecraft:worldgen_region_random", at the chunk's minimum block (y 0).
[[nodiscard]] inline std::unique_ptr<core::jrand::RandomSource> worldgenRegionRandom(std::int64_t levelSeed, int chunkX, int chunkZ) {
    Xoroshiro root(levelSeed);
    auto      positional = root.forkPositional();
    auto      named      = positional->fromHashOf("minecraft:worldgen_region_random");
    auto      factory    = named->forkPositional();
    return factory->at(chunkX * 16, 0, chunkZ * 16);
}

} // namespace gf::structure::pure
