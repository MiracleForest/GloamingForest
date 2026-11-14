#pragma once

// Contract "landmark", pure part (header owned by F0, implemented by F6 with gtests against F13
// oracle goldens). Translations of upstream's LegacyLandmarkPlacements, independent of the game.
//
// upstream puts one landmark center near every multiple of 256 blocks (getNearestCenterXZ), picks the
// landmark by the biome at the center (BIOME_2_STRUCTURES) or, in other biomes, a variety
// landmark seeded by the overworld seed (pickVarietyLandmark). Landmark keys are upstream structure ids
// ("gloamingforest:lich_tower").

#include <cstdint>
#include <functional>
#include <string>

namespace gf::core::landmark {

/// A block column (x, z).
struct ColumnXZ {
    int x = 0, z = 0;
    friend bool operator==(ColumnXZ const&, ColumnXZ const&) = default;
};

/// LegacyLandmarkPlacements#getNearestCenterXZ: the block column of the landmark center nearest
/// to chunk (chunkX, chunkZ).
[[nodiscard]] ColumnXZ nearestCenter(int chunkX, int chunkZ);
/// LegacyLandmarkPlacements#chunkHasLandmarkCenter.
[[nodiscard]] bool chunkHasCenter(int chunkX, int chunkZ);
/// LegacyLandmarkPlacements#manhattanDistanceFromLandmarkCenter (in chunks).
[[nodiscard]] int manhattanDistanceFromCenter(int chunkX, int chunkZ);
/// LegacyLandmarkPlacements#pickVarietyLandmark with the overworld `seed`: the variety landmark
/// key for a chunk (lich towers and naga courtyards at fixed region offsets, else the weighted
/// VARIETY_LANDMARKS list).
[[nodiscard]] std::string pickVariety(std::int64_t seed, int chunkX, int chunkZ);
/// The biome id at a block column ("gloamingforest:snowy_forest").
using BiomeAtFn = std::function<std::string(int blockX, int blockZ)>;
/// LegacyLandmarkPlacements#pickLandmarkForChunk: the biome's landmark (BIOME_2_STRUCTURES) or
/// the variety landmark.
[[nodiscard]] std::string pickForChunk(std::int64_t seed, int chunkX, int chunkZ, BiomeAtFn const& biomeAt);

} // namespace gf::core::landmark
