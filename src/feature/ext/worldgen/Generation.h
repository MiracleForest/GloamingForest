#pragma once

// Contract extension of "worldgen" (F5, rule R18; to be folded into Worldgen.h by A8).
//
// - The order of the biome source's possible biomes (Java's BiomeSource#possibleBiomes), which
//   FeatureSorter turns into the feature seed indices of every placed feature. U1 sets it with
//   its biome list; without it the imported biome ids are used in alphabetical order.
// - The generator itself, for a dimension backend that does not go through the game's
//   CustomDimension (F4, spike S1): createGenerator(dimension) returns the Twilight generator.
// - Statistics for timing budgets and selftests, and an in-memory run of the whole pipeline.
//
//     gf::worldgen::ext::setPossibleBiomes({"twilightforest:forest", "twilightforest:dense_forest", ...});

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "feature/worldgen/Worldgen.h"

class Dimension;
class WorldGenerator;

namespace gf::worldgen::ext {

/// Java's GenerationStep.Decoration (index = step of the biome "features" lists).
enum class DecorationStep {
    RawGeneration,
    Lakes,
    LocalModifications,
    UndergroundStructures,
    SurfaceStructures,
    Strongholds,
    UndergroundOres,
    UndergroundDecoration,
    FluidSprings,
    VegetalDecoration,
    TopLayerModification,
};

/// The biome source's possible biomes, in its order (feature indices depend on it).
void setPossibleBiomes(std::vector<std::string> biomeIds);
[[nodiscard]] std::vector<std::string> possibleBiomes();
/// The placed features of a biome per decoration step (from its imported JSON; empty: unknown).
[[nodiscard]] std::vector<std::vector<std::string>> biomeFeatures(std::string const& biomeId);
/// The configured carvers of a biome ("air" step).
[[nodiscard]] std::vector<std::string> biomeCarvers(std::string const& biomeId);

/// The Twilight Forest generator for `dimension` (nullptr when not possible); the game owns it.
[[nodiscard]] std::unique_ptr<WorldGenerator> createGenerator(Dimension& dimension);
/// Whether the generator of the Twilight Forest is installed in the running level.
[[nodiscard]] bool generatorActive();

struct Stats {
    std::uint64_t chunksGenerated = 0; ///< loadChunk runs (Biomes .. Carvers)
    std::uint64_t chunksDecorated = 0; ///< decoration runs (Features .. Spawns)
    double        generateMs      = 0; ///< total time of those runs
    double        decorateMs      = 0;
};
[[nodiscard]] Stats stats();

/// Runs the whole pipeline (every stage, every registered provider) on `count` chunks held in
/// memory, starting at chunk (x0, z0) row by row `width` chunks wide, with `seed`. Returns the
/// milliseconds spent and fills `hash` with a hash of the generated blocks (determinism checks).
double runInMemory(std::int64_t seed, int x0, int z0, int width, int count, std::uint64_t* hash = nullptr);

} // namespace gf::worldgen::ext
