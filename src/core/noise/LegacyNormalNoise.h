#pragma once

// NormalNoise.createLegacyNetherBiome of Minecraft 1.21.1 (F5, next to the contract's
// core/noise/Noise.h, which stays as F0 wrote it, rule R18): a NormalNoise whose two PerlinNoise
// are built with PerlinNoise.createLegacyForLegacyNetherBiome (octaves drawn in sequence from a
// java.util.Random instead of the positional "octave_<n>" sources). RandomState uses it for the
// temperature and vegetation noises of noise settings with "legacy_random_source": true (the
// Nether). Pure C++ (gf-core), immutable after construction; checked against the Java oracle's
// tests/golden/noise.json in tests/worldgen.

#include "core/jrand/JRand.h"
#include "core/noise/Noise.h"

namespace gf::core::noise {

class LegacyNormalNoise {
public:
    /// NormalNoise.createLegacyNetherBiome(random, params): the octaves must end at or below 0.
    [[nodiscard]] static LegacyNormalNoise createLegacyNetherBiome(jrand::RandomSource& random, NoiseParameters const& params);
    [[nodiscard]] double getValue(double x, double y, double z) const;
    [[nodiscard]] double maxValue() const { return mMaxValue; }

private:
    PerlinNoise mFirst, mSecond;
    double      mValueFactor = 0;
    double      mMaxValue    = 0;
};

} // namespace gf::core::noise
