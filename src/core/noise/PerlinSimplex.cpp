// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java-parity PerlinSimplexNoise (Minecraft 1.21.1) and Biome's static noises. Written from the
// published algorithm; checked against Java values in tests/worldgen.
#include "core/noise/PerlinSimplex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#pragma clang fp contract(off)

namespace gf::core::noise {

namespace {

/// Forwards to a source and remembers the first three doubles (SimplexNoise's xo, yo, zo).
class Recording final : public jrand::RandomSource {
public:
    explicit Recording(jrand::RandomSource& inner) : mInner(inner) {}
    void         setSeed(std::int64_t seed) override { mInner.setSeed(seed); }
    int          nextInt() override { return mInner.nextInt(); }
    int          nextInt(int bound) override { return mInner.nextInt(bound); }
    std::int64_t nextLong() override { return mInner.nextLong(); }
    bool         nextBoolean() override { return mInner.nextBoolean(); }
    float        nextFloat() override { return mInner.nextFloat(); }
    double       nextDouble() override {
        double d = mInner.nextDouble();
        if (mCount < 3) mDoubles[mCount++] = d;
        return d;
    }
    double                                          nextGaussian() override { return mInner.nextGaussian(); }
    void                                            consumeCount(int count) override { mInner.consumeCount(count); }
    std::unique_ptr<jrand::RandomSource>            fork() override { return mInner.fork(); }
    std::unique_ptr<jrand::PositionalRandomFactory> forkPositional() override { return mInner.forkPositional(); }

    double offset(int i) const { return mDoubles[i] * 256.0; }

private:
    jrand::RandomSource& mInner;
    double               mDoubles[3]{};
    int                  mCount = 0;
};

} // namespace

PerlinSimplexNoise::PerlinSimplexNoise(jrand::RandomSource& random, std::vector<int> octavesIn) {
    std::set<int> octaves(octavesIn.begin(), octavesIn.end());
    if (octaves.empty()) return; // Java: IllegalArgumentException("Need some octaves!")
    int i = -*octaves.begin();
    int j = *octaves.rbegin();
    int k = i + j + 1;
    mLevels.resize(static_cast<std::size_t>(k));
    Recording rec(random);
    auto      first = std::make_unique<SimplexNoise>(rec);
    double    fxo = rec.offset(0), fyo = rec.offset(1), fzo = rec.offset(2);
    int       l   = j;
    SimplexNoise const* firstPtr = first.get();
    if (j >= 0 && j < k && octaves.contains(0)) mLevels[static_cast<std::size_t>(j)] = {std::move(first), fxo, fyo};
    for (int i1 = j + 1; i1 < k; ++i1) {
        if (i1 >= 0 && octaves.contains(l - i1)) {
            Recording r(random);
            auto      n = std::make_unique<SimplexNoise>(r);
            mLevels[static_cast<std::size_t>(i1)] = {std::move(n), r.offset(0), r.offset(1)};
        } else {
            random.consumeCount(262);
        }
    }
    if (j > 0) {
        double v  = firstPtr->getValue(fxo, fyo, fzo) * static_cast<double>(9.223372E18F);
        auto   k1 = v >= 9.2233720368547758e18 ? std::numeric_limits<std::int64_t>::max()
                  : v <= -9.2233720368547758e18 ? std::numeric_limits<std::int64_t>::min()
                                                : static_cast<std::int64_t>(v); // Java's saturating (long) cast
        jrand::WorldgenRandom rs(std::make_unique<jrand::LegacyRandom>(k1));
        for (int j1 = l - 1; j1 >= 0; --j1) {
            if (j1 < k && octaves.contains(l - j1)) {
                Recording r(rs);
                auto      n = std::make_unique<SimplexNoise>(r);
                mLevels[static_cast<std::size_t>(j1)] = {std::move(n), r.offset(0), r.offset(1)};
            } else {
                rs.consumeCount(262);
            }
        }
    }
    mHighestFreqInputFactor = std::pow(2.0, static_cast<double>(j));
    mHighestFreqValueFactor = 1.0 / (std::pow(2.0, static_cast<double>(k)) - 1.0);
}

double PerlinSimplexNoise::getValue(double x, double y, bool useNoiseOffsets) const {
    double d0 = 0.0;
    double d1 = mHighestFreqInputFactor;
    double d2 = mHighestFreqValueFactor;
    for (auto const& level : mLevels) {
        if (level.noise) {
            d0 += level.noise->getValue(x * d1 + (useNoiseOffsets ? level.xo : 0.0), y * d1 + (useNoiseOffsets ? level.yo : 0.0)) * d2;
        }
        d1 /= 2.0;
        d2 *= 2.0;
    }
    return d0;
}

PerlinSimplexNoise const& PerlinSimplexNoise::temperatureNoise() {
    static PerlinSimplexNoise const n = [] {
        jrand::WorldgenRandom r(std::make_unique<jrand::LegacyRandom>(1234));
        return PerlinSimplexNoise(r, {0});
    }();
    return n;
}

PerlinSimplexNoise const& PerlinSimplexNoise::frozenTemperatureNoise() {
    static PerlinSimplexNoise const n = [] {
        jrand::WorldgenRandom r(std::make_unique<jrand::LegacyRandom>(3456));
        return PerlinSimplexNoise(r, {-2, -1, 0});
    }();
    return n;
}

PerlinSimplexNoise const& PerlinSimplexNoise::biomeInfoNoise() {
    static PerlinSimplexNoise const n = [] {
        jrand::WorldgenRandom r(std::make_unique<jrand::LegacyRandom>(2345));
        return PerlinSimplexNoise(r, {0});
    }();
    return n;
}

} // namespace gf::core::noise
