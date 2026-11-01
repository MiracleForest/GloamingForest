// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): deterministic math for the sound synthesizer.
#include "core/synth/SynthMath.h"

#include <cmath>

namespace gf::core::synth::math {

namespace {

// 2*pi split into a high part with few mantissa bits and the remainder, for exact-ish reduction.
constexpr double kTwoPiHi = 6.28318530717958623200;
constexpr double kTwoPiLo = 2.44929359829470635445e-16;
constexpr double kPiOver2 = 1.57079632679489661923;

// sin(r) and cos(r) for |r| <= pi/4 (Taylor series, error < 1e-17 there).
double sinPoly(double r) {
    double r2 = r * r;
    double t  = r;
    double s  = r;
    for (int i = 1; i <= 11; ++i) {
        t *= -r2 / double((2 * i) * (2 * i + 1));
        s += t;
    }
    return s;
}

double cosPoly(double r) {
    double r2 = r * r;
    double t  = 1.0;
    double s  = 1.0;
    for (int i = 1; i <= 11; ++i) {
        t *= -r2 / double((2 * i - 1) * (2 * i));
        s += t;
    }
    return s;
}

// x reduced to [-pi/4, pi/4] plus the quadrant (0..3).
double reduce(double x, int& quadrant) {
    // first to [-pi, pi]
    double k = floor(x / kTwoPi + 0.5);
    x        = (x - k * kTwoPiHi) - k * kTwoPiLo;
    double q = floor(x / kPiOver2 + 0.5);
    x        = x - q * kPiOver2;
    int qi   = int(q);
    quadrant = ((qi % 4) + 4) % 4;
    return x;
}

} // namespace

double floor(double x) { return std::floor(x); } // exact in IEEE 754 on every platform

double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double sqrt(double x) { return x <= 0 ? 0.0 : std::sqrt(x); } // correctly rounded (IEEE 754)

double sin(double x) {
    if (!(x == x)) return 0.0;
    int    q = 0;
    double r = reduce(x, q);
    switch (q) {
    case 0:  return sinPoly(r);
    case 1:  return cosPoly(r);
    case 2:  return -sinPoly(r);
    default: return -cosPoly(r);
    }
}

double cos(double x) {
    if (!(x == x)) return 1.0;
    int    q = 0;
    double r = reduce(x, q);
    switch (q) {
    case 0:  return cosPoly(r);
    case 1:  return -sinPoly(r);
    case 2:  return -cosPoly(r);
    default: return sinPoly(r);
    }
}

double tan(double x) {
    double c = cos(x);
    if (abs(c) < 1e-300) c = 1e-300;
    return sin(x) / c;
}

double exp(double x) {
    if (!(x == x)) return 0.0;
    if (x < -745.0) return 0.0;
    if (x > 709.0) return 1e308;
    double k = floor(x / kLn2 + 0.5);
    double r = x - k * kLn2; // |r| <= ln2/2
    // e^r by Taylor series to r^18 (|r| < 0.35: error < 1e-18)
    double t = 1.0;
    double s = 1.0;
    for (int i = 1; i <= 18; ++i) {
        t *= r / double(i);
        s += t;
    }
    return std::ldexp(s, int(k)); // exact scaling by 2^k
}

double log(double x) {
    if (!(x > 0)) return -1e300;
    int    e = 0;
    double m = std::frexp(x, &e); // x = m * 2^e, m in [0.5, 1): exact
    if (m < 0.70710678118654752440) {
        m *= 2.0;
        e -= 1;
    }
    // log(m) = 2 atanh(s), s = (m - 1) / (m + 1), |s| < 0.172
    double s  = (m - 1.0) / (m + 1.0);
    double s2 = s * s;
    double t  = s;
    double r  = s;
    for (int i = 1; i <= 14; ++i) {
        t *= s2;
        r += t / double(2 * i + 1);
    }
    return 2.0 * r + double(e) * kLn2;
}

double pow(double a, double b) {
    if (a <= 0) return 0.0;
    if (b == 0) return 1.0;
    if (b == 1) return a;
    return exp(b * log(a));
}

double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    if (abs(x) < 1e-5) return x - x * x * x / 3.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

double gainToDb(double gain) { return gain <= 1e-15 ? -300.0 : 20.0 * log(gain) / kLn10; }

double midiToHz(double note) { return 440.0 * exp((note - 69.0) * (kLn2 / 12.0)); }

} // namespace gf::core::synth::math
