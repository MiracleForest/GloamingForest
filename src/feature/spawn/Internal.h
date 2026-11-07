#pragma once

// Internals of the spawn engine (F5) shared with the generator pipeline and the selftests. Not a
// contract.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/ext/worldgen/Spawning.h"
#include "feature/spawn/Spawn.h"

class BlockPos;
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
/// Holder#is(biome tag): gf::tags (TF's biome tags), plus the vanilla biome tags the spawner reads
/// (minecraft:is_river, minecraft:reduce_water_ambient_spawns) built in.
[[nodiscard]] bool biomeHasTag(std::string const& biome, std::string const& tag);

/// The spawn table of a biome (registered, else read from its imported JSON); thread-safe.
[[nodiscard]] Table tableOf(std::string const& biomeId);

/// NaturalSpawner#mobsAt in the Twilight Forest: ChunkGenerator#getMobsAt changed by NeoForge's
/// PotentialSpawns event. The special spawn functions (onSpecialSpawn) answer first: F6's
/// EntityEvents#structureSpecialSpawns gives a landmark start's controlled list (its
/// gatherPotentialSpawns) or a structure's spawn override; otherwise the biome's list of `category`.
[[nodiscard]] std::vector<SpawnEntry> spawnListAt(BlockSource& region, BlockPos const& pos, std::string const& category, std::string const& biome);

/// ChunkAccess#getHeight(WORLD_SURFACE, x, z): the highest non-air block of the column (minY - 1
/// when empty), found below the chunk's highest non-air sub-chunk.
[[nodiscard]] int worldSurfaceY(BlockSource& region, int x, int z);

/// The Bedrock entity id of a Java entity id: "minecraft:" added to a bare id, and the vanilla ids
/// Bedrock names otherwise ("minecraft:zombified_piglin" -> "minecraft:zombie_pigman", ...).
[[nodiscard]] std::string bedrockEntityId(std::string_view javaId);

} // namespace gf::spawn::detail
