#pragma once

// Internals of the spawn engine (F5) shared with the generator pipeline and the selftests. Not a
// contract.

#include <optional>
#include <string>
#include <vector>

#include "feature/ext/worldgen/Spawning.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Spawn.h"

namespace gf::spawn::detail {

/// NaturalSpawner#spawnMobsForChunkGeneration for the center chunk of `region` (Spawns stage):
/// positions are chosen on the generation thread with `random`, the mobs are spawned on the server
/// thread once the chunk is loaded.
void spawnForChunkGeneration(worldgen::Region& region, std::string const& biomeId, int chunkX, int chunkZ, RandomSource& random);

/// EntityEvents#gatherPotentialSpawns for one landmark start: the controlled list of `category` at
/// `pos`, nullopt when the start does not decide (the caller keeps looking / uses the biome).
[[nodiscard]] std::optional<std::vector<SpawnEntry>>
controlledSpawnsOf(landmark::Start const& start, landmark::Descriptor const& descriptor, BlockPos const& pos, std::string const& category);
/// ChunkGenerator#getMobsAt's structure spawn overrides of one start (StructureSpawnOverride:
/// "full" box or "piece" boxes), nullopt when the start has none for `category` at `pos`.
[[nodiscard]] std::optional<std::vector<SpawnEntry>>
spawnOverridesOf(landmark::Start const& start, landmark::Descriptor const& descriptor, BlockPos const& pos, std::string const& category);
/// The spawn list index at `pos` (EntityEvents#getSpawnListIndexAt; -1: none).
[[nodiscard]] int spawnIndexAt(landmark::Start const& start, BlockPos const& pos);
/// HollowHillStructure#canSpawnMob.
[[nodiscard]] bool hollowHillCanSpawnMob(BlockPos const& pos, landmark::BlockBox const& startBox);

/// The spawn table of a biome (registered, else read from its imported JSON); thread-safe.
[[nodiscard]] Table tableOf(std::string const& biomeId);

} // namespace gf::spawn::detail
