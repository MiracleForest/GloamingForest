#pragma once

// Contract extension of "spawn" (F5, rule R18; to be folded into Spawn.h by A8).
//
// TF's landmark controlled spawns (EntityEvents#gatherPotentialSpawns) need two answers that the
// landmark contract's Start does not carry:
// - the spawn list index at a position (EntityEvents#getSpawnListIndexAt: the highest
//   SpawnIndexProvider index of the pieces containing it; -1 or nothing: no controlled monsters).
//   Without a registered function the start's data "spawn_indices" is used (one int per entry of
//   Start::pieceBoxes, null for a piece that is not a SpawnIndexProvider), else every piece has
//   index 0 (TFStructureComponent's default). F6 / the structure units register their pieces'
//   indices per landmark key (dark tower roof 1, final castle floors 1..3, boss rooms -1, ...).
// - an extra area check of the landmark (HollowHillStructure#canSpawnMob, the half dome of the
//   hollow hills, is built in for the three hollow hill keys).
//
// The spawners (NaturalSpawner#spawnCategoryForPosition and #spawnMobsForChunkGeneration) also
// ask the entity type and the mob for answers the placement rule does not give:
// - the heightmap of SpawnPlacements#getHeightmapType (MOTION_BLOCKING_NO_LEAVES unless set, as
//   Java's default and every TF registration),
// - the entity's size (EntityType#getSpawnAABB), Mob#getMaxSpawnClusterSize (4),
//   Mob#isMaxGroupSizeReached (false), Mob#checkSpawnRules and Mob#checkSpawnObstruction
//   (EventHooks#checkSpawnPosition). The defaults are Java's base classes: PathfinderMob's walk
//   target value by category (Monster: dark enough, 0.5 - light >= 0; Animal in "creature": grass
//   below or light - 0.5 >= 0; others 0) and Mob#checkSpawnObstruction (no other mob, player, boat
//   or minecart box overlaps the new mob's). Entity units register their overrides (TF's ghasts,
//   golems, kobolds, penguins, giant miners, ...).
//
//     gf::spawn::ext::registerSpawnIndex("twilightforest:dark_tower", [](auto const& start, BlockPos const& pos) { ... });
//     gf::spawn::ext::registerSpawnBehaviour("twilightforest:kobold", {.maxClusterSize = 6});

#include <functional>
#include <optional>
#include <string>

#include "feature/landmark/Landmark.h"
#include "feature/spawn/Spawn.h"

class BlockPos;

namespace gf::spawn::ext {

using SpawnIndexFn = std::function<int(landmark::Start const& start, BlockPos const& pos)>;
/// Replaces the spawn list index lookup of landmark `key`.
void registerSpawnIndex(std::string const& landmarkKey, SpawnIndexFn fn);

using SpawnAreaFn = std::function<bool(landmark::Start const& start, BlockPos const& pos)>;
/// Adds a condition monsters of landmark `key` must meet (false: the controlled list does not apply).
void registerSpawnArea(std::string const& landmarkKey, SpawnAreaFn fn);

/// SpawnPlacements#getHeightmapType of `entityId` (chunk-generation spawns).
void setPlacementHeightmap(std::string const& entityId, worldgen::Heightmap heightmap);

/// What the spawners ask the mob being spawned (see above); unset members keep Java's default.
struct SpawnBehaviour {
    double width  = 0.6; ///< EntityType#getWidth / getHeight (the spawn AABB)
    double height = 1.8;
    int    maxClusterSize = 4; ///< Mob#getMaxSpawnClusterSize
    /// Mob#isMaxGroupSizeReached(groupSize).
    std::function<bool(int groupSize)> maxGroupSizeReached;
    /// Mob#checkSpawnRules at `pos` (the mob's block position).
    std::function<bool(worldgen::Region& region, BlockPos const& pos, SpawnReason reason)> checkSpawnRules;
    /// Mob#checkSpawnObstruction of a mob of this size at (x, y, z) (feet center).
    std::function<bool(worldgen::Region& region, double x, double y, double z)> checkSpawnObstruction;
};
void registerSpawnBehaviour(std::string const& entityId, SpawnBehaviour behaviour);

} // namespace gf::spawn::ext
