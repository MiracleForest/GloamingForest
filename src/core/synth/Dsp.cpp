// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): DSP building blocks of the synthesizer.
#include "core/synth/Dsp.h"

#include <algorithm>

#include "core/synth/SynthMath.h"

namespace gf::core::synth::dsp {

namespace {

double polyBlep(double t, double dt) {
    if (dt <= 0) return 0;
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0;
}

double frac(double x) { return x - math::floor(x); }

double flushTiny(double x) { return (x < 1e-30 && x > -1e-30) ? 0.0 : x; }

} // namespace

Wave parseWave(std::string_view n) {
    if (n == "saw" || n == "sawtooth") return Wave::Saw;
    if (n == "square") return Wave::Square;
    if (n == "triangle" || n == "tri") return Wave::Triangle;
    if (n == "pulse") return Wave::Pulse;
    return Wave::Sine;
}

double oscSample(Wave wave, double phase, double dt, double pw) {
    switch (wave) {
    case Wave::Sine: return math::sin(math::kTwoPi * phase);
    case Wave::Saw:  return (2.0 * phase - 1.0) - polyBlep(phase, dt);
    case Wave::Square:
    case Wave::Pulse: {
        double w = wave == Wave::Square ? 0.5 : math::clamp(pw, 0.02, 0.98);
        double v = phase < w ? 1.0 : -1.0;
        v += polyBlep(phase, dt);
        v -= polyBlep(frac(phase + 1.0 - w), dt);
        return v;
    }
    case Wave::Triangle: return 1.0 - 4.0 * math::abs(phase - 0.5);
    }
    return 0;
}

NoiseColor parseNoise(std::string_view n) {
    if (n == "pink") return NoiseColor::Pink;
    if (n == "brown" || n == "red") return NoiseColor::Brown;
    if (n == "blue") return NoiseColor::Blue;
    return NoiseColor::White;
}

double Noise::next() {
    double white = mRng.bipolar();
    switch (mColor) {
    case NoiseColor::White: return white;
    case NoiseColor::Pink: {
        // Paul Kellet's refined pink filter
        mB[0]       = 0.99886 * mB[0] + white * 0.0555179;
        mB[1]       = 0.99332 * mB[1] + white * 0.0750759;
        mB[2]       = 0.96900 * mB[2] + white * 0.1538520;
        mB[3]       = 0.86650 * mB[3] + white * 0.3104856;
        mB[4]       = 0.55000 * mB[4] + white * 0.5329522;
        mB[5]       = -0.7616 * mB[5] - white * 0.0168980;
        double pink = mB[0] + mB[1] + mB[2] + mB[3] + mB[4] + mB[5] + mB[6] + white * 0.5362;
        mB[6]       = white * 0.115926;
        return pink * 0.11;
    }
    case NoiseColor::Brown: {
        mLast = (mLast + 0.02 * white) / 1.02;
        return mLast * 3.5;
    }
    case NoiseColor::Blue: {
        double v = (white - mLast) * 0.5;
        mLast    = white;
        return v;
    }
    }
    return white;
}

FilterType parseFilter(std::string_view n) {
    if (n == "hp" || n == "highpass") return FilterType::Highpass;
    if (n == "bp" || n == "bandpass") return FilterType::Bandpass;
    if (n == "notch") return FilterType::Notch;
    if (n == "peak") return FilterType::Peak;
    if (n == "bell" || n == "eq") return FilterType::Bell;
    if (n == "lowshelf") return FilterType::LowShelf;
    if (n == "highshelf") return FilterType::HighShelf;
    if (n == "allpass") return FilterType::Allpass;
    return FilterType::Lowpass;
}

void Svf::setup(FilterType type, double freq, double q, double gainDb, double rate) {
    mType  = type;
    freq   = math::clamp(freq, 10.0, rate * 0.49);
    q      = math::clamp(q, 0.05, 60.0);
    double g = math::tan(math::kPi * freq / rate);
    double k = 1.0 / q;
    mA       = math::dbToGain(gainDb / 2.0); // 10^(gain/40)
    switch (type) {
    case FilterType::Lowpass:  mM0 = 0, mM1 = 0, mM2 = 1; break;
    case FilterType::Bandpass: mM0 = 0, mM1 = k, mM2 = 0; break;
    case FilterType::Highpass: mM0 = 1, mM1 = -k, mM2 = -1; break;
    case FilterType::Notch:    mM0 = 1, mM1 = -k, mM2 = 0; break;
    case FilterType::Peak:     mM0 = 1, mM1 = -k, mM2 = -2; break;
    case FilterType::Allpass:  mM0 = 1, mM1 = -2 * k, mM2 = 0; break;
    case FilterType::Bell:
        k   = 1.0 / (q * mA);
        mM0 = 1, mM1 = k * (mA * mA - 1), mM2 = 0;
        break;
    case FilterType::LowShelf:
        g /= math::sqrt(mA);
        mM0 = 1, mM1 = k * (mA - 1), mM2 = mA * mA - 1;
        break;
    case FilterType::HighShelf:
        g *= math::sqrt(mA);
        mM0 = mA * mA, mM1 = k * (1 - mA) * mA, mM2 = 1 - mA * mA;
        break;
    }
    mK  = k;
    mA1 = 1.0 / (1.0 + g * (g + k));
    mA2 = g * mA1;
    mA3 = g * mA2;
}

double Svf::process(double v0) {
    double v3 = v0 - mIc2;
    double v1 = mA1 * mIc1 + mA2 * v3;
    double v2 = mIc2 + mA2 * mIc1 + mA3 * v3;
    mIc1      = flushTiny(2 * v1 - mIc1);
    mIc2      = flushTiny(2 * v2 - mIc2);
    return mM0 * v0 + mM1 * v1 + mM2 * v2;
}

double adsr(double t, double gate, double a, double d, double s, double r, bool exp) {
    if (t < 0) return 0;
    auto held = [&](double x) {
        if (x < a) return a > 0 ? x / a : 1.0;
        double y = x - a;
        if (d <= 0) return s;
        if (exp) return s + (1.0 - s) * math::exp(-5.0 * y / d);
        return y < d ? 1.0 + (s - 1.0) * (y / d) : s;
    };
    if (t < gate) return held(t);
    double level = held(gate);
    double y     = t - gate;
    if (r <= 0 || y >= r) return 0;
    if (exp) return level * math::exp(-6.9 * y / r);
    return level * (1.0 - y / r);
}

void ModalBank::add(double freq, double decay, double gain, double phase, double rate) {
    if (freq <= 0 || freq >= rate * 0.49 || decay <= 0) return;
    double w = math::kTwoPi * freq / rate;
    Mode   m{};
    m.c  = math::cos(math::kTwoPi * phase);
    m.s  = math::sin(math::kTwoPi * phase);
    m.cw = math::cos(w);
    m.sw = math::sin(w);
    m.r  = math::exp(-6.907755 / (decay * rate));
    m.g  = gain;
    mModes.push_back(m);
}

double ModalBank::next() {
    double out = 0;
    for (auto& m : mModes) {
        out += m.g * m.s;
        double c = (m.c * m.cw - m.s * m.sw) * m.r;
        double s = (m.c * m.sw + m.s * m.cw) * m.r;
        m.c      = flushTiny(c);
        m.s      = flushTiny(s);
    }
    return out;
}

Pluck::Pluck(double freq, double decay, double brightness, double rate, std::uint64_t seed) {
    freq           = math::clamp(freq, 20.0, rate * 0.25);
    double period  = rate / freq - 0.5; // the averaging filter adds half a sample
    std::size_t n  = std::size_t(math::floor(period));
    mFrac          = period - double(n);
    mLine.assign(n + 2, 0.0); // x[n-D-2] .. x[n-1]
    Random rng(seed);
    double lp    = 0;
    double coeff = math::clamp(brightness, 0.02, 1.0);
    for (auto& v : mLine) {
        lp += coeff * (rng.bipolar() - lp);
        v = lp;
    }
    // remove DC from the excitation
    double mean = 0;
    for (double v : mLine) mean += v;
    mean /= double(mLine.size());
    for (auto& v : mLine) v -= mean;
    mGain = math::exp(-6.907755 / (math::clamp(decay, 0.01, 60.0) * freq));
}

double Pluck::next() {
    // y[n] = g ((1 - f) avg(x[n-D], x[n-D-1]) + f avg(x[n-D-1], x[n-D-2])): loop delay D + 1/2 + f
    std::size_t size = mLine.size();
    double      xc   = mLine[mPos];                // x[n-D-2], the oldest
    double      xb   = mLine[(mPos + 1) % size];   // x[n-D-1]
    double      xa   = mLine[(mPos + 2) % size];   // x[n-D]
    double      y    = mGain * ((1.0 - mFrac) * 0.5 * (xa + xb) + mFrac * 0.5 * (xb + xc));
    y                = flushTiny(y);
    mLine[mPos]      = y;
    mPos             = (mPos + 1) % size;
    mLast            = y;
    return y;
}

void Delay::push(double x) {
    mBuf[mPos] = x;
    mPos       = (mPos + 1) % mBuf.size();
}

double Delay::tap(double delay) const {
    std::size_t size = mBuf.size();
    delay            = math::clamp(delay, 0.0, double(size - 2));
    std::size_t d    = std::size_t(math::floor(delay));
    double      f    = delay - double(d);
    auto        at   = [&](std::size_t k) { return mBuf[(mPos + size * 2 - 1 - k) % size]; };
    return at(d) * (1.0 - f) + at(d + 1) * f;
}

Reverb::Reverb(double room, double damp, double rate) {
    static constexpr int kComb[8]    = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kAllpass[4] = {556, 441, 341, 225};
    double               scale       = rate / 44100.0;
    for (int i = 0; i < 8; ++i) mCombs[i].buf.assign(std::max<std::size_t>(1, std::size_t(kComb[i] * scale)), 0.0);
    for (int i = 0; i < 4; ++i) mAllpasses[i].buf.assign(std::max<std::size_t>(1, std::size_t(kAllpass[i] * scale)), 0.0);
    mFeedback = math::clamp(room, 0.0, 1.0) * 0.28 + 0.7;
    mDamp     = math::clamp(damp, 0.0, 1.0) * 0.4;
}

double Reverb::process(double x) {
    double in  = x * 0.015;
    double out = 0;
    for (auto& c : mCombs) {
        double y = c.buf[c.pos];
        c.store  = flushTiny(y * (1.0 - mDamp) + c.store * mDamp);
        c.buf[c.pos] = in + c.store * mFeedback;
        c.pos        = (c.pos + 1) % c.buf.size();
        out += y;
    }
    for (auto& a : mAllpasses) {
        double b     = a.buf[a.pos];
        double y     = -out + b;
        a.buf[a.pos] = flushTiny(out + b * 0.5);
        a.pos        = (a.pos + 1) % a.buf.size();
        out          = y;
    }
    return out * 3.0;
}

} // namespace gf::core::synth::dsp
