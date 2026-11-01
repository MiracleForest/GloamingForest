// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the patch interpreter (procedural sound recipes -> PCM).
#include "core/synth/Patch.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "core/synth/Dsp.h"
#include "core/synth/Random.h"
#include "core/synth/SynthMath.h"

namespace gf::core::synth {

namespace {

constexpr double kMaxSeconds = 600.0;
constexpr int    kMaxDepth   = 8;

// ---- expressions --------------------------------------------------------------------------------------

struct Scope {
    std::map<std::string, double> params;
    Random*                       rng = nullptr;
};

class ExprParser {
public:
    ExprParser(std::string_view s, Scope& scope) : mS(s), mScope(scope) {}

    double parse() {
        double v = expr();
        space();
        if (mPos != mS.size()) fail("unexpected '" + std::string(1, mS[mPos]) + "'");
        return v;
    }

private:
    [[noreturn]] void fail(std::string const& what) const {
        throw PatchError("expression \"" + std::string(mS) + "\": " + what);
    }
    void space() {
        while (mPos < mS.size() && std::isspace(static_cast<unsigned char>(mS[mPos]))) ++mPos;
    }
    bool eat(char c) {
        space();
        if (mPos < mS.size() && mS[mPos] == c) {
            ++mPos;
            return true;
        }
        return false;
    }
    double expr() {
        double v = term();
        while (true) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term() {
        double v = power();
        while (true) {
            if (eat('*')) v *= power();
            else if (eat('/')) {
                double d = power();
                v        = d == 0 ? 0 : v / d;
            } else return v;
        }
    }
    double power() {
        double v = unary();
        if (eat('^')) {
            double e = power();
            v        = v > 0 ? math::pow(v, e) : (e == 0 ? 1.0 : 0.0);
        }
        return v;
    }
    double unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return primary();
    }
    std::string ident() {
        std::size_t start = mPos;
        while (mPos < mS.size() && (std::isalnum(static_cast<unsigned char>(mS[mPos])) || mS[mPos] == '_' || mS[mPos] == '.')) ++mPos;
        return std::string(mS.substr(start, mPos - start));
    }
    double primary() {
        space();
        if (mPos >= mS.size()) fail("unexpected end");
        char c = mS[mPos];
        if (c == '(') {
            ++mPos;
            double v = expr();
            if (!eat(')')) fail("missing ')'");
            return v;
        }
        if (c == '$') {
            ++mPos;
            std::string name = ident();
            auto        it   = mScope.params.find(name);
            if (it == mScope.params.end()) fail("unknown parameter $" + name);
            return it->second;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            std::size_t start = mPos;
            while (mPos < mS.size()
                   && (std::isdigit(static_cast<unsigned char>(mS[mPos])) || mS[mPos] == '.'
                       || ((mS[mPos] == 'e' || mS[mPos] == 'E') && mPos + 1 < mS.size()
                           && (std::isdigit(static_cast<unsigned char>(mS[mPos + 1])) || mS[mPos + 1] == '-' || mS[mPos + 1] == '+'))
                       || ((mS[mPos] == '-' || mS[mPos] == '+') && mPos > start && (mS[mPos - 1] == 'e' || mS[mPos - 1] == 'E')))) {
                ++mPos;
            }
            return Json::parse(mS.substr(start, mPos - start)).number();
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string name = ident();
            if (name == "pi") return math::kPi;
            if (!eat('(')) fail("unknown name " + name);
            std::vector<double> args;
            if (!eat(')')) {
                do {
                    args.push_back(expr());
                } while (eat(','));
                if (!eat(')')) fail("missing ')' after arguments of " + name);
            }
            return call(name, args);
        }
        fail("unexpected '" + std::string(1, c) + "'");
    }
    double arg(std::vector<double> const& a, std::size_t i, std::string const& fn) const {
        if (i >= a.size()) fail(fn + "() needs " + std::to_string(i + 1) + " argument(s)");
        return a[i];
    }
    double call(std::string const& fn, std::vector<double> const& a) {
        Random& rng = *mScope.rng;
        if (fn == "rand") return rng.uniform(arg(a, 0, fn), arg(a, 1, fn));
        if (fn == "irand") {
            double lo = math::floor(arg(a, 0, fn));
            double hi = math::floor(arg(a, 1, fn));
            return lo + double(rng.below(int(hi - lo) + 1));
        }
        if (fn == "choose") {
            if (a.empty()) fail("choose() needs arguments");
            return a[std::size_t(rng.below(int(a.size())))];
        }
        if (fn == "min") return std::min(arg(a, 0, fn), arg(a, 1, fn));
        if (fn == "max") return std::max(arg(a, 0, fn), arg(a, 1, fn));
        if (fn == "clamp") return math::clamp(arg(a, 0, fn), arg(a, 1, fn), arg(a, 2, fn));
        if (fn == "abs") return math::abs(arg(a, 0, fn));
        if (fn == "floor") return math::floor(arg(a, 0, fn));
        if (fn == "round") return math::round(arg(a, 0, fn));
        if (fn == "semi") return math::semitones(arg(a, 0, fn));
        if (fn == "hz") return math::midiToHz(arg(a, 0, fn));
        if (fn == "db") return math::dbToGain(arg(a, 0, fn));
        if (fn == "exp") return math::exp(arg(a, 0, fn));
        if (fn == "log") return math::log(arg(a, 0, fn));
        if (fn == "sqrt") return math::sqrt(arg(a, 0, fn));
        if (fn == "lerp") return arg(a, 0, fn) + (arg(a, 1, fn) - arg(a, 0, fn)) * arg(a, 2, fn);
        fail("unknown function " + fn + "()");
    }

    std::string_view mS;
    Scope&           mScope;
    std::size_t      mPos = 0;
};

double evalNum(Json const& v, Scope& scope, double def) {
    if (v.isNull()) return def;
    if (v.isNumber() || v.isBool()) return v.number();
    if (v.isString()) return ExprParser(v.string(), scope).parse();
    throw PatchError("expected a number or an expression, got " + v.dump());
}

double evalKey(Json const& obj, std::string_view key, Scope& scope, double def) { return evalNum(obj[key], scope, def); }

// ---- time-varying values -------------------------------------------------------------------------------

std::uint64_t mixSeed(std::uint64_t a, std::uint64_t b) {
    std::uint64_t s = a ^ (b * 0x9E3779B97F4A7C15ull);
    return splitMix64(s);
}

double hashUniform(std::uint64_t seed, std::int64_t k) {
    std::uint64_t s = mixSeed(seed, std::uint64_t(k));
    return double(splitMix64(s) >> 11) * (1.0 / 9007199254740992.0);
}

class Value {
public:
    static Value constant(double v) {
        Value x;
        x.mConst = v;
        return x;
    }

    static Value compile(Json const& j, Scope& scope, double def) {
        Value v;
        if (j.isNull() || j.isNumber() || j.isString() || j.isBool()) {
            v.mConst = evalNum(j, scope, def);
            return v;
        }
        if (j.isArray()) { // shorthand: [[t, v], ...] breakpoints
            Json e = Json::object();
            e.set("env", j);
            return compile(e, scope, def);
        }
        if (!j.isObject()) throw PatchError("bad value " + j.dump());
        if (auto const* env = j.find("env")) {
            v.mKind = Kind::Env;
            if (!env->isArray() || env->size() == 0) throw PatchError("\"env\" needs [[time, value], ...]");
            for (auto const& p : env->items()) {
                if (!p.isArray() || p.size() < 2) throw PatchError("\"env\" points are [time, value]");
                v.mPts.emplace_back(evalNum(p[0], scope, 0), evalNum(p[1], scope, 0));
            }
            v.mRel   = j.flag("rel", false);
            v.mCurve = curveOf(j.str("curve", "lin"));
            return v;
        }
        if (j.has("from") || j.has("to")) {
            v.mKind  = Kind::Sweep;
            v.mFrom  = evalKey(j, "from", scope, def);
            v.mTo    = evalKey(j, "to", scope, v.mFrom);
            v.mTime  = evalKey(j, "time", scope, -1);
            v.mDelay = evalKey(j, "delay", scope, 0);
            v.mCurve = curveOf(j.str("curve", "exp"));
            return v;
        }
        if (auto const* lfo = j.find("lfo")) {
            v.mKind  = Kind::Lfo;
            v.mWave  = lfo->isString() ? lfo->string() : "sine";
            v.mRate  = evalKey(j, "rate", scope, 5);
            v.mDepth = evalKey(j, "depth", scope, 0);
            v.mPhase = evalKey(j, "phase", scope, 0);
            v.mSeed  = scope.rng->next();
            v.mArgs.push_back(compile(j["center"], scope, def));
            return v;
        }
        if (auto const* mul = j.find("mul")) {
            v.mKind = Kind::Mul;
            for (auto const& x : mul->items()) v.mArgs.push_back(compile(x, scope, 1));
            return v;
        }
        if (auto const* add = j.find("add")) {
            v.mKind = Kind::Add;
            for (auto const& x : add->items()) v.mArgs.push_back(compile(x, scope, 0));
            return v;
        }
        throw PatchError("unknown value " + j.dump());
    }

    [[nodiscard]] bool isConstant() const {
        if (mKind == Kind::Const) return true;
        if (mKind == Kind::Mul || mKind == Kind::Add) {
            return std::all_of(mArgs.begin(), mArgs.end(), [](Value const& a) { return a.isConstant(); });
        }
        return false;
    }

    /// The value `t` seconds into a layer of `len` seconds.
    [[nodiscard]] double at(double t, double len) const {
        switch (mKind) {
        case Kind::Const: return mConst;
        case Kind::Env: {
            double tt = mRel && len > 0 ? t / len : t;
            if (tt <= mPts.front().first) return mPts.front().second;
            for (std::size_t i = 1; i < mPts.size(); ++i) {
                if (tt < mPts[i].first) {
                    double t0 = mPts[i - 1].first, t1 = mPts[i].first;
                    double x  = t1 > t0 ? (tt - t0) / (t1 - t0) : 1.0;
                    return interp(mPts[i - 1].second, mPts[i].second, x);
                }
            }
            return mPts.back().second;
        }
        case Kind::Sweep: {
            double dur = mTime > 0 ? mTime : len - mDelay;
            double x   = dur > 0 ? math::clamp((t - mDelay) / dur, 0.0, 1.0) : 1.0;
            return interp(mFrom, mTo, x);
        }
        case Kind::Lfo: {
            double c = mArgs[0].at(t, len);
            double p = t * mRate + mPhase;
            double w = 0;
            if (mWave == "noise") {
                w = hashUniform(mSeed, std::int64_t(math::floor(p))) * 2.0 - 1.0;
            } else if (mWave == "smooth") {
                double       fl = math::floor(p);
                double       x  = p - fl;
                double       a  = hashUniform(mSeed, std::int64_t(fl)) * 2.0 - 1.0;
                double       b  = hashUniform(mSeed, std::int64_t(fl) + 1) * 2.0 - 1.0;
                double const s  = x * x * (3.0 - 2.0 * x);
                w               = a + (b - a) * s;
            } else {
                double ph = p - math::floor(p);
                if (mWave == "tri" || mWave == "triangle") w = 1.0 - 4.0 * math::abs(ph - 0.5);
                else if (mWave == "square") w = ph < 0.5 ? 1.0 : -1.0;
                else if (mWave == "saw") w = 2.0 * ph - 1.0;
                else w = math::sin(math::kTwoPi * ph);
            }
            return c + mDepth * w;
        }
        case Kind::Mul: {
            double r = 1;
            for (auto const& a : mArgs) r *= a.at(t, len);
            return r;
        }
        case Kind::Add: {
            double r = 0;
            for (auto const& a : mArgs) r += a.at(t, len);
            return r;
        }
        }
        return 0;
    }

private:
    enum class Kind { Const, Env, Sweep, Lfo, Mul, Add };
    static int curveOf(std::string const& s) { return s == "exp" ? 1 : (s == "step" ? 2 : (s == "smooth" ? 3 : 0)); }
    double     interp(double a, double b, double x) const {
        switch (mCurve) {
        case 1:
            if (a > 0 && b > 0) return a * math::pow(b / a, x);
            return a + (b - a) * x;
        case 2:  return x < 1.0 ? a : b;
        case 3:  return a + (b - a) * x * x * (3.0 - 2.0 * x);
        default: return a + (b - a) * x;
        }
    }

    Kind                                   mKind  = Kind::Const;
    double                                 mConst = 0;
    std::vector<std::pair<double, double>> mPts;
    bool                                   mRel   = false;
    int                                    mCurve = 0;
    double                                 mFrom = 0, mTo = 0, mTime = -1, mDelay = 0;
    std::string                            mWave;
    double                                 mRate = 0, mDepth = 0, mPhase = 0;
    std::uint64_t                          mSeed = 0;
    std::vector<Value>                     mArgs;
};

// ---- presets --------------------------------------------------------------------------------------------

struct ModeSpec {
    double ratio, decay, gain;
};

std::vector<ModeSpec> modalPreset(std::string const& name) {
    if (name == "wood") return {{1.0, 0.12, 1.0}, {2.57, 0.07, 0.5}, {4.21, 0.045, 0.3}, {6.13, 0.03, 0.18}};
    if (name == "metal")
        return {{1.0, 1.4, 1.0}, {2.756, 1.0, 0.6}, {5.404, 0.75, 0.45}, {8.933, 0.55, 0.3}, {13.34, 0.4, 0.2}};
    if (name == "glass") return {{1.0, 0.9, 1.0}, {2.32, 0.55, 0.6}, {4.25, 0.4, 0.4}, {6.63, 0.28, 0.25}, {9.38, 0.2, 0.15}};
    if (name == "bell")
        return {{0.56, 2.0, 0.6}, {0.92, 1.6, 0.5}, {1.19, 1.4, 1.0}, {1.71, 1.0, 0.5}, {2.0, 0.9, 0.6}, {2.74, 0.7, 0.4}, {3.0, 0.6, 0.35}, {3.76, 0.45, 0.25}, {4.07, 0.4, 0.2}};
    if (name == "stone") return {{1.0, 0.05, 1.0}, {1.73, 0.035, 0.7}, {2.91, 0.025, 0.5}, {4.4, 0.02, 0.3}};
    if (name == "bone") return {{1.0, 0.06, 1.0}, {2.2, 0.04, 0.6}, {3.9, 0.03, 0.4}, {5.8, 0.02, 0.25}};
    if (name == "ice") return {{1.0, 0.25, 1.0}, {2.9, 0.18, 0.7}, {5.2, 0.12, 0.5}, {8.3, 0.09, 0.35}, {12.1, 0.06, 0.2}};
    if (name == "drum") return {{1.0, 0.3, 1.0}, {1.59, 0.2, 0.5}, {2.14, 0.15, 0.35}, {2.3, 0.12, 0.3}, {2.65, 0.1, 0.2}};
    throw PatchError("unknown modal preset \"" + name + "\" (wood, metal, glass, bell, stone, bone, ice, drum)");
}

struct Formant {
    double f, gainDb, bw;
};

std::array<Formant, 3> vowel(int i) {
    static constexpr std::array<std::array<Formant, 3>, 5> table = {{
        {{{800, 0, 80}, {1150, -6, 90}, {2900, -32, 120}}},  // a
        {{{400, 0, 60}, {1700, -14, 80}, {2600, -18, 120}}}, // e
        {{{250, 0, 60}, {1750, -30, 90}, {2600, -16, 100}}}, // i
        {{{400, 0, 40}, {750, -11, 80}, {2400, -21, 100}}},  // o
        {{{350, 0, 40}, {600, -20, 80}, {2400, -32, 100}}},  // u
    }};
    return table[std::size_t(std::clamp(i, 0, 4))];
}

// ---- rendering --------------------------------------------------------------------------------------------

struct Ctx {
    int                 rate;
    double              patchLength;
    std::uint64_t       seed;
    PatchLibrary const* library;
    int                 depth;
};

Json const& resolvePatch(Json const& p, PatchLibrary const* lib) {
    if (p.isString()) {
        Json const* found = lib ? lib->find(p.string()) : nullptr;
        if (!found) throw PatchError("unknown patch \"" + p.string() + "\"");
        return *found;
    }
    if (!p.isObject()) throw PatchError("a patch is an object or a library name");
    return p;
}

/// Adds `src` (times `gain`) to `out` from sample `at`; a negative `at` (a layer that starts before
/// the patch) cuts off the head of `src` instead of delaying it.
void mixInto(std::vector<double>& out, std::vector<double> const& src, std::int64_t at, double gain) {
    std::size_t skip = 0;
    if (at < 0) {
        skip = std::size_t(-at);
        at   = 0;
    }
    if (skip >= src.size()) return;
    std::size_t need = std::size_t(at) + (src.size() - skip);
    if (out.size() < need) out.resize(need, 0.0);
    for (std::size_t i = skip; i < src.size(); ++i) out[std::size_t(at) + (i - skip)] += src[i] * gain;
}

std::vector<double> renderPatchBuffer(Json const& patch, Scope& parent, Json const& params, std::uint64_t seed, int rate, PatchLibrary const* lib, int depth, bool finishIt);

/// Source samples of one layer (before amplitude, filters and shaping).
std::vector<double> layerSource(Json const& L, std::string const& type, Scope& scope, Ctx const& ctx, double len, std::size_t n, std::uint64_t seed) {
    double const        rate = ctx.rate;
    std::vector<double> buf(n, 0.0);
    if (type == "osc") {
        auto   wave  = dsp::parseWave(L.str("wave", "sine"));
        Value  freq  = Value::compile(L["freq"], scope, 440);
        Value  pw    = Value::compile(L["pw"], scope, 0.5);
        double vRate = 0, vDepth = 0, vDelay = 0;
        if (auto const* vib = L.find("vibrato")) {
            vRate  = evalKey(*vib, "rate", scope, 5);
            vDepth = evalKey(*vib, "depth", scope, 0.3);
            vDelay = evalKey(*vib, "delay", scope, 0);
        }
        int    voices = 1;
        double detune = 0;
        if (auto const* uni = L.find("unison")) {
            voices = std::clamp(int(evalKey(*uni, "voices", scope, 3)), 1, 9);
            detune = evalKey(*uni, "detune", scope, 12);
        }
        Random              prng(seed);
        std::vector<double> phases(static_cast<std::size_t>(voices));
        double              startPhase = evalKey(L, "phase", scope, -1);
        for (auto& p : phases) p = startPhase >= 0 ? startPhase : prng.uniform();
        bool const constFreq = freq.isConstant() && pw.isConstant();
        double     f0        = freq.at(0, len);
        double     w0        = pw.at(0, len);
        for (std::size_t i = 0; i < n; ++i) {
            double t = double(i) / rate;
            double f = constFreq ? f0 : freq.at(t, len);
            if (vDepth != 0 && t >= vDelay) f *= math::semitones(vDepth * math::sin(math::kTwoPi * vRate * (t - vDelay)));
            double w = constFreq ? w0 : pw.at(t, len);
            double s = 0;
            for (int v = 0; v < voices; ++v) {
                double cents = voices > 1 ? detune * (2.0 * v / (voices - 1) - 1.0) : 0.0;
                double fv    = cents != 0 ? f * math::semitones(cents / 100.0) : f;
                double dt    = math::clamp(fv / rate, 0.0, 0.49);
                double& ph   = phases[std::size_t(v)];
                s += dsp::oscSample(wave, ph, dt, w);
                ph += dt;
                ph -= math::floor(ph);
            }
            buf[i] = s / double(voices);
        }
    } else if (type == "noise") {
        dsp::Noise noise(dsp::parseNoise(L.str("color", "white")), seed);
        Value      hold = Value::compile(L["hold"], scope, 0);
        double     held = 0, acc = 1.0;
        for (std::size_t i = 0; i < n; ++i) {
            double h = hold.at(double(i) / rate, len);
            if (h > 0) {
                acc += h / rate;
                if (acc >= 1.0) {
                    acc -= math::floor(acc);
                    held = noise.next();
                }
                buf[i] = held;
            } else {
                buf[i] = noise.next();
            }
        }
    } else if (type == "fm") {
        Value  freq     = Value::compile(L["freq"], scope, 440);
        Value  index    = Value::compile(L["index"], scope, 2);
        double ratio    = evalKey(L, "ratio", scope, 1);
        double modFixed = evalKey(L, "modFreq", scope, 0);
        double feedback = evalKey(L, "feedback", scope, 0);
        auto   carrier  = dsp::parseWave(L.str("wave", "sine"));
        double pc = 0, pm = 0, prev = 0;
        for (std::size_t i = 0; i < n; ++i) {
            double t  = double(i) / rate;
            double f  = freq.at(t, len);
            double fm = modFixed > 0 ? modFixed : f * ratio;
            double m  = math::sin(math::kTwoPi * pm + feedback * prev);
            prev      = m;
            double ph = pc + index.at(t, len) * m / math::kTwoPi;
            ph -= math::floor(ph);
            buf[i] = dsp::oscSample(carrier, ph, f / rate, 0.5);
            pc += f / rate;
            pc -= math::floor(pc);
            pm += fm / rate;
            pm -= math::floor(pm);
        }
    } else if (type == "pluck") {
        dsp::Pluck pl(evalKey(L, "freq", scope, 220), evalKey(L, "decay", scope, 1.0), evalKey(L, "bright", scope, 0.5), rate, seed);
        for (std::size_t i = 0; i < n; ++i) buf[i] = pl.next();
    } else if (type == "modal") {
        double                base   = evalKey(L, "freq", scope, 440);
        double                dscale = evalKey(L, "decay", scope, 1);
        double                spread = evalKey(L, "spread", scope, 0);
        std::vector<ModeSpec> modes;
        Json const&           m = L["modes"];
        if (m.isString()) modes = modalPreset(m.string());
        else if (m.isArray()) {
            for (auto const& x : m.items()) {
                modes.push_back({evalNum(x[0], scope, 1), evalNum(x[1], scope, 0.2), evalNum(x[2], scope, 1)});
            }
        } else modes = modalPreset("wood");
        Random           mrng(seed);
        dsp::ModalBank   bank;
        for (auto const& md : modes) {
            double f = base * md.ratio * (spread > 0 ? math::semitones(mrng.bipolar() * spread) : 1.0);
            bank.add(f, md.decay * dscale, md.gain, mrng.uniform(), rate);
        }
        double     strike = evalKey(L, "strike", scope, 0.004);
        dsp::Noise nz(dsp::NoiseColor::White, seed ^ 0x5157);
        std::size_t sn = std::size_t(strike * rate);
        for (std::size_t i = 0; i < n; ++i) {
            double s = bank.next();
            if (i < sn) s += nz.next() * 0.5 * (1.0 - double(i) / double(sn));
            buf[i] = s;
        }
    } else if (type == "grains") {
        Value  density = Value::compile(L["density"], scope, 40);
        Value  gamp    = Value::compile(L["grainAmp"], scope, 1);
        auto   range   = [&](char const* key, double lo, double hi) {
            Json const& r = L[key];
            if (r.isArray()) return std::pair{evalNum(r[0], scope, lo), evalNum(r[1], scope, hi)};
            if (!r.isNull()) {
                double v = evalNum(r, scope, lo);
                return std::pair{v, v};
            }
            return std::pair{lo, hi};
        };
        auto        [g0, g1] = range("grain", 0.01, 0.04);
        auto        [f0, f1] = range("freq", 400, 4000);
        std::string gw       = L.str("wave", "sine");
        double      q        = evalKey(L, "q", scope, 6);
        Random      grng(seed);
        double      t = 0;
        while (t < len) {
            double d = math::clamp(density.at(t, len), 0.1, 20000.0);
            t += -math::log(std::max(1e-9, 1.0 - grng.uniform())) / d;
            if (t >= len) break;
            double gl   = grng.uniform(g0, g1);
            double gf   = f0 > 0 && f1 > 0 ? f0 * math::pow(f1 / f0, grng.uniform()) : f0;
            double ga   = gamp.at(t, len) * grng.uniform(0.4, 1.0);
            auto   at   = std::size_t(t * rate);
            auto   glen = std::size_t(std::max(4.0, gl * rate));
            if (gw == "noise") {
                dsp::Svf   bp;
                dsp::Noise gn(dsp::NoiseColor::White, grng.next());
                bp.setup(dsp::FilterType::Bandpass, gf, q, 0, rate);
                for (std::size_t i = 0; i < glen && at + i < n; ++i) {
                    double e = math::sin(math::kPi * double(i) / double(glen));
                    buf[at + i] += ga * e * bp.process(gn.next()) * 1.5;
                }
            } else if (gw == "click") {
                double ph = 0;
                for (std::size_t i = 0; i < glen && at + i < n; ++i) {
                    double e = math::exp(-6.0 * double(i) / double(glen));
                    buf[at + i] += ga * e * math::sin(math::kTwoPi * ph);
                    ph += gf / rate;
                }
            } else {
                double ph = grng.uniform();
                for (std::size_t i = 0; i < glen && at + i < n; ++i) {
                    double e = math::sin(math::kPi * double(i) / double(glen));
                    buf[at + i] += ga * e * e * math::sin(math::kTwoPi * ph);
                    ph += gf / rate;
                    ph -= math::floor(ph);
                }
            }
        }
    } else if (type == "patch") {
        if (ctx.depth >= kMaxDepth) throw PatchError("nested patches too deep");
        Json const& inner = resolvePatch(L["patch"], ctx.library);
        auto        sub   = renderPatchBuffer(inner, scope, L["params"], seed, ctx.rate, ctx.library, ctx.depth + 1, true);
        for (std::size_t i = 0; i < n && i < sub.size(); ++i) buf[i] = sub[i];
        if (sub.size() > n) { // the layer ends before the nested patch: fade out instead of a click
            std::size_t fn = std::min<std::size_t>(n, std::size_t(0.01 * rate));
            for (std::size_t i = 0; i < fn; ++i) buf[n - 1 - i] *= double(i) / double(fn);
        }
    } else {
        throw PatchError("unknown layer type \"" + type + "\" (osc, noise, fm, pluck, modal, grains, patch)");
    }
    return buf;
}

void applyFilters(std::vector<double>& buf, Json const& filters, Scope& scope, double rate, double len) {
    if (filters.isNull()) return;
    auto one = [&](Json const& f) {
        auto       type = dsp::parseFilter(f.str("type", "lp"));
        Value      freq = Value::compile(f["freq"], scope, 1000);
        Value      q    = Value::compile(f["q"], scope, 0.707);
        double     gain = evalKey(f, "gain", scope, 0);
        int        poles = std::clamp(int(evalKey(f, "stages", scope, 1)), 1, 4);
        bool const fixed = freq.isConstant() && q.isConstant();
        std::vector<dsp::Svf> svf(static_cast<std::size_t>(poles));
        for (std::size_t i = 0; i < buf.size(); ++i) {
            if (i == 0 || (!fixed && i % 16 == 0)) {
                double t = double(i) / rate;
                for (auto& s : svf) s.setup(type, freq.at(t, len), q.at(t, len), gain, rate);
            }
            double x = buf[i];
            for (auto& s : svf) x = s.process(x);
            buf[i] = x;
        }
    };
    if (filters.isArray()) {
        for (auto const& f : filters.items()) one(f);
    } else {
        one(filters);
    }
}

void applyFormant(std::vector<double>& buf, Json const& F, Scope& scope, double rate, double len) {
    Value                    vow;
    static std::string const letters = "aeiou";
    if (F["vowel"].isString() && F["vowel"].string().size() == 1 && letters.find(F["vowel"].string()[0]) != std::string::npos) {
        vow = Value::constant(double(letters.find(F["vowel"].string()[0]))); // "a", "e", "i", "o", "u"
    } else {
        vow = Value::compile(F["vowel"], scope, 0);
    }
    double shift = evalKey(F, "shift", scope, 1);
    double qmul  = evalKey(F, "q", scope, 1);
    double mix   = evalKey(F, "mix", scope, 1);
    std::array<dsp::Svf, 3> bp;
    std::array<double, 3>   gains{};
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (i % 32 == 0) { // coefficients and gains follow the vowel every 32 samples
            double v  = math::clamp(vow.at(double(i) / rate, len), 0.0, 4.0);
            int    a  = int(math::floor(v));
            double x  = v - a;
            auto   fa = vowel(a);
            auto   fb = vowel(std::min(a + 1, 4));
            for (int k = 0; k < 3; ++k) {
                double f  = (fa[std::size_t(k)].f + (fb[std::size_t(k)].f - fa[std::size_t(k)].f) * x) * shift;
                double bw = (fa[std::size_t(k)].bw + (fb[std::size_t(k)].bw - fa[std::size_t(k)].bw) * x) * shift;
                bp[std::size_t(k)].setup(dsp::FilterType::Bandpass, f, math::clamp(f / bw * qmul, 0.5, 40), 0, rate);
                gains[std::size_t(k)] = math::dbToGain(fa[std::size_t(k)].gainDb + (fb[std::size_t(k)].gainDb - fa[std::size_t(k)].gainDb) * x);
            }
        }
        double s = 0;
        for (int k = 0; k < 3; ++k) s += bp[std::size_t(k)].process(buf[i]) * gains[std::size_t(k)];
        buf[i] = mix * s * 2.0 + (1.0 - mix) * buf[i];
    }
}

void shape(std::vector<double>& buf, Json const& L, Scope& scope, double rate) {
    if (L.has("drive")) {
        double d = evalKey(L, "drive", scope, 0);
        if (d > 0) {
            double norm = math::tanh(d);
            for (auto& x : buf) x = math::tanh(x * d) / norm;
        }
    }
    if (auto const* c = L.find("crush")) {
        double bits = evalKey(*c, "bits", scope, 16);
        double hold = evalKey(*c, "rate", scope, rate);
        double step = bits < 16 ? 2.0 / math::pow(2.0, bits) : 0.0;
        double acc = 1.0, held = 0;
        for (auto& x : buf) {
            acc += hold / rate;
            if (acc >= 1.0) {
                acc -= math::floor(acc);
                held = step > 0 ? math::round(x / step) * step : x;
            }
            x = held;
        }
    }
}

void renderLayer(Json const& L, Scope& scope, Ctx const& ctx, std::vector<double>& out, double offset, double gainScale, std::uint64_t seed, bool repeated) {
    if (!L.isObject()) throw PatchError("a layer is an object");
    if (!repeated) {
        if (auto const* rep = L.find("repeat")) {
            int    count  = std::clamp(int(evalKey(*rep, "count", scope, 1)), 0, 512);
            double every  = evalKey(*rep, "every", scope, 0.1);
            double jitter = evalKey(*rep, "jitter", scope, 0);
            double decay  = evalKey(*rep, "decay", scope, 1);
            double g      = 1;
            for (int i = 0; i < count; ++i) {
                double at = offset + every * i + (jitter > 0 ? scope.rng->uniform(0, jitter) : 0.0);
                renderLayer(L, scope, ctx, out, at, gainScale * g, mixSeed(seed, std::uint64_t(i) + 101), true);
                g *= decay;
            }
            return;
        }
    }
    double const rate  = ctx.rate;
    double       start = evalKey(L, "start", scope, 0);
    double       len   = evalKey(L, "length", scope, std::max(0.0, ctx.patchLength - start));
    len                = math::clamp(len, 0.0, kMaxSeconds);
    std::string type   = L.str("type", "osc");

    // amplitude envelope
    double a = 0.002, d = 0, s = 1, r = 0.01;
    bool   expEnv = true;
    Value  ampCurve;
    bool   hasCurve = false;
    Json const& env = L["env"];
    if (env.isObject() && (env.has("a") || env.has("d") || env.has("s") || env.has("r"))) {
        a      = evalKey(env, "a", scope, a);
        d      = evalKey(env, "d", scope, d);
        s      = evalKey(env, "s", scope, 1);
        r      = evalKey(env, "r", scope, r);
        expEnv = env.str("curve", "exp") == "exp";
    } else if (!env.isNull()) {
        ampCurve = Value::compile(env, scope, 1);
        hasCurve = true;
        r        = 0;
    }
    if (type == "pluck" || type == "modal") r = std::max(r, evalKey(L, "ring", scope, 0)); // natural decay past the gate
    Value amp  = Value::compile(L["amp"], scope, 1);
    double gdb = evalKey(L, "gain", scope, 0);

    std::size_t n = std::size_t((len + r) * rate);
    if (n == 0) return;
    auto buf = layerSource(L, type, scope, ctx, len, n, seed);

    bool   ampConst = amp.isConstant();
    double amp0     = amp.at(0, len);
    double trRate = 0, trDepth = 0;
    if (auto const* tr = L.find("tremolo")) {
        trRate  = evalKey(*tr, "rate", scope, 6);
        trDepth = evalKey(*tr, "depth", scope, 0.5);
    }
    for (std::size_t i = 0; i < n; ++i) {
        double t = double(i) / rate;
        double e = hasCurve ? ampCurve.at(t, len) : dsp::adsr(t, len, a, d, s, r, expEnv);
        double g = (ampConst ? amp0 : amp.at(t, len)) * e;
        if (trDepth != 0) g *= 1.0 - trDepth * (0.5 + 0.5 * math::sin(math::kTwoPi * trRate * t));
        buf[i] *= g;
    }
    applyFilters(buf, L["filter"], scope, rate, len);
    if (auto const* f = L.find("formant")) applyFormant(buf, *f, scope, rate, len);
    shape(buf, L, scope, rate);
    mixInto(out, buf, std::int64_t(math::round((offset + start) * rate)), gainScale * math::dbToGain(gdb));
}

void applyFx(std::vector<double>& buf, Json const& fxList, Scope& scope, int rate, std::uint64_t seed) {
    if (!fxList.isArray()) return;
    double const total = double(buf.size()) / rate;
    for (auto const& fx : fxList.items()) {
        std::string type = fx.str("type", "");
        if (type == "reverb") {
            double     mix = evalKey(fx, "mix", scope, 0.25);
            dsp::Reverb rv(evalKey(fx, "room", scope, 0.5), evalKey(fx, "damp", scope, 0.5), rate);
            double     pre = evalKey(fx, "predelay", scope, 0);
            dsp::Delay dl(std::size_t(pre * rate) + 2);
            for (auto& x : buf) {
                dl.push(x);
                double wet = rv.process(pre > 0 ? dl.tap(pre * rate) : x);
                x          = x * (1.0 - mix) + wet * mix;
            }
        } else if (type == "echo") {
            double     time = evalKey(fx, "time", scope, 0.2);
            double     fb   = math::clamp(evalKey(fx, "feedback", scope, 0.35), 0.0, 0.95);
            double     mix  = evalKey(fx, "mix", scope, 0.3);
            double     lpf  = evalKey(fx, "damp", scope, 4000);
            dsp::Delay dl(std::size_t(time * rate) + 4);
            dsp::Svf   lp;
            lp.setup(dsp::FilterType::Lowpass, lpf, 0.707, 0, rate);
            for (auto& x : buf) {
                double d = dl.tap(time * rate - 1);
                dl.push(x + lp.process(d) * fb);
                x = x + d * mix;
            }
        } else if (type == "chorus") {
            double     rt  = evalKey(fx, "rate", scope, 0.8);
            double     dep = evalKey(fx, "depth", scope, 0.004);
            double     mix = evalKey(fx, "mix", scope, 0.5);
            double     bas = evalKey(fx, "delay", scope, 0.012);
            dsp::Delay dl(std::size_t((bas + dep) * rate) + 4);
            for (std::size_t i = 0; i < buf.size(); ++i) {
                dl.push(buf[i]);
                double t = double(i) / rate;
                double d = (bas + dep * math::sin(math::kTwoPi * rt * t)) * rate;
                buf[i]   = buf[i] * (1.0 - mix * 0.5) + dl.tap(d) * mix * 0.5;
            }
        } else if (type == "drive") {
            double dr = evalKey(fx, "amount", scope, 2);
            if (dr > 0) {
                double norm = math::tanh(dr);
                for (auto& x : buf) x = math::tanh(x * dr) / norm;
            }
        } else if (type == "filter") {
            // {"type": "filter", "filter": "hp", "freq": 80, "q": 0.7}: the filter's own type is "filter"
            Json f = fx;
            f.set("type", Json(fx.str("filter", "lp")));
            applyFilters(buf, f, scope, rate, total);
        } else if (type == "crush") {
            Json l = Json::object();
            l.set("crush", fx);
            shape(buf, l, scope, rate);
        } else if (type == "reverse") {
            std::reverse(buf.begin(), buf.end());
        } else if (type == "gain") {
            double g = math::dbToGain(evalKey(fx, "db", scope, 0));
            for (auto& x : buf) x *= g;
        } else if (type == "fadein") {
            std::size_t n = std::size_t(evalKey(fx, "time", scope, 0.01) * rate);
            for (std::size_t i = 0; i < n && i < buf.size(); ++i) buf[i] *= double(i) / double(n);
        } else {
            throw PatchError("unknown effect \"" + type + "\" (reverb, echo, chorus, drive, filter, crush, reverse, gain, fadein)");
        }
    }
    (void)seed;
}

void evalParams(Json const& defs, Scope& scope) {
    if (!defs.isObject()) return;
    for (auto const& [k, v] : defs.members()) scope.params[k] = evalNum(v, scope, 0);
}

std::vector<double> renderPatchBuffer(Json const& patch, Scope& parent, Json const& params, std::uint64_t seed, int rate, PatchLibrary const* lib, int depth, bool finishIt) {
    Random rng(seed);
    Scope  scope;
    scope.rng = &rng;
    // nested patches see their own defaults, then the caller's overrides (evaluated by the caller)
    evalParams(patch["params"], scope);
    if (params.isObject()) {
        for (auto const& [k, v] : params.members()) scope.params[k] = evalNum(v, parent, 0);
    }
    Ctx ctx{rate, 0, seed, lib, depth};
    ctx.patchLength = math::clamp(evalKey(patch, "length", scope, 1.0), 0.0, kMaxSeconds);
    std::vector<double> out(std::size_t(ctx.patchLength * rate), 0.0);
    Json const&         layers = patch["layers"];
    if (!layers.isArray()) throw PatchError("a patch needs \"layers\": [...]");
    std::uint64_t layerIndex = 0;
    for (auto const& L : layers.items()) {
        renderLayer(L, scope, ctx, out, 0, 1.0, mixSeed(seed, ++layerIndex), false);
    }
    double tail = math::clamp(evalKey(patch, "tail", scope, 0.25), 0.0, 30.0);
    out.resize(out.size() + std::size_t(tail * rate), 0.0);
    if (finishIt) detail::finish(out, patch, scope.params, seed, rate, true);
    return out;
}

} // namespace

namespace detail {

void finish(std::vector<double>& buf, Json const& spec, std::map<std::string, double> const& params, std::uint64_t seed, int rate, bool trim) {
    Random rng(mixSeed(seed, 0xF1));
    Scope  scope;
    scope.params = params;
    scope.rng    = &rng;
    applyFx(buf, spec["fx"], scope, rate, seed);
    double peak = 0;
    for (double x : buf) peak = std::max(peak, math::abs(x));
    Json const& norm = spec["normalize"];
    if (!(norm.isBool() && !norm.boolean()) && peak > 1e-9) {
        double target = norm.isNull() || norm.isBool() ? -1.0 : evalNum(norm, scope, -1.0);
        double g      = math::dbToGain(target) / peak;
        for (auto& x : buf) x *= g;
        peak *= g;
    }
    double gain = math::dbToGain(evalKey(spec, "gain", scope, 0));
    if (gain != 1.0) {
        for (auto& x : buf) x *= gain;
        peak *= gain;
    }
    if (trim) {
        // drop the silent tail (below -80 dBFS), keep 10 ms after the last audible sample
        double      thr  = math::dbToGain(-80.0);
        std::size_t last = 0;
        for (std::size_t i = 0; i < buf.size(); ++i) {
            if (math::abs(buf[i]) > thr) last = i;
        }
        std::size_t keep = std::min(buf.size(), last + std::size_t(0.01 * rate) + 1);
        buf.resize(std::max<std::size_t>(keep, std::size_t(0.02 * rate)), 0.0);
    }
    double      fade = evalKey(spec, "fade", scope, 0.005);
    std::size_t fn   = std::min(buf.size(), std::size_t(fade * rate));
    for (std::size_t i = 0; i < fn; ++i) buf[buf.size() - 1 - i] *= double(i) / double(fn);
    for (auto& x : buf) x = math::clamp(x, -1.0, 1.0);
}

} // namespace detail

std::vector<float> renderPatch(Json const& patch, Json const& params, std::uint64_t seed, int sampleRate, PatchLibrary const* library) {
    if (sampleRate < 8000 || sampleRate > 96000) throw PatchError("bad sample rate");
    Json const& p = resolvePatch(patch, library);
    Random      rng(seed);
    Scope       root;
    root.rng = &rng;
    // overrides may refer to the patch defaults ("$pitch*2")
    evalParams(p["params"], root);
    auto buf = renderPatchBuffer(p, root, params, seed, sampleRate, library, 0, true);
    return std::vector<float>(buf.begin(), buf.end());
}

double evalExpression(std::string const& expr, std::map<std::string, double> const& params, std::uint64_t seed) {
    Random rng(seed);
    Scope  scope;
    scope.params = params;
    scope.rng    = &rng;
    return ExprParser(expr, scope).parse();
}

} // namespace gf::core::synth
