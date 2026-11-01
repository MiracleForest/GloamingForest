#pragma once

// Contract extension of "worldgen" (F5, rule R18; to be folded into Worldgen.h by A8).
//
// - The order of the biome source's possible biomes (Java's BiomeSource#possibleBiomes), which
//   FeatureSorter turns into the feature seed indices of every placed feature. U1 sets it with
//   its biome list; without it the imported biome ids are used in alphabetical order.
// - The generator itself, for a dimension backend that does not go through the game's
//   CustomDimension (F4, spike S1): createGenerator(dimension) returns the Twilight generator.
// - Statistics for timing budgets and selftests, and an in-memory run of the whole pipeline.
// - The stage order as Java runs it, which differs from the stage comment of Worldgen.h:
//     loadChunk:  Biomes -> Starts -> Noise -> Surface -> Blanket -> Carvers
//     decoration: for each GenerationStep.Decoration step 0..10: Pieces (the structures of that
//                 step), then the step's placed features; then Features -> LargeWriter -> Spawns
//   * Blanket runs at surface time, right after the surface provider and the Surface steps: TF
//     injects ChunkBlanketProcessors#chunkBlanketing after ChunkGenerator#buildSurface in
//     ChunkStatusTasks#generateSurface (tf-asm ChunkStatusTaskTransformer), so the canopy and the
//     glacier see the bare ground (WORLD_SURFACE_WG) and every carver and feature sees them.
//     blanketContext() gives the blanket the WorldGenRegion random of that step (fork() it per
//     processor that applies, as TF does) and the biomes in the chunk's sections.
//   * Pieces runs once per decoration step, with decorationContext() set: place only the
//     structures whose GenerationStep is `step`, the index-th structure of the step (in the
//     structure registry order Java uses) with structureRandom(index), i.e. Java's
//     worldgenrandom.setFeatureSeed(decorationSeed, index, step) before StructureStart#placeInChunk.
// - Heights: Chunk::height is Java's ChunkAccess#getHeight (the y of the highest block the
//   heightmap counts, minY - 1 for an empty column); Region::height is WorldGenLevel#getHeight
//   (that y + 1, the first free y).
// - Biomes: Region::biome(pos) is Java's LevelReader#getBiome(pos), i.e. BiomeManager's zoom
//   (core/jrand/BiomeManager.h) over the quart biomes of the chunks (the biome source outside
//   them); Chunk::biome / setBiome are the raw quart biomes the generator writes.
//
//     gf::worldgen::ext::setPossibleBiomes({"twilightforest:forest", "twilightforest:dense_forest", ...});
//     gf::worldgen::addStage(gf::worldgen::Stage::Pieces, "landmark.pieces", 0, [](auto& chunk, auto& region) {
//         auto const& d = *gf::worldgen::ext::decorationContext();
//         for (auto& s : structuresOfStep(d.step)) { auto random = d.structureRandom(s.index); ... }
//     });

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

/// What a BlanketFn gets besides (Chunk, Region) (ChunkBlanketProcessors#chunkBlanketing).
struct BlanketContext {
    /// WorldGenRegion#getRandom() of the surface step (RandomState's "minecraft:worldgen_region_random"
    /// factory at the chunk's world position), shared by every blanket of the chunk in registration
    /// order: a processor that applies takes its random with fork(), one that does not leaves it.
    RandomSource* regionRandom = nullptr;
    /// The biomes in the chunk's sections (the raw quart biomes), to match biomesForApplication.
    std::vector<std::string> chunkBiomes;
};
/// The context of the BlanketFn call running on this thread (nullptr outside a blanket call).
[[nodiscard]] BlanketContext* blanketContext();

/// What a Pieces step gets (ChunkGenerator#applyBiomeDecoration's structure part).
struct DecorationContext {
    int          chunkX = 0, chunkZ = 0; ///< the chunk being decorated
    int          step   = 0;             ///< the GenerationStep.Decoration being run (DecorationStep)
    std::int64_t decorationSeed = 0;     ///< WorldgenRandom#setDecorationSeed(seed, minBlockX, minBlockZ)
    /// A Xoroshiro WorldgenRandom set by setFeatureSeed(decorationSeed, index, step): the random of
    /// the index-th structure of this step (Java counts every structure of the step in registry order).
    [[nodiscard]] std::unique_ptr<core::jrand::WorldgenRandom> structureRandom(int index) const;
};
/// The context of the Pieces step running on this thread (nullptr outside a Pieces call).
[[nodiscard]] DecorationContext const* decorationContext();

struct Stats {
    std::uint64_t chunksGenerated = 0; ///< loadChunk runs (Biomes .. Carvers)
    std::uint64_t chunksDecorated = 0; ///< decoration runs (Features .. Spawns)
    double        generateMs      = 0; ///< total time of those runs
    double        decorateMs      = 0;
    double        biomeWriteMs    = 0; ///< part of generateMs: writing the biome source's biomes into chunks
};
[[nodiscard]] Stats stats();

/// Runs the whole pipeline (every stage, every registered provider) on `count` chunks held in
/// memory, starting at chunk (x0, z0) row by row `width` chunks wide, with `seed`. Returns the
/// milliseconds spent and fills `hash` with a hash of the generated blocks (determinism checks).
double runInMemory(std::int64_t seed, int x0, int z0, int width, int count, std::uint64_t* hash = nullptr);

} // namespace gf::worldgen::ext
