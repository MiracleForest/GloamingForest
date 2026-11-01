#pragma once

// Internals of the spawn engine (F5) shared with the generator pipeline and the selftests. Not a
// contract.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/ext/worldgen/Spawning.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Spawn.h"

class BlockSource;

namespace gf::spawn::detail {

/// NaturalSpawner#spawnMobsForChunkGeneration for the center chunk of `region` (Spawns stage):
/// positions are chosen on the generation thread with `random`, the mobs are spawned on the server
/// thread once the chunk is loaded. Returns the number of mobs queued.
int spawnForChunkGeneration(worldgen::Region& region, std::string const& biomeId, int chunkX, int chunkZ, RandomSource& random);

/// Mob#checkSpawnRules without an override (ext::SpawnBehaviour's walk target, by `category` when
/// ByCategory); `generation`: the brightness estimated from the blocks above.
[[nodiscard]] bool spawnRulesDefault(worldgen::Region& region, BlockPos const& pos, ext::WalkTarget walkTarget, std::string_view category, bool generation);
/// Mob#checkSpawnObstruction without an override at feet (x, y, z): the liquid check (Mob) unless
/// EntitiesOnly, then the entities of `entities` (nullptr: chunk generation, none).
[[nodiscard]] bool obstructionDefault(
    worldgen::Region&          region,
    BlockSource*               entities,
    ext::SpawnBehaviour const& mob,
    std::string_view           category,
    double                     x,
    double                     y,
    double                     z
);
/// Holder#is(biome tag): the vanilla tags the spawner reads built in, gf::tags, the data files.
[[nodiscard]] bool biomeHasTag(std::string const& biome, std::string const& tag);

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

/// StructureStart#isValid: a start with pieces (or generated).
[[nodiscard]] inline bool startValid(landmark::Start const& start) { return start.generated || !start.pieceBoxes.empty(); }

/// ChunkAccess#getHeight(WORLD_SURFACE, x, z): the highest non-air block of the column (minY - 1
/// when empty), found below the chunk's highest non-air sub-chunk.
[[nodiscard]] int worldSurfaceY(BlockSource& region, int x, int z);

} // namespace gf::spawn::detail
