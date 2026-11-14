// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java-parity noise and density functions: bit-exact equivalents of Minecraft 1.21.1's
// ImprovedNoise, SimplexNoise, PerlinNoise, NormalNoise, the vanilla DensityFunctions, CubicSpline
// and NoiseChunk's interpolation, which the upstream 4.8 builds its terrain on. Written
// from the published algorithms; checked against Java values in tests/worldgen.
#include "core/noise/Noise.h"

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "core/jrand/Mth.h"
#include "core/noise/LegacyNormalNoise.h"
#include "core/noise/NoiseChunk.h"

#pragma clang fp contract(off)

namespace gf::core::noise {

using Json = nlohmann::json;
namespace mth = gf::core::mth;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/// SimplexNoise.GRADIENT (shared with ImprovedNoise#gradDot).
constexpr int kGradient[16][3] = {
    {1, 1, 0},
    {-1, 1, 0},
    {1, -1, 0},
    {-1, -1, 0},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
    {0, 1, 1},
    {0, -1, 1},
    {0, 1, -1},
    {0, -1, -1},
    {1, 1, 0},
    {0, -1, 1},
    {-1, 1, 0},
    {0, -1, -1},
};

double dot(int const g[3], double x, double y, double z) { return g[0] * x + g[1] * y + g[2] * z; }

/// java.lang.Math#min / #max for doubles (NaN propagates, -0.0 < 0.0).
double jmin(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(b)) return b;
    return a <= b ? a : b;
}
double jmax(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(a)) return b;
    return a >= b ? a : b;
}

} // namespace

// ==== ImprovedNoise ================================================================================

struct ImprovedNoise::Impl {
    std::array<std::uint8_t, 256> p{};
    double                        xo = 0, yo = 0, zo = 0;

    int perm(int index) const { return p[static_cast<std::size_t>(index & 0xFF)]; }

    static double gradDot(int gradIndex, double x, double y, double z) { return dot(kGradient[gradIndex & 15], x, y, z); }

    double sampleAndLerp(int gx, int gy, int gz, double dx, double weirdDy, double dz, double dy) const {
        int    i  = perm(gx);
        int    j  = perm(gx + 1);
        int    k  = perm(i + gy);
        int    l  = perm(i + gy + 1);
        int    i1 = perm(j + gy);
        int    j1 = perm(j + gy + 1);
        double d0 = gradDot(perm(k + gz), dx, weirdDy, dz);
        double d1 = gradDot(perm(i1 + gz), dx - 1.0, weirdDy, dz);
        double d2 = gradDot(perm(l + gz), dx, weirdDy - 1.0, dz);
        double d3 = gradDot(perm(j1 + gz), dx - 1.0, weirdDy - 1.0, dz);
        double d4 = gradDot(perm(k + gz + 1), dx, weirdDy, dz - 1.0);
        double d5 = gradDot(perm(i1 + gz + 1), dx - 1.0, weirdDy, dz - 1.0);
        double d6 = gradDot(perm(l + gz + 1), dx, weirdDy - 1.0, dz - 1.0);
        double d7 = gradDot(perm(j1 + gz + 1), dx - 1.0, weirdDy - 1.0, dz - 1.0);
        double d8 = mth::smoothstep(dx);
        double d9 = mth::smoothstep(dy);
        double d10 = mth::smoothstep(dz);
        return mth::lerp3(d8, d9, d10, d0, d1, d2, d3, d4, d5, d6, d7);
    }

    double noise(double x, double y, double z, double yScale, double yMax) const {
        double d0 = x + xo;
        double d1 = y + yo;
        double d2 = z + zo;
        int    i  = mth::floor(d0);
        int    j  = mth::floor(d1);
        int    k  = mth::floor(d2);
        double d3 = d0 - static_cast<double>(i);
        double d4 = d1 - static_cast<double>(j);
        double d5 = d2 - static_cast<double>(k);
        double d6;
        if (yScale != 0.0) {
            double d7 = (yMax >= 0.0 && yMax < d4) ? yMax : d4;
            d6        = static_cast<double>(mth::floor(d7 / yScale + static_cast<double>(1.0E-7F))) * yScale;
        } else {
            d6 = 0.0;
        }
        return sampleAndLerp(i, j, k, d3, d4 - d6, d5, d4);
    }
};

ImprovedNoise::ImprovedNoise(jrand::RandomSource& random) {
    auto impl = std::make_shared<Impl>();
    impl->xo  = random.nextDouble() * 256.0;
    impl->yo  = random.nextDouble() * 256.0;
    impl->zo  = random.nextDouble() * 256.0;
    for (int i = 0; i < 256; ++i) impl->p[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    for (int i = 0; i < 256; ++i) {
        int j = random.nextInt(256 - i);
        std::swap(impl->p[static_cast<std::size_t>(i)], impl->p[static_cast<std::size_t>(i + j)]);
    }
    mImpl = std::move(impl);
}

double ImprovedNoise::noise(double x, double y, double z) const { return mImpl->noise(x, y, z, 0.0, 0.0); }
double ImprovedNoise::noise(double x, double y, double z, double yScale, double yMax) const {
    return mImpl->noise(x, y, z, yScale, yMax);
}

// ==== SimplexNoise =================================================================================

struct SimplexNoise::Impl {
    std::array<int, 512> p{};
    double               xo = 0, yo = 0, zo = 0;

    static inline double const kF2 = 0.5 * (std::sqrt(3.0) - 1.0); // SimplexNoise.F2
    static inline double const kG2 = (3.0 - std::sqrt(3.0)) / 6.0; // SimplexNoise.G2

    int perm(int index) const { return p[static_cast<std::size_t>(index & 0xFF)]; }

    static double corner(int gradientIndex, double x, double y, double z, double offset) {
        double d0 = offset - x * x - y * y - z * z;
        double d1;
        if (d0 < 0.0) {
            d1 = 0.0;
        } else {
            d0 *= d0;
            d1 = d0 * d0 * dot(kGradient[gradientIndex], x, y, z);
        }
        return d1;
    }

    double value2(double x, double y) const {
        double d0 = (x + y) * kF2;
        int    i  = mth::floor(x + d0);
        int    j  = mth::floor(y + d0);
        double d1 = static_cast<double>(i + j) * kG2;
        double d2 = static_cast<double>(i) - d1;
        double d3 = static_cast<double>(j) - d1;
        double d4 = x - d2;
        double d5 = y - d3;
        int    k, l;
        if (d4 > d5) {
            k = 1;
            l = 0;
        } else {
            k = 0;
            l = 1;
        }
        double d6  = d4 - static_cast<double>(k) + kG2;
        double d7  = d5 - static_cast<double>(l) + kG2;
        double d8  = d4 - 1.0 + 2.0 * kG2;
        double d9  = d5 - 1.0 + 2.0 * kG2;
        int    i1  = i & 0xFF;
        int    j1  = j & 0xFF;
        int    k1  = perm(i1 + perm(j1)) % 12;
        int    l1  = perm(i1 + k + perm(j1 + l)) % 12;
        int    i2  = perm(i1 + 1 + perm(j1 + 1)) % 12;
        double d10 = corner(k1, d4, d5, 0.0, 0.5);
        double d11 = corner(l1, d6, d7, 0.0, 0.5);
        double d12 = corner(i2, d8, d9, 0.0, 0.5);
        return 70.0 * (d10 + d11 + d12);
    }

    double value3(double x, double y, double z) const {
        double d1 = (x + y + z) * 0.3333333333333333;
        int    i  = mth::floor(x + d1);
        int    j  = mth::floor(y + d1);
        int    k  = mth::floor(z + d1);
        double d3 = static_cast<double>(i + j + k) * 0.16666666666666666;
        double d4 = static_cast<double>(i) - d3;
        double d5 = static_cast<double>(j) - d3;
        double d6 = static_cast<double>(k) - d3;
        double d7 = x - d4;
        double d8 = y - d5;
        double d9 = z - d6;
        int    l, i1, j1, k1, l1, i2;
        if (d7 >= d8) {
            if (d8 >= d9) {
                l = 1, i1 = 0, j1 = 0, k1 = 1, l1 = 1, i2 = 0;
            } else if (d7 >= d9) {
                l = 1, i1 = 0, j1 = 0, k1 = 1, l1 = 0, i2 = 1;
            } else {
                l = 0, i1 = 0, j1 = 1, k1 = 1, l1 = 0, i2 = 1;
            }
        } else if (d8 < d9) {
            l = 0, i1 = 0, j1 = 1, k1 = 0, l1 = 1, i2 = 1;
        } else if (d7 < d9) {
            l = 0, i1 = 1, j1 = 0, k1 = 0, l1 = 1, i2 = 1;
        } else {
            l = 0, i1 = 1, j1 = 0, k1 = 1, l1 = 1, i2 = 0;
        }
        double d10 = d7 - static_cast<double>(l) + 0.16666666666666666;
        double d11 = d8 - static_cast<double>(i1) + 0.16666666666666666;
        double d12 = d9 - static_cast<double>(j1) + 0.16666666666666666;
        double d13 = d7 - static_cast<double>(k1) + 0.3333333333333333;
        double d14 = d8 - static_cast<double>(l1) + 0.3333333333333333;
        double d15 = d9 - static_cast<double>(i2) + 0.3333333333333333;
        double d16 = d7 - 1.0 + 0.5;
        double d17 = d8 - 1.0 + 0.5;
        double d18 = d9 - 1.0 + 0.5;
        int    j2  = i & 0xFF;
        int    k2  = j & 0xFF;
        int    l2  = k & 0xFF;
        int    i3  = perm(j2 + perm(k2 + perm(l2))) % 12;
        int    j3  = perm(j2 + l + perm(k2 + i1 + perm(l2 + j1))) % 12;
        int    k3  = perm(j2 + k1 + perm(k2 + l1 + perm(l2 + i2))) % 12;
        int    l3  = perm(j2 + 1 + perm(k2 + 1 + perm(l2 + 1))) % 12;
        double d19 = corner(i3, d7, d8, d9, 0.6);
        double d20 = corner(j3, d10, d11, d12, 0.6);
        double d21 = corner(k3, d13, d14, d15, 0.6);
        double d22 = corner(l3, d16, d17, d18, 0.6);
        return 32.0 * (d19 + d20 + d21 + d22);
    }
};

SimplexNoise::SimplexNoise(jrand::RandomSource& random) {
    auto impl = std::make_shared<Impl>();
    impl->xo  = random.nextDouble() * 256.0;
    impl->yo  = random.nextDouble() * 256.0;
    impl->zo  = random.nextDouble() * 256.0;
    for (int i = 0; i < 256; ++i) impl->p[static_cast<std::size_t>(i)] = i;
    for (int i = 0; i < 256; ++i) {
        int j = random.nextInt(256 - i);
        std::swap(impl->p[static_cast<std::size_t>(i)], impl->p[static_cast<std::size_t>(j + i)]);
    }
    mImpl = std::move(impl);
}

double SimplexNoise::getValue(double x, double y) const { return mImpl->value2(x, y); }
double SimplexNoise::getValue(double x, double y, double z) const { return mImpl->value3(x, y, z); }

// ==== PerlinNoise ==================================================================================

struct PerlinNoise::Impl {
    std::vector<std::optional<ImprovedNoise>> levels;
    int                                       firstOctave = 0;
    std::vector<double>                       amplitudes;
    double                                    lowestFreqValueFactor = 0;
    double                                    lowestFreqInputFactor = 0;
    double                                    maxValue              = 0;

    static double wrap(double value) {
        return value - static_cast<double>(mth::lfloor(value / 3.3554432E7 + 0.5)) * 3.3554432E7;
    }

    double getValue(double x, double y, double z) const {
        double d0 = 0.0;
        double d1 = lowestFreqInputFactor;
        double d2 = lowestFreqValueFactor;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            if (auto const& n = levels[i]) {
                double d3 = n->noise(wrap(x * d1), wrap(y * d1), wrap(z * d1), 0.0 * d1, 0.0 * d1);
                d0 += amplitudes[i] * d3 * d2;
            }
            d1 *= 2.0;
            d2 /= 2.0;
        }
        return d0;
    }

    double edgeValue(double multiplier) const {
        double d0 = 0.0;
        double d1 = lowestFreqValueFactor;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            if (levels[i]) d0 += amplitudes[i] * multiplier * d1;
            d1 /= 2.0;
        }
        return d0;
    }

    void finish() {
        int i                 = static_cast<int>(amplitudes.size());
        int j                 = -firstOctave;
        lowestFreqInputFactor = std::pow(2.0, static_cast<double>(-j));
        lowestFreqValueFactor = std::pow(2.0, static_cast<double>(i - 1)) / (std::pow(2.0, static_cast<double>(i)) - 1.0);
        maxValue              = edgeValue(2.0);
    }
};

PerlinNoise PerlinNoise::create(jrand::RandomSource& random, NoiseParameters const& params) {
    auto impl         = std::make_shared<Impl>();
    impl->firstOctave = params.firstOctave;
    impl->amplitudes  = params.amplitudes;
    impl->levels.resize(params.amplitudes.size());
    auto factory = random.forkPositional();
    for (std::size_t k = 0; k < params.amplitudes.size(); ++k) {
        if (params.amplitudes[k] != 0.0) {
            int  l   = params.firstOctave + static_cast<int>(k);
            auto src = factory->fromHashOf("octave_" + std::to_string(l));
            impl->levels[k].emplace(*src);
        }
    }
    impl->finish();
    PerlinNoise out;
    out.mImpl = std::move(impl);
    return out;
}

PerlinNoise PerlinNoise::createLegacy(jrand::RandomSource& random, NoiseParameters const& params) {
    auto impl         = std::make_shared<Impl>();
    impl->firstOctave = params.firstOctave;
    impl->amplitudes  = params.amplitudes;
    int i             = static_cast<int>(params.amplitudes.size());
    int j             = -params.firstOctave;
    impl->levels.resize(static_cast<std::size_t>(i));
    ImprovedNoise first(random);
    if (j >= 0 && j < i && params.amplitudes[static_cast<std::size_t>(j)] != 0.0) impl->levels[static_cast<std::size_t>(j)] = first;
    for (int i1 = j - 1; i1 >= 0; --i1) {
        if (i1 < i) {
            if (params.amplitudes[static_cast<std::size_t>(i1)] != 0.0) impl->levels[static_cast<std::size_t>(i1)].emplace(random);
            else random.consumeCount(262);
        } else {
            random.consumeCount(262);
        }
    }
    impl->finish();
    PerlinNoise out;
    out.mImpl = std::move(impl);
    return out;
}

double PerlinNoise::getValue(double x, double y, double z) const { return mImpl ? mImpl->getValue(x, y, z) : 0.0; }
double PerlinNoise::maxValue() const { return mImpl ? mImpl->maxValue : 0.0; }

// ==== NormalNoise ==================================================================================

namespace {

/// NormalNoise's valueFactor: TARGET_DEVIATION / 2 over the expected deviation of the octaves from
/// the first to the last non-zero amplitude.
double normalValueFactor(NoiseParameters const& params) {
    int j = std::numeric_limits<int>::max();
    int k = std::numeric_limits<int>::min();
    for (std::size_t l = 0; l < params.amplitudes.size(); ++l) {
        if (params.amplitudes[l] != 0.0) {
            j = std::min(j, static_cast<int>(l));
            k = std::max(k, static_cast<int>(l));
        }
    }
    auto expectedDeviation = [](int octaves) { return 0.1 * (1.0 + 1.0 / static_cast<double>(octaves + 1)); };
    return 0.16666666666666666 / expectedDeviation(k - j);
}

/// NormalNoise#getValue over its two PerlinNoise (the second sampled at INPUT_FACTOR times the point).
double normalValue(PerlinNoise const& first, PerlinNoise const& second, double valueFactor, double x, double y, double z) {
    double d0 = x * 1.0181268882175227;
    double d1 = y * 1.0181268882175227;
    double d2 = z * 1.0181268882175227;
    return (first.getValue(x, y, z) + second.getValue(d0, d1, d2)) * valueFactor;
}

} // namespace

struct NormalNoise::Impl {
    PerlinNoise first, second;
    double      valueFactor = 0;
    double      maxValue    = 0;
};

NormalNoise NormalNoise::create(jrand::RandomSource& random, NoiseParameters const& params) {
    auto impl         = std::make_shared<Impl>();
    impl->first       = PerlinNoise::create(random, params);
    impl->second      = PerlinNoise::create(random, params);
    impl->valueFactor = normalValueFactor(params);
    impl->maxValue    = (impl->first.maxValue() + impl->second.maxValue()) * impl->valueFactor;
    NormalNoise out;
    out.mImpl = std::move(impl);
    return out;
}

double NormalNoise::getValue(double x, double y, double z) const {
    if (!mImpl) return 0.0;
    return normalValue(mImpl->first, mImpl->second, mImpl->valueFactor, x, y, z);
}
double NormalNoise::maxValue() const { return mImpl ? mImpl->maxValue : 0.0; }

// ==== LegacyNormalNoise ============================================================================

LegacyNormalNoise LegacyNormalNoise::createLegacyNetherBiome(jrand::RandomSource& random, NoiseParameters const& params) {
    LegacyNormalNoise out;
    out.mFirst       = PerlinNoise::createLegacy(random, params);
    out.mSecond      = PerlinNoise::createLegacy(random, params);
    out.mValueFactor = normalValueFactor(params);
    out.mMaxValue    = (out.mFirst.maxValue() + out.mSecond.maxValue()) * out.mValueFactor;
    return out;
}

double LegacyNormalNoise::getValue(double x, double y, double z) const { return normalValue(mFirst, mSecond, mValueFactor, x, y, z); }

// ==== density functions ============================================================================

namespace {

struct ChunkCache;

/// The evaluation point: a single point (chunk == nullptr) or a NoiseChunk fill.
struct Ctx {
    int         x = 0, y = 0, z = 0;
    ChunkCache* chunk     = nullptr;
    bool        blockLoop = false; ///< evaluating a block of the chunk (interpolation applies)
};

struct Node {
    virtual ~Node() = default;
    [[nodiscard]] virtual double compute(Ctx const& c) const = 0;
    double                       lo = -kInf, hi = kInf;
};
using NodeP = std::shared_ptr<Node const>;

/// Per-chunk caches of the flat_cache / interpolated markers.
struct ChunkCache {
    NoiseShape shape;
    int        chunkMinX = 0, chunkMinZ = 0;
    int        cellWidth = 8, cellHeight = 8, cellCountXZ = 2, cellCountY = 32, cellNoiseMinY = -4;
    int        firstCellX = 0, firstCellZ = 0, firstNoiseX = 0, firstNoiseZ = 0, noiseSizeXZ = 4;
    std::unordered_map<Node const*, std::vector<double>> flat;
    std::unordered_map<Node const*, std::vector<double>> corners;
};

struct Constant final : Node {
    double v;
    explicit Constant(double value) : v(value) { lo = hi = value; }
    double compute(Ctx const&) const override { return v; }
};

struct Custom final : Node {
    Density::Evaluator fn;
    double compute(Ctx const& c) const override { return fn(DensityPoint{c.x, c.y, c.z}); }
};

enum class Op2 { Add, Mul, Min, Max };

/// TwoArgumentSimpleFunction (Ap2) and MulOrAdd.
struct Ap2 final : Node {
    Op2   op;
    NodeP a, b;
    double compute(Ctx const& c) const override {
        double d0 = a->compute(c);
        switch (op) {
        case Op2::Add:
            return d0 + b->compute(c);
        case Op2::Mul:
            return d0 == 0.0 ? 0.0 : d0 * b->compute(c);
        case Op2::Min:
            return d0 < b->lo ? d0 : jmin(d0, b->compute(c));
        case Op2::Max:
            return d0 > b->hi ? d0 : jmax(d0, b->compute(c));
        }
        return 0.0;
    }
};

struct MulOrAdd final : Node {
    bool   mul;
    NodeP  input;
    double argument;
    double compute(Ctx const& c) const override {
        double d = input->compute(c);
        return mul ? d * argument : d + argument;
    }
};

NodeP makeAp2(Op2 op, NodeP a1, NodeP a2) {
    double d0 = a1->lo, d1 = a2->lo, d2 = a1->hi, d3 = a2->hi;
    double lo = 0, hi = 0;
    switch (op) {
    case Op2::Add:
        lo = d0 + d1, hi = d2 + d3;
        break;
    case Op2::Min:
        lo = jmin(d0, d1), hi = jmin(d2, d3);
        break;
    case Op2::Max:
        lo = jmax(d0, d1), hi = jmax(d2, d3);
        break;
    case Op2::Mul:
        lo = (d0 > 0.0 && d1 > 0.0) ? d0 * d1 : ((d2 < 0.0 && d3 < 0.0) ? d2 * d3 : jmin(d0 * d3, d2 * d1));
        hi = (d0 > 0.0 && d1 > 0.0) ? d2 * d3 : ((d2 < 0.0 && d3 < 0.0) ? d0 * d1 : jmax(d0 * d1, d2 * d3));
        break;
    }
    if (op == Op2::Mul || op == Op2::Add) {
        auto const* c1 = dynamic_cast<Constant const*>(a1.get());
        auto const* c2 = dynamic_cast<Constant const*>(a2.get());
        if (c1 || c2) {
            auto n      = std::make_shared<MulOrAdd>();
            n->mul      = op == Op2::Mul;
            n->input    = c1 ? a2 : a1;
            n->argument = c1 ? c1->v : c2->v;
            n->lo       = lo;
            n->hi       = hi;
            return n;
        }
    }
    auto n = std::make_shared<Ap2>();
    n->op  = op;
    n->a   = std::move(a1);
    n->b   = std::move(a2);
    n->lo  = lo;
    n->hi  = hi;
    return n;
}

enum class MapType { Abs, Square, Cube, HalfNegative, QuarterNegative, Squeeze };

double transformMapped(MapType t, double d) {
    switch (t) {
    case MapType::Abs:
        return std::fabs(d);
    case MapType::Square:
        return d * d;
    case MapType::Cube:
        return d * d * d;
    case MapType::HalfNegative:
        return d > 0.0 ? d : d * 0.5;
    case MapType::QuarterNegative:
        return d > 0.0 ? d : d * 0.25;
    case MapType::Squeeze: {
        double d0 = mth::clamp(d, -1.0, 1.0);
        return d0 / 2.0 - d0 * d0 * d0 / 24.0;
    }
    }
    return d;
}

struct Mapped final : Node {
    MapType t;
    NodeP   in;
    double  compute(Ctx const& c) const override { return transformMapped(t, in->compute(c)); }
};

NodeP makeMapped(MapType t, NodeP in) {
    auto n    = std::make_shared<Mapped>();
    double d0 = in->lo, d1 = in->hi;
    double d2 = transformMapped(t, d0), d3 = transformMapped(t, d1);
    if (t == MapType::Abs || t == MapType::Square) {
        n->lo = jmax(0.0, d0);
        n->hi = jmax(d2, d3);
    } else {
        n->lo = d2;
        n->hi = d3;
    }
    n->t  = t;
    n->in = std::move(in);
    return n;
}

struct Clamp final : Node {
    NodeP  in;
    double min, max;
    double compute(Ctx const& c) const override { return mth::clamp(in->compute(c), min, max); }
};

struct YClampedGradient final : Node {
    int    fromY, toY;
    double fromValue, toValue;
    double compute(Ctx const& c) const override {
        return mth::clampedMap(static_cast<double>(c.y), static_cast<double>(fromY), static_cast<double>(toY), fromValue, toValue);
    }
};

struct RangeChoice final : Node {
    NodeP  input, inRange, outRange;
    double minInclusive, maxExclusive;
    double compute(Ctx const& c) const override {
        double d = input->compute(c);
        return (d >= minInclusive && d < maxExclusive) ? inRange->compute(c) : outRange->compute(c);
    }
};

struct NoiseNode final : Node {
    NormalNoise noise;
    double      xzScale = 1, yScale = 1;
    double      compute(Ctx const& c) const override {
        return noise.getValue(static_cast<double>(c.x) * xzScale, static_cast<double>(c.y) * yScale, static_cast<double>(c.z) * xzScale);
    }
};

struct ShiftedNoise final : Node {
    NodeP       sx, sy, sz;
    NormalNoise noise;
    double      xzScale = 1, yScale = 1;
    double      compute(Ctx const& c) const override {
        double d0 = static_cast<double>(c.x) * xzScale + sx->compute(c);
        double d1 = static_cast<double>(c.y) * yScale + sy->compute(c);
        double d2 = static_cast<double>(c.z) * xzScale + sz->compute(c);
        return noise.getValue(d0, d1, d2);
    }
};

enum class ShiftKind { A, B, All };

struct Shift final : Node {
    ShiftKind   kind;
    NormalNoise noise;
    double      compute(Ctx const& c) const override {
        double x = c.x, y = c.y, z = c.z;
        switch (kind) {
        case ShiftKind::A:
            return noise.getValue(x * 0.25, 0.0, z * 0.25) * 4.0;
        case ShiftKind::B:
            return noise.getValue(z * 0.25, x * 0.25, 0.0) * 4.0;
        case ShiftKind::All:
            return noise.getValue(x * 0.25, y * 0.25, z * 0.25) * 4.0;
        }
        return 0.0;
    }
};

struct WeirdScaled final : Node {
    NodeP       input;
    NormalNoise noise;
    bool        type1 = true;
    static double rarity3D(double d) { return d < -0.5 ? 0.75 : (d < 0.0 ? 1.0 : (d < 0.5 ? 1.5 : 2.0)); }
    static double rarity2D(double d) {
        return d < -0.75 ? 0.5 : (d < -0.5 ? 0.75 : (d < 0.5 ? 1.0 : (d < 0.75 ? 2.0 : 3.0)));
    }
    double compute(Ctx const& c) const override {
        double d  = input->compute(c);
        double d1 = type1 ? rarity3D(d) : rarity2D(d);
        return d1 * std::fabs(noise.getValue(static_cast<double>(c.x) / d1, static_cast<double>(c.y) / d1, static_cast<double>(c.z) / d1));
    }
};

enum class MarkerKind { Interpolated, FlatCache, Cache2D, CacheOnce, CacheAllInCell };

struct Marker final : Node {
    MarkerKind kind;
    NodeP      in;

    double compute(Ctx const& c) const override {
        if (c.chunk) {
            if (kind == MarkerKind::FlatCache) return flat(c);
            if (kind == MarkerKind::Interpolated && c.blockLoop) return interpolate(c);
        }
        Ctx inner   = c;
        inner.blockLoop = false;
        if (kind == MarkerKind::Interpolated) return in->compute(inner);
        return in->compute(c);
    }

    double flat(Ctx const& c) const {
        auto& cc  = *c.chunk;
        auto  it  = cc.flat.find(this);
        int   n   = cc.noiseSizeXZ + 1;
        if (it == cc.flat.end()) {
            std::vector<double> values(static_cast<std::size_t>(n * n));
            for (int i = 0; i < n; ++i) {
                int k = mth::toBlock(cc.firstNoiseX + i);
                for (int l = 0; l < n; ++l) {
                    int j1 = mth::toBlock(cc.firstNoiseZ + l);
                    values[static_cast<std::size_t>(i * n + l)] = in->compute(Ctx{k, 0, j1, c.chunk, false});
                }
            }
            it = cc.flat.emplace(this, std::move(values)).first;
        }
        int k = mth::fromBlock(c.x) - cc.firstNoiseX;
        int l = mth::fromBlock(c.z) - cc.firstNoiseZ;
        if (k >= 0 && l >= 0 && k < n && l < n) return it->second[static_cast<std::size_t>(k * n + l)];
        return in->compute(c);
    }

    double interpolate(Ctx const& c) const {
        auto& cc = *c.chunk;
        int   nx = cc.cellCountXZ + 1, ny = cc.cellCountY + 1;
        auto  it = cc.corners.find(this);
        if (it == cc.corners.end()) {
            std::vector<double> values(static_cast<std::size_t>(nx * nx * ny));
            for (int cx = 0; cx < nx; ++cx)
                for (int cz = 0; cz < nx; ++cz)
                    for (int cy = 0; cy < ny; ++cy) {
                        Ctx corner{
                            (cc.firstCellX + cx) * cc.cellWidth,
                            (cc.cellNoiseMinY + cy) * cc.cellHeight,
                            (cc.firstCellZ + cz) * cc.cellWidth,
                            c.chunk,
                            false,
                        };
                        values[static_cast<std::size_t>((cx * nx + cz) * ny + cy)] = in->compute(corner);
                    }
            it = cc.corners.emplace(this, std::move(values)).first;
        }
        auto const& v   = it->second;
        int         lx  = c.x - cc.firstCellX * cc.cellWidth;
        int         lz  = c.z - cc.firstCellZ * cc.cellWidth;
        int         cx  = lx / cc.cellWidth, xIn = lx % cc.cellWidth;
        int         cz  = lz / cc.cellWidth, zIn = lz % cc.cellWidth;
        int         cy  = mth::floorDiv(c.y, cc.cellHeight) - cc.cellNoiseMinY;
        int         yIn = c.y - (cc.cellNoiseMinY + cy) * cc.cellHeight;
        if (cx < 0 || cz < 0 || cy < 0 || cx >= cc.cellCountXZ || cz >= cc.cellCountXZ || cy >= cc.cellCountY) {
            Ctx inner       = c;
            inner.blockLoop = false;
            return in->compute(inner);
        }
        auto at = [&](int x, int y, int z) { return v[static_cast<std::size_t>(((cx + x) * nx + (cz + z)) * ny + (cy + y))]; };
        double n000 = at(0, 0, 0), n001 = at(0, 0, 1), n100 = at(1, 0, 0), n101 = at(1, 0, 1);
        double n010 = at(0, 1, 0), n011 = at(0, 1, 1), n110 = at(1, 1, 0), n111 = at(1, 1, 1);
        double dy   = static_cast<double>(yIn) / static_cast<double>(cc.cellHeight);
        double dx   = static_cast<double>(xIn) / static_cast<double>(cc.cellWidth);
        double dz   = static_cast<double>(zIn) / static_cast<double>(cc.cellWidth);
        double xz00 = mth::lerp(dy, n000, n010);
        double xz10 = mth::lerp(dy, n100, n110);
        double xz01 = mth::lerp(dy, n001, n011);
        double xz11 = mth::lerp(dy, n101, n111);
        double z0   = mth::lerp(dx, xz00, xz10);
        double z1   = mth::lerp(dx, xz01, xz11);
        return mth::lerp(dz, z0, z1);
    }
};

// ---- CubicSpline (float precision, as in Java) ----
struct Spline {
    virtual ~Spline()                                 = default;
    [[nodiscard]] virtual float apply(Ctx const& c) const = 0;
};
using SplineP = std::shared_ptr<Spline const>;

struct SplineConstant final : Spline {
    float v;
    explicit SplineConstant(float value) : v(value) {}
    float apply(Ctx const&) const override { return v; }
};

struct SplineMultipoint final : Spline {
    NodeP                coordinate;
    std::vector<float>   locations;
    std::vector<SplineP> values;
    std::vector<float>   derivatives;

    static float linearExtend(float point, std::vector<float> const& loc, float value, std::vector<float> const& der, std::size_t index) {
        float f = der[index];
        return f == 0.0F ? value : value + f * (point - loc[index]);
    }

    float apply(Ctx const& c) const override {
        float f = static_cast<float>(coordinate->compute(c));
        int   i = mth::binarySearch(0, static_cast<int>(locations.size()), [&](int k) { return f < locations[static_cast<std::size_t>(k)]; }) - 1;
        int   j = static_cast<int>(locations.size()) - 1;
        if (i < 0) return linearExtend(f, locations, values[0]->apply(c), derivatives, 0);
        if (i == j) return linearExtend(f, locations, values[static_cast<std::size_t>(j)]->apply(c), derivatives, static_cast<std::size_t>(j));
        auto  ui = static_cast<std::size_t>(i);
        float f1 = locations[ui];
        float f2 = locations[ui + 1];
        float f3 = (f - f1) / (f2 - f1);
        float f4 = derivatives[ui];
        float f5 = derivatives[ui + 1];
        float f6 = values[ui]->apply(c);
        float f7 = values[ui + 1]->apply(c);
        float f8 = f4 * (f2 - f1) - (f7 - f6);
        float f9 = -f5 * (f2 - f1) + (f7 - f6);
        return mth::lerpf(f3, f6, f7) + f3 * (1.0F - f3) * mth::lerpf(f3, f8, f9);
    }
};

struct SplineNode final : Node {
    SplineP spline;
    double  compute(Ctx const& c) const override { return static_cast<double>(spline->apply(c)); }
};

// ---- registry of custom types ----
std::mutex& typesMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, Density::Builder>& customTypes() {
    static std::map<std::string, Density::Builder> m;
    return m;
}

std::string normalizeId(std::string id) {
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

/// Compiles density JSON for one seed (RandomState: Xoroshiro positional source, noises by id).
class Compiler {
public:
    Compiler(std::int64_t seed, Density::Resolver const& resolver)
    : mSeed(seed),
      mResolver(resolver),
      mFactory(jrand::Xoroshiro128pp(seed).forkPositional()) {}

    NodeP build(Json const& j) {
        if (j.is_number()) return std::make_shared<Constant>(j.get<double>());
        if (j.is_string()) return ref(normalizeId(j.get<std::string>()));
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return nullptr;
        std::string type = normalizeId(j["type"].get<std::string>());
        auto        arg  = [&](char const* key) -> NodeP { return j.contains(key) ? build(j[key]) : nullptr; };
        auto        num  = [&](char const* key, double def) { return j.contains(key) && j[key].is_number() ? j[key].get<double>() : def; };

        if (type == "minecraft:constant") return std::make_shared<Constant>(num("argument", 0.0));
        if (type == "minecraft:add" || type == "minecraft:mul" || type == "minecraft:min" || type == "minecraft:max") {
            auto a = arg("argument1"), b = arg("argument2");
            if (!a || !b) return nullptr;
            Op2 op = type == "minecraft:add" ? Op2::Add : type == "minecraft:mul" ? Op2::Mul : type == "minecraft:min" ? Op2::Min : Op2::Max;
            return makeAp2(op, a, b);
        }
        static std::map<std::string, MapType> const mapped{
            {"minecraft:abs", MapType::Abs},
            {"minecraft:square", MapType::Square},
            {"minecraft:cube", MapType::Cube},
            {"minecraft:half_negative", MapType::HalfNegative},
            {"minecraft:quarter_negative", MapType::QuarterNegative},
            {"minecraft:squeeze", MapType::Squeeze},
        };
        if (auto it = mapped.find(type); it != mapped.end()) {
            auto a = arg("argument");
            return a ? makeMapped(it->second, a) : nullptr;
        }
        if (type == "minecraft:clamp") {
            auto in = arg("input");
            if (!in) return nullptr;
            auto n = std::make_shared<Clamp>();
            n->in  = in;
            n->min = num("min", 0.0);
            n->max = num("max", 0.0);
            n->lo  = n->min;
            n->hi  = n->max;
            return n;
        }
        if (type == "minecraft:y_clamped_gradient") {
            auto n       = std::make_shared<YClampedGradient>();
            n->fromY     = static_cast<int>(num("from_y", 0));
            n->toY       = static_cast<int>(num("to_y", 0));
            n->fromValue = num("from_value", 0);
            n->toValue   = num("to_value", 0);
            n->lo        = jmin(n->fromValue, n->toValue);
            n->hi        = jmax(n->fromValue, n->toValue);
            return n;
        }
        if (type == "minecraft:range_choice") {
            auto n = std::make_shared<RangeChoice>();
            n->input = arg("input"), n->inRange = arg("when_in_range"), n->outRange = arg("when_out_of_range");
            if (!n->input || !n->inRange || !n->outRange) return nullptr;
            n->minInclusive = num("min_inclusive", 0);
            n->maxExclusive = num("max_exclusive", 0);
            n->lo           = jmin(n->inRange->lo, n->outRange->lo);
            n->hi           = jmax(n->inRange->hi, n->outRange->hi);
            return n;
        }
        if (type == "minecraft:noise") {
            auto noise = makeNoise(j.value("noise", Json()));
            if (!noise) return nullptr;
            auto n     = std::make_shared<NoiseNode>();
            n->noise   = *noise;
            n->xzScale = num("xz_scale", 1.0);
            n->yScale  = num("y_scale", 1.0);
            n->lo      = -noise->maxValue();
            n->hi      = noise->maxValue();
            return n;
        }
        if (type == "minecraft:shifted_noise") {
            auto noise = makeNoise(j.value("noise", Json()));
            auto n     = std::make_shared<ShiftedNoise>();
            n->sx = arg("shift_x"), n->sy = arg("shift_y"), n->sz = arg("shift_z");
            if (!noise || !n->sx || !n->sy || !n->sz) return nullptr;
            n->noise   = *noise;
            n->xzScale = num("xz_scale", 1.0);
            n->yScale  = num("y_scale", 1.0);
            n->lo      = -noise->maxValue();
            n->hi      = noise->maxValue();
            return n;
        }
        if (type == "minecraft:shift_a" || type == "minecraft:shift_b" || type == "minecraft:shift") {
            auto noise = makeNoise(j.value("argument", Json()));
            if (!noise) return nullptr;
            auto n   = std::make_shared<Shift>();
            n->kind  = type == "minecraft:shift_a" ? ShiftKind::A : type == "minecraft:shift_b" ? ShiftKind::B : ShiftKind::All;
            n->noise = *noise;
            n->lo    = -noise->maxValue() * 4.0;
            n->hi    = noise->maxValue() * 4.0;
            return n;
        }
        if (type == "minecraft:weird_scaled_sampler") {
            auto noise = makeNoise(j.value("noise", Json()));
            auto in    = arg("input");
            if (!noise || !in) return nullptr;
            auto n   = std::make_shared<WeirdScaled>();
            n->input = in;
            n->noise = *noise;
            n->type1 = j.value("rarity_value_mapper", std::string("type_1")) == "type_1";
            n->lo    = 0.0;
            n->hi    = (n->type1 ? 2.0 : 3.0) * noise->maxValue();
            return n;
        }
        static std::map<std::string, MarkerKind> const markers{
            {"minecraft:interpolated", MarkerKind::Interpolated},
            {"minecraft:flat_cache", MarkerKind::FlatCache},
            {"minecraft:cache_2d", MarkerKind::Cache2D},
            {"minecraft:cache_once", MarkerKind::CacheOnce},
            {"minecraft:cache_all_in_cell", MarkerKind::CacheAllInCell},
        };
        if (auto it = markers.find(type); it != markers.end()) {
            auto a = arg("argument");
            if (!a) return nullptr;
            auto n  = std::make_shared<Marker>();
            n->kind = it->second;
            n->in   = a;
            n->lo   = a->lo;
            n->hi   = a->hi;
            return n;
        }
        if (type == "minecraft:blend_alpha") return std::make_shared<Constant>(1.0);
        if (type == "minecraft:blend_offset" || type == "minecraft:beardifier") return std::make_shared<Constant>(0.0);
        if (type == "minecraft:blend_density") return arg("argument");
        if (type == "minecraft:spline") {
            auto s = buildSpline(j.value("spline", Json()));
            if (!s) return nullptr;
            auto n    = std::make_shared<SplineNode>();
            n->spline = s;
            return n;
        }
        Density::Builder builder;
        {
            std::lock_guard lock(typesMutex());
            if (auto it = customTypes().find(type); it != customTypes().end()) builder = it->second;
        }
        if (builder) {
            auto eval = builder(j, mSeed, mResolver);
            if (!eval) return nullptr;
            auto n = std::make_shared<Custom>();
            n->fn  = std::move(eval);
            return n;
        }
        return nullptr;
    }

private:
    NodeP ref(std::string const& id) {
        if (auto it = mRefs.find(id); it != mRefs.end()) return it->second;
        if (!mResolver.density || mBuilding.contains(id)) return nullptr;
        auto json = mResolver.density(id);
        if (!json) return nullptr;
        mBuilding.insert(id);
        auto node = build(*json);
        mBuilding.erase(id);
        if (node) mRefs[id] = node;
        return node;
    }

    std::optional<NormalNoise> makeNoise(Json const& j) {
        std::string                    key;
        std::optional<NoiseParameters> params;
        if (j.is_string()) {
            key = normalizeId(j.get<std::string>());
            if (auto it = mNoises.find(key); it != mNoises.end()) return it->second;
            if (mResolver.noise) params = mResolver.noise(key);
        } else if (j.is_object()) {
            key    = j.dump(); // inline parameters have no key in Java; any stable name will do
            params = parseNoise(j);
        }
        if (!params) return std::nullopt;
        auto src   = mFactory->fromHashOf(key);
        auto noise = NormalNoise::create(*src, *params);
        mNoises.emplace(key, noise);
        return noise;
    }

    static std::optional<NoiseParameters> parseNoise(Json const& j) {
        if (!j.is_object() || !j.contains("amplitudes")) return std::nullopt;
        NoiseParameters p;
        p.firstOctave = j.value("firstOctave", 0);
        for (auto const& a : j["amplitudes"]) p.amplitudes.push_back(a.get<double>());
        return p;
    }

    SplineP buildSpline(Json const& j) {
        if (j.is_number()) return std::make_shared<SplineConstant>(j.get<float>());
        if (!j.is_object() || !j.contains("coordinate") || !j.contains("points")) return nullptr;
        auto s        = std::make_shared<SplineMultipoint>();
        s->coordinate = build(j["coordinate"]);
        if (!s->coordinate) return nullptr;
        for (auto const& p : j["points"]) {
            auto v = buildSpline(p.value("value", Json()));
            if (!v) return nullptr;
            s->locations.push_back(p.value("location", 0.0F));
            s->values.push_back(v);
            s->derivatives.push_back(p.value("derivative", 0.0F));
        }
        if (s->locations.empty()) return nullptr;
        return s;
    }

    std::int64_t                                    mSeed;
    Density::Resolver const&                        mResolver;
    std::unique_ptr<jrand::PositionalRandomFactory> mFactory;
    std::map<std::string, NodeP>                    mRefs;
    std::set<std::string>                           mBuilding;
    std::map<std::string, NormalNoise>              mNoises;
};

} // namespace

struct Density::Impl {
    NodeP root;
};

std::optional<Density> Density::fromJson(nlohmann::json const& json, std::int64_t seed, Resolver const& resolver) {
    Compiler c(seed, resolver);
    auto     root = c.build(json);
    if (!root) return std::nullopt;
    auto impl  = std::make_shared<Impl>();
    impl->root = std::move(root);
    Density d;
    d.mImpl = std::move(impl);
    return d;
}

void Density::registerType(std::string const& typeId, Builder build) {
    std::lock_guard lock(typesMutex());
    customTypes()[normalizeId(typeId)] = std::move(build);
}

double Density::compute(DensityPoint const& p) const { return mImpl ? mImpl->root->compute(Ctx{p.blockX, p.blockY, p.blockZ}) : 0.0; }
double Density::minValue() const { return mImpl ? mImpl->root->lo : 0.0; }
double Density::maxValue() const { return mImpl ? mImpl->root->hi : 0.0; }

// ==== NoiseChunk ===================================================================================

struct NoiseChunk::Impl {
    NodeP root;
};

std::shared_ptr<NoiseChunk const>
NoiseChunk::fromJson(nlohmann::json const& json, std::int64_t seed, Density::Resolver const& resolver, NoiseShape shape) {
    Compiler c(seed, resolver);
    auto     root = c.build(json);
    if (!root) return nullptr;
    auto impl  = std::make_shared<Impl>();
    impl->root = std::move(root);
    return std::make_shared<NoiseChunk const>(std::move(impl), shape);
}

std::vector<double> NoiseChunk::fill(int chunkX, int chunkZ) const {
    ChunkCache cc;
    cc.shape         = mShape;
    cc.chunkMinX     = chunkX * 16;
    cc.chunkMinZ     = chunkZ * 16;
    cc.cellWidth     = mth::toBlock(mShape.sizeHorizontal);
    cc.cellHeight    = mth::toBlock(mShape.sizeVertical);
    cc.cellCountXZ   = 16 / cc.cellWidth;
    cc.cellCountY    = mth::floorDiv(mShape.height, cc.cellHeight);
    cc.cellNoiseMinY = mth::floorDiv(mShape.minY, cc.cellHeight);
    cc.firstCellX    = mth::floorDiv(cc.chunkMinX, cc.cellWidth);
    cc.firstCellZ    = mth::floorDiv(cc.chunkMinZ, cc.cellWidth);
    cc.firstNoiseX   = mth::fromBlock(cc.chunkMinX);
    cc.firstNoiseZ   = mth::fromBlock(cc.chunkMinZ);
    cc.noiseSizeXZ   = mth::fromBlock(cc.cellCountXZ * cc.cellWidth);

    std::vector<double> out(static_cast<std::size_t>(mShape.height) * 256);
    for (int y = 0; y < mShape.height; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                out[static_cast<std::size_t>((y * 16 + z) * 16 + x)] =
                    mImpl->root->compute(Ctx{cc.chunkMinX + x, mShape.minY + y, cc.chunkMinZ + z, &cc, true});
    return out;
}

} // namespace gf::core::noise
