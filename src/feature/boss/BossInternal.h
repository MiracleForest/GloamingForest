#pragma once

// Internals of F8's boss kit for its selftests (not a contract).

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class BlockPos;
class BlockSource;

namespace gf::feature::boss {

/// The player conditions and spawn logic of TF's spawner block entities: Default (any player in
/// range), AboveMinus4 (the nearest player above pos.y - 4: alpha yeti, ur-ghast, snow queen),
/// AboveMinus2, BelowPlus4 (the nearest below pos.y + 4: minoshroom), Lich (above pos.y - 4, spawns
/// on the floor below), KnightPhantoms (above pos.y - 2, six around the block), Never (final boss).
enum class SpawnerKind { Default, AboveMinus4, AboveMinus2, BelowPlus4, Lich, KnightPhantoms, Never };

/// The height condition of a spawner kind for the nearest player's feet (Player#getY).
[[nodiscard]] bool heightRuleAllows(SpawnerKind kind, int spawnerY, double playerY);

/// The spawner of a block id as the kit runs it.
struct SpawnerRule {
    std::string    entityId;
    int            range = 0;
    std::string    particle;
    SpawnerKind    kind = SpawnerKind::Default;
    nlohmann::json particleParams;
};

/// Treats `type` as a member of twilightforest:multiplayer_inclusive_entities (fixtures).
void markInclusiveForTest(std::string const& type);
void unmarkInclusiveForTest(std::string const& type);
/// The last death chest placed and the last conquest requested (selftests).
[[nodiscard]] std::optional<core::GlobalPos>                         lastDeathChest();
[[nodiscard]] std::optional<std::pair<core::GlobalPos, std::string>> lastConquest();
void                                                                 resetTestRecords();
/// Whether the boss with this unique id has a bar.
[[nodiscard]] bool hasBar(std::int64_t uid);
/// Removes a spawner registration (fixture spawners live only while their test runs).
void unregisterSpawnerForTest(std::string const& blockId);
/// Removes a boss registration (the fixture boss lives only while the tests run).
void unregisterBossForTest(std::string const& entityId);
/// The spawner rule of a block (nullopt: not a registered spawner).
[[nodiscard]] std::optional<SpawnerRule> spawnerRuleForTest(std::string const& blockId);
/// BossSpawnerBlockEntity#anyPlayerInRange of the spawner `blockId` at `pos`.
[[nodiscard]] bool anyPlayerInRangeForTest(int dim, BlockPos const& pos, std::string const& blockId);
/// Makes `blockId` a sinister spawner block while a test runs (the real block is U22's).
void registerSinisterBlockForTest(std::string const& blockId);
void unregisterSinisterBlockForTest(std::string const& blockId);
/// Gives a fixture spawner block a variant's rules (nullopt: back to Default).
void setSpawnerKindForTest(std::string const& blockId, std::optional<SpawnerKind> kind);
/// Mobs spawned by sinister spawners so far.
[[nodiscard]] int sinisterSpawnedForTest();
/// IBossLootBuffer#tryDeposit's rule: a piston could push or break the block (PushReaction != BLOCK).
[[nodiscard]] bool pistonMovable(BlockSource& region, BlockPos const& pos);
/// Engine death ticks held back so far (dying boss bodies kept past Bedrock's 20 ticks).
[[nodiscard]] int heldDeathTicksForTest();
/// Removes the lifecycle overrides of a type (fixture overrides live only while a test runs).
void clearLifecycleForTest(std::string const& entityId);
/// SpawnPlacements#checkSpawnRules (SPAWNER) as the sinister spawner applies it.
[[nodiscard]] bool spawnRulesAllowForTest(BlockSource& region, BlockPos const& pos, bool monster);

} // namespace gf::feature::boss
