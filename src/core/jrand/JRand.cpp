// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java-parity random sources: bit-exact equivalents of java.util.Random and of Minecraft 1.21.1's
// LegacyRandomSource, XoroshiroRandomSource (Xoroshiro128PlusPlus, RandomSupport), WorldgenRandom,
// MarsagliaPolarGaussian, the positional random factories and Mth#getSeed, which The Gloaming
// Forest 4.8 relies on for its world generation. Written from the published algorithms; checked
// against Java values in tests/worldgen.
#include "core/jrand/JRand.h"

#include <array>
#include <cstring>
#include <string>

#include "core/jrand/Mth.h"
#include "core/jrand/RandomSupport.h"

#pragma clang fp contract(off)

namespace gf::core::jrand {

using mth::ladd;
using mth::lmul;
using mth::lushr;

namespace {

constexpr std::int64_t kMultiplier = 0x5DEECE66DLL;
constexpr std::int64_t kIncrement  = 0xBLL;
constexpr std::int64_t kMask48     = (1LL << 48) - 1;

constexpr float  kFloatUnit  = 5.9604645E-8F;               // 2^-24
constexpr double kDoubleUnit = static_cast<double>(1.110223E-16F); // MC's float constant, exactly 2^-53

// ---- MD5 (RFC 1321), for RandomSupport#seedFromHashOf ------------------------------------------
std::array<std::uint8_t, 16> md5(std::string_view text) {
    static constexpr std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static constexpr int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::string msg(text);
    std::uint64_t bitLen = static_cast<std::uint64_t>(msg.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>((bitLen >> (8 * i)) & 0xFF));

    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            auto const* p = reinterpret_cast<unsigned char const*>(msg.data() + chunk + 4 * i);
            m[i] = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
                 | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }
        std::uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f;
            int           g;
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
            f = f + a + K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + ((f << S[i]) | (f >> (32 - S[i])));
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    std::array<std::uint8_t, 16> out{};
    std::uint32_t                 words[4] = {a0, b0, c0, d0};
    for (int w = 0; w < 4; ++w)
        for (int i = 0; i < 4; ++i) out[static_cast<std::size_t>(4 * w + i)] = static_cast<std::uint8_t>((words[w] >> (8 * i)) & 0xFF);
    return out;
}

/// MarsagliaPolarGaussian#nextGaussian over `nextDouble` (shared by every source).
template <class NextDouble>
double polarGaussian(double& nextNext, bool& haveNextNext, NextDouble&& nextDouble) {
    if (haveNextNext) {
        haveNextNext = false;
        return nextNext;
    }
    double d0, d1, d2;
    do {
        d0 = 2.0 * nextDouble() - 1.0;
        d1 = 2.0 * nextDouble() - 1.0;
        d2 = d0 * d0 + d1 * d1;
    } while (d2 >= 1.0 || d2 == 0.0);
    // MarsagliaPolarGaussian uses Math.log (HotSpot's intrinsic), which can differ from
    // StrictMath.log in the last bit; the C runtime's log matches it on every Java golden
    // (tests/worldgen), and java.util.Random's StrictMath values as well.
    double d3    = std::sqrt(-2.0 * std::log(d2) / d2);
    nextNext     = d1 * d3;
    haveNextNext = true;
    return d0 * d3;
}

/// BitRandomSource#nextInt(bound) over `next(bits)`.
template <class Next>
int bitNextInt(int bound, Next&& next) {
    if (bound <= 0) return 0; // Java throws IllegalArgumentException("Bound must be positive")
    if ((bound & (bound - 1)) == 0) {
        return static_cast<int>(lmul(static_cast<std::int64_t>(bound), static_cast<std::int64_t>(next(31))) >> 31);
    }
    int i, j;
    do {
        i = next(31);
        j = i % bound;
    } while (mth::iadd(i - j, bound - 1) < 0);
    return j;
}

class LegacyPositionalFactory final : public PositionalRandomFactory {
public:
    explicit LegacyPositionalFactory(std::int64_t seed) : mSeed(seed) {}
    std::unique_ptr<RandomSource> at(int x, int y, int z) override {
        return std::make_unique<LegacyRandom>(mthGetSeed(x, y, z) ^ mSeed);
    }
    std::unique_ptr<RandomSource> fromHashOf(std::string_view name) override {
        return std::make_unique<LegacyRandom>(static_cast<std::int64_t>(javaStringHash(name)) ^ mSeed);
    }
    /// LegacyPositionalRandomFactory#fromSeed: the seed alone (the factory's own seed is not mixed in).
    static std::unique_ptr<RandomSource> fromSeed(std::int64_t seed) { return std::make_unique<LegacyRandom>(seed); }

private:
    std::int64_t mSeed;
};

class XoroshiroPositionalFactory final : public PositionalRandomFactory {
public:
    XoroshiroPositionalFactory(std::int64_t lo, std::int64_t hi) : mLo(lo), mHi(hi) {}
    std::unique_ptr<RandomSource> at(int x, int y, int z) override {
        return std::make_unique<Xoroshiro128pp>(mthGetSeed(x, y, z) ^ mLo, mHi);
    }
    std::unique_ptr<RandomSource> fromHashOf(std::string_view name) override {
        auto s = seedFromHashOf(name);
        return std::make_unique<Xoroshiro128pp>(s.lo ^ mLo, s.hi ^ mHi);
    }
    /// XoroshiroPositionalRandomFactory#fromSeed.
    std::unique_ptr<RandomSource> fromSeed(std::int64_t seed) const { return std::make_unique<Xoroshiro128pp>(seed ^ mLo, seed ^ mHi); }

private:
    std::int64_t mLo, mHi;
};

/// The source a WorldgenRandom draws from, plus the WorldgenRandom's own gaussian state (the
/// contract header keeps only the source pointer and the count).
class WorldgenSource final : public RandomSource {
public:
    explicit WorldgenSource(std::unique_ptr<RandomSource> inner)
    : mInner(std::move(inner)),
      mLegacy(dynamic_cast<LegacyRandom*>(mInner.get())) {}

    /// WorldgenRandom#next: the legacy LCG's bits, otherwise the top bits of nextLong().
    int next(int bits) {
        if (mLegacy) return mLegacy->next(bits);
        return static_cast<int>(lushr(mInner->nextLong(), 64 - bits));
    }

    void         setSeed(std::int64_t seed) override { mInner->setSeed(seed); }
    int          nextInt() override { return mInner->nextInt(); }
    int          nextInt(int bound) override { return mInner->nextInt(bound); }
    std::int64_t nextLong() override { return mInner->nextLong(); }
    bool         nextBoolean() override { return mInner->nextBoolean(); }
    float        nextFloat() override { return mInner->nextFloat(); }
    double       nextDouble() override { return mInner->nextDouble(); }
    double       nextGaussian() override { return mInner->nextGaussian(); }
    std::unique_ptr<RandomSource>            fork() override { return mInner->fork(); }
    std::unique_ptr<PositionalRandomFactory> forkPositional() override { return mInner->forkPositional(); }

    double nextNextGaussian     = 0;
    bool   haveNextNextGaussian = false;

private:
    std::unique_ptr<RandomSource> mInner;
    LegacyRandom*                 mLegacy;
};

} // namespace

// ---- RandomSupport ------------------------------------------------------------------------------

std::int64_t mixStafford13(std::int64_t z) {
    z = lmul(z ^ lushr(z, 30), static_cast<std::int64_t>(0xBF58476D1CE4E5B9ULL));
    z = lmul(z ^ lushr(z, 27), static_cast<std::int64_t>(0x94D049BB133111EBULL));
    return z ^ lushr(z, 31);
}

Seed128bit upgradeSeedTo128bitUnmixed(std::int64_t seed) {
    std::int64_t lo = seed ^ kSilverRatio64;
    return {lo, ladd(lo, kGoldenRatio64)};
}

Seed128bit upgradeSeedTo128bit(std::int64_t seed) {
    auto s = upgradeSeedTo128bitUnmixed(seed);
    return {mixStafford13(s.lo), mixStafford13(s.hi)};
}

Seed128bit seedFromHashOf(std::string_view name) {
    auto          digest = md5(name);
    std::uint64_t lo = 0, hi = 0;
    for (int i = 0; i < 8; ++i) lo = (lo << 8) | digest[static_cast<std::size_t>(i)];
    for (int i = 8; i < 16; ++i) hi = (hi << 8) | digest[static_cast<std::size_t>(i)];
    return {static_cast<std::int64_t>(lo), static_cast<std::int64_t>(hi)};
}

std::unique_ptr<RandomSource> positionalFromSeed(PositionalRandomFactory& factory, std::int64_t seed) {
    if (dynamic_cast<LegacyPositionalFactory*>(&factory)) return LegacyPositionalFactory::fromSeed(seed);
    if (auto* x = dynamic_cast<XoroshiroPositionalFactory*>(&factory)) return x->fromSeed(seed);
    return nullptr;
}

// ---- RandomSource -------------------------------------------------------------------------------

void RandomSource::consumeCount(int count) {
    for (int i = 0; i < count; ++i) (void)nextInt();
}

// ---- LegacyRandom (java.util.Random / LegacyRandomSource) ---------------------------------------

LegacyRandom::LegacyRandom(std::int64_t seed) { setSeed(seed); }

void LegacyRandom::setSeed(std::int64_t seed) {
    mSeed                 = (seed ^ kMultiplier) & kMask48;
    mHaveNextNextGaussian = false;
}

int LegacyRandom::next(int bits) {
    mSeed = ladd(lmul(mSeed, kMultiplier), kIncrement) & kMask48;
    return static_cast<int>(mSeed >> (48 - bits));
}

int LegacyRandom::nextInt() { return next(32); }

int LegacyRandom::nextInt(int bound) {
    return bitNextInt(bound, [this](int bits) { return next(bits); });
}

std::int64_t LegacyRandom::nextLong() {
    int i = next(32);
    int j = next(32);
    return ladd(static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(i)) << 32), static_cast<std::int64_t>(j));
}

bool LegacyRandom::nextBoolean() { return next(1) != 0; }

float LegacyRandom::nextFloat() { return static_cast<float>(next(24)) * kFloatUnit; }

double LegacyRandom::nextDouble() {
    int          i = next(26);
    int          j = next(27);
    std::int64_t k = (static_cast<std::int64_t>(i) << 27) + static_cast<std::int64_t>(j);
    return static_cast<double>(k) * kDoubleUnit;
}

double LegacyRandom::nextGaussian() {
    return polarGaussian(mNextNextGaussian, mHaveNextNextGaussian, [this] { return nextDouble(); });
}

std::unique_ptr<RandomSource> LegacyRandom::fork() { return std::make_unique<LegacyRandom>(nextLong()); }

std::unique_ptr<PositionalRandomFactory> LegacyRandom::forkPositional() {
    return std::make_unique<LegacyPositionalFactory>(nextLong());
}

// ---- Xoroshiro128pp (XoroshiroRandomSource) -----------------------------------------------------

Xoroshiro128pp::Xoroshiro128pp(std::int64_t seed) { setSeed(seed); }

Xoroshiro128pp::Xoroshiro128pp(std::int64_t seedLo, std::int64_t seedHi) {
    if ((seedLo | seedHi) == 0) {
        seedLo = kGoldenRatio64;
        seedHi = kSilverRatio64;
    }
    mLo = static_cast<std::uint64_t>(seedLo);
    mHi = static_cast<std::uint64_t>(seedHi);
}

void Xoroshiro128pp::setSeed(std::int64_t seed) {
    auto s = upgradeSeedTo128bit(seed);
    if ((s.lo | s.hi) == 0) {
        s.lo = kGoldenRatio64;
        s.hi = kSilverRatio64;
    }
    mLo                   = static_cast<std::uint64_t>(s.lo);
    mHi                   = static_cast<std::uint64_t>(s.hi);
    mHaveNextNextGaussian = false;
}

std::int64_t Xoroshiro128pp::nextLong() {
    std::uint64_t i = mLo;
    std::uint64_t j = mHi;
    auto          rotl = [](std::uint64_t v, int n) { return (v << n) | (v >> (64 - n)); };
    std::uint64_t k    = rotl(i + j, 17) + i;
    j ^= i;
    mLo = rotl(i, 49) ^ j ^ (j << 21);
    mHi = rotl(j, 28);
    return static_cast<std::int64_t>(k);
}

int Xoroshiro128pp::nextInt() { return static_cast<int>(nextLong()); }

int Xoroshiro128pp::nextInt(int bound) {
    if (bound <= 0) return 0; // Java throws
    auto          ubound = static_cast<std::uint64_t>(static_cast<std::uint32_t>(bound));
    std::uint64_t i      = static_cast<std::uint32_t>(nextInt());
    std::uint64_t j      = i * ubound;
    std::uint64_t k      = j & 0xFFFFFFFFull;
    if (k < ubound) {
        // Integer.remainderUnsigned(~bound + 1, bound)
        auto l = static_cast<std::uint64_t>((static_cast<std::uint32_t>(~bound) + 1u) % static_cast<std::uint32_t>(bound));
        while (k < l) {
            i = static_cast<std::uint32_t>(nextInt());
            j = i * ubound;
            k = j & 0xFFFFFFFFull;
        }
    }
    return static_cast<int>(static_cast<std::int64_t>(j) >> 32);
}

bool Xoroshiro128pp::nextBoolean() { return (nextLong() & 1) != 0; }

float Xoroshiro128pp::nextFloat() { return static_cast<float>(lushr(nextLong(), 40)) * kFloatUnit; }

double Xoroshiro128pp::nextDouble() { return static_cast<double>(lushr(nextLong(), 11)) * kDoubleUnit; }

double Xoroshiro128pp::nextGaussian() {
    return polarGaussian(mNextNextGaussian, mHaveNextNextGaussian, [this] { return nextDouble(); });
}

std::unique_ptr<RandomSource> Xoroshiro128pp::fork() {
    std::int64_t lo = nextLong();
    std::int64_t hi = nextLong();
    return std::make_unique<Xoroshiro128pp>(lo, hi);
}

std::unique_ptr<PositionalRandomFactory> Xoroshiro128pp::forkPositional() {
    std::int64_t lo = nextLong();
    std::int64_t hi = nextLong();
    return std::make_unique<XoroshiroPositionalFactory>(lo, hi);
}

// ---- WorldgenRandom -----------------------------------------------------------------------------
// WorldgenRandom extends LegacyRandomSource: every value is built from next(bits) of the wrapped
// source (BitRandomSource defaults), while fork / forkPositional / setSeed go to the source.

namespace {
WorldgenSource& wsrc(std::unique_ptr<RandomSource> const& p) { return static_cast<WorldgenSource&>(*p); }
} // namespace

WorldgenRandom::WorldgenRandom(std::unique_ptr<RandomSource> source)
: mSource(std::make_unique<WorldgenSource>(source ? std::move(source) : std::make_unique<LegacyRandom>(0))) {}

std::int64_t WorldgenRandom::setDecorationSeed(std::int64_t levelSeed, int minBlockX, int minBlockZ) {
    setSeed(levelSeed);
    std::int64_t i = nextLong() | 1;
    std::int64_t j = nextLong() | 1;
    std::int64_t k = ladd(lmul(static_cast<std::int64_t>(minBlockX), i), lmul(static_cast<std::int64_t>(minBlockZ), j)) ^ levelSeed;
    setSeed(k);
    return k;
}

void WorldgenRandom::setFeatureSeed(std::int64_t decorationSeed, int index, int step) {
    std::int64_t i = ladd(ladd(decorationSeed, static_cast<std::int64_t>(index)), static_cast<std::int64_t>(mth::imul(10000, step)));
    setSeed(i);
}

void WorldgenRandom::setLargeFeatureSeed(std::int64_t baseSeed, int chunkX, int chunkZ) {
    setSeed(baseSeed);
    std::int64_t i = nextLong();
    std::int64_t j = nextLong();
    std::int64_t k = lmul(static_cast<std::int64_t>(chunkX), i) ^ lmul(static_cast<std::int64_t>(chunkZ), j) ^ baseSeed;
    setSeed(k);
}

void WorldgenRandom::setLargeFeatureWithSalt(std::int64_t levelSeed, int regionX, int regionZ, int salt) {
    std::int64_t i = ladd(
        ladd(ladd(lmul(static_cast<std::int64_t>(regionX), 341873128712LL), lmul(static_cast<std::int64_t>(regionZ), 132897987541LL)), levelSeed),
        static_cast<std::int64_t>(salt)
    );
    setSeed(i);
}

std::unique_ptr<RandomSource> WorldgenRandom::seedSlimeChunk(int chunkX, int chunkZ, std::int64_t levelSeed, std::int64_t salt) {
    using mth::imul;
    std::int64_t s = levelSeed;
    s              = ladd(s, static_cast<std::int64_t>(imul(imul(chunkX, chunkX), 4987142)));
    s              = ladd(s, static_cast<std::int64_t>(imul(chunkX, 5947611)));
    s              = ladd(s, lmul(static_cast<std::int64_t>(imul(chunkZ, chunkZ)), 4392871LL));
    s              = ladd(s, static_cast<std::int64_t>(imul(chunkZ, 389711)));
    return std::make_unique<LegacyRandom>(s ^ salt);
}

void WorldgenRandom::setSeed(std::int64_t seed) { wsrc(mSource).setSeed(seed); }

int WorldgenRandom::nextInt() {
    ++mCount;
    return wsrc(mSource).next(32);
}

int WorldgenRandom::nextInt(int bound) {
    return bitNextInt(bound, [this](int bits) {
        ++mCount;
        return wsrc(mSource).next(bits);
    });
}

std::int64_t WorldgenRandom::nextLong() {
    mCount += 2;
    int i = wsrc(mSource).next(32);
    int j = wsrc(mSource).next(32);
    return ladd(static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(i)) << 32), static_cast<std::int64_t>(j));
}

bool WorldgenRandom::nextBoolean() {
    ++mCount;
    return wsrc(mSource).next(1) != 0;
}

float WorldgenRandom::nextFloat() {
    ++mCount;
    return static_cast<float>(wsrc(mSource).next(24)) * kFloatUnit;
}

double WorldgenRandom::nextDouble() {
    mCount += 2;
    int          i = wsrc(mSource).next(26);
    int          j = wsrc(mSource).next(27);
    std::int64_t k = (static_cast<std::int64_t>(i) << 27) + static_cast<std::int64_t>(j);
    return static_cast<double>(k) * kDoubleUnit;
}

double WorldgenRandom::nextGaussian() {
    auto& s = wsrc(mSource);
    return polarGaussian(s.nextNextGaussian, s.haveNextNextGaussian, [this] { return nextDouble(); });
}

std::unique_ptr<RandomSource>            WorldgenRandom::fork() { return wsrc(mSource).fork(); }
std::unique_ptr<PositionalRandomFactory> WorldgenRandom::forkPositional() { return wsrc(mSource).forkPositional(); }

// ---- hashing --------------------------------------------------------------------------------------

int javaStringHash(std::string_view text) {
    // UTF-8 -> UTF-16 code units, then s[0]*31^(n-1) + ... + s[n-1]
    std::uint32_t h = 0;
    auto          addUnit = [&h](std::uint32_t unit) { h = h * 31u + unit; };
    std::size_t   i       = 0;
    while (i < text.size()) {
        auto          c  = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = c;
        int           n  = 0;
        if (c >= 0xF0) {
            cp = c & 0x07;
            n  = 3;
        } else if (c >= 0xE0) {
            cp = c & 0x0F;
            n  = 2;
        } else if (c >= 0xC0) {
            cp = c & 0x1F;
            n  = 1;
        }
        ++i;
        for (int k = 0; k < n && i < text.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            addUnit(0xD800 + (cp >> 10));
            addUnit(0xDC00 + (cp & 0x3FF));
        } else {
            addUnit(cp);
        }
    }
    return static_cast<int>(h);
}

std::int64_t mthGetSeed(int x, int y, int z) {
    std::int64_t i = static_cast<std::int64_t>(mth::imul(x, 3129871)) ^ lmul(static_cast<std::int64_t>(z), 116129781LL)
                   ^ static_cast<std::int64_t>(y);
    i = ladd(lmul(lmul(i, i), 42317861LL), lmul(i, 11LL));
    return i >> 16;
}

} // namespace gf::core::jrand

// ---- fdlibm log (java.lang.StrictMath#log) -----------------------------------------------------
// Translated from fdlibm 5.3 e_log.c: Copyright (C) 1993 by Sun Microsystems, Inc. All rights
// reserved. Developed at SunSoft, a Sun Microsystems, Inc. business. Permission to use, copy,
// modify, and distribute this software is freely granted, provided that this notice is preserved.
namespace gf::core::mth {

namespace {
constexpr double ln2_hi = 6.93147180369123816490e-01;
constexpr double ln2_lo = 1.90821492927058770002e-10;
constexpr double two54  = 1.80143985094819840000e+16;
constexpr double Lg1    = 6.666666666666735130e-01;
constexpr double Lg2    = 3.999999999940941908e-01;
constexpr double Lg3    = 2.857142874366239149e-01;
constexpr double Lg4    = 2.222219843214978396e-01;
constexpr double Lg5    = 1.818357216161805012e-01;
constexpr double Lg6    = 1.531383769920937332e-01;
constexpr double Lg7    = 1.479819860511658591e-01;

std::int32_t hiWord(double x) {
    std::uint64_t b;
    std::memcpy(&b, &x, sizeof b);
    return static_cast<std::int32_t>(b >> 32);
}
std::uint32_t loWord(double x) {
    std::uint64_t b;
    std::memcpy(&b, &x, sizeof b);
    return static_cast<std::uint32_t>(b);
}
double withHi(double x, std::int32_t hi) {
    std::uint64_t b;
    std::memcpy(&b, &x, sizeof b);
    b = (b & 0xFFFFFFFFull) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32);
    std::memcpy(&x, &b, sizeof b);
    return x;
}
} // namespace

double strictLog(double x) {
    double        hfsq, f, s, z, R, w, t1, t2, dk;
    std::int32_t  k, hx, i, j;
    std::uint32_t lx;

    hx = hiWord(x);
    lx = loWord(x);
    k  = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | static_cast<std::int32_t>(lx)) == 0) return -INFINITY;
        if (hx < 0) return NAN;
        k -= 54;
        x *= two54;
        hx = hiWord(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    i = (hx + 0x95f64) & 0x100000;
    x = withHi(x, hx | (i ^ 0x3ff00000));
    k += (i >> 20);
    f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) {
        if (f == 0.0) {
            if (k == 0) return 0.0;
            dk = static_cast<double>(k);
            return dk * ln2_hi + dk * ln2_lo;
        }
        R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0) return f - R;
        dk = static_cast<double>(k);
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
    s  = f / (2.0 + f);
    dk = static_cast<double>(k);
    z  = s * s;
    i  = hx - 0x6147a;
    w  = z * z;
    j  = 0x6b851 - hx;
    t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    R = t2 + t1;
    if (i > 0) {
        hfsq = 0.5 * f * f;
        if (k == 0) return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }
    if (k == 0) return f - s * (f - R);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

} // namespace gf::core::mth
