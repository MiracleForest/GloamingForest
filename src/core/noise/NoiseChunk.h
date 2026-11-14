#pragma once

// Java-parity chunk density filling (net.minecraft.world.level.levelgen.NoiseChunk of 1.21.1,
// without aquifers, ore veins and blending, which The Gloaming Forest disables). Pure C++ (gf-core).
//
// Density::compute evaluates a density function at one point, where the NoiseChunk markers
// (interpolated, flat_cache, cache_2d, cache_once, cache_all_in_cell) pass through, like Java's
// SinglePointContext. Terrain needs the chunk semantics instead: `interpolated` interpolates the
// cell corners trilinearly (in Java's lerp order) and `flat_cache` samples its argument once per
// quart column at y = 0. NoiseChunk compiles the same JSON with those semantics:
//
//     auto nc = gf::core::noise::NoiseChunk::fromJson(finalDensity, seed, resolver, {.minY = -32, .height = 256});
//     std::vector<double> d = nc->fill(chunkX, chunkZ);   // index ((y - minY) * 16 + z) * 16 + x
//
// U2 (terrain) builds the upstream terrain on it; a compiled NoiseChunk is immutable and thread-safe.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/noise/Noise.h"

namespace gf::core::noise {

/// NoiseSettings of a noise_settings JSON ("noise": {min_y, height, size_horizontal, size_vertical}).
struct NoiseShape {
    int minY           = -32;
    int height         = 256;
    int sizeHorizontal = 2; ///< cell width = sizeHorizontal * 4
    int sizeVertical   = 2; ///< cell height = sizeVertical * 4
};

class NoiseChunk {
public:
    /// Compiles `json` (a density function, usually noise_router.final_density) for `seed`.
    [[nodiscard]] static std::shared_ptr<NoiseChunk const>
    fromJson(nlohmann::json const& json, std::int64_t seed, Density::Resolver const& resolver, NoiseShape shape);

    /// The densities of every block of chunk (chunkX, chunkZ), y from minY up to minY + height,
    /// index ((y - minY) * 16 + localZ) * 16 + localX.
    [[nodiscard]] std::vector<double> fill(int chunkX, int chunkZ) const;
    [[nodiscard]] NoiseShape const&   shape() const { return mShape; }

    struct Impl;
    explicit NoiseChunk(std::shared_ptr<Impl const> impl, NoiseShape shape) : mImpl(std::move(impl)), mShape(shape) {}

private:
    std::shared_ptr<Impl const> mImpl;
    NoiseShape                  mShape;
};

} // namespace gf::core::noise
