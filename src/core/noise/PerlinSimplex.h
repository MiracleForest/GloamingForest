#pragma once

// Java-parity PerlinSimplexNoise (net.minecraft.world.level.levelgen.synth.PerlinSimplexNoise of
// 1.21.1), the 2D octave simplex noise behind Biome's temperature noises and the noise-based
// placement counts (BIOME_INFO_NOISE). Pure C++ (gf-core), immutable after construction.

#include <memory>
#include <vector>

#include "core/jrand/JRand.h"
#include "core/noise/Noise.h"

namespace gf::core::noise {

class PerlinSimplexNoise {
public:
    /// `octaves` as in Java (any order, duplicates ignored), e.g. {0} or {-2, -1, 0}.
    PerlinSimplexNoise(jrand::RandomSource& random, std::vector<int> octaves);
    [[nodiscard]] double getValue(double x, double y, bool useNoiseOffsets) const;

    /// Biome.TEMPERATURE_NOISE, Biome.FROZEN_TEMPERATURE_NOISE and Biome.BIOME_INFO_NOISE.
    [[nodiscard]] static PerlinSimplexNoise const& temperatureNoise();
    [[nodiscard]] static PerlinSimplexNoise const& frozenTemperatureNoise();
    [[nodiscard]] static PerlinSimplexNoise const& biomeInfoNoise();

private:
    struct Level {
        std::unique_ptr<SimplexNoise> noise;
        double                        xo = 0, yo = 0;
    };
    std::vector<Level> mLevels;
    double             mHighestFreqInputFactor = 1;
    double             mHighestFreqValueFactor = 1;
};

} // namespace gf::core::noise
