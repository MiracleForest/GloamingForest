// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): deterministic math for the sound synthesizer.
#pragma once

// The synthesizer must render byte-identical files on every machine and compiler (`synth.ps1
// -Check` in CI compares the rendered audio with the committed tree), so it never calls the C
// runtime's transcendental functions, whose last bits differ between CRT versions. These use only
// +, -, *, /, sqrt, floor and ldexp/frexp, which IEEE 754 defines exactly. Accuracy is ~1e-15
// relative, far below anything audible.

namespace gf::core::synth::math {

inline constexpr double kPi    = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;
inline constexpr double kLn2   = 0.69314718055994530942;
inline constexpr double kLn10  = 2.30258509299404568402;

[[nodiscard]] double sin(double x);
[[nodiscard]] double cos(double x);
[[nodiscard]] double tan(double x);
/// e^x (0 below -745, +inf above 709).
[[nodiscard]] double exp(double x);
/// Natural logarithm (x <= 0 gives -1e300).
[[nodiscard]] double log(double x);
/// a^b for a > 0 (0 for a <= 0).
[[nodiscard]] double pow(double a, double b);
[[nodiscard]] double tanh(double x);
[[nodiscard]] double sqrt(double x);
[[nodiscard]] double floor(double x);
/// Round half away from zero.
[[nodiscard]] double        round(double x);
[[nodiscard]] inline double abs(double x) { return x < 0 ? -x : x; }
[[nodiscard]] inline double clamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

/// Decibels <-> linear gain.
[[nodiscard]] inline double dbToGain(double db) { return exp(db * (kLn10 / 20.0)); }
[[nodiscard]] double        gainToDb(double gain);
/// MIDI note number -> frequency (A4 = 69 = 440 Hz).
[[nodiscard]] double midiToHz(double note);
/// Semitones -> frequency ratio.
[[nodiscard]] inline double semitones(double st) { return exp(st * (kLn2 / 12.0)); }

} // namespace gf::core::synth::math
