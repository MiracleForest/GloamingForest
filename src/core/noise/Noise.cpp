// Neutral stub of the Java-parity noise (F0). F5 replaces this file (rule R18).
#include "core/noise/Noise.h"

#include "core/Stub.h"

namespace gf::core::noise {

struct ImprovedNoise::Impl {};
struct SimplexNoise::Impl {};
struct PerlinNoise::Impl {};
struct NormalNoise::Impl {};
struct Density::Impl {};

ImprovedNoise::ImprovedNoise(jrand::RandomSource&) { GF_STUB("noise.ImprovedNoise"); }
double ImprovedNoise::noise(double, double, double) const { return 0.0; }
double ImprovedNoise::noise(double, double, double, double, double) const { return 0.0; }

SimplexNoise::SimplexNoise(jrand::RandomSource&) { GF_STUB("noise.SimplexNoise"); }
double SimplexNoise::getValue(double, double) const { return 0.0; }
double SimplexNoise::getValue(double, double, double) const { return 0.0; }

PerlinNoise PerlinNoise::create(jrand::RandomSource&, NoiseParameters const&) {
    GF_STUB("noise.PerlinNoise");
    return {};
}
PerlinNoise PerlinNoise::createLegacy(jrand::RandomSource&, NoiseParameters const&) {
    GF_STUB("noise.PerlinNoise");
    return {};
}
double PerlinNoise::getValue(double, double, double) const { return 0.0; }
double PerlinNoise::maxValue() const { return 0.0; }

NormalNoise NormalNoise::create(jrand::RandomSource&, NoiseParameters const&) {
    GF_STUB("noise.NormalNoise");
    return {};
}
double NormalNoise::getValue(double, double, double) const { return 0.0; }
double NormalNoise::maxValue() const { return 0.0; }

std::optional<Density> Density::fromJson(nlohmann::json const&, std::int64_t, Resolver const&) {
    GF_STUB("noise.Density");
    return std::nullopt;
}
void   Density::registerType(std::string const&, Builder) { GF_STUB("noise.Density.registerType"); }
double Density::compute(DensityPoint const&) const { return 0.0; }
double Density::minValue() const { return 0.0; }
double Density::maxValue() const { return 0.0; }

} // namespace gf::core::noise
