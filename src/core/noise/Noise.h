#pragma once

// Contract "worldgen", Java-parity noise and density functions (header owned by F0, implemented by
// F5 with gtest goldens against Java values; spike S2). Pure C++ (gf-core), thread-safe for reads.
//
// Bit-exact equivalents of Minecraft 1.21.1:
//   - ImprovedNoise   net.minecraft.world.level.levelgen.synth.ImprovedNoise
//   - SimplexNoise    ...synth.SimplexNoise
//   - PerlinNoise     ...synth.PerlinNoise (octaves; legacy and modern seeding)
//   - NormalNoise     ...synth.NormalNoise (two PerlinNoise, NoiseParameters)
//   - Density         a compiled density function tree (vanilla density functions plus the ones
//                     TF registers: forested_terrain, raw_biome_noise, raw_biome_terrain,
//                     skylight_terrain, box / focused / absolute difference / sqrt / tanh hill /
//                     hollow hill), built from worldgen JSON.
//
// Objects hold their state behind a shared implementation, so this header does not change when
// the implementation does.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/jrand/JRand.h"

namespace gf::core::noise {

class ImprovedNoise {
public:
    explicit ImprovedNoise(jrand::RandomSource& random);
    [[nodiscard]] double noise(double x, double y, double z) const;
    /// The legacy variant with a y scale / y max (ImprovedNoise#noise(x, y, z, yScale, yMax)).
    [[nodiscard]] double noise(double x, double y, double z, double yScale, double yMax) const;

private:
    struct Impl;
    std::shared_ptr<Impl const> mImpl;
};

class SimplexNoise {
public:
    explicit SimplexNoise(jrand::RandomSource& random);
    [[nodiscard]] double getValue(double x, double y) const;
    [[nodiscard]] double getValue(double x, double y, double z) const;

private:
    struct Impl;
    std::shared_ptr<Impl const> mImpl;
};

/// NoiseParameters: first octave and the amplitudes of the octaves.
struct NoiseParameters {
    int                 firstOctave = 0;
    std::vector<double> amplitudes;
};

class PerlinNoise {
public:
    /// PerlinNoise.create(random, firstOctave, amplitudes) (modern, positional seeding).
    [[nodiscard]] static PerlinNoise create(jrand::RandomSource& random, NoiseParameters const& params);
    /// PerlinNoise.createLegacyForLegacyNetherBiome / legacy init (octaves from a java.util.Random).
    [[nodiscard]] static PerlinNoise createLegacy(jrand::RandomSource& random, NoiseParameters const& params);
    [[nodiscard]] double getValue(double x, double y, double z) const;
    [[nodiscard]] double maxValue() const;

private:
    struct Impl;
    std::shared_ptr<Impl const> mImpl;
};

class NormalNoise {
public:
    [[nodiscard]] static NormalNoise create(jrand::RandomSource& random, NoiseParameters const& params);
    [[nodiscard]] double getValue(double x, double y, double z) const;
    [[nodiscard]] double maxValue() const;

private:
    struct Impl;
    std::shared_ptr<Impl const> mImpl;
};

/// The point a density function is evaluated at (DensityFunction.FunctionContext).
struct DensityPoint {
    int blockX = 0, blockY = 0, blockZ = 0;
};

/// A compiled density function (DensityFunction).
class Density {
public:
    /// Resolves noise / density references by id ("minecraft:..." or "twilightforest:...").
    struct Resolver {
        std::function<std::optional<NoiseParameters>(std::string const& noiseId)>   noise;
        std::function<std::optional<nlohmann::json>(std::string const& densityId)> density;
    };
    /// Builds a density function from its worldgen JSON for `seed` (nullopt: unknown type).
    [[nodiscard]] static std::optional<Density> fromJson(nlohmann::json const& json, std::int64_t seed, Resolver const& resolver);
    /// A custom density type ("twilightforest:forested_terrain" ...), registered by its owner
    /// (U2): `build` receives the type's JSON and returns the evaluator.
    using Evaluator = std::function<double(DensityPoint const&)>;
    using Builder   = std::function<Evaluator(nlohmann::json const& json, std::int64_t seed, Resolver const& resolver)>;
    static void registerType(std::string const& typeId, Builder build);

    [[nodiscard]] double compute(DensityPoint const& p) const;
    [[nodiscard]] double minValue() const;
    [[nodiscard]] double maxValue() const;

private:
    struct Impl;
    std::shared_ptr<Impl const> mImpl;
};

} // namespace gf::core::noise
