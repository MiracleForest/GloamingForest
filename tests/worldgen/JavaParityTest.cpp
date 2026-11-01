// Java-parity goldens for the random sources and noises of src/core/{jrand,noise} (F5).
//
// tests/worldgen/oracle/Oracle.java prints values of Minecraft 1.21.1's own classes (the server
// jar, called through the official mappings); JavaGoldens.inc holds its output. Every line is
// rebuilt here from the C++ implementation and must match character for character (floating
// point values are compared by their raw bits).
#include <gtest/gtest.h>

#include <bit>
#include <map>
#include <sstream>
#include <string>

#include "core/jrand/JRand.h"
#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "core/noise/PerlinSimplex.h"

#include "JavaGoldens.inc"

using namespace gf::core;
using jrand::RandomSource;

namespace {

std::int64_t bits(double d) { return std::bit_cast<std::int64_t>(d); }

std::string dump(std::string const& label, RandomSource& r) {
    std::ostringstream sb;
    sb << label << " ints:";
    for (int i = 0; i < 4; i++) sb << " " << r.nextInt();
    sb << " bounded:";
    for (int b : {1, 2, 16, 100, 1000, 7, 2147483647}) sb << " " << r.nextInt(b);
    sb << " longs:";
    for (int i = 0; i < 2; i++) sb << " " << r.nextLong();
    sb << " floats:";
    for (int i = 0; i < 2; i++) sb << " " << std::bit_cast<std::int32_t>(r.nextFloat());
    sb << " doubles:";
    for (int i = 0; i < 2; i++) sb << " " << bits(r.nextDouble());
    sb << " gauss:";
    for (int i = 0; i < 3; i++) sb << " " << bits(r.nextGaussian());
    sb << " bools:";
    for (int i = 0; i < 3; i++) sb << " " << (r.nextBoolean() ? "true" : "false");
    return sb.str();
}

/// The C++ side of every Oracle.java line, keyed by its first two words.
std::map<std::string, std::string> cppLines() {
    std::map<std::string, std::string> out;
    auto add = [&](std::string const& line) {
        auto sp  = line.find(' ');
        auto sp2 = line.find(' ', sp + 1);
        out[line.substr(0, sp2)] = line;
    };
    std::int64_t const seeds[] = {0LL, 42LL, -123456789LL, 8675309123456789LL};
    {
        jrand::LegacyRandom jr(0);
        int                 a = jr.nextInt();
        int                 c = jr.nextInt(100);
        auto                l = jr.nextLong();
        std::ostringstream  s;
        s << "jur0 " << a << " " << c << " " << l << " " << bits(jr.nextGaussian());
        add(s.str());
    }
    for (auto s : seeds) {
        auto        str = std::to_string(s);
        jrand::LegacyRandom l(s);
        add(dump("legacy " + str, l));
        jrand::Xoroshiro128pp x(s);
        add(dump("xoro " + str, x));
        jrand::WorldgenRandom w(std::make_unique<jrand::Xoroshiro128pp>(s));
        auto                  deco = w.setDecorationSeed(s, 160, -320);
        add("wgx " + str + " deco " + std::to_string(deco));
        add(dump("wgx-after-deco " + str, w));
        w.setFeatureSeed(deco, 3, 9);
        add(dump("wgx-feature " + str, w));
        w.setLargeFeatureSeed(s, -7, 12);
        add(dump("wgx-large " + str, w));
        w.setLargeFeatureWithSalt(s, 3, -4, 10387312);
        add(dump("wgx-salt " + str, w));
        jrand::WorldgenRandom wl(std::make_unique<jrand::LegacyRandom>(s));
        auto                  decoL = wl.setDecorationSeed(s, -48, 32);
        add("wgl " + str + " deco " + std::to_string(decoL));
        add(dump("wgl-after-deco " + str, wl));
        add(dump("slime " + str, *jrand::WorldgenRandom::seedSlimeChunk(5, -9, s, 987234911LL)));
        auto xf = jrand::Xoroshiro128pp(s).forkPositional();
        add(dump("xoro-at " + str, *xf->at(13, -64, -200)));
        add(dump("xoro-hash " + str, *xf->fromHashOf("octave_-3")));
        add(dump("xoro-fork " + str, *jrand::Xoroshiro128pp(s).fork()));
        auto lf = jrand::LegacyRandom(s).forkPositional();
        add(dump("legacy-at " + str, *lf->at(13, -64, -200)));
        add(dump("legacy-hash " + str, *lf->fromHashOf("minecraft:offset")));
        jrand::Xoroshiro128pp lohi(s, ~s);
        add(dump("xoro-lohi " + str, lohi));
        jrand::Xoroshiro128pp xc(s);
        xc.consumeCount(262);
        add(dump("xoro-consume " + str, xc));
        jrand::LegacyRandom lc(s);
        lc.consumeCount(262);
        add(dump("legacy-consume " + str, lc));
    }
    {
        int                pts[5][3] = {{0, 0, 0}, {1, 2, 3}, {-1000, 64, 2500}, {30000000, -64, -30000000}, {123456, 255, -98765}};
        std::ostringstream s;
        s << "getSeed";
        for (auto& p : pts) s << " " << jrand::mthGetSeed(p[0], p[1], p[2]);
        add(s.str());
        std::ostringstream h;
        h << "hash";
        for (char const* t : {"", "octave_-3", "minecraft:offset", "twilightforest:dark_forest", "\xe6\x97\xa5\xe6\x9c\xac"})
            h << " " << jrand::javaStringHash(t);
        add(h.str());
        std::ostringstream lg;
        lg << "log";
        for (double d : {0.5, 1e-300, 0.999999, 1.0000001, 0.123456789, 4.9e-324, 0.25}) lg << " " << bits(mth::strictLog(d));
        add(lg.str());
    }
    {
        // HashSet<BlockPos> iteration order of a 5 x 6 x 3 block of positions
        std::vector<int> hashes;
        for (int y = 0; y < 6; y++)
            for (int dx = -2; dx <= 2; dx++)
                for (int dz = -2; dz <= 2; dz += 2) hashes.push_back(jrand::blockPosHash(100 + dx, 64 + y, -37 + dz));
        std::ostringstream o;
        o << "hashset";
        for (auto i : jrand::hashSetOrder(hashes)) o << " " << i;
        add(o.str());
        std::ostringstream s;
        s << "mthsin";
        for (float f : {0.f, 0.5f, 1.25f, -2.7f, 3.14159f, 100.75f, -1234.5f})
            s << " " << std::bit_cast<std::int32_t>(jrand::mthSin(f)) << " " << std::bit_cast<std::int32_t>(jrand::mthCos(f));
        add(s.str());
        std::ostringstream p;
        p << "perlinsimplex";
        double q[4][2] = {{0, 0}, {12.5, -3.25}, {-1000.125, 77.75}, {31.0, 5.0}};
        for (auto& v : q) {
            p << " " << bits(noise::PerlinSimplexNoise::temperatureNoise().getValue(v[0], v[1], false));
            p << " " << bits(noise::PerlinSimplexNoise::frozenTemperatureNoise().getValue(v[0], v[1], false));
        }
        add(p.str());
    }
    double np[5][3] = {{0, 0, 0}, {0.5, 1.25, -3.75}, {1234.567, -89.01, 2345.678}, {-0.001, 100.5, 7.25}, {1e6, 3.3, -1e6}};
    for (auto s : seeds) {
        auto str = std::to_string(s);
        {
            jrand::Xoroshiro128pp xr(s);
            noise::ImprovedNoise  in(xr);
            std::ostringstream    o;
            o << "improved " << str;
            for (auto& p : np) o << " " << bits(in.noise(p[0], p[1], p[2]));
            for (auto& p : np) o << " " << bits(in.noise(p[0], p[1], p[2], 0.5, 0.75));
            add(o.str());
        }
        {
            jrand::LegacyRandom lr(s);
            noise::SimplexNoise sn(lr);
            std::ostringstream  o;
            o << "simplex " << str;
            for (auto& p : np) o << " " << bits(sn.getValue(p[0], p[2]));
            for (auto& p : np) o << " " << bits(sn.getValue(p[0], p[1], p[2]));
            add(o.str());
        }
        {
            jrand::Xoroshiro128pp xr(s);
            auto                  pn = noise::PerlinNoise::create(xr, {-4, {1.0, 0.0, 2.0, 1.0}});
            std::ostringstream    o;
            o << "perlin " << str;
            for (auto& p : np) o << " " << bits(pn.getValue(p[0], p[1], p[2]));
            o << " max " << bits(pn.maxValue());
            add(o.str());
        }
        {
            jrand::LegacyRandom lr(s);
            auto                pl = noise::PerlinNoise::createLegacy(lr, {-5, {1.0, 0.0, 2.0, 1.0}});
            std::ostringstream  o;
            o << "perlinlegacy " << str;
            for (auto& p : np) o << " " << bits(pl.getValue(p[0], p[1], p[2]));
            o << " max " << bits(pl.maxValue());
            add(o.str());
        }
        noise::NoiseParameters params{-7, {1.0, 1.0, 0.0, 0.5}};
        {
            jrand::Xoroshiro128pp xr(s);
            auto                  nn = noise::NormalNoise::create(xr, params);
            std::ostringstream    o;
            o << "normal " << str;
            for (auto& p : np) o << " " << bits(nn.getValue(p[0], p[1], p[2]));
            o << " max " << bits(nn.maxValue());
            add(o.str());
        }
        {
            auto               pf    = jrand::Xoroshiro128pp(s).forkPositional();
            auto               keyed = noise::NormalNoise::create(*pf->fromHashOf("minecraft:gf_test"), params);
            std::ostringstream o;
            o << "keyednoise " << str;
            for (auto& p : np) o << " " << bits(keyed.getValue(p[0] * 0.25, p[1] * 0.5, p[2] * 0.25));
            add(o.str());
        }
    }
    return out;
}

} // namespace

TEST(WorldgenJavaParity, EveryOracleLineMatches) {
    auto cpp = cppLines();
    int  n   = 0;
    for (char const* line : kJavaGoldens) {
        std::string java(line);
        auto        sp  = java.find(' ');
        auto        sp2 = java.find(' ', sp + 1);
        auto        key = java.substr(0, sp2);
        auto        it  = cpp.find(key);
        ASSERT_NE(it, cpp.end()) << "no C++ line for " << key;
        EXPECT_EQ(it->second, java) << key;
        ++n;
    }
    EXPECT_EQ(n, 103);
}

TEST(WorldgenJavaParity, WellKnownJavaUtilRandomValues) {
    jrand::LegacyRandom r(0);
    EXPECT_EQ(r.nextInt(), -1155484576);
    jrand::LegacyRandom d(0);
    EXPECT_EQ(d.nextDouble(), 0.730967787376657);
    jrand::LegacyRandom g(0);
    EXPECT_EQ(g.nextGaussian(), 0.8025330637390305);
    jrand::LegacyRandom l(0);
    EXPECT_EQ(l.nextLong(), -4962768465676381896LL);
    jrand::LegacyRandom b(42);
    EXPECT_EQ(b.nextInt(10), 0);
}

TEST(WorldgenJavaParity, NoiseDensityUsesRandomStateSeeding) {
    // a "minecraft:noise" density samples NormalNoise.create(xoroshiro(seed).forkPositional()
    // .fromHashOf(<noise id>)) at (x * xz_scale, y * y_scale, z * xz_scale), like RandomState
    noise::NoiseParameters params{-7, {1.0, 1.0, 0.0, 0.5}};
    noise::Density::Resolver res;
    res.noise = [&](std::string const& id) -> std::optional<noise::NoiseParameters> {
        if (id == "minecraft:gf_test") return params;
        return std::nullopt;
    };
    for (std::int64_t seed : {0LL, 42LL, -123456789LL}) {
        auto d = noise::Density::fromJson(
            nlohmann::json::parse(R"({"type":"minecraft:noise","noise":"gf_test","xz_scale":0.25,"y_scale":0.5})"),
            seed,
            res
        );
        ASSERT_TRUE(d.has_value());
        auto keyed = noise::NormalNoise::create(*jrand::Xoroshiro128pp(seed).forkPositional()->fromHashOf("minecraft:gf_test"), params);
        for (auto p : {noise::DensityPoint{0, 0, 0}, noise::DensityPoint{17, -20, 33}, noise::DensityPoint{-400, 100, 900}}) {
            EXPECT_EQ(bits(d->compute(p)), bits(keyed.getValue(p.blockX * 0.25, p.blockY * 0.5, p.blockZ * 0.25)));
        }
        EXPECT_EQ(d->maxValue(), keyed.maxValue());
        EXPECT_EQ(d->minValue(), -keyed.maxValue());
    }
}
