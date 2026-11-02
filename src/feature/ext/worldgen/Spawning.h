#pragma once

// Contract extension of "spawn" (F5, rule R18; to be folded into Spawn.h by A8).
//
// TF's landmark controlled spawns (EntityEvents#gatherPotentialSpawns, structureSpecialSpawns) and
// the structures' spawn overrides reach the spawners through gf::spawn::onSpecialSpawn: F6 answers
// them from its starts (the pieces' SpawnIndexProvider indices, landmark::Generator::spawnIndex for
// a landmark's own lookup, such as the hollow hills' HollowHillStructure#canSpawnMob dome).
//
// The spawners (NaturalSpawner#spawnCategoryForPosition and #spawnMobsForChunkGeneration) also
// ask the entity type and the mob for answers the placement rule does not give:
// - the heightmap of SpawnPlacements#getHeightmapType (MOTION_BLOCKING_NO_LEAVES unless set, as
//   Java's default and every TF registration),
// - the entity's size (EntityType#getSpawnAABB), Mob#getMaxSpawnClusterSize (4),
//   Mob#isMaxGroupSizeReached (false), Mob#checkSpawnRules and Mob#checkSpawnObstruction
//   (EventHooks#checkSpawnPosition). The defaults are Java's base classes:
//   * checkSpawnRules: PathfinderMob#getWalkTargetValue(pos) >= 0 with the walk target of the
//     mob's class (`walkTarget`): Monster 0.5 - brightness, Animal 10 on grass else brightness -
//     0.5, Mob / PathfinderMob / WaterAnimal / AmbientCreature 0. Unset (ByCategory) it follows the
//     spawn category: "monster" -> Monster, "creature" -> Animal (chunk-generation spawns are
//     always "creature"), every other category -> 0. A creature-category mob that is not an Animal
//     (a PathfinderMob with walk target 0) must set WalkTarget::Zero or register checkSpawnRules.
//   * checkSpawnObstruction (`obstruction`): Mob's `!containsAnyLiquid(box) && isUnobstructed`
//     (no water or lava block, waterlogged blocks included, in the spawn box, and no living mob,
//     player, boat or minecart box overlapping it), or WaterAnimal's `isUnobstructed` only (also
//     Axolotl, Drowned and Guardian). Unset (ByCategory) the water categories take WaterAnimal's.
//     During chunk generation the entity part never refuses (Java's WorldGenRegion#getEntities is
//     empty), so the default there is the liquid check alone.
//   Brightness during chunk generation is estimated from the blocks above (Bedrock lights chunks
//   after decoration). Entity units register their overrides (TF's ghasts, golems, kobolds,
//   penguins, giant miners, ...).
//
//     gf::spawn::ext::registerSpawnBehaviour("twilightforest:kobold", {.maxClusterSize = 6});
//     gf::spawn::ext::registerSpawnBehaviour("minecraft:drowned", {.walkTarget = WalkTarget::Monster, .obstruction = Obstruction::EntitiesOnly});

#include <functional>
#include <optional>
#include <string>

#include "feature/spawn/Spawn.h"

class BlockPos;

namespace gf::spawn::ext {

/// SpawnPlacements#getHeightmapType of `entityId` (chunk-generation spawns).
void setPlacementHeightmap(std::string const& entityId, worldgen::Heightmap heightmap);

/// The walk target value behind the default Mob#checkSpawnRules (PathfinderMob#getWalkTargetValue).
enum class WalkTarget {
    ByCategory, ///< "monster": Monster, "creature": Animal, other categories: Zero
    Zero,       ///< Mob, PathfinderMob, WaterAnimal, AmbientCreature: always true
    Monster,    ///< Monster: 0.5 - brightness >= 0
    Animal,     ///< Animal: grass below, else brightness - 0.5 >= 0
};
/// What the default Mob#checkSpawnObstruction refuses.
enum class Obstruction {
    ByCategory,        ///< the water categories: EntitiesOnly, the others: LiquidAndEntities
    LiquidAndEntities, ///< Mob: liquid in the spawn box, or an entity that blocks building in it
    EntitiesOnly,      ///< WaterAnimal, Axolotl, Drowned, Guardian: an entity only
};

/// What the spawners ask the mob being spawned (see above); unset members keep Java's default.
struct SpawnBehaviour {
    double width  = 0.6; ///< EntityType#getWidth / getHeight (the spawn AABB)
    double height = 1.8;
    int    maxClusterSize = 4; ///< Mob#getMaxSpawnClusterSize
    /// The Java base class answers of the defaults (used when the functions below are not set).
    WalkTarget  walkTarget  = WalkTarget::ByCategory;
    Obstruction obstruction = Obstruction::ByCategory;
    /// Mob#isMaxGroupSizeReached(groupSize).
    std::function<bool(int groupSize)> maxGroupSizeReached;
    /// Mob#checkSpawnRules at `pos` (the mob's block position); replaces the walk target default.
    std::function<bool(worldgen::Region& region, BlockPos const& pos, SpawnReason reason)> checkSpawnRules;
    /// Mob#checkSpawnObstruction of a mob of this size at (x, y, z) (feet center); replaces the
    /// default (liquid and entity checks).
    std::function<bool(worldgen::Region& region, double x, double y, double z)> checkSpawnObstruction;
};
void registerSpawnBehaviour(std::string const& entityId, SpawnBehaviour behaviour);

} // namespace gf::spawn::ext
