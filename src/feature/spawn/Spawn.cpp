// Neutral stub of the "spawn" contract (F0). F5 replaces this file (rule R18).
#include "feature/spawn/Spawn.h"

#include "core/Stub.h"

namespace gf::spawn {

void registerPlacement(std::string const&, PlacementRule, PredicateFn) { GF_STUB("spawn.registerPlacement"); }

Actor* spawnMob(BlockSource&, std::string const&, Vec3 const&, SpawnReason, InitFn) {
    GF_STUB("spawn.spawnMob");
    return nullptr;
}

void setBiomeSpawns(std::string const&, Table) { GF_STUB("spawn.setBiomeSpawns"); }
void onSpecialSpawn(SpecialSpawnFn) { GF_STUB("spawn.onSpecialSpawn"); }

} // namespace gf::spawn
