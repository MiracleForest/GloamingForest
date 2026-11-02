#pragma once

// java.util.Random as Minecraft's LegacyRandomSource (RandomSource.create(seed)), the random of a
// seeded LootContext. The loot interpreter keeps its own copy so it does not depend on F5's
// jrand implementation (A8 may fold it onto gf::core::jrand::LegacyRandom).

#include <cmath>
#include <cstdint>
#include <memory>

#include "core/jrand/JRand.h"

namespace gf::loot {

class JavaRandom final : public core::jrand::RandomSource {
public:
    explicit JavaRandom(std::int64_t seed) { setSeed(seed); }

    void setSeed(std::int64_t seed) override {
        mSeed       = (static_cast<std::uint64_t>(seed) ^ 0x5DEECE66DULL) & kMask;
        mHaveGauss  = false;
    }
    int next(int bits) {
        mSeed = (mSeed * 0x5DEECE66DULL + 0xBULL) & kMask;
        return static_cast<int>(static_cast<std::int64_t>(mSeed) >> (48 - bits));
    }
    int nextInt() override { return next(32); }
    int nextInt(int bound) override {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
        // Java: `bits - val + (bound - 1) < 0` with int wraparound; computed in 32-bit unsigned
        // arithmetic (no signed overflow): the sign bit of the wrapped sum
        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (((static_cast<std::uint32_t>(bits) - static_cast<std::uint32_t>(val) + static_cast<std::uint32_t>(bound - 1)) & 0x80000000u) != 0);
        return val;
    }
    std::int64_t nextLong() override {
        // ((long) next(32) << 32) + next(32) with Java's wraparound (unsigned arithmetic)
        auto hi = static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32))) << 32;
        auto lo = static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32)));
        return static_cast<std::int64_t>(hi + lo);
    }
    bool         nextBoolean() override { return next(1) != 0; }
    float        nextFloat() override { return static_cast<float>(next(24)) * 5.9604645E-8F; }
    double       nextDouble() override {
        return static_cast<double>((static_cast<std::int64_t>(next(26)) << 27) + next(27)) * 1.1102230246251565E-16;
    }
    double nextGaussian() override {
        if (mHaveGauss) {
            mHaveGauss = false;
            return mNextGauss;
        }
        double v1, v2, s;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            s  = v1 * v1 + v2 * v2;
        } while (s >= 1 || s == 0);
        double m   = std::sqrt(-2 * std::log(s) / s);
        mNextGauss = v2 * m;
        mHaveGauss = true;
        return v1 * m;
    }
    std::unique_ptr<core::jrand::RandomSource>            fork() override { return std::make_unique<JavaRandom>(nextLong()); }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }

private:
    static constexpr std::uint64_t kMask = (1ULL << 48) - 1;
    std::uint64_t                  mSeed = 0;
    double                         mNextGauss = 0;
    bool                           mHaveGauss = false;
};

/// Mth.nextInt(random, min, max): min when min >= max.
inline int nextIntBetween(core::jrand::RandomSource& r, int min, int max) { return min >= max ? min : r.nextInt(max - min + 1) + min; }
/// Mth.nextFloat(random, min, max).
inline float nextFloatBetween(core::jrand::RandomSource& r, float min, float max) { return min >= max ? min : r.nextFloat() * (max - min) + min; }

} // namespace gf::loot
