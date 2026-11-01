// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/landmarks/LegacyLandmarkPlacements.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TerrainColumn.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Golden tests of the Java oracle (unit F13). tests/golden holds facts dumped from TF 4.8 running
// on a NeoForge 1.21.1 server (tools/oracle/run.ps1); these tests
//   - check the manifest (every file listed, unchanged since it was generated, from the pinned TF),
//   - check every file's structure and internal consistency,
//   - recompute what is small enough to recompute independently: Minecraft's random sources
//     (java.util.Random LCG, Xoroshiro128++, WorldgenRandom seeding, Mth#getSeed,
//     String#hashCode), TF's landmark grid (getNearestCenterXZ, chunkHasLandmarkCenter,
//     manhattanDistanceFromLandmarkCenter, pickVarietyLandmark, pickLandmarkForChunk) and TF's
//     biome column layers (TerrainColumn#getBiome) - so the goldens are checked against the
//     algorithms, and the reference code below documents them for the units that port them
//     (F5 jrand, F6 landmark, U1 biomes).

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/VolumeHash.h"

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr char const* kTfVersion = "4.8.3802";
constexpr char const* kTfCommit  = "9e40a75721c716b2fdf80c16d1058fe718825e9f";

// ---------------------------------------------------------------------------------------------
// files

// tests/golden: $GF_GOLDEN_DIR, this source file's directory, or found walking up from the
// working directory (xmake runs the tests from the build directory).
fs::path goldenDir() {
    static fs::path const dir = [] {
        if (char const* env = std::getenv("GF_GOLDEN_DIR"); env && *env) return fs::path(env);
        std::error_code ec;
        fs::path here = fs::path(__FILE__).parent_path();
        if (here.is_absolute() && fs::exists(here / "manifest.json", ec)) return here;
        for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
            if (fs::exists(p / "tests/golden/manifest.json", ec)) return p / "tests/golden";
            if (!here.is_absolute() && fs::exists(p / here / "manifest.json", ec)) return p / here;
            if (p == p.root_path()) break;
        }
        return fs::path();
    }();
    return dir;
}

std::string readFile(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

json const& golden(std::string const& rel) {
    static std::map<std::string, json> cache;
    auto it = cache.find(rel);
    if (it == cache.end()) {
        fs::path p = goldenDir() / rel;
        json j = fs::exists(p) ? json::parse(readFile(p), nullptr, false) : json();
        it = cache.emplace(rel, std::move(j)).first;
    }
    return it->second;
}

json const& manifest() { return golden("manifest.json"); }

std::vector<std::string> filesOfKind(std::string const& kind) {
    std::vector<std::string> out;
    for (auto const& [rel, e] : manifest().at("files").items()) {
        if (e.value("kind", "") == kind) out.push_back(rel);
    }
    return out;
}

#define REQUIRE_GOLDENS()                                                                                              \
    do {                                                                                                               \
        ASSERT_FALSE(goldenDir().empty()) << "tests/golden not found (set GF_GOLDEN_DIR)";                            \
        ASSERT_TRUE(manifest().is_object()) << "tests/golden/manifest.json missing or not JSON";                     \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Java integer semantics

constexpr std::int64_t wrap(std::uint64_t v) { return static_cast<std::int64_t>(v); }
constexpr std::int64_t mul(std::int64_t a, std::int64_t b) { return wrap(std::uint64_t(a) * std::uint64_t(b)); }
constexpr std::int64_t add(std::int64_t a, std::int64_t b) { return wrap(std::uint64_t(a) + std::uint64_t(b)); }
constexpr std::int32_t imul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b)); }
constexpr std::int64_t ushr(std::int64_t v, int n) { return wrap(std::uint64_t(v) >> n); }

// Java's String#hashCode over the UTF-16 code units of UTF-8 text.
std::int32_t javaStringHash(std::string const& utf8) {
    std::uint32_t h = 0;
    auto unit       = [&](std::uint32_t u) { h = h * 31u + u; };
    for (std::size_t i = 0; i < utf8.size();) {
        auto          c  = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = c;
        int           n  = 0;
        if (c >= 0xF0) cp = c & 0x07, n = 3;
        else if (c >= 0xE0) cp = c & 0x0F, n = 2;
        else if (c >= 0xC0) cp = c & 0x1F, n = 1;
        for (int k = 1; k <= n && i + k < utf8.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        i += n + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit(0xD800 + (cp >> 10));
            unit(0xDC00 + (cp & 0x3FF));
        } else {
            unit(cp);
        }
    }
    return static_cast<std::int32_t>(h);
}

// net.minecraft.util.Mth#getSeed
std::int64_t mthGetSeed(std::int32_t x, std::int32_t y, std::int32_t z) {
    std::int64_t l = std::int64_t(imul(x, 3129871)) ^ mul(z, 116129781LL) ^ std::int64_t(y);
    l              = add(mul(mul(l, l), 42317861LL), mul(l, 11LL));
    return l >> 16;
}

// ---------------------------------------------------------------------------------------------
// Minecraft 1.21.1 random sources (reference implementations checked against the goldens)

constexpr std::int64_t kGolden = -7046029254386353131LL; // RandomSupport.GOLDEN_RATIO_64
constexpr std::int64_t kSilver = 7640891576956012809LL;  // RandomSupport.SILVER_RATIO_64

std::int64_t mixStafford13(std::int64_t z) {
    z = mul(z ^ ushr(z, 30), -4658895280553007687LL);
    z = mul(z ^ ushr(z, 27), -7723592293110705685LL);
    return z ^ ushr(z, 31);
}

struct Seed128 {
    std::int64_t lo, hi;
};
Seed128 upgradeUnmixed(std::int64_t seed) {
    std::int64_t lo = seed ^ kSilver;
    return {lo, add(lo, kGolden)};
}
Seed128 upgrade(std::int64_t seed) {
    Seed128 s = upgradeUnmixed(seed);
    return {mixStafford13(s.lo), mixStafford13(s.hi)};
}

class Rng {
public:
    virtual ~Rng()                         = default;
    virtual void         setSeed(std::int64_t seed) = 0;
    virtual std::int32_t nextInt()                  = 0;
    virtual std::int32_t nextInt(std::int32_t bound) = 0;
    virtual std::int64_t nextLong()                 = 0;
    virtual bool         nextBoolean()              = 0;
    virtual float        nextFloat()                = 0;
    virtual double       nextDouble()               = 0;
    virtual void         consumeCount(int n) {
        for (int i = 0; i < n; ++i) (void)nextInt();
    }
    // MarsagliaPolarGaussian (Math.log / Math.sqrt)
    double nextGaussian() {
        if (mHaveNext) {
            mHaveNext = false;
            return mNext;
        }
        while (true) {
            double d = 2.0 * nextDouble() - 1.0;
            double e = 2.0 * nextDouble() - 1.0;
            double f = d * d + e * e;
            if (f < 1.0 && f != 0.0) {
                double g  = std::sqrt(-2.0 * std::log(f) / f);
                mNext     = e * g;
                mHaveNext = true;
                return d * g;
            }
        }
    }

protected:
    void resetGaussian() { mHaveNext = false; }

private:
    double mNext     = 0;
    bool   mHaveNext = false;
};

constexpr float  kFloatUnit  = 5.9604645E-8F;
constexpr double kDoubleUnit = static_cast<double>(1.110223E-16F); // a float constant in Minecraft

// BitRandomSource on top of next(bits)
class BitRng : public Rng {
public:
    virtual std::int32_t next(int bits) = 0;
    std::int32_t         nextInt() override { return next(32); }
    std::int32_t         nextInt(std::int32_t bound) override {
        if ((bound & (bound - 1)) == 0) return static_cast<std::int32_t>((std::int64_t(bound) * next(31)) >> 31);
        std::int32_t i, j;
        do {
            i = next(31);
            j = i % bound;
        } while (static_cast<std::int32_t>(std::uint32_t(i) - std::uint32_t(j) + std::uint32_t(bound - 1)) < 0);
        return j;
    }
    std::int64_t nextLong() override {
        std::int32_t i = next(32);
        std::int32_t j = next(32);
        return add(wrap(std::uint64_t(std::int64_t(i)) << 32), j);
    }
    bool   nextBoolean() override { return next(1) != 0; }
    float  nextFloat() override { return static_cast<float>(next(24)) * kFloatUnit; }
    double nextDouble() override {
        std::int32_t i = next(26);
        std::int32_t j = next(27);
        return static_cast<double>((std::int64_t(i) << 27) + j) * kDoubleUnit;
    }
};

class Legacy final : public BitRng {
public:
    explicit Legacy(std::int64_t seed) { setSeed(seed); }
    void setSeed(std::int64_t seed) override {
        mSeed = (std::uint64_t(seed) ^ 0x5DEECE66DULL) & kMask;
        resetGaussian();
    }
    std::int32_t next(int bits) override {
        mSeed = (mSeed * 0x5DEECE66DULL + 0xBULL) & kMask;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(mSeed >> (48 - bits)));
    }

private:
    static constexpr std::uint64_t kMask = (1ULL << 48) - 1;
    std::uint64_t                  mSeed = 0;
};

class Xoroshiro final : public Rng {
public:
    explicit Xoroshiro(std::int64_t seed) { setSeed(seed); }
    Xoroshiro(std::int64_t lo, std::int64_t hi) { set(lo, hi); }
    void setSeed(std::int64_t seed) override {
        Seed128 s = upgrade(seed);
        set(s.lo, s.hi);
        resetGaussian();
    }
    std::int32_t nextInt() override { return static_cast<std::int32_t>(nextLong()); }
    std::int32_t nextInt(std::int32_t bound) override {
        std::uint64_t l = std::uint32_t(nextInt());
        std::uint64_t m = l * std::uint64_t(bound);
        std::uint64_t n = m & 0xFFFFFFFFULL;
        if (n < std::uint64_t(bound)) {
            std::uint32_t i = (0u - std::uint32_t(bound)) % std::uint32_t(bound);
            while (n < i) {
                l = std::uint32_t(nextInt());
                m = l * std::uint64_t(bound);
                n = m & 0xFFFFFFFFULL;
            }
        }
        return static_cast<std::int32_t>(m >> 32);
    }
    std::int64_t nextLong() override {
        std::uint64_t l = mLo, m = mHi;
        std::uint64_t n = std::rotl(l + m, 17) + l;
        m ^= l;
        mLo = std::rotl(l, 49) ^ m ^ (m << 21);
        mHi = std::rotl(m, 28);
        return wrap(n);
    }
    bool   nextBoolean() override { return (nextLong() & 1) != 0; }
    float  nextFloat() override { return static_cast<float>(bits(24)) * kFloatUnit; }
    double nextDouble() override { return static_cast<double>(bits(53)) * kDoubleUnit; }
    void   consumeCount(int n) override {
        for (int i = 0; i < n; ++i) (void)nextLong();
    }

private:
    std::int64_t bits(int n) { return wrap(std::uint64_t(nextLong()) >> (64 - n)); }
    void         set(std::int64_t lo, std::int64_t hi) {
        mLo = std::uint64_t(lo);
        mHi = std::uint64_t(hi);
        if ((mLo | mHi) == 0) mLo = std::uint64_t(kGolden), mHi = std::uint64_t(kSilver);
    }
    std::uint64_t mLo = 0, mHi = 0;
};

// WorldgenRandom over a legacy or xoroshiro source
class Worldgen final : public BitRng {
public:
    explicit Worldgen(bool legacy) : mLegacy(legacy) {
        if (legacy) mSource = std::make_unique<Legacy>(0);
        else mSource = std::make_unique<Xoroshiro>(0);
    }
    void         setSeed(std::int64_t seed) override { mSource->setSeed(seed); }
    std::int32_t next(int bits) override {
        ++mCount;
        if (mLegacy) return static_cast<Legacy&>(*mSource).next(bits);
        return static_cast<std::int32_t>(std::uint64_t(mSource->nextLong()) >> (64 - bits));
    }
    std::int64_t setDecorationSeed(std::int64_t levelSeed, std::int32_t minX, std::int32_t minZ) {
        setSeed(levelSeed);
        std::int64_t l = nextLong() | 1;
        std::int64_t m = nextLong() | 1;
        std::int64_t n = add(mul(minX, l), mul(minZ, m)) ^ levelSeed;
        setSeed(n);
        return n;
    }
    void setFeatureSeed(std::int64_t decorationSeed, std::int32_t index, std::int32_t step) {
        setSeed(add(add(decorationSeed, index), imul(10000, step)));
    }
    void setLargeFeatureSeed(std::int64_t base, std::int32_t chunkX, std::int32_t chunkZ) {
        setSeed(base);
        std::int64_t l = nextLong();
        std::int64_t m = nextLong();
        setSeed(mul(chunkX, l) ^ mul(chunkZ, m) ^ base);
    }
    void setLargeFeatureWithSalt(std::int64_t levelSeed, std::int32_t rx, std::int32_t rz, std::int32_t salt) {
        setSeed(add(add(add(mul(rx, 341873128712LL), mul(rz, 132897987541LL)), levelSeed), salt));
    }
    [[nodiscard]] int count() const { return mCount; }

private:
    bool                 mLegacy;
    std::unique_ptr<Rng> mSource;
    int                  mCount = 0;
};

Legacy slimeChunk(std::int32_t cx, std::int32_t cz, std::int64_t levelSeed, std::int64_t salt) {
    std::int64_t s = add(add(add(add(levelSeed, imul(imul(cx, cx), 4987142)), imul(cx, 5947611)), mul(imul(cz, cz), 4392871LL)),
                         imul(cz, 389711));
    return Legacy(s ^ salt);
}

std::unique_ptr<Rng> makeSource(json const& e, bool legacy) {
    if (legacy) return std::make_unique<Legacy>(e.at("seed").get<std::int64_t>());
    if (e.contains("seed")) return std::make_unique<Xoroshiro>(e.at("seed").get<std::int64_t>());
    return std::make_unique<Xoroshiro>(e.at("seedLo").get<std::int64_t>(), e.at("seedHi").get<std::int64_t>());
}

// Gaussians go through Math.log, which may differ from std::log in the last place.
bool closeUlps(double a, double b, std::int64_t ulps = 4) {
    if (a == b) return true;
    if (std::signbit(a) != std::signbit(b)) return false;
    auto ia = std::bit_cast<std::int64_t>(a), ib = std::bit_cast<std::int64_t>(b);
    return std::llabs(ia - ib) <= ulps;
}

// rng.json "mixed": nextInt(10), nextGaussian, nextDouble, nextGaussian, nextGaussian, nextLong,
// nextFloat, nextBoolean, nextInt (the cached second Gaussian survives the calls in between)
void checkMixed(Rng& r, json const& e) {
    json const& v = e.at("mixed");
    ASSERT_EQ(v.size(), 9u);
    EXPECT_EQ(r.nextInt(10), v.at(0).get<std::int32_t>());
    EXPECT_TRUE(closeUlps(r.nextGaussian(), v.at(1).get<double>()));
    EXPECT_EQ(r.nextDouble(), v.at(2).get<double>());
    EXPECT_TRUE(closeUlps(r.nextGaussian(), v.at(3).get<double>()));
    EXPECT_TRUE(closeUlps(r.nextGaussian(), v.at(4).get<double>()));
    EXPECT_EQ(r.nextLong(), v.at(5).get<std::int64_t>());
    EXPECT_EQ(r.nextFloat(), static_cast<float>(v.at(6).get<double>()));
    EXPECT_EQ(r.nextBoolean(), v.at(7).get<bool>());
    EXPECT_EQ(r.nextInt(), v.at(8).get<std::int32_t>());
}

// rng.json "nextIntBetweenInclusiveM5To5": RandomSource#nextIntBetweenInclusive(-5, 5)
void checkBetweenInclusive(Rng& r, json const& e) {
    for (json const& v : e.at("nextIntBetweenInclusiveM5To5")) EXPECT_EQ(r.nextInt(5 - (-5) + 1) + (-5), v.get<std::int32_t>());
}

// ---------------------------------------------------------------------------------------------
// TF's LegacyLandmarkPlacements (translated)

struct Center {
    std::int32_t x, z;
};

Center nearestCenter(std::int32_t chunkX, std::int32_t chunkZ) {
    std::int32_t regionX = (chunkX + 8) >> 4;
    std::int32_t regionZ = (chunkZ + 8) >> 4;
    std::int64_t seed    = mul(regionX, 3129871LL) ^ mul(regionZ, 116129781LL);
    seed                 = add(mul(mul(seed, seed), 42317861LL), mul(seed, 7LL));
    auto num0            = static_cast<std::int32_t>((seed >> 12) & 3);
    auto num1            = static_cast<std::int32_t>((seed >> 15) & 3);
    auto num2            = static_cast<std::int32_t>((seed >> 18) & 3);
    auto num3            = static_cast<std::int32_t>((seed >> 21) & 3);
    std::int32_t centerX = 8 + num0 - num1;
    std::int32_t centerZ = 8 + num2 - num3;
    std::int32_t ccz = regionZ >= 0 ? (regionZ * 16 + centerZ - 8) * 16 + 8 : (regionZ * 16 + (16 - centerZ) - 8) * 16 + 9;
    std::int32_t ccx = regionX >= 0 ? (regionX * 16 + centerX - 8) * 16 + 8 : (regionX * 16 + (16 - centerX) - 8) * 16 + 9;
    return {ccx, ccz};
}

bool chunkHasCenter(std::int32_t chunkX, std::int32_t chunkZ) {
    Center c = nearestCenter(chunkX, chunkZ);
    return chunkX == c.x >> 4 && chunkZ == c.z >> 4;
}

std::int32_t manhattan(std::int32_t chunkX, std::int32_t chunkZ) {
    Center c = nearestCenter(chunkX, chunkZ);
    return std::abs(chunkX - (c.x >> 4)) + std::abs(chunkZ - (c.z >> 4));
}

// Java's Math.round(float) (ties towards positive infinity)
std::int32_t javaRound(float f) { return static_cast<std::int32_t>(std::floor(static_cast<double>(f) + 0.5)); }

std::string pickVariety(std::int64_t seed, std::int32_t chunkX, std::int32_t chunkZ, std::vector<std::pair<std::string, int>> const& weights) {
    chunkX = javaRound(static_cast<float>(chunkX) / 16.0F) * 16;
    chunkZ = javaRound(static_cast<float>(chunkZ) / 16.0F) * 16;
    std::int32_t ox = std::abs(((chunkX + 64) >> 4) % 8);
    std::int32_t oz = std::abs(((chunkZ + 64) >> 4) % 8);
    if ((ox == 4 && oz == 5) || (ox == 4 && oz == 3)) return "twilightforest:lich_tower";
    if ((ox == 5 && oz == 4) || (ox == 3 && oz == 4)) return "twilightforest:naga_courtyard";
    int total = 0;
    for (auto const& w : weights) total += w.second;
    Legacy       random(add(add(seed, mul(chunkX, 25117LL)), mul(chunkZ, 151121LL)));
    std::int32_t pick = random.nextInt(total);
    for (auto const& w : weights) {
        pick -= w.second;
        if (pick < 0) return w.first;
    }
    return "twilightforest:small_hollow_hill";
}

// LegacyLandmarkPlacements.BIOME_2_STRUCTURES
std::map<std::string, std::string> const& biomeLandmarks() {
    static std::map<std::string, std::string> const m = {
        {"twilightforest:enchanted_forest", "twilightforest:quest_grove"},
        {"twilightforest:lake", "twilightforest:quest_island"},
        {"twilightforest:swamp", "twilightforest:labyrinth"},
        {"twilightforest:fire_swamp", "twilightforest:hydra_lair"},
        {"twilightforest:dark_forest", "twilightforest:knight_stronghold"},
        {"twilightforest:dark_forest_center", "twilightforest:dark_tower"},
        {"twilightforest:snowy_forest", "twilightforest:yeti_cave"},
        {"twilightforest:glacier", "twilightforest:aurora_palace"},
        {"twilightforest:highlands", "twilightforest:troll_cave"},
        {"twilightforest:final_plateau", "twilightforest:final_castle"},
    };
    return m;
}

// owners.tsv ids of one kind (empty when docs/spec/owners.tsv is not next to tests/golden)
std::set<std::string> ownedIds(std::string const& kind) {
    std::set<std::string> out;
    fs::path              p = goldenDir().parent_path().parent_path() / "docs/spec/owners.tsv";
    std::ifstream         in(p);
    std::string           line;
    while (std::getline(in, line)) {
        auto t1 = line.find('\t');
        if (t1 == std::string::npos || line.compare(0, t1, kind) != 0 || t1 != kind.size()) continue;
        auto t2 = line.find('\t', t1 + 1);
        out.insert(line.substr(t1 + 1, t2 - t1 - 1));
    }
    return out;
}

} // namespace

// =============================================================================================
// manifest and headers

TEST(OracleGolden, ManifestListsEveryFileUnchanged) {
    REQUIRE_GOLDENS();
    json const& m = manifest();
    EXPECT_EQ(m.value("format", 0), 1);
    EXPECT_EQ(m.at("source").value("twilightforest", ""), kTfVersion);
    EXPECT_EQ(m.at("source").value("commit", ""), kTfCommit);
    EXPECT_EQ(m.at("source").value("minecraft", ""), "1.21.1");

    std::set<std::string> listed;
    for (auto const& [rel, e] : m.at("files").items()) {
        listed.insert(rel);
        fs::path p = goldenDir() / rel;
        ASSERT_TRUE(fs::exists(p)) << rel;
        std::string bytes = readFile(p);
        EXPECT_EQ(static_cast<std::int64_t>(bytes.size()), e.at("bytes").get<std::int64_t>()) << rel;
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(gf::core::fnv1a64(bytes)));
        EXPECT_EQ(std::string(hex), e.at("fnv1a64").get<std::string>()) << rel << " changed after it was generated: rerun tools/oracle/run.ps1";

        json const& g = golden(rel);
        ASSERT_TRUE(g.is_object()) << rel;
        EXPECT_EQ(g.value("format", 0), 1) << rel;
        EXPECT_EQ(g.value("kind", ""), e.value("kind", "?")) << rel;
        EXPECT_EQ(g.at("source").value("twilightforest", ""), kTfVersion) << rel;
        EXPECT_EQ(g.at("source").value("commit", ""), kTfCommit) << rel;
        if (e.contains("seed")) EXPECT_EQ(g.at("seed").get<std::int64_t>(), e.at("seed").get<std::int64_t>()) << rel;
    }
    for (auto const& f : fs::recursive_directory_iterator(goldenDir())) {
        if (!f.is_regular_file() || f.path().extension() != ".json") continue;
        std::string rel = fs::relative(f.path(), goldenDir()).generic_string();
        if (rel == "manifest.json") continue;
        EXPECT_TRUE(listed.contains(rel)) << rel << " is not in tests/golden/manifest.json";
    }
    // every kind, and every seed for the seed-dependent ones
    for (char const* kind : {"rng", "noise", "mobs", "loot", "recipes", "uncrafting"}) EXPECT_EQ(filesOfKind(kind).size(), 1u) << kind;
    for (char const* kind : {"biomes", "heights", "density", "landmarks", "surface"}) {
        EXPECT_EQ(filesOfKind(kind).size(), m.at("seeds").size()) << kind;
    }
}

// =============================================================================================
// rng

TEST(OracleGolden, RngLegacyMatchesJavaUtilRandom) {
    REQUIRE_GOLDENS();
    json const& g = golden("rng.json");
    ASSERT_TRUE(g.contains("legacy"));
    for (json const& e : g.at("legacy")) {
        std::int64_t seed = e.at("seed");
        SCOPED_TRACE("legacy seed " + std::to_string(seed));
        {
            Legacy r(seed);
            for (json const& v : e.at("nextInt")) EXPECT_EQ(r.nextInt(), v.get<std::int32_t>());
        }
        for (json const& b : e.at("nextIntBound")) {
            Legacy r(seed);
            for (json const& v : b.at("values")) EXPECT_EQ(r.nextInt(b.at("bound").get<std::int32_t>()), v.get<std::int32_t>());
        }
        {
            Legacy r(seed);
            for (json const& v : e.at("nextLong")) EXPECT_EQ(r.nextLong(), v.get<std::int64_t>());
        }
        {
            Legacy r(seed);
            for (json const& v : e.at("nextBoolean")) EXPECT_EQ(r.nextBoolean(), v.get<bool>());
        }
        {
            Legacy r(seed);
            for (json const& v : e.at("nextFloat")) EXPECT_EQ(r.nextFloat(), static_cast<float>(v.get<double>()));
        }
        {
            Legacy r(seed);
            for (json const& v : e.at("nextDouble")) EXPECT_EQ(r.nextDouble(), v.get<double>());
        }
        {
            Legacy r(seed);
            for (json const& v : e.at("nextGaussian")) EXPECT_TRUE(closeUlps(r.nextGaussian(), v.get<double>()));
        }
        {
            Legacy r(seed);
            checkMixed(r, e);
        }
        {
            Legacy r(seed);
            checkBetweenInclusive(r, e);
        }
        for (json const& b : e.at("next")) {
            Legacy r(seed);
            for (json const& v : b.at("values")) EXPECT_EQ(r.next(b.at("bits").get<int>()), v.get<std::int32_t>());
        }
        {
            Legacy r(seed);
            r.consumeCount(5);
            for (json const& v : e.at("consumeCount5ThenNextLong")) EXPECT_EQ(r.nextLong(), v.get<std::int64_t>());
        }
        {
            Legacy r(seed);
            Legacy fork(r.nextLong());
            for (json const& v : e.at("forkNextLong")) EXPECT_EQ(fork.nextLong(), v.get<std::int64_t>());
            for (json const& v : e.at("parentAfterForkNextLong")) EXPECT_EQ(r.nextLong(), v.get<std::int64_t>());
        }
        {
            Legacy       r(seed);
            std::int64_t pseed = r.nextLong(); // LegacyPositionalRandomFactory
            json const&  pos   = e.at("positional");
            for (json const& a : pos.at("at")) {
                Legacy pr(mthGetSeed(a.at("x"), a.at("y"), a.at("z")) ^ pseed);
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
                EXPECT_EQ(pr.nextInt(16), a.at("nextInt16").get<std::int32_t>());
            }
            for (json const& a : pos.at("fromHashOf")) {
                Legacy pr(std::int64_t(javaStringHash(a.at("name").get<std::string>())) ^ pseed);
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
            }
            for (json const& a : pos.at("fromSeed")) {
                Legacy pr(a.at("seed").get<std::int64_t>());
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
            }
            EXPECT_EQ(r.nextLong(), pos.at("parentAfterForkNextLong").get<std::int64_t>());
        }
    }
}

TEST(OracleGolden, RngXoroshiroMatchesXoroshiro128pp) {
    REQUIRE_GOLDENS();
    json const& g = golden("rng.json");
    for (json const& e : g.at("randomSupport")) {
        std::int64_t seed = e.at("seed");
        EXPECT_EQ(mixStafford13(seed), e.at("mixStafford13").get<std::int64_t>());
        EXPECT_EQ(upgrade(seed).lo, e.at("upgradeLo").get<std::int64_t>());
        EXPECT_EQ(upgrade(seed).hi, e.at("upgradeHi").get<std::int64_t>());
        EXPECT_EQ(upgradeUnmixed(seed).lo, e.at("unmixedLo").get<std::int64_t>());
        EXPECT_EQ(upgradeUnmixed(seed).hi, e.at("unmixedHi").get<std::int64_t>());
    }
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> md5;
    for (json const& h : g.at("javaStringHash")) md5[h.at("text").get<std::string>()] = {h.at("md5Lo").get<std::int64_t>(), h.at("md5Hi").get<std::int64_t>()};

    for (json const& e : g.at("xoroshiro")) {
        SCOPED_TRACE("xoroshiro " + e.dump().substr(0, 60));
        { auto r = makeSource(e, false); for (json const& v : e.at("nextInt")) EXPECT_EQ(r->nextInt(), v.get<std::int32_t>()); }
        for (json const& b : e.at("nextIntBound")) {
            auto r = makeSource(e, false);
            for (json const& v : b.at("values")) EXPECT_EQ(r->nextInt(b.at("bound").get<std::int32_t>()), v.get<std::int32_t>());
        }
        { auto r = makeSource(e, false); for (json const& v : e.at("nextLong")) EXPECT_EQ(r->nextLong(), v.get<std::int64_t>()); }
        { auto r = makeSource(e, false); for (json const& v : e.at("nextBoolean")) EXPECT_EQ(r->nextBoolean(), v.get<bool>()); }
        { auto r = makeSource(e, false); for (json const& v : e.at("nextFloat")) EXPECT_EQ(r->nextFloat(), static_cast<float>(v.get<double>())); }
        { auto r = makeSource(e, false); for (json const& v : e.at("nextDouble")) EXPECT_EQ(r->nextDouble(), v.get<double>()); }
        { auto r = makeSource(e, false); for (json const& v : e.at("nextGaussian")) EXPECT_TRUE(closeUlps(r->nextGaussian(), v.get<double>())); }
        { auto r = makeSource(e, false); checkMixed(*r, e); }
        { auto r = makeSource(e, false); checkBetweenInclusive(*r, e); }
        {
            auto r = makeSource(e, false);
            r->consumeCount(5);
            for (json const& v : e.at("consumeCount5ThenNextLong")) EXPECT_EQ(r->nextLong(), v.get<std::int64_t>());
        }
        {
            auto         r  = makeSource(e, false);
            std::int64_t lo = r->nextLong(), hi = r->nextLong();
            Xoroshiro    fork(lo, hi);
            for (json const& v : e.at("forkNextLong")) EXPECT_EQ(fork.nextLong(), v.get<std::int64_t>());
            for (json const& v : e.at("parentAfterForkNextLong")) EXPECT_EQ(r->nextLong(), v.get<std::int64_t>());
        }
        {
            auto         r  = makeSource(e, false);
            std::int64_t lo = r->nextLong(), hi = r->nextLong(); // XoroshiroPositionalRandomFactory
            json const&  pos = e.at("positional");
            for (json const& a : pos.at("at")) {
                Xoroshiro pr(mthGetSeed(a.at("x"), a.at("y"), a.at("z")) ^ lo, hi);
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
                EXPECT_EQ(pr.nextInt(16), a.at("nextInt16").get<std::int32_t>());
            }
            for (json const& a : pos.at("fromHashOf")) {
                auto [mlo, mhi] = md5.at(a.at("name").get<std::string>());
                Xoroshiro pr(mlo ^ lo, mhi ^ hi);
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
            }
            for (json const& a : pos.at("fromSeed")) {
                std::int64_t s = a.at("seed");
                Xoroshiro    pr(s ^ lo, s ^ hi);
                EXPECT_EQ(pr.nextLong(), a.at("nextLong").get<std::int64_t>());
            }
            EXPECT_EQ(r->nextLong(), pos.at("parentAfterForkNextLong").get<std::int64_t>());
        }
    }
}

TEST(OracleGolden, RngWorldgenSeeding) {
    REQUIRE_GOLDENS();
    json const& g = golden("rng.json");
    for (json const& e : g.at("worldgen")) {
        bool         legacy    = e.at("source") == "legacy";
        std::int64_t levelSeed = e.at("levelSeed");
        SCOPED_TRACE(e.at("source").get<std::string>() + " " + std::to_string(levelSeed));
        for (json const& d : e.at("decoration")) {
            Worldgen     wr(legacy);
            std::int64_t ds = wr.setDecorationSeed(levelSeed, d.at("minBlockX"), d.at("minBlockZ"));
            EXPECT_EQ(ds, d.at("decorationSeed").get<std::int64_t>());
            for (json const& v : d.at("thenNextLong")) EXPECT_EQ(wr.nextLong(), v.get<std::int64_t>());
            for (json const& f : d.at("features")) {
                Worldgen fr(legacy);
                fr.setFeatureSeed(ds, f.at("index"), f.at("step"));
                for (json const& v : f.at("nextInt16")) EXPECT_EQ(fr.nextInt(16), v.get<std::int32_t>());
                EXPECT_EQ(fr.count(), f.at("count").get<int>());
            }
        }
        for (json const& l : e.at("largeFeature")) {
            Worldgen wr(legacy);
            wr.setLargeFeatureSeed(levelSeed, l.at("chunkX"), l.at("chunkZ"));
            for (json const& v : l.at("nextInt16")) EXPECT_EQ(wr.nextInt(16), v.get<std::int32_t>());
            EXPECT_EQ(wr.nextLong(), l.at("nextLong").get<std::int64_t>());
        }
        for (json const& l : e.at("largeFeatureWithSalt")) {
            Worldgen wr(legacy);
            wr.setLargeFeatureWithSalt(levelSeed, l.at("regionX"), l.at("regionZ"), l.at("salt"));
            for (json const& v : l.at("nextInt16")) EXPECT_EQ(wr.nextInt(16), v.get<std::int32_t>());
        }
    }
    for (json const& s : g.at("slimeChunk")) {
        Legacy r = slimeChunk(s.at("chunkX"), s.at("chunkZ"), s.at("levelSeed"), s.at("salt"));
        for (json const& v : s.at("nextInt10")) EXPECT_EQ(r.nextInt(10), v.get<std::int32_t>());
    }
    for (json const& h : g.at("javaStringHash")) EXPECT_EQ(javaStringHash(h.at("text").get<std::string>()), h.at("hash").get<std::int32_t>()) << h.at("text").get<std::string>();
    for (json const& s : g.at("mthGetSeed")) EXPECT_EQ(mthGetSeed(s.at("x"), s.at("y"), s.at("z")), s.at("seed").get<std::int64_t>());
}

// =============================================================================================
// noise

TEST(OracleGolden, NoiseIsWellFormed) {
    REQUIRE_GOLDENS();
    json const& g  = golden("noise.json");
    std::size_t n3 = g.at("points3").size(), n2 = g.at("points2").size();
    ASSERT_GT(n3, 0u);
    for (json const& e : g.at("improved")) {
        EXPECT_EQ(e.at("values").size(), n3);
        for (json const& v : e.at("values")) EXPECT_LE(std::abs(v.get<double>()), 1.1);
        for (json const& s : e.at("yScaled")) EXPECT_EQ(s.at("values").size(), n3);
    }
    for (json const& e : g.at("simplex")) {
        EXPECT_EQ(e.at("values2").size(), n2);
        EXPECT_EQ(e.at("values3").size(), n3);
    }
    for (char const* key : {"perlin", "perlinLegacyNetherBiome", "normal", "normalLegacyNetherBiome"}) {
        ASSERT_FALSE(g.at(key).empty()) << key;
        for (json const& e : g.at(key)) {
            ASSERT_EQ(e.at("values").size(), n3) << key;
            double max = e.at("maxValue");
            EXPECT_GT(max, 0.0) << key;
            // NormalNoise#maxValue bounds its values; PerlinNoise's maxValue bounds them too
            for (json const& v : e.at("values")) EXPECT_LE(std::abs(v.get<double>()), max + 1e-9) << key;
        }
    }
}

// =============================================================================================
// biomes

TEST(OracleGolden, BiomeGridsAndColumnsAgree) {
    REQUIRE_GOLDENS();
    for (std::string const& rel : filesOfKind("biomes")) {
        SCOPED_TRACE(rel);
        json const&              g       = golden(rel);
        std::vector<std::string> palette = g.at("palette");
        std::string              chars   = g.at("paletteChars");
        ASSERT_EQ(chars.size(), palette.size());
        auto biomeAt = [&](json const& grid, int i, int j) -> std::string {
            char c = grid.at("rows").at(j).get<std::string>().at(i);
            return palette.at(chars.find(c));
        };
        json const& mainGrid = g.at("main");
        json const& wide = g.at("wide");
        for (json const* grid : {&mainGrid, &wide}) {
            ASSERT_EQ(grid->at("rows").size(), grid->at("height").get<std::size_t>());
            for (json const& row : grid->at("rows")) {
                ASSERT_EQ(row.get<std::string>().size(), grid->at("width").get<std::size_t>());
                for (char c : row.get<std::string>()) ASSERT_NE(chars.find(c), std::string::npos);
            }
        }
        int mx0 = mainGrid.at("x0"), mz0 = mainGrid.at("z0"), mw = mainGrid.at("width"), mh = mainGrid.at("height"), ms = mainGrid.at("step");
        auto mainAt = [&](int qx, int qz) -> std::optional<std::string> {
            if ((qx - mx0) % ms != 0 || (qz - mz0) % ms != 0) return std::nullopt;
            int i = (qx - mx0) / ms, j = (qz - mz0) / ms;
            if (qx < mx0 || qz < mz0 || i >= mw || j >= mh) return std::nullopt;
            return biomeAt(mainGrid, i, j);
        };
        // the wide grid agrees with the main grid where they overlap
        int wx0 = wide.at("x0"), wz0 = wide.at("z0"), ws = wide.at("step");
        int overlap = 0;
        for (int j = 0; j < wide.at("height").get<int>(); ++j) {
            for (int i = 0; i < wide.at("width").get<int>(); ++i) {
                if (auto m = mainAt(wx0 + i * ws, wz0 + j * ws)) {
                    EXPECT_EQ(*m, biomeAt(wide, i, j));
                    ++overlap;
                }
            }
        }
        EXPECT_GT(overlap, 0);
        // getNoiseBiome = the main biome's column layer nearest to quart y (TerrainColumn#getBiome)
        json const& columns = g.at("columns");
        int         checked = 0;
        for (json const& s : g.at("noise")) {
            auto m = mainAt(s.at(0), s.at(2));
            if (!m) continue;
            ASSERT_TRUE(columns.contains(*m)) << *m;
            int         qy      = s.at(1);
            std::string best    = *m; // no layers: the key biome
            bool        have    = false;
            double      bestKey = 0;
            // reduce((a, b) -> |a - qy| <= |b - qy| ? a : b) over the layers in ascending elevation
            for (json const& layer : columns.at(*m)) {
                double key = layer.at(0);
                if (!have || std::abs(key - qy) < std::abs(bestKey - qy)) {
                    best    = layer.at(1);
                    bestKey = key;
                    have    = true;
                }
            }
            EXPECT_EQ(best, s.at(3).get<std::string>()) << s.dump();
            ++checked;
        }
        EXPECT_GT(checked, 100);
        // zoomed samples name known biomes
        std::set<std::string> known(palette.begin(), palette.end());
        for (auto const& [k, layers] : columns.items()) {
            for (json const& layer : layers) known.insert(layer.at(1).get<std::string>());
        }
        for (json const& z : g.at("zoom")) EXPECT_TRUE(known.contains(z.at(3).get<std::string>())) << z.dump();
    }
}

// =============================================================================================
// heights, surface, density

TEST(OracleGolden, HeightGridsAreConsistent) {
    REQUIRE_GOLDENS();
    for (char const* kind : {"heights", "surface"}) {
        for (std::string const& rel : filesOfKind(kind)) {
            SCOPED_TRACE(rel);
            json const& g = golden(rel);
            int         minY = g.at("minY"), maxY = g.at("maxY"), w = g.at("width"), h = g.at("height");
            json const& ws = g.at("worldSurfaceWg");
            json const& of = g.at("oceanFloorWg");
            ASSERT_EQ(ws.size(), std::size_t(h));
            ASSERT_EQ(of.size(), std::size_t(h));
            for (int j = 0; j < h; ++j) {
                ASSERT_EQ(ws.at(j).size(), std::size_t(w));
                for (int i = 0; i < w; ++i) {
                    int a = ws.at(j).at(i), b = of.at(j).at(i);
                    EXPECT_GE(a, minY);
                    EXPECT_LE(a, maxY);
                    EXPECT_LE(b, a);
                }
            }
            if (g.contains("top")) {
                std::size_t palette = g.at("palette").size();
                ASSERT_EQ(g.at("top").size(), std::size_t(h));
                for (json const& row : g.at("top")) EXPECT_EQ(row.get<std::string>().size(), std::size_t(w));
                EXPECT_GT(palette, 1u);
            }
        }
    }
}

TEST(OracleGolden, DensityRouterSamples) {
    REQUIRE_GOLDENS();
    for (std::string const& rel : filesOfKind("density")) {
        SCOPED_TRACE(rel);
        json const& g = golden(rel);
        std::size_t n = g.at("points").size();
        ASSERT_GT(n, 0u);
        ASSERT_TRUE(g.at("router").contains("final_density"));
        for (auto const& [name, f] : g.at("router").items()) {
            double lo = f.at("min"), hi = f.at("max");
            if (f.contains("constant")) {
                EXPECT_EQ(lo, hi) << name;
                continue;
            }
            ASSERT_EQ(f.at("values").size(), n) << name;
            for (json const& v : f.at("values")) {
                EXPECT_GE(v.get<double>(), lo - 1e-9) << name;
                EXPECT_LE(v.get<double>(), hi + 1e-9) << name;
            }
        }
        EXPECT_FALSE(g.at("router").at("final_density").contains("constant"));
        EXPECT_EQ(g.at("terrain").at("depth").size(), n);
        EXPECT_EQ(g.at("terrain").at("scale").size(), n);
    }
}

// =============================================================================================
// landmarks

TEST(OracleGolden, LandmarkGridMatchesLegacyLandmarkPlacements) {
    REQUIRE_GOLDENS();
    for (std::string const& rel : filesOfKind("landmarks")) {
        SCOPED_TRACE(rel);
        json const&  g    = golden(rel);
        std::int64_t seed = g.at("seed");
        std::vector<std::pair<std::string, int>> weights;
        for (json const& w : g.at("varietyWeights")) weights.emplace_back(w.at(0).get<std::string>(), w.at(1).get<int>());
        ASSERT_FALSE(weights.empty());
        int started = 0;
        for (json const& c : g.at("centers")) {
            std::int32_t rx = c.at("regionX"), rz = c.at("regionZ"), cx = c.at("chunkX"), cz = c.at("chunkZ");
            Center       nc = nearestCenter(rx * 16, rz * 16);
            EXPECT_EQ(nc.x, c.at("x").get<int>()) << c.dump();
            EXPECT_EQ(nc.z, c.at("z").get<int>()) << c.dump();
            EXPECT_EQ(cx, nc.x >> 4);
            EXPECT_EQ(cz, nc.z >> 4);
            EXPECT_TRUE(c.at("hasCenter").get<bool>());
            EXPECT_TRUE(chunkHasCenter(cx, cz));
            std::string variety = pickVariety(seed, cx, cz, weights);
            EXPECT_EQ(variety, c.at("variety").get<std::string>()) << c.dump();
            // pickLandmarkForChunk: the biome's landmark, else the variety landmark
            auto        it   = biomeLandmarks().find(c.at("pickBiome").get<std::string>());
            std::string pick = it != biomeLandmarks().end() ? it->second : variety;
            EXPECT_EQ(pick, c.at("pick").get<std::string>()) << c.dump();
            if (!c.at("starts").empty()) ++started;
        }
        EXPECT_GT(started, 0) << "no structure started at any landmark center";
        for (json const& c : g.at("chunks")) {
            std::int32_t cx = c.at("chunkX"), cz = c.at("chunkZ");
            Center       nc = nearestCenter(cx, cz);
            EXPECT_EQ(nc.x, c.at("x").get<int>()) << c.dump();
            EXPECT_EQ(nc.z, c.at("z").get<int>()) << c.dump();
            EXPECT_EQ(chunkHasCenter(cx, cz), c.at("hasCenter").get<bool>()) << c.dump();
            EXPECT_EQ(manhattan(cx, cz), c.at("manhattan").get<int>()) << c.dump();
            EXPECT_EQ(pickVariety(seed, cx, cz, weights), c.at("variety").get<std::string>()) << c.dump();
        }
    }
}

// =============================================================================================
// mobs, loot, recipes, uncrafting

TEST(OracleGolden, MobsCoverEveryTfEntity) {
    REQUIRE_GOLDENS();
    json const& entities = golden("mobs.json").at("entities");
    ASSERT_FALSE(entities.empty());
    int living = 0;
    for (auto const& [id, e] : entities.items()) {
        EXPECT_FALSE(e.contains("error")) << id << ": " << e.value("error", "");
        EXPECT_GE(e.at("width").get<double>(), 0.0) << id; // charm_effect, protection_box: 0 x 0
        EXPECT_GE(e.at("height").get<double>(), 0.0) << id;
        if (e.value("living", false)) {
            ++living;
            EXPECT_GT(e.at("maxHealth").get<double>(), 0.0) << id;
            EXPECT_TRUE(e.at("attributes").contains("minecraft:generic.max_health")) << id;
            EXPECT_GE(e.at("armor").get<int>(), 0) << id;
            EXPECT_GE(e.at("experienceMin").get<int>(), 0) << id;
            EXPECT_LE(e.at("experienceMin").get<int>(), e.at("experienceMax").get<int>()) << id;
        }
    }
    EXPECT_GT(living, 50);
    std::set<std::string> const owned = ownedIds("entity");
    for (std::string const& id : owned) EXPECT_TRUE(entities.contains(id)) << id << " (owners.tsv) has no mob golden";
    if (!owned.empty()) {
        for (auto const& [id, e] : entities.items()) EXPECT_TRUE(owned.contains(id)) << id << " is not in owners.tsv";
    }
}

TEST(OracleGolden, LootStatisticsAreConsistent) {
    REQUIRE_GOLDENS();
    json const&  g     = golden("loot.json");
    std::int64_t rolls = g.at("rolls");
    ASSERT_GT(rolls, 0);
    json const& tables = g.at("tables");
    int         rolled = 0;
    for (auto const& [id, t] : tables.items()) {
        if (t.contains("skipped")) continue;
        ++rolled;
        EXPECT_LE(t.at("emptyRolls").get<std::int64_t>(), rolls) << id;
        for (auto const& [item, s] : t.at("items").items()) {
            std::int64_t present = s.at("present"), total = s.at("total"), lo = s.at("min"), hi = s.at("max");
            EXPECT_GT(present, 0) << id << " " << item;
            EXPECT_LE(present, rolls) << id << " " << item;
            EXPECT_LE(lo, hi) << id << " " << item;
            EXPECT_GE(total, present * lo) << id << " " << item;
            EXPECT_LE(total, present * hi) << id << " " << item;
        }
    }
    EXPECT_GT(rolled, 500);
    for (std::string const& id : ownedIds("loot_table")) EXPECT_TRUE(tables.contains(id)) << id << " (owners.tsv) has no loot golden";
}

TEST(OracleGolden, RecipesCoverTfRecipes) {
    REQUIRE_GOLDENS();
    json const& recipes = golden("recipes.json").at("recipes");
    ASSERT_FALSE(recipes.empty());
    for (auto const& [id, r] : recipes.items()) {
        EXPECT_FALSE(r.at("type").get<std::string>().empty()) << id;
        if (r.contains("width")) EXPECT_EQ(r.at("ingredients").size(), r.at("width").get<std::size_t>() * r.at("height").get<std::size_t>()) << id;
    }
    // recipe files whose load conditions failed on the oracle (compat recipes of absent mods)
    std::set<std::string> notLoaded;
    for (json const& id : golden("recipes.json").at("notLoaded")) notLoaded.insert(id.get<std::string>());
    for (std::string const& id : ownedIds("recipe")) {
        EXPECT_TRUE(recipes.contains(id) || notLoaded.contains(id)) << id << " (owners.tsv) has no recipe golden";
    }
}

TEST(OracleGolden, UncraftingOptionsAreWellFormed) {
    REQUIRE_GOLDENS();
    json const& g     = golden("uncrafting.json");
    json const& items = g.at("items");
    ASSERT_FALSE(items.empty());
    int tf = 0;
    for (auto const& [id, options] : items.items()) {
        if (id.starts_with("twilightforest:")) ++tf;
        for (json const& o : options) {
            ASSERT_EQ(o.at("grid").size(), 9u) << id;
            EXPECT_GE(o.at("inputCount").get<int>(), 1) << id;
            EXPECT_GE(o.at("cost").get<int>(), 0) << id;
            int filled = 0;
            for (json const& c : o.at("grid")) filled += !c.get<std::string>().empty();
            EXPECT_GT(filled, 0) << id;
        }
    }
    EXPECT_GT(tf, 50);
}
