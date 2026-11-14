// Java oracle goldens for the random sources and noises of src/core/{jrand,noise} (F5).
//
// tests/golden/rng.json and tests/golden/noise.json are dumped by the Java oracle (F13,
// tools/oracle) from Minecraft 1.21.1's own classes running upstream 4.8 on NeoForge. Every case of both
// files is recomputed here with the C++ implementation and must match exactly: integers equal,
// doubles with the same bit pattern (the goldens hold Java's shortest round-trip form, which
// nlohmann reads back exactly), floats widened to double. The density goldens belong to U1 / U2
// (they need upstream's biome-driven density types); only what the vanilla density primitives decide
// alone is checked here (the constant router entries and the clamp bounds of upstream's final density).
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS // std::getenv (GF_GOLDEN_DIR)
#endif

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/jrand/JRand.h"
#include "core/jrand/RandomSupport.h"
#include "core/noise/LegacyNormalNoise.h"
#include "core/noise/Noise.h"

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace gf::core;

// ---- files ---------------------------------------------------------------------------------------

// tests/golden: $GF_GOLDEN_DIR, next to this file's directory, or found walking up from the working
// directory (xmake runs the tests from the build directory).
fs::path goldenDir() {
    static fs::path const dir = [] {
        if (char const* env = std::getenv("GF_GOLDEN_DIR"); env && *env) return fs::path(env);
        std::error_code ec;
        fs::path        here = fs::path(__FILE__).parent_path().parent_path() / "golden";
        if (here.is_absolute() && fs::exists(here / "rng.json", ec)) return here;
        for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
            if (fs::exists(p / "tests/golden/rng.json", ec)) return p / "tests/golden";
            if (p == p.root_path()) break;
        }
        return fs::path();
    }();
    return dir;
}

json readJson(fs::path const& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return json::parse(ss.str(), nullptr, false);
}

json const& golden(std::string const& rel) {
    static std::map<std::string, json> cache;
    auto                               it = cache.find(rel);
    if (it == cache.end()) {
        fs::path p = goldenDir() / rel;
        it         = cache.emplace(rel, fs::exists(p) ? readJson(p) : json()).first;
    }
    return it->second;
}

#define REQUIRE_GOLDEN(g, rel) ASSERT_TRUE((g).is_object()) << "tests/golden/" << (rel) << " not found or not JSON (set GF_GOLDEN_DIR)"

// ---- comparisons ---------------------------------------------------------------------------------

std::string show(double d) {
    std::ostringstream o;
    o << std::setprecision(17) << d;
    return o.str();
}

/// Mismatches of one golden list: counted, the first few described.
struct Diffs {
    int         checked = 0;
    int         failed  = 0;
    std::string first;

    void check(bool ok, std::string const& what) {
        ++checked;
        if (ok) return;
        if (failed < 12) first += "\n  " + what;
        ++failed;
    }
    template <class T, class U>
    void same(T got, U want, std::string const& what) {
        std::ostringstream o;
        o << what << ": got " << got << ", Java " << want;
        check(got == static_cast<T>(want), o.str());
    }
    /// The same double, bit for bit.
    void sameDouble(double got, double want, std::string const& what) {
        auto a = std::bit_cast<std::int64_t>(got), b = std::bit_cast<std::int64_t>(want);
        check(a == b, what + ": got " + show(got) + ", Java " + show(want) + " (" + std::to_string(a - b) + " ulp)");
    }
};

#define EXPECT_NO_DIFFS(d, min)                                                                                        \
    do {                                                                                                               \
        EXPECT_GE((d).checked, (min)) << "fewer golden values than expected";                                          \
        EXPECT_EQ((d).failed, 0) << (d).failed << " of " << (d).checked << " values differ from Java:" << (d).first;   \
    } while (0)

// ---- sources -------------------------------------------------------------------------------------

using SourceFactory = std::function<std::unique_ptr<jrand::RandomSource>()>;

/// rng.json "legacy" / "xoroshiro" entry: a fresh source of that entry.
SourceFactory factoryOf(json const& e, bool legacy) {
    if (legacy) return [seed = e.at("seed").get<std::int64_t>()] { return std::make_unique<jrand::LegacyRandom>(seed); };
    if (e.contains("seed")) return [seed = e.at("seed").get<std::int64_t>()] { return std::make_unique<jrand::Xoroshiro128pp>(seed); };
    return [lo = e.at("seedLo").get<std::int64_t>(), hi = e.at("seedHi").get<std::int64_t>()] {
        return std::make_unique<jrand::Xoroshiro128pp>(lo, hi);
    };
}

/// Every method list of one source entry (RngDump#source), each from a fresh source.
void checkSource(json const& e, SourceFactory const& make, bool legacy, Diffs& d) {
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextInt")) d.same(r->nextInt(), v.get<std::int32_t>(), "nextInt #" + std::to_string(i++));
    }
    for (auto const& b : e.at("nextIntBound")) {
        auto r     = make();
        int  bound = b.at("bound").get<int>();
        int  i     = 0;
        for (auto const& v : b.at("values")) d.same(r->nextInt(bound), v.get<std::int32_t>(), "nextInt(" + std::to_string(bound) + ") #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextLong")) d.same(r->nextLong(), v.get<std::int64_t>(), "nextLong #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextBoolean")) d.same(r->nextBoolean(), v.get<bool>(), "nextBoolean #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextFloat")) d.sameDouble(static_cast<double>(r->nextFloat()), v.get<double>(), "nextFloat #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextDouble")) d.sameDouble(r->nextDouble(), v.get<double>(), "nextDouble #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextGaussian")) d.sameDouble(r->nextGaussian(), v.get<double>(), "nextGaussian #" + std::to_string(i++));
    }
    {
        auto r = make();
        int  i = 0;
        for (auto const& v : e.at("nextIntBetweenInclusiveM5To5"))
            d.same(r->nextIntBetweenInclusive(-5, 5), v.get<std::int32_t>(), "nextIntBetweenInclusive(-5, 5) #" + std::to_string(i++));
    }
    if (legacy) {
        for (auto const& b : e.at("next")) {
            jrand::LegacyRandom r(e.at("seed").get<std::int64_t>());
            int                 bits = b.at("bits").get<int>();
            int                 i    = 0;
            for (auto const& v : b.at("values")) d.same(r.next(bits), v.get<std::int32_t>(), "next(" + std::to_string(bits) + ") #" + std::to_string(i++));
        }
    }
    {
        auto r = make();
        r->consumeCount(5);
        int i = 0;
        for (auto const& v : e.at("consumeCount5ThenNextLong")) d.same(r->nextLong(), v.get<std::int64_t>(), "consumeCount(5), nextLong #" + std::to_string(i++));
    }
    {
        // Gaussian caching interleaved with the other calls
        auto        r   = make();
        json const& ops = e.at("mixedOps");
        json const& v   = e.at("mixed");
        d.check(ops.size() == v.size(), "mixed: as many values as operations");
        for (std::size_t i = 0; i < ops.size() && i < v.size(); ++i) {
            auto        op   = ops[i].get<std::string>();
            std::string what = "mixed #" + std::to_string(i) + " " + op;
            if (op == "nextInt(10)") d.same(r->nextInt(10), v[i].get<std::int32_t>(), what);
            else if (op == "nextInt") d.same(r->nextInt(), v[i].get<std::int32_t>(), what);
            else if (op == "nextGaussian") d.sameDouble(r->nextGaussian(), v[i].get<double>(), what);
            else if (op == "nextDouble") d.sameDouble(r->nextDouble(), v[i].get<double>(), what);
            else if (op == "nextLong") d.same(r->nextLong(), v[i].get<std::int64_t>(), what);
            else if (op == "nextFloat") d.sameDouble(static_cast<double>(r->nextFloat()), v[i].get<double>(), what);
            else if (op == "nextBoolean") d.same(r->nextBoolean(), v[i].get<bool>(), what);
            else d.check(false, what + ": unknown operation");
        }
    }
    {
        auto r    = make();
        auto fork = r->fork();
        int  i    = 0;
        for (auto const& v : e.at("forkNextLong")) d.same(fork->nextLong(), v.get<std::int64_t>(), "fork(), nextLong #" + std::to_string(i++));
        i = 0;
        for (auto const& v : e.at("parentAfterForkNextLong")) d.same(r->nextLong(), v.get<std::int64_t>(), "after fork(), parent nextLong #" + std::to_string(i++));
    }
    {
        auto        r   = make();
        auto        pf  = r->forkPositional();
        json const& pos = e.at("positional");
        for (auto const& a : pos.at("at")) {
            int  x = a.at("x"), y = a.at("y"), z = a.at("z");
            auto pr   = pf->at(x, y, z);
            auto what = "forkPositional().at(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
            d.same(pr->nextLong(), a.at("nextLong").get<std::int64_t>(), what + " nextLong");
            d.same(pr->nextInt(16), a.at("nextInt16").get<std::int32_t>(), what + " nextInt(16)");
        }
        for (auto const& a : pos.at("fromHashOf")) {
            auto name = a.at("name").get<std::string>();
            auto pr   = pf->fromHashOf(name);
            d.same(pr->nextLong(), a.at("nextLong").get<std::int64_t>(), "fromHashOf(\"" + name + "\") nextLong");
            d.same(pr->nextInt(16), a.at("nextInt16").get<std::int32_t>(), "fromHashOf(\"" + name + "\") nextInt(16)");
        }
        for (auto const& a : pos.at("fromSeed")) {
            auto s  = a.at("seed").get<std::int64_t>();
            auto pr = jrand::positionalFromSeed(*pf, s);
            d.check(pr != nullptr, "fromSeed: a factory of a known kind");
            if (pr) d.same(pr->nextLong(), a.at("nextLong").get<std::int64_t>(), "fromSeed(" + std::to_string(s) + ") nextLong");
        }
        d.same(r->nextLong(), pos.at("parentAfterForkNextLong").get<std::int64_t>(), "after forkPositional(), parent nextLong");
    }
}

std::unique_ptr<jrand::RandomSource> sourceOf(std::string const& kind, std::int64_t seed) {
    if (kind == "legacy") return std::make_unique<jrand::LegacyRandom>(seed);
    return std::make_unique<jrand::Xoroshiro128pp>(seed);
}

/// RngDump#make: a WorldgenRandom over a fresh source whose seed is always set before use.
std::unique_ptr<jrand::WorldgenRandom> worldgenOver(std::string const& kind) {
    return std::make_unique<jrand::WorldgenRandom>(sourceOf(kind, 0));
}

} // namespace

// =============================================================================================
// rng.json

TEST(WorldgenOracleGolden, RngLegacyRandomSource) {
    json const& g = golden("rng.json");
    REQUIRE_GOLDEN(g, "rng.json");
    Diffs d;
    for (json const& e : g.at("legacy")) {
        Diffs one;
        checkSource(e, factoryOf(e, true), true, one);
        d.checked += one.checked;
        d.failed += one.failed;
        if (one.failed) d.first += "\n legacy seed " + std::to_string(e.at("seed").get<std::int64_t>()) + ":" + one.first;
    }
    EXPECT_NO_DIFFS(d, 8 * 200);
}

TEST(WorldgenOracleGolden, RngXoroshiroRandomSource) {
    json const& g = golden("rng.json");
    REQUIRE_GOLDEN(g, "rng.json");
    Diffs d;
    for (json const& e : g.at("xoroshiro")) {
        std::string label = e.contains("seed") ? "seed " + std::to_string(e.at("seed").get<std::int64_t>())
                                               : "seedLo " + std::to_string(e.at("seedLo").get<std::int64_t>()) + " seedHi "
                                                     + std::to_string(e.at("seedHi").get<std::int64_t>());
        Diffs one;
        if (e.contains("seed")) {
            // XoroshiroRandomSource(seed) = (seedLo, seedHi) of RandomSupport#upgradeSeedTo128bit
            auto s = jrand::upgradeSeedTo128bit(e.at("seed").get<std::int64_t>());
            one.same(s.lo, e.at("seedLo").get<std::int64_t>(), "upgradeSeedTo128bit lo");
            one.same(s.hi, e.at("seedHi").get<std::int64_t>(), "upgradeSeedTo128bit hi");
        }
        checkSource(e, factoryOf(e, false), false, one);
        d.checked += one.checked;
        d.failed += one.failed;
        if (one.failed) d.first += "\n xoroshiro " + label + ":" + one.first;
    }
    EXPECT_NO_DIFFS(d, 12 * 190);
}

TEST(WorldgenOracleGolden, RngWorldgenRandomSeeding) {
    json const& g = golden("rng.json");
    REQUIRE_GOLDEN(g, "rng.json");
    Diffs d;
    for (json const& e : g.at("worldgen")) {
        auto        kind      = e.at("source").get<std::string>();
        auto        levelSeed = e.at("levelSeed").get<std::int64_t>();
        std::string label     = kind + " " + std::to_string(levelSeed);
        for (json const& b : e.at("decoration")) {
            int  x = b.at("minBlockX"), z = b.at("minBlockZ");
            auto wr = worldgenOver(kind);
            auto ds = wr->setDecorationSeed(levelSeed, x, z);
            auto at = label + " setDecorationSeed(" + std::to_string(x) + ", " + std::to_string(z) + ")";
            d.same(ds, b.at("decorationSeed").get<std::int64_t>(), at);
            int i = 0;
            for (auto const& v : b.at("thenNextLong")) d.same(wr->nextLong(), v.get<std::int64_t>(), at + " then nextLong #" + std::to_string(i++));
            for (json const& f : b.at("features")) {
                int  index = f.at("index"), step = f.at("step");
                auto fr    = worldgenOver(kind);
                fr->setFeatureSeed(ds, index, step);
                auto ft = at + " setFeatureSeed(" + std::to_string(index) + ", " + std::to_string(step) + ")";
                i       = 0;
                for (auto const& v : f.at("nextInt16")) d.same(fr->nextInt(16), v.get<std::int32_t>(), ft + " nextInt(16) #" + std::to_string(i++));
                d.same(fr->getCount(), f.at("count").get<int>(), ft + " getCount");
            }
        }
        for (json const& l : e.at("largeFeature")) {
            int  cx = l.at("chunkX"), cz = l.at("chunkZ");
            auto wr = worldgenOver(kind);
            wr->setLargeFeatureSeed(levelSeed, cx, cz);
            auto at = label + " setLargeFeatureSeed(" + std::to_string(cx) + ", " + std::to_string(cz) + ")";
            int  i  = 0;
            for (auto const& v : l.at("nextInt16")) d.same(wr->nextInt(16), v.get<std::int32_t>(), at + " nextInt(16) #" + std::to_string(i++));
            d.same(wr->nextLong(), l.at("nextLong").get<std::int64_t>(), at + " nextLong");
        }
        for (json const& l : e.at("largeFeatureWithSalt")) {
            int  rx = l.at("regionX"), rz = l.at("regionZ"), salt = l.at("salt");
            auto wr = worldgenOver(kind);
            wr->setLargeFeatureWithSalt(levelSeed, rx, rz, salt);
            auto at = label + " setLargeFeatureWithSalt(" + std::to_string(rx) + ", " + std::to_string(rz) + ", " + std::to_string(salt) + ")";
            int  i  = 0;
            for (auto const& v : l.at("nextInt16")) d.same(wr->nextInt(16), v.get<std::int32_t>(), at + " nextInt(16) #" + std::to_string(i++));
        }
    }
    EXPECT_NO_DIFFS(d, 6 * 4 * (1 + 2 + 3 * 4) + 6 * 4 * 4 + 6 * 12 * 3);
}

TEST(WorldgenOracleGolden, RngSlimeChunks) {
    json const& g = golden("rng.json");
    REQUIRE_GOLDEN(g, "rng.json");
    Diffs d;
    for (json const& s : g.at("slimeChunk")) {
        int  cx = s.at("chunkX"), cz = s.at("chunkZ");
        auto levelSeed = s.at("levelSeed").get<std::int64_t>();
        auto r         = jrand::WorldgenRandom::seedSlimeChunk(cx, cz, levelSeed, s.at("salt").get<std::int64_t>());
        auto at        = "seedSlimeChunk(" + std::to_string(cx) + ", " + std::to_string(cz) + ", " + std::to_string(levelSeed) + ")";
        int  i         = 0;
        for (auto const& v : s.at("nextInt10")) d.same(r->nextInt(10), v.get<std::int32_t>(), at + " nextInt(10) #" + std::to_string(i++));
    }
    EXPECT_NO_DIFFS(d, 15 * 4);
}

TEST(WorldgenOracleGolden, RngRandomSupport) {
    json const& g = golden("rng.json");
    REQUIRE_GOLDEN(g, "rng.json");
    Diffs d;
    for (json const& e : g.at("randomSupport")) {
        auto seed = e.at("seed").get<std::int64_t>();
        auto at   = "seed " + std::to_string(seed);
        d.same(jrand::mixStafford13(seed), e.at("mixStafford13").get<std::int64_t>(), at + " mixStafford13");
        auto s = jrand::upgradeSeedTo128bit(seed);
        d.same(s.lo, e.at("upgradeLo").get<std::int64_t>(), at + " upgradeSeedTo128bit lo");
        d.same(s.hi, e.at("upgradeHi").get<std::int64_t>(), at + " upgradeSeedTo128bit hi");
        auto u = jrand::upgradeSeedTo128bitUnmixed(seed);
        d.same(u.lo, e.at("unmixedLo").get<std::int64_t>(), at + " upgradeSeedTo128bitUnmixed lo");
        d.same(u.hi, e.at("unmixedHi").get<std::int64_t>(), at + " upgradeSeedTo128bitUnmixed hi");
    }
    for (json const& h : g.at("javaStringHash")) {
        auto text = h.at("text").get<std::string>();
        d.same(jrand::javaStringHash(text), h.at("hash").get<std::int32_t>(), "String#hashCode(\"" + text + "\")");
        auto md5 = jrand::seedFromHashOf(text);
        d.same(md5.lo, h.at("md5Lo").get<std::int64_t>(), "seedFromHashOf(\"" + text + "\") lo");
        d.same(md5.hi, h.at("md5Hi").get<std::int64_t>(), "seedFromHashOf(\"" + text + "\") hi");
    }
    for (json const& s : g.at("mthGetSeed")) {
        int x = s.at("x"), y = s.at("y"), z = s.at("z");
        d.same(jrand::mthGetSeed(x, y, z), s.at("seed").get<std::int64_t>(),
               "Mth#getSeed(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
    }
    EXPECT_NO_DIFFS(d, 8 * 5 + 8 * 3 + 6);
}

// =============================================================================================
// noise.json

namespace {

std::vector<std::array<double, 3>> pointsOf(json const& rows, std::size_t dims) {
    std::vector<std::array<double, 3>> out;
    for (auto const& r : rows) {
        std::array<double, 3> p{};
        for (std::size_t i = 0; i < dims && i < r.size(); ++i) p[i] = r[i].get<double>();
        out.push_back(p);
    }
    return out;
}

std::string pointText(std::array<double, 3> const& p, std::size_t dims) {
    std::string s = "(" + show(p[0]) + ", " + show(p[1]);
    if (dims == 3) s += ", " + show(p[2]);
    return s + ")";
}

std::string headOf(json const& e) {
    std::string s = e.at("source").get<std::string>() + " seed " + std::to_string(e.at("seed").get<std::int64_t>());
    if (e.contains("firstOctave")) s += " octaves " + std::to_string(e.at("firstOctave").get<int>()) + " " + e.at("amplitudes").dump();
    return s;
}

noise::NoiseParameters paramsOf(json const& e) {
    noise::NoiseParameters p;
    p.firstOctave = e.at("firstOctave").get<int>();
    for (auto const& a : e.at("amplitudes")) p.amplitudes.push_back(a.get<double>());
    return p;
}

/// The values of a 3D noise at points3 (`values` of the entry).
void checkValues3(json const& values, std::vector<std::array<double, 3>> const& pts, std::string const& what,
                  std::function<double(double, double, double)> const& fn, Diffs& d) {
    d.check(values.size() == pts.size(), what + ": one value per point");
    for (std::size_t i = 0; i < pts.size() && i < values.size(); ++i)
        d.sameDouble(fn(pts[i][0], pts[i][1], pts[i][2]), values[i].get<double>(), what + " at " + pointText(pts[i], 3));
}

/// ImprovedNoise / SimplexNoise's xo, yo, zo: the constructor's first three draws (nextDouble() *
/// 256), replayed on a fresh source (the contract keeps them private; the values at the points
/// check that the noise uses them).
void checkOffsets(json const& e, Diffs& d) {
    auto r = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
    for (char const* k : {"xo", "yo", "zo"}) d.sameDouble(r->nextDouble() * 256.0, e.at(k).get<double>(), headOf(e) + " " + k);
}

} // namespace

TEST(WorldgenOracleGolden, NoiseImproved) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts = pointsOf(g.at("points3"), 3);
    Diffs d;
    for (json const& e : g.at("improved")) {
        checkOffsets(e, d);
        auto                 src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        noise::ImprovedNoise n(*src);
        checkValues3(e.at("values"), pts, headOf(e) + " noise", [&](double x, double y, double z) { return n.noise(x, y, z); }, d);
        for (json const& s : e.at("yScaled")) {
            double yScale = s.at("yScale"), yMax = s.at("yMax");
            checkValues3(s.at("values"), pts, headOf(e) + " noise(yScale " + show(yScale) + ", yMax " + show(yMax) + ")",
                         [&](double x, double y, double z) { return n.noise(x, y, z, yScale, yMax); }, d);
        }
    }
    EXPECT_NO_DIFFS(d, 6 * (3 + 4 * 48));
}

TEST(WorldgenOracleGolden, NoiseSimplex) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts3 = pointsOf(g.at("points3"), 3);
    auto  pts2 = pointsOf(g.at("points2"), 2);
    Diffs d;
    for (json const& e : g.at("simplex")) {
        checkOffsets(e, d);
        auto                src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        noise::SimplexNoise n(*src);
        json const&         v2 = e.at("values2");
        d.check(v2.size() == pts2.size(), headOf(e) + " values2: one value per point");
        for (std::size_t i = 0; i < pts2.size() && i < v2.size(); ++i)
            d.sameDouble(n.getValue(pts2[i][0], pts2[i][1]), v2[i].get<double>(), headOf(e) + " getValue at " + pointText(pts2[i], 2));
        checkValues3(e.at("values3"), pts3, headOf(e) + " getValue", [&](double x, double y, double z) { return n.getValue(x, y, z); }, d);
    }
    EXPECT_NO_DIFFS(d, 6 * (3 + 32 + 48));
}

TEST(WorldgenOracleGolden, NoisePerlin) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts = pointsOf(g.at("points3"), 3);
    Diffs d;
    for (json const& e : g.at("perlin")) {
        auto src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        auto n   = noise::PerlinNoise::create(*src, paramsOf(e));
        d.sameDouble(n.maxValue(), e.at("maxValue").get<double>(), headOf(e) + " maxValue");
        checkValues3(e.at("values"), pts, headOf(e) + " getValue", [&](double x, double y, double z) { return n.getValue(x, y, z); }, d);
    }
    EXPECT_NO_DIFFS(d, 30 * 49);
}

TEST(WorldgenOracleGolden, NoisePerlinLegacyNetherBiome) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts = pointsOf(g.at("points3"), 3);
    Diffs d;
    for (json const& e : g.at("perlinLegacyNetherBiome")) {
        auto src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        auto n   = noise::PerlinNoise::createLegacy(*src, paramsOf(e));
        d.sameDouble(n.maxValue(), e.at("maxValue").get<double>(), headOf(e) + " maxValue");
        checkValues3(e.at("values"), pts, headOf(e) + " getValue", [&](double x, double y, double z) { return n.getValue(x, y, z); }, d);
    }
    EXPECT_NO_DIFFS(d, 12 * 49);
}

TEST(WorldgenOracleGolden, NoiseNormal) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts = pointsOf(g.at("points3"), 3);
    Diffs d;
    for (json const& e : g.at("normal")) {
        auto src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        auto n   = noise::NormalNoise::create(*src, paramsOf(e));
        d.sameDouble(n.maxValue(), e.at("maxValue").get<double>(), headOf(e) + " maxValue");
        checkValues3(e.at("values"), pts, headOf(e) + " getValue", [&](double x, double y, double z) { return n.getValue(x, y, z); }, d);
    }
    EXPECT_NO_DIFFS(d, 30 * 49);
}

TEST(WorldgenOracleGolden, NoiseNormalLegacyNetherBiome) {
    json const& g = golden("noise.json");
    REQUIRE_GOLDEN(g, "noise.json");
    auto  pts = pointsOf(g.at("points3"), 3);
    Diffs d;
    for (json const& e : g.at("normalLegacyNetherBiome")) {
        auto src = sourceOf(e.at("source").get<std::string>(), e.at("seed").get<std::int64_t>());
        auto n   = noise::LegacyNormalNoise::createLegacyNetherBiome(*src, paramsOf(e));
        d.sameDouble(n.maxValue(), e.at("maxValue").get<double>(), headOf(e) + " maxValue");
        checkValues3(e.at("values"), pts, headOf(e) + " getValue", [&](double x, double y, double z) { return n.getValue(x, y, z); }, d);
    }
    EXPECT_NO_DIFFS(d, 12 * 49);
}

// =============================================================================================
// density/seed-*.json: what the vanilla density primitives decide alone

TEST(WorldgenOracleGolden, DensityRouterConstantsAndClampBounds) {
    ASSERT_FALSE(goldenDir().empty()) << "tests/golden not found (set GF_GOLDEN_DIR)";
    fs::path root     = goldenDir().parent_path().parent_path();
    fs::path imported = root / "assets/gen/import/terrain/data/gloamingforest/worldgen";
    json     settings = readJson(imported / "noise_settings/gloaming_noise_gen.json");
    ASSERT_TRUE(settings.is_object() && settings.contains("noise_router")) << "upstream's imported noise settings (" << imported.generic_string() << ")";
    json const& router = settings.at("noise_router");

    // upstream's biome-driven terrain (U1 / U2) is not part of this check: its references resolve to 0;
    // the vanilla noises only need parameters to compile (min / max do not depend on them)
    noise::Density::Resolver res;
    res.density = [&](std::string const& id) -> std::optional<json> {
        if (id == "gloamingforest:raw_biome_terrain" || id == "gloamingforest:raw_biome_noise") return json(0.0);
        auto colon = id.find(':');
        auto file  = imported / "density_function" / (id.substr(colon + 1) + ".json");
        if (id.compare(0, colon, "gloamingforest") != 0 || !fs::exists(file)) return std::nullopt;
        return readJson(file);
    };
    res.noise = [](std::string const& id) -> std::optional<noise::NoiseParameters> {
        if (id == "minecraft:ridge") return noise::NoiseParameters{-7, {1.0, 2.0, 1.0, 0.0, 0.0, 0.0}};
        if (id == "minecraft:surface") return noise::NoiseParameters{-6, {1.0, 1.0, 1.0}};
        return std::nullopt;
    };

    int files = 0;
    for (auto const& f : fs::directory_iterator(goldenDir() / "density")) {
        json g = readJson(f.path());
        ASSERT_TRUE(g.is_object()) << f.path().generic_string();
        ++files;
        auto        seed = g.at("seed").get<std::int64_t>();
        std::string at   = f.path().filename().string();
        Diffs       d;
        for (auto const& [name, entry] : g.at("router").items()) {
            ASSERT_TRUE(router.contains(name)) << at << ": router entry " << name << " not in upstream's noise settings";
            auto fn = noise::Density::fromJson(router.at(name), seed, res);
            ASSERT_TRUE(fn.has_value()) << at << ": " << name << " compiles";
            d.sameDouble(fn->minValue(), entry.at("min").get<double>(), at + " " + name + " minValue");
            d.sameDouble(fn->maxValue(), entry.at("max").get<double>(), at + " " + name + " maxValue");
            if (!entry.contains("constant")) continue;
            double c = entry.at("constant");
            for (auto const& p : g.at("points")) {
                int x = p[0], y = p[1], z = p[2];
                d.sameDouble(fn->compute({x, y, z}), c, at + " " + name + " at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            }
        }
        EXPECT_NO_DIFFS(d, 15 * 2 + 13 * 256);
    }
    EXPECT_EQ(files, 3);
}
