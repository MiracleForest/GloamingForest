#pragma once

// The structure part of Minecraft 1.21.1's ChunkGenerator#applyBiomeDecoration (F6, pure, gf-core,
// gtested against the F13 oracle's random goldens): the seed a landmark's pieces are placed with.
//
// Java decorates a chunk step by step (GenerationStep.Decoration 0..10). At each step it walks
// every structure of the Structure registry whose step it is, in registry order, and before the
// starts of the l-th one it calls worldgenrandom.setFeatureSeed(decorationSeed, l, step) on one
// WorldgenRandom over a XoroshiroRandomSource; decorationSeed is setDecorationSeed(levelSeed,
// minBlockX, minBlockZ) of the chunk's section origin. `l` counts every structure of the step,
// Minecraft's own included, whether or not it has a start in the chunk.
//
// Registry order: RegistryDataLoader#loadContentsFromManager registers the entries in the order of
// FileToIdConverter#listMatchingResources, a TreeMap keyed by the file locations
// "<ns>:worldgen/structure/<path>.json"; ResourceLocation#compareTo compares the path first, then
// the namespace.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::landmark::decoration {

/// The number of GenerationStep.Decoration steps.
inline constexpr int kSteps = 11;
/// GenerationStep.Decoration#getSerializedName -> ordinal (-1: unknown).
[[nodiscard]] int stepIndex(std::string_view name);
/// The serialized name of a step ("" outside 0..10).
[[nodiscard]] std::string_view stepName(int step);

/// A Structure registry entry: its id and its GenerationStep.Decoration ordinal.
struct Structure {
    std::string id;
    int         step = 0;
};
/// Minecraft 1.21.1's 34 Structure registry entries (data/minecraft/worldgen/structure/*.json of the
/// vanilla data pack) with their steps.
[[nodiscard]] std::vector<Structure> const& vanillaStructures();

/// Whether structure `a` comes before structure `b` in the Structure registry (see above; an id
/// without a namespace is "minecraft:").
[[nodiscard]] bool registryBefore(std::string_view a, std::string_view b);

/// Where applyBiomeDecoration seeds a structure: its step and its index among the structures of
/// that step.
struct Slot {
    int  step  = 0;
    int  index = 0;
    friend bool operator==(Slot const&, Slot const&) = default;
};
/// The slot of structure `id` decorating at `step`, among `registry` (every registered structure;
/// `id` itself may be listed or not): the number of entries of `step` before it in registry order.
[[nodiscard]] Slot slotOf(std::string const& id, int step, std::vector<Structure> const& registry);

/// WorldgenRandom(XoroshiroRandomSource)#setDecorationSeed(levelSeed, chunkX << 4, chunkZ << 4): the
/// decoration seed of chunk (chunkX, chunkZ).
[[nodiscard]] std::int64_t decorationSeed(std::int64_t levelSeed, int chunkX, int chunkZ);
/// The random the starts of the structure in `slot` are placed with: a WorldgenRandom over a
/// XoroshiroRandomSource after setFeatureSeed(decorationSeed, slot.index, slot.step), i.e. seeded
/// with decorationSeed + index + 10000 * step.
[[nodiscard]] std::unique_ptr<jrand::WorldgenRandom> structureRandom(std::int64_t decorationSeed, Slot slot);

} // namespace gf::core::landmark::decoration
