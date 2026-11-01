#pragma once

// Contract "spawn" (header owned by F0, implemented by F5).
//
// The spawn engine of the Twilight Forest: per-biome spawn tables (U1, from BiomeMaker), per-entity
// placement rules (SpawnPlacements / TF's registerSpawnPlacements), controlled spawns inside
// landmarks (read from the landmark descriptors, F6) and TF's structureSpecialSpawns.
//
//     gf::spawn::registerPlacement("twilightforest:boar", gf::spawn::PlacementRule::OnGround);
//     Actor* a = gf::spawn::spawnMob(region, "twilightforest:boar", pos, gf::spawn::SpawnReason::Spawner);

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "feature/worldgen/Worldgen.h"

class Actor;
class BlockPos;
class BlockSource;
class Vec3;

namespace gf::spawn {

using worldgen::RandomSource;

/// Java's SpawnPlacementTypes.
enum class PlacementRule { OnGround, InWater, InLava, NoRestrictions };

/// Java's MobSpawnType.
enum class SpawnReason {
    Natural,
    ChunkGeneration,
    Spawner,
    Structure,
    Breeding,
    MobSummoned,
    Jockey,
    Event,
    Conversion,
    Reinforcement,
    Triggered,
    Bucket,
    SpawnEgg,
    Command,
    Dispenser,
    Patrol,
    TrialSpawner,
};

/// Extra condition of a placement (Java's SpawnPlacements.SpawnPredicate).
using PredicateFn = std::function<bool(BlockSource& region, std::string const& entityId, BlockPos const& pos, SpawnReason reason, RandomSource& random)>;
/// Runs on the new mob before it is added (finalizeSpawn equivalents, variants, equipment).
using InitFn = std::function<void(Actor& actor)>;

struct SpawnEntry {
    std::string entityId;
    int         weight   = 1;
    int         minCount = 1;
    int         maxCount = 1;
};
/// A biome's spawn table by MobCategory ("monster", "creature", "ambient", "water_creature",
/// "water_ambient", "underground_water_creature", "axolotls", "misc").
struct Table {
    std::map<std::string, std::vector<SpawnEntry>> byCategory;
    /// MobSpawnSettings#creatureGenerationProbability.
    float creatureProbability = 0.1f;
};

void registerPlacement(std::string const& entityId, PlacementRule rule, PredicateFn predicate = {});
/// Spawns `entityId` at `pos` (placement rule and predicate are not checked: the caller decides).
/// nullptr when the id is unknown or the spawn failed.
Actor* spawnMob(BlockSource& region, std::string const& entityId, Vec3 const& pos, SpawnReason reason, InitFn init = {});
/// Replaces the spawn table of `biomeId`.
void setBiomeSpawns(std::string const& biomeId, Table table);
/// Natural-spawn override for a position (TF's structureSpecialSpawns, landmark controlled
/// spawns): return a list to use instead of the biome's for `category`, nullopt to pass.
using SpecialSpawnFn = std::function<std::optional<std::vector<SpawnEntry>>(BlockSource& region, BlockPos const& pos, std::string const& category)>;
void onSpecialSpawn(SpecialSpawnFn fn);

} // namespace gf::spawn
