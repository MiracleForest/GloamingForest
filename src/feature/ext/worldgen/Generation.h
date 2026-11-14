#pragma once

// Contract extension of "worldgen" (F5, rule R18; to be folded into Worldgen.h by A8).
//
// - The order of the biome source's possible biomes (Java's BiomeSource#possibleBiomes), which
//   FeatureSorter turns into the feature seed indices of every placed feature. U1 sets it with
//   its biome list; without it the imported biome ids are used in alphabetical order.
// - The generator itself, for a dimension backend that does not go through the game's
//   CustomDimension (F4, spike S1): createGenerator(dimension) returns the Gloaming generator.
// - Statistics for timing budgets and selftests, and an in-memory run of the whole pipeline.
// - The stage order as Java runs it, which differs from the stage comment of Worldgen.h:
//     loadChunk:  Biomes -> Starts -> Noise -> Surface -> Blanket -> Carvers
//     decoration: for each GenerationStep.Decoration step 0..10: the structure steps of that step
//                 (the plain Pieces steps at SurfaceStructures), then the step's placed features;
//                 then Features -> LargeWriter -> Spawns
//   * Blanket runs at surface time, right after the surface provider and the Surface steps: upstream
//     injects ChunkBlanketProcessors#chunkBlanketing after ChunkGenerator#buildSurface in
//     ChunkStatusTasks#generateSurface (tf-asm ChunkStatusTaskTransformer), so the canopy and the
//     glacier see the bare ground (WORLD_SURFACE_WG) and every carver and feature sees them.
//     blanketContext() gives the blanket the WorldGenRegion random of that step (fork() it per
//     processor that applies, as upstream does) and the biomes in the chunk's sections.
//   * Structures: Java's applyBiomeDecoration places, at each step, the structures of that step
//     before the step's features, each after worldgenrandom.setFeatureSeed(decorationSeed, index,
//     step), where `index` counts EVERY structure of the step in Java's structure registry
//     (Minecraft's own structures included, in registry order: structuresByStep(), structureSlot()).
//     - A plain Stage::Pieces step keeps Worldgen.h's meaning: it runs ONCE per chunk, at
//       SurfaceStructures (the step of most upstream landmarks), before that step's features.
//     - addStructureStep() (opt in) runs at EVERY decoration step 0..10: it places only the
//       structures whose step is decorationContext()->step, each with structureRandomOf(id)
//       (= structureRandom(structureSlot(id)->index)). At SurfaceStructures the plain Pieces steps
//       and the structure steps run together by (order, registration).
//     decorationContext() is set during both.
// - Heights: Chunk::height is Java's ChunkAccess#getHeight (the y of the highest block the
//   heightmap counts, minY - 1 for an empty column); Region::height is WorldGenLevel#getHeight
//   (that y + 1, the first free y).
// - Biomes: Region::biome(pos) is Java's LevelReader#getBiome(pos), i.e. BiomeManager's zoom
//   (core/jrand/BiomeManager.h) over the quart biomes of the chunks (the biome source outside
//   them); Chunk::biome / setBiome are the raw quart biomes the generator writes (during loadChunk).
//   The game's chunks store a biome per block: the generator writes the zoomed biome of every
//   block (what Java's client and gameplay read per block), so Bedrock's own per-block readers
//   (grass and water colours, precipitation, snow, the client) see Java's borders; the raw quarts
//   come from the biome source (and the chunk's distinct raw biomes are kept for the decoration).
//
//     gf::worldgen::ext::setPossibleBiomes({"gloamingforest:forest", "gloamingforest:dense_forest", ...});
//     gf::worldgen::ext::addStructureStep("landmark.pieces", 0, [](auto& chunk, auto& region) {
//         auto const& d = *gf::worldgen::ext::decorationContext();
//         for (auto const& id : gf::worldgen::ext::structuresByStep()[d.step])
//             if (auto random = d.structureRandomOf(id)) { ... place id's starts reaching the chunk ... }
//     });

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

/// The Gloaming Forest generator for `dimension` (nullptr when not possible); the game owns it.
[[nodiscard]] std::unique_ptr<WorldGenerator> createGenerator(Dimension& dimension);
/// Whether the generator of the Gloaming Forest is installed in the running level.
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

/// What a structure step gets (ChunkGenerator#applyBiomeDecoration's structure part).
struct DecorationContext {
    int          chunkX = 0, chunkZ = 0; ///< the chunk being decorated
    int          step   = 0;             ///< the GenerationStep.Decoration being run (DecorationStep)
    std::int64_t decorationSeed = 0;     ///< WorldgenRandom#setDecorationSeed(seed, minBlockX, minBlockZ)
    /// A Xoroshiro WorldgenRandom set by setFeatureSeed(decorationSeed, index, step): the random of
    /// the index-th structure of this step. `index` counts every structure of the step in Java's
    /// structure registry, Minecraft's own included (structureSlot), not only upstream's.
    [[nodiscard]] std::unique_ptr<core::jrand::WorldgenRandom> structureRandom(int index) const;
    /// structureRandom(structureSlot(structureId)->index); nullptr when the structure is unknown or
    /// belongs to another step.
    [[nodiscard]] std::unique_ptr<core::jrand::WorldgenRandom> structureRandomOf(std::string const& structureId) const;
};
/// The context of the structure step running on this thread (nullptr outside a Pieces / structure
/// step call).
[[nodiscard]] DecorationContext const* decorationContext();

/// Adds a structure step that runs at every decoration step (see above); `order` and the
/// registration order sort it with the other structure steps (and, at SurfaceStructures, with the
/// plain Stage::Pieces steps). Re-adding an id replaces it.
void addStructureStep(std::string const& id, int order, ChunkFn fn);

/// Java's structure registry grouped by GenerationStep.Decoration ([step] -> structure ids in
/// registry order): Minecraft 1.21.1's 34 structures and upstream 4.8's 21 built in, plus any other
/// <mod>/data/<ns>/worldgen/structure JSON, ordered as RegistryDataLoader loads them (the file
/// paths "worldgen/structure/<path>.json" sorted, then the namespaces).
[[nodiscard]] std::vector<std::vector<std::string>> structuresByStep();
struct StructureSlot {
    int step  = 0; ///< GenerationStep.Decoration ordinal
    int index = 0; ///< the structure's position among every structure of that step
};
/// The step and the index Java seeds structure `structureId` with (nullopt: not registered).
[[nodiscard]] std::optional<StructureSlot> structureSlot(std::string const& structureId);

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
