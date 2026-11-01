// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the sound synthesizer (contract "synth", F3): deterministic math, JSON, expressions,
// the MDCT, the Vorbis encoder against an independent decoder (stb_vorbis), patches and tracks, and
// the committed audio (assets/gen/audio): every file decodes, every definition is TF's.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "StbVorbis.h"
#include "core/synth/Json.h"
#include "core/synth/Mdct.h"
#include "core/synth/Patch.h"
#include "core/synth/Random.h"
#include "core/synth/Sequencer.h"
#include "core/synth/SynthMath.h"
#include "core/synth/Vorbis.h"
#include "core/synth/Wav.h"

using namespace gf::core::synth;
using gf::tests::synth::decodeVorbis;

namespace {

double snrDb(std::vector<float> const& a, std::vector<float> const& b) {
    double sig = 0, err = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        sig += double(a[i]) * a[i];
        err += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
    }
    return 10.0 * std::log10(sig / std::max(err, 1e-30));
}

std::vector<float> testSignal(int rate, int n) {
    std::vector<float> pcm(static_cast<std::size_t>(n));
    Random             rng(7);
    for (int i = 0; i < n; ++i) {
        double t = double(i) / rate;
        pcm[std::size_t(i)] = float(0.4 * math::sin(math::kTwoPi * 440 * t) + 0.2 * math::sin(math::kTwoPi * 1250 * t) * (t > 0.3 ? 1 : 0)
                                    + 0.05 * rng.bipolar());
    }
    return pcm;
}

namespace fs = std::filesystem;

fs::path envPath(char const* name) {
    char*       value = nullptr;
    std::size_t size  = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
    fs::path p(value);
    std::free(value);
    return p;
}

// The repository root: $GF_ROOT, or found walking up from this source file or the working directory
// (xmake runs the tests from the build directory).
fs::path repoRoot() {
    static fs::path const dir = [] {
        if (fs::path env = envPath("GF_ROOT"); !env.empty()) return env;
        std::error_code ec;
        auto const      isRoot = [&](fs::path const& p) { return fs::exists(p / "tools/sounds/baseline/inventory.json", ec); };
        fs::path        here   = fs::path(__FILE__).parent_path();
        for (fs::path start : {here, fs::current_path(ec)}) {
            if (!start.is_absolute()) start = fs::current_path(ec) / start;
            for (fs::path p = start; !p.empty(); p = p.parent_path()) {
                if (isRoot(p)) return p;
                if (p == p.root_path()) break;
            }
        }
        return fs::path();
    }();
    return dir;
}

std::vector<std::uint8_t> readFile(fs::path const& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string readText(fs::path const& p) {
    std::ifstream      f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

} // namespace

TEST(SynthMath, MatchesTheCRuntime) {
    for (double x = -60.0; x <= 60.0; x += 0.0137) {
        EXPECT_NEAR(math::sin(x), std::sin(x), 1e-12) << x;
        EXPECT_NEAR(math::cos(x), std::cos(x), 1e-12) << x;
    }
    for (double x = -20.0; x <= 20.0; x += 0.071) {
        EXPECT_NEAR(math::exp(x) / std::exp(x), 1.0, 1e-13) << x;
        EXPECT_NEAR(math::tanh(x), std::tanh(x), 1e-13) << x;
    }
    for (double x = 1e-6; x <= 1e6; x *= 1.37) EXPECT_NEAR(math::log(x), std::log(x), 1e-12) << x;
    EXPECT_NEAR(math::pow(2.0, 10.0), 1024.0, 1e-9);
    EXPECT_NEAR(math::midiToHz(69), 440.0, 1e-9);
    EXPECT_NEAR(math::midiToHz(60), 261.6255653, 1e-6);
    EXPECT_NEAR(math::dbToGain(-6.0206), 0.5, 1e-4);
    EXPECT_EQ(math::round(2.5), 3.0);
    EXPECT_EQ(math::round(-2.5), -3.0);
}

TEST(SynthRandom, IsReproducible) {
    Random a(42), b(42), c(43);
    for (int i = 0; i < 100; ++i) {
        auto x = a.next();
        EXPECT_EQ(x, b.next());
        EXPECT_NE(x, c.next());
    }
    Random u(1);
    for (int i = 0; i < 1000; ++i) {
        double v = u.uniform();
        EXPECT_GE(v, 0.0);
        EXPECT_LT(v, 1.0);
    }
    EXPECT_EQ(hashString("mob/naga/hiss1"), hashString("mob/naga/hiss1"));
    EXPECT_NE(hashString("mob/naga/hiss1"), hashString("mob/naga/hiss2"));
}

TEST(SynthJson, ParsesAndDumps) {
    std::string const uEscape = std::string(1, '\\') + "u00e9"; // a JSON unicode escape
    auto j = Json::parse(R"json({"a": 1.5, "b": [1, "x", true, null], "c": {"d": -2e-3}, "s": "q\")json" + uEscape + "\"}");
    EXPECT_DOUBLE_EQ(j["a"].number(), 1.5);
    EXPECT_EQ(j["b"].size(), 4u);
    EXPECT_EQ(j["b"][1].string(), "x");
    EXPECT_TRUE(j["b"][2].boolean());
    EXPECT_TRUE(j["b"][3].isNull());
    EXPECT_DOUBLE_EQ(j["c"]["d"].number(), -0.002);
    EXPECT_EQ(j["s"].string(), "q\"\xc3\xa9");
    EXPECT_TRUE(j["missing"].isNull());
    EXPECT_EQ(j.members().front().first, "a"); // key order kept
    auto back = Json::parse(j.dump());
    EXPECT_EQ(back.dump(), j.dump());
    EXPECT_THROW((void)Json::parse("{\"a\": }"), JsonError);
    EXPECT_THROW((void)Json::parse("[1, 2"), JsonError);
    EXPECT_NO_THROW((void)Json::parse("// comment\n[1, 2,]"));
}

TEST(SynthExpression, Evaluates) {
    std::map<std::string, double> p{{"pitch", 2.0}, {"len", 0.5}};
    EXPECT_DOUBLE_EQ(evalExpression("$pitch*220+1", p), 441.0);
    EXPECT_DOUBLE_EQ(evalExpression("-(1+2)*3", p), -9.0);
    EXPECT_DOUBLE_EQ(evalExpression("2^10", p), 1024.0);
    EXPECT_DOUBLE_EQ(evalExpression("min($len, 0.2)", p), 0.2);
    EXPECT_NEAR(evalExpression("semi(12)", p), 2.0, 1e-12);
    EXPECT_NEAR(evalExpression("hz(69)", p), 440.0, 1e-9);
    EXPECT_DOUBLE_EQ(evalExpression("1e-3*1000", p), 1.0);
    double r = evalExpression("rand(5, 6)", p, 9);
    EXPECT_GE(r, 5.0);
    EXPECT_LT(r, 6.0);
    EXPECT_DOUBLE_EQ(r, evalExpression("rand(5, 6)", p, 9));
    EXPECT_THROW((void)evalExpression("$nope", p), PatchError);
    EXPECT_THROW((void)evalExpression("frobnicate(1)", p), PatchError);
}

TEST(SynthMdct, FastMatchesDefinition) {
    for (int n : {16, 64, 256, 2048}) {
        std::vector<double> x(static_cast<std::size_t>(n)), a(static_cast<std::size_t>(n / 2)), b(static_cast<std::size_t>(n / 2));
        Random              r{static_cast<std::uint64_t>(n)};
        for (auto& v : x) v = r.bipolar();
        Mdct(n).forward(x.data(), a.data());
        Mdct::forwardDirect(n, x.data(), b.data());
        for (int k = 0; k < n / 2; ++k) EXPECT_NEAR(a[std::size_t(k)], b[std::size_t(k)], 1e-9 * n) << "n=" << n << " k=" << k;
    }
}

TEST(SynthVorbis, DecodesWithStbVorbis) {
    for (int rate : {44100, 22050}) {
        for (double q : {0.0, 0.4, 1.0}) {
            int  n   = rate + 777; // not a multiple of the block size: the end is trimmed exactly
            auto pcm = testSignal(rate, n);
            VorbisOptions o;
            o.sampleRate = rate;
            o.quality    = q;
            auto ogg     = encodeVorbis(pcm, o);
            int  ch = 0, sr = 0;
            auto dec     = decodeVorbis(ogg, &ch, &sr);
            ASSERT_EQ(ch, 1);
            ASSERT_EQ(sr, rate);
            ASSERT_EQ(dec.size(), pcm.size()) << "rate " << rate << " q " << q;
            double snr = snrDb(pcm, dec);
            EXPECT_GT(snr, q < 0.2 ? 12.0 : (q < 0.6 ? 18.0 : 24.0)) << "rate " << rate << " q " << q;
            auto info = probeOggVorbis(ogg);
            EXPECT_TRUE(info.valid) << info.error;
            EXPECT_EQ(info.samples, std::int64_t(pcm.size()));
        }
    }
}

TEST(SynthVorbis, SilenceShortAndLoud) {
    VorbisOptions o;
    for (std::size_t n : {std::size_t(1), std::size_t(100), std::size_t(1024), std::size_t(5000)}) {
        std::vector<float> silent(n, 0.0f);
        auto               dec = decodeVorbis(encodeVorbis(silent, o));
        ASSERT_EQ(dec.size(), n);
        for (float v : dec) EXPECT_NEAR(v, 0.0f, 1e-4f);
    }
    std::vector<float> loud(20000);
    for (std::size_t i = 0; i < loud.size(); ++i) loud[i] = (i / 50) % 2 ? 0.99f : -0.99f; // square wave
    auto dec = decodeVorbis(encodeVorbis(loud, o));
    ASSERT_EQ(dec.size(), loud.size());
    EXPECT_GT(snrDb(loud, dec), 12.0);
}

TEST(SynthVorbis, IsDeterministicAndChecksPages) {
    auto          pcm = testSignal(44100, 30000);
    VorbisOptions o;
    auto          a = encodeVorbis(pcm, o);
    auto          b = encodeVorbis(pcm, o);
    EXPECT_EQ(a, b);
    a[a.size() / 2] ^= 0x40; // a flipped bit fails the page CRC
    EXPECT_FALSE(probeOggVorbis(a).valid);
    std::uint8_t const zero[1] = {0};
    EXPECT_EQ(oggCrc(zero, 0), 0u);
}

TEST(SynthWav, RoundTrips) {
    std::vector<float> pcm{0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    int                rate = 0;
    auto               back = decodeWav(encodeWav(pcm, 32000), &rate);
    EXPECT_EQ(rate, 32000);
    ASSERT_EQ(back.size(), pcm.size());
    for (std::size_t i = 0; i < pcm.size(); ++i) EXPECT_NEAR(back[i], pcm[i], 1.0 / 16384);
}

TEST(SynthPatch, RendersDeterministically) {
    auto patch = Json::parse(R"json({
        "params": {"pitch": 220, "len": 0.3},
        "length": "$len", "tail": 0.1,
        "layers": [
            {"type": "osc", "wave": "saw", "freq": {"from": "$pitch", "to": "$pitch*2"}, "env": {"a": 0.01, "d": 0.1, "s": 0.5, "r": 0.05},
             "filter": [{"type": "lp", "freq": 2000}], "formant": {"vowel": "a"}},
            {"type": "noise", "color": "pink", "amp": "rand(0.1, 0.3)", "filter": [{"type": "bp", "freq": 900, "q": 2}]},
            {"type": "modal", "modes": "glass", "freq": 1200, "length": 0.01, "ring": 0.2},
            {"type": "grains", "density": 50, "freq": [500, 2000], "wave": "noise"},
            {"type": "fm", "freq": 330, "ratio": 2, "index": {"env": [[0, 4], [0.3, 0]]}, "amp": 0.3},
            {"type": "pluck", "freq": 196, "decay": 0.5, "amp": 0.5},
            {"type": "osc", "freq": 880, "amp": 0.1, "length": 0.02, "repeat": {"count": 3, "every": 0.05}}
        ],
        "fx": [{"type": "echo", "time": 0.05}, {"type": "reverb", "mix": 0.2}, {"type": "filter", "filter": "hp", "freq": 60}]
    })json");
    auto a = renderPatch(patch, Json::object(), 5, 44100);
    auto b = renderPatch(patch, Json::object(), 5, 44100);
    auto c = renderPatch(patch, Json::object(), 6, 44100);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    float peak = 0;
    for (float v : a) peak = std::max(peak, std::fabs(v));
    EXPECT_NEAR(peak, 0.891, 0.01); // normalised to -1 dBFS
    EXPECT_GT(a.size(), std::size_t(0.3 * 44100));
    EXPECT_LT(a.size(), std::size_t(0.5 * 44100));
    Json over = Json::object();
    over.set("len", 0.6);
    EXPECT_GT(renderPatch(patch, over, 5, 44100).size(), a.size());
}

TEST(SynthPatch, ReportsBadInput) {
    EXPECT_THROW((void)renderPatch(Json::parse(R"json({"layers": [{"type": "kazoo"}]})json"), Json(), 1, 44100), PatchError);
    EXPECT_THROW((void)renderPatch(Json::parse(R"json({"length": 1})json"), Json(), 1, 44100), PatchError);
    EXPECT_THROW((void)renderPatch(Json("missing/patch"), Json(), 1, 44100), PatchError);
    EXPECT_THROW((void)renderPatch(Json::parse(R"json({"layers": [{"freq": "$undefined"}]})json"), Json(), 1, 44100), PatchError);
}

TEST(SynthPatch, UsesTheLibrary) {
    PatchLibrary lib;
    lib.add("beep", Json::parse(R"json({"params": {"f": 440}, "length": 0.1, "layers": [{"freq": "$f"}]})json"));
    auto outer = Json::parse(R"json({"length": 0.2, "layers": [{"type": "patch", "patch": "beep", "params": {"f": 660}}]})json");
    auto pcm   = renderPatch(outer, Json(), 1, 22050, &lib);
    EXPECT_GT(pcm.size(), std::size_t(0.1 * 22050));
    EXPECT_FALSE(renderPatch(Json("beep"), Json(), 1, 22050, &lib).empty());
}

TEST(SynthSequencer, ParsesNotes) {
    EXPECT_EQ(parseNote("C4"), 60);
    EXPECT_EQ(parseNote("A4"), 69);
    EXPECT_EQ(parseNote("F#3"), 54);
    EXPECT_EQ(parseNote("Bb2"), 46);
    EXPECT_EQ(parseNote("c-1"), 0);
    EXPECT_EQ(parseNote("H2"), -1);
    EXPECT_EQ(parseNote("C"), -1);
    EXPECT_EQ(parseNote("C4x"), -1);
}

TEST(SynthSequencer, RendersExactLength) {
    PatchLibrary lib;
    lib.add("tone", Json::parse(R"json({"params": {"freq": 440, "dur": 0.2}, "length": "$dur", "layers": [{"freq": "$freq"}]})json"));
    auto track = Json::parse(R"json({
        "bpm": 120, "rpb": 4, "length": 3.0, "seed": 3,
        "instruments": {"t": {"patch": "tone", "gain": -6}},
        "patterns": {"A": {"rows": 8, "channels": [{"inst": "t", "notes": "C4 . E4:0.5 - G4+C5 . . ."}]}},
        "order": ["A", {"p": "A", "t": 2}]
    })json");
    auto a = renderTrack(track, 16000, &lib);
    EXPECT_EQ(a.size(), std::size_t(3 * 16000));
    EXPECT_EQ(a, renderTrack(track, 16000, &lib));
    float peak = 0;
    for (float v : a) peak = std::max(peak, std::fabs(v));
    EXPECT_GT(peak, 0.5f);
    auto bad = Json::parse(R"json({"bpm": 120, "instruments": {}, "patterns": {"A": {"rows": 4, "channels": [{"inst": "x", "notes": "C4"}]}}, "order": ["A"]})json");
    EXPECT_THROW((void)renderTrack(bad, 16000, &lib), PatchError);
}

TEST(SynthPatch, NegativeStartCutsTheHead) {
    // the same layer starting 0.1 s before the patch: its first 0.1 s is cut off, not delayed
    auto patch = [](double start) {
        return Json::parse(
            R"json({"length": 0.5, "tail": 0, "normalize": false, "fade": 0, "layers": [
                {"type": "osc", "wave": "sine", "freq": 100, "amp": 0.5, "length": 0.4, "env": {"a": 0.0001, "d": 0, "s": 1, "r": 0.0001},
                 "start": )json"
            + std::to_string(start) + "}]}"
        );
    };
    int const rate = 8000;
    auto      a    = renderPatch(patch(-0.1), Json(), 1, rate);
    auto      b    = renderPatch(patch(0.0), Json(), 1, rate);
    ASSERT_GT(b.size(), std::size_t(0.35 * rate));
    ASSERT_LT(a.size(), b.size());
    std::size_t const shift = std::size_t(0.1 * rate);
    for (std::size_t i = 0; i < std::size_t(0.25 * rate); ++i) ASSERT_FLOAT_EQ(a[i], b[i + shift]) << i;
}

// Every committed sound decodes with an independent decoder (stb_vorbis) to the length its last
// granule position announces: mono, 22.05 or 44.1 kHz, finite samples near -1..1 (a lossy decode may\n// overshoot full scale a little; garbage would not stay this close).
TEST(SynthAssets, EveryFileDecodes) {
    auto const root = repoRoot();
    ASSERT_FALSE(root.empty()) << "repository root not found (set GF_ROOT)";
    int files = 0;
    for (auto const& entry : fs::recursive_directory_iterator(root / "assets/gen/audio")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ogg") continue;
        ++files;
        auto const bytes = readFile(entry.path());
        auto const info  = probeOggVorbis(bytes);
        auto const name  = fs::relative(entry.path(), root).generic_string();
        ASSERT_TRUE(info.valid) << name << ": " << info.error;
        int  channels = 0, rate = 0;
        auto pcm = decodeVorbis(bytes, &channels, &rate);
        EXPECT_EQ(channels, 1) << name;
        EXPECT_TRUE(rate == 22050 || rate == 44100) << name << ": " << rate << " Hz";
        EXPECT_EQ(std::int64_t(pcm.size()), info.samples) << name;
        EXPECT_GT(pcm.size(), std::size_t(0)) << name;
        bool finite = std::all_of(pcm.begin(), pcm.end(), [](float x) { return std::isfinite(x) && std::fabs(x) <= 1.5f; });
        EXPECT_TRUE(finite) << name << ": a sample is not finite or far outside -1..1";
    }
    EXPECT_GT(files, 200);
}

// Every committed sound_definitions fragment has TF's variants (count, volume, pitch, stream),
// subtitle and category (tools/sounds/baseline/inventory.json, generated from TF's sounds.json), and
// every file it names exists.
TEST(SynthAssets, DefinitionsMatchTheInventory) {
    auto const root = repoRoot();
    ASSERT_FALSE(root.empty()) << "repository root not found (set GF_ROOT)";
    auto const inventory = Json::parse(readText(root / "tools/sounds/baseline/inventory.json"))["events"];
    ASSERT_TRUE(inventory.isObject());
    std::map<std::string, Json> defs;
    for (auto const& unit : fs::directory_iterator(root / "assets/gen/audio")) {
        auto const fragment = unit.path() / "rp/sounds.d" / (unit.path().filename().string() + ".json");
        if (!fs::exists(fragment)) continue;
        auto const doc = Json::parse(readText(fragment));
        for (auto const& [id, def] : doc.members()) {
            EXPECT_EQ(defs.count(id), 0u) << id << " defined twice";
            defs[id] = def;
            for (auto const& s : def["sounds"].items()) {
                auto const name = s.isString() ? s.string() : s.str("name", "");
                if (name.rfind("sounds/twilightforest/", 0) == 0) EXPECT_TRUE(fs::exists(unit.path() / "rp" / (name + ".ogg"))) << id << ": " << name;
            }
        }
    }
    auto keys = [](Json const& variants) {
        std::vector<std::string> out;
        for (auto const& v : variants.items()) {
            double vol = 1, pitch = 1;
            bool   stream = false;
            if (v.isObject()) {
                vol    = v["volume"].isNull() ? 1.0 : v["volume"].number();
                pitch  = v["pitch"].isNull() ? 1.0 : v["pitch"].number();
                stream = v["stream"].isBool() && v["stream"].boolean();
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%.4f|%.4f|%d", vol, pitch, stream ? 1 : 0);
            out.emplace_back(buf);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    EXPECT_EQ(defs.size(), inventory.members().size());
    for (auto const& [id, want] : inventory.members()) {
        auto it = defs.find(id);
        if (it == defs.end()) {
            ADD_FAILURE() << id << ": no definition";
            continue;
        }
        auto const& def = it->second;
        EXPECT_EQ(def.str("category", ""), want.str("category", "")) << id;
        EXPECT_EQ(def.str("subtitle", ""), want.str("subtitle", "")) << id;
        EXPECT_EQ(keys(def["sounds"]), keys(want["variants"])) << id;
    }
    // TF's parrot imitations: the imitated mob's sounds at pitch 1.8, volume 0.6
    auto const& parrot = defs["entity.twilightforest.parrot.imitate.alpha_yeti"];
    ASSERT_TRUE(parrot["sounds"].isArray());
    for (auto const& s : parrot["sounds"].items()) {
        EXPECT_DOUBLE_EQ(s["pitch"].number(), 1.8);
        EXPECT_DOUBLE_EQ(s["volume"].number(), 0.6);
    }
}