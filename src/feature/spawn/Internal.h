#pragma once

// Internals of the spawn engine (F5) shared with the generator pipeline. Not a contract.

#include <string>

#include "feature/spawn/Spawn.h"

namespace gf::spawn::detail {

/// NaturalSpawner#spawnMobsForChunkGeneration for the center chunk of `region` (Spawns stage):
/// positions are chosen on the generation thread with `random`, the mobs are spawned on the server
/// thread once the chunk is loaded.
void spawnForChunkGeneration(worldgen::Region& region, std::string const& biomeId, int chunkX, int chunkZ, RandomSource& random);

} // namespace gf::spawn::detail
