// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/chunkgenerators/TerrainDensityRouter.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/NoiseDensityRouter.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/SqrtDensityFunction.java
//   src/main/java/gloamingforest/init/TFDensityFunctions.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U2 (terrain): the Gloaming Forest's density graph with Minecraft 1.21.1's NoiseChunk semantics
// (pure C++, gf-core). F5's core/noise NoiseChunk cannot interpolate below a custom type (upstream's
// skylight terrain is sqrt(clamp(interpolated(...)))) and asks an std::function per block, so U2
// compiles the noise settings' density JSON itself:
//   - vanilla types: constant, add, mul, min, max, abs, square, cube, half_negative,
//     quarter_negative, squeeze, clamp, y_clamped_gradient, noise, range_choice, the markers
//     (interpolated, flat_cache, cache_2d, cache_once, cache_all_in_cell), blend_alpha (1),
//     blend_offset / beardifier (0), blend_density (pass-through);
//   - upstream's types: biome_driven_terrain, biome_driven_noise (the column-cached routers of mapAll),
//     sqrt, focused, hollow_hill, coord_min, coord_max.
// Noises come from the seed exactly as RandomState builds them (Xoroshiro positional factory,
// fromHashOf(noise id), NormalNoise.create). A compiled graph is immutable and thread-safe.
//
//     auto g = Density::compile(json, seed, resolver, {-32, 256, 2, 2});
//     std::vector<double> d;
//     g->fillChunk(cx, cz, sampler, d);       // index ((y - minY) * 16 + z) * 16 + x, y < minY + height
//     double v = g->computePoint(x, y, z, sampler);  // SinglePointContext: markers pass through

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/noise/Noise.h"
#include "core/noise/NoiseChunk.h"
#include "core/terrain/Functions.h"

namespace gf::core::terrain {

/// BiomeDensitySource#sampleTerrain(blockX, blockZ) of the world being generated.
using TerrainSampler = std::function<DensityData(int blockX, int blockZ)>;
/// An extra density added to every block of a fill (the beardifier / upstream's structure terraformers).
using ExtraDensity = std::function<double(int x, int y, int z)>;

class Density {
public:
    using Resolver = noise::Density::Resolver;

    /// Compiles a density function JSON (or a reference) for `seed`; nullptr with `error` set when
    /// a type or a reference is unknown.
    [[nodiscard]] static std::shared_ptr<Density const>
    compile(nlohmann::json const& json, std::int64_t seed, Resolver const& resolver, noise::NoiseShape shape, std::string* error = nullptr);

    /// NoiseBasedChunkGenerator#doFill's densities of chunk (chunkX, chunkZ): every block y from
    /// minY to minY + height - 1 (index ((y - minY) * 16 + localZ) * 16 + localX), plus `extra`.
    void fillChunk(int chunkX, int chunkZ, TerrainSampler const& sampler, std::vector<double>& out, ExtraDensity const& extra = {}) const;
    /// NoiseBasedChunkGenerator#iterateNoiseColumn's densities of column (x, z) (a one-cell
    /// NoiseChunk at the cell holding the column), y ascending from minY; no beardifier (Java
    /// passes BeardifierMarker there).
    void fillColumn(int x, int z, TerrainSampler const& sampler, std::vector<double>& out) const;
    /// DensityFunction#compute at a SinglePointContext (markers pass through, uncached routers).
    [[nodiscard]] double computePoint(int x, int y, int z, TerrainSampler const& sampler) const;
    [[nodiscard]] double minValue() const;
    [[nodiscard]] double maxValue() const;
    [[nodiscard]] noise::NoiseShape const& shape() const { return mShape; }

    struct Graph;
    Density(std::shared_ptr<Graph const> graph, noise::NoiseShape shape) : mGraph(std::move(graph)), mShape(shape) {}

private:
    std::shared_ptr<Graph const> mGraph;
    noise::NoiseShape            mShape;
};

/// The vanilla noise parameters upstream's terrain and surface use (minecraft:ridge, surface,
/// surface_secondary, ...): Minecraft 1.21.1's NoiseData values; nullopt for others.
[[nodiscard]] std::optional<noise::NoiseParameters> vanillaNoise(std::string const& id);

/// RandomState's positional factory of `seed` (Xoroshiro, upstream's settings are not legacy).
[[nodiscard]] std::unique_ptr<jrand::PositionalRandomFactory> randomStateFactory(std::int64_t seed);
/// RandomState#getOrCreateNoise(id): NormalNoise.create(factory.fromHashOf(id), params).
[[nodiscard]] noise::NormalNoise randomStateNoise(std::int64_t seed, std::string const& id, noise::NoiseParameters const& params);

} // namespace gf::core::terrain
