// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): DSP building blocks of the synthesizer.
#pragma once

// Oscillators (band-limited saw/square through PolyBLEP), noise colours, a state-variable filter
// (Zavalishin TPT: low/high/band/notch/peak/bell/shelves, safe under fast modulation), envelopes,
// damped modal resonators, a Karplus-Strong string, delays and a Freeverb-style reverb.
// Everything is deterministic (core/synth/SynthMath.h, core/synth/Random.h).

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/synth/Random.h"

namespace gf::core::synth::dsp {

enum class Wave { Sine, Saw, Square, Triangle, Pulse };
[[nodiscard]] Wave parseWave(std::string_view name);

/// One oscillator sample at `phase` in [0, 1) advancing by `dt` (= freq / rate) per sample.
[[nodiscard]] double oscSample(Wave wave, double phase, double dt, double pulseWidth = 0.5);

enum class NoiseColor { White, Pink, Brown, Blue };
[[nodiscard]] NoiseColor parseNoise(std::string_view name);

class Noise {
public:
    Noise(NoiseColor color, std::uint64_t seed) : mColor(color), mRng(seed) {}
    double next();

private:
    NoiseColor mColor;
    Random     mRng;
    double     mB[7]{};
    double     mLast = 0;
};

enum class FilterType { Lowpass, Highpass, Bandpass, Notch, Peak, Bell, LowShelf, HighShelf, Allpass };
[[nodiscard]] FilterType parseFilter(std::string_view name);

/// Topology-preserving-transform state-variable filter.
class Svf {
public:
    void setup(FilterType type, double freq, double q, double gainDb, double rate);
    double process(double x);
    void   reset() { mIc1 = mIc2 = 0; }

private:
    FilterType mType = FilterType::Lowpass;
    double     mA1 = 0, mA2 = 0, mA3 = 0, mK = 1, mA = 1;
    double     mM0 = 0, mM1 = 0, mM2 = 1;
    double     mIc1 = 0, mIc2 = 0;
};

/// ADSR level at time t (seconds) for a gate of `gate` seconds; times in seconds, `exp` uses
/// exponential decay/release curves.
[[nodiscard]] double adsr(double t, double gate, double a, double d, double s, double r, bool exp);

/// Exponentially decaying sinusoid bank (modal synthesis of struck objects).
class ModalBank {
public:
    void   add(double freq, double decaySeconds, double gain, double phase, double rate);
    double next();
    [[nodiscard]] bool empty() const { return mModes.empty(); }

private:
    struct Mode {
        double c, s, cw, sw, r, g;
    };
    std::vector<Mode> mModes;
};

/// Karplus-Strong plucked string.
class Pluck {
public:
    Pluck(double freq, double decaySeconds, double brightness, double rate, std::uint64_t seed);
    double next();

private:
    std::vector<double> mLine;
    std::size_t         mPos  = 0;
    double              mFrac = 0;
    double              mGain = 0.99;
    double              mLast = 0;
};

/// Fractional delay line.
class Delay {
public:
    explicit Delay(std::size_t maxSamples = 1) : mBuf(maxSamples + 2, 0.0) {}
    void   push(double x);
    /// The sample `delay` samples ago (fractional, linear interpolation).
    [[nodiscard]] double tap(double delay) const;

private:
    std::vector<double> mBuf;
    std::size_t         mPos = 0;
};

/// Mono Freeverb (8 damped combs + 4 allpasses), sizes scaled to the sample rate.
class Reverb {
public:
    Reverb(double room, double damp, double rate);
    double process(double x);

private:
    struct Comb {
        std::vector<double> buf;
        std::size_t         pos   = 0;
        double              store = 0;
    };
    struct Allpass {
        std::vector<double> buf;
        std::size_t         pos = 0;
    };
    Comb    mCombs[8];
    Allpass mAllpasses[4];
    double  mFeedback;
    double  mDamp;
};

} // namespace gf::core::synth::dsp
