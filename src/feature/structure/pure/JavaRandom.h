// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Pure C++ (no mc/ll includes; the gtests use it too). java.util.Random / Minecraft's
// LegacyRandomSource bit for bit, plus the Mth helpers the structure kit needs (Mth.getSeed,
// positiveModulo, floor, ceil, sin / cos tables are not needed: TF's structures use Math.cos /
// Mth.cos only for float geometry).
//
// The structure kit creates its own position-seeded sources exactly as vanilla does
// (StructurePlaceSettings#getRandom -> RandomSource.create(Mth.getSeed(pos)) -> LegacyRandomSource),
// so template processors and golden volume hashes do not depend on which RandomSource the caller
// passes in.

#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>

#include "core/jrand/JRand.h"

namespace gf::structure::pure {

/// net.minecraft.util.Mth#getSeed(x, y, z) (Java int multiplication of x wraps).
[[nodiscard]] inline std::int64_t mthGetSeed(int x, int y, int z) {
    auto         xi = static_cast<std::int32_t>(static_cast<std::uint32_t>(x) * 3129871u);
    std::int64_t i  = static_cast<std::int64_t>(xi) ^ static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) * 116129781ull) ^
                     static_cast<std::int64_t>(y);
    auto u = static_cast<std::uint64_t>(i);
    u      = u * u * 42317861ull + u * 11ull;
    return static_cast<std::int64_t>(u) >> 16;
}

/// Java's String#hashCode over ASCII / UTF-8 bytes (ids are ASCII).
[[nodiscard]] inline std::int32_t javaHash(std::string_view s) {
    std::uint32_t h = 0;
    for (unsigned char c : s) h = h * 31u + c;
    return static_cast<std::int32_t>(h);
}

/// BlockPos#asLong.
[[nodiscard]] inline std::int64_t blockPosAsLong(int x, int y, int z) {
    constexpr int           kBitsXZ = 26, kBitsY = 12;
    constexpr std::uint64_t kMaskXZ = (1ull << kBitsXZ) - 1, kMaskY = (1ull << kBitsY) - 1;
    std::uint64_t           l = 0;
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(x)) & kMaskXZ) << (kBitsY + kBitsXZ);
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(y)) & kMaskY);
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) & kMaskXZ) << kBitsY;
    return static_cast<std::int64_t>(l);
}

/// Mth.floor / Mth.ceil for floats and doubles.
[[nodiscard]] inline int mthFloor(double v) {
    int i = static_cast<int>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}
[[nodiscard]] inline int mthCeil(double v) {
    int i = static_cast<int>(v);
    return v > static_cast<double>(i) ? i + 1 : i;
}
/// Mth.positiveModulo(float, float).
[[nodiscard]] inline float positiveModulo(float a, float b) { return std::fmod(std::fmod(a, b) + b, b); }
[[nodiscard]] inline int   positiveModulo(int a, int b) { return ((a % b) + b) % b; }
/// Mth.floor(float) / Mth.ceil(float) on the float value itself.
[[nodiscard]] inline int mthFloorF(float v) { return mthFloor(static_cast<double>(v)); }
[[nodiscard]] inline int mthCeilF(float v) { return mthCeil(static_cast<double>(v)); }

/// Mth's sine table (SIN[i] = (float) Math.sin(i * PI * 2 / 65536)).
[[nodiscard]] inline float const* mthSinTable() {
    static float const* table = [] {
        static float t[65536];
        for (int i = 0; i < 65536; i++) t[i] = static_cast<float>(std::sin(static_cast<double>(i) * 3.141592653589793 * 2.0 / 65536.0));
        return t;
    }();
    return table;
}
/// Mth.sin / Mth.cos (1.21.1: float argument, table lookup).
[[nodiscard]] inline float mthSin(float v) { return mthSinTable()[static_cast<int>(v * 10430.378F) & 65535]; }
[[nodiscard]] inline float mthCos(float v) { return mthSinTable()[static_cast<int>(v * 10430.378F + 16384.0F) & 65535]; }
inline constexpr float kMthPi    = 3.1415927F;
inline constexpr float kMthTwoPi = 6.2831855F;
/// Java's Math.round(float).
[[nodiscard]] inline int javaRound(float v) { return static_cast<int>(std::floor(static_cast<double>(v) + 0.5)); }

/// java.util.Random / LegacyRandomSource.
class JavaRandom final : public core::jrand::RandomSource {
public:
    explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }

    void setSeed(std::int64_t seed) override {
        mSeed         = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kMask;
        mHaveGaussian = false;
    }
    [[nodiscard]] int next(int bits) {
        mSeed = (mSeed * kMultiplier + kIncrement) & kMask;
        return static_cast<int>(static_cast<std::int64_t>(mSeed) >> (48 - bits));
    }
    [[nodiscard]] int nextInt() override { return next(32); }
    [[nodiscard]] int nextInt(int bound) override {
        if (bound <= 0) return 0; // Java throws IllegalArgumentException; callers translate TF code that never passes it
        if ((bound & (bound - 1)) == 0) return static_cast<int>((static_cast<std::int64_t>(bound) * static_cast<std::int64_t>(next(31))) >> 31);
        int bits = 0, val = 0;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (static_cast<std::int32_t>(static_cast<std::uint32_t>(bits) - static_cast<std::uint32_t>(val) + static_cast<std::uint32_t>(bound - 1)) < 0);
        return val;
    }
    [[nodiscard]] std::int64_t nextLong() override {
        auto hi = static_cast<std::int64_t>(next(32));
        auto lo = static_cast<std::int64_t>(next(32));
        return static_cast<std::int64_t>((static_cast<std::uint64_t>(hi) << 32) + static_cast<std::uint64_t>(lo));
    }
    [[nodiscard]] bool   nextBoolean() override { return next(1) != 0; }
    [[nodiscard]] float  nextFloat() override { return static_cast<float>(next(24)) * 5.9604645E-8F; }
    [[nodiscard]] double nextDouble() override {
        auto a = static_cast<std::int64_t>(next(26));
        auto b = static_cast<std::int64_t>(next(27));
        return static_cast<double>((a << 27) + b) * 1.1102230246251565E-16;
    }
    [[nodiscard]] double nextGaussian() override {
        if (mHaveGaussian) {
            mHaveGaussian = false;
            return mNextGaussian;
        }
        double v1 = 0, v2 = 0, s = 0;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            s  = v1 * v1 + v2 * v2;
        } while (s >= 1 || s == 0);
        double m      = std::sqrt(-2 * std::log(s) / s);
        mNextGaussian = v2 * m;
        mHaveGaussian = true;
        return v1 * m;
    }
    [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> fork() override { return std::make_unique<JavaRandom>(nextLong()); }
    [[nodiscard]] std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override {
        return std::make_unique<Positional>(nextLong());
    }

    /// LegacyRandomSource.LegacyPositionalRandomFactory.
    class Positional final : public core::jrand::PositionalRandomFactory {
    public:
        explicit Positional(std::int64_t seed) : mBase(seed) {}
        [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> at(int x, int y, int z) override {
            return std::make_unique<JavaRandom>(mthGetSeed(x, y, z) ^ mBase);
        }
        [[nodiscard]] std::unique_ptr<core::jrand::RandomSource> fromHashOf(std::string_view name) override {
            return std::make_unique<JavaRandom>(static_cast<std::int64_t>(javaHash(name)) ^ mBase);
        }

    private:
        std::int64_t mBase;
    };

private:
    static constexpr std::uint64_t kMultiplier = 0x5DEECE66Dull;
    static constexpr std::uint64_t kIncrement  = 0xBull;
    static constexpr std::uint64_t kMask       = (1ull << 48) - 1;

    std::uint64_t mSeed         = 0;
    double        mNextGaussian = 0;
    bool          mHaveGaussian = false;
};

/// Mth.randomBetweenInclusive(random, min, max): random.nextInt(max - min + 1) + min.
[[nodiscard]] inline int randomBetweenInclusive(core::jrand::RandomSource& random, int min, int max) {
    return random.nextInt(max - min + 1) + min;
}

/// Mth.nextInt(random, min, max): min >= max ? min : random.nextInt(max - min + 1) + min.
[[nodiscard]] inline int mthNextInt(core::jrand::RandomSource& random, int min, int max) {
    return min >= max ? min : random.nextInt(max - min + 1) + min;
}

} // namespace gf::structure::pure
