#pragma once

// Internals of F8's boss kit for its selftests (not a contract).

#include <cstdint>
#include <functional>
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
/// Whether the last conquest requested found a start of the home structure to conquer.
[[nodiscard]] bool lastConquestFound();
void               resetTestRecords();
/// Death chests placed that wait for their container (filled, or their loot popped out, later).
[[nodiscard]] int pendingFillsForTest();
/// Whether the mob `uid` has qualified players (MultiplayerInclusivityAttachment) on record.
[[nodiscard]] bool qualifiedForTest(std::int64_t uid);
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
/// The facing state of the death chest `blockId` placed with a death's random seeded `seed` (the
/// state's name and value; empty when the block has no facing state).
[[nodiscard]] std::pair<std::string, std::string> deathChestFacingForTest(std::string const& blockId, std::int64_t seed);
/// IBossLootBuffer#tryDeposit's rule: a piston could push or break the block (PushReaction != BLOCK).
[[nodiscard]] bool pistonMovable(BlockSource& region, BlockPos const& pos);
/// Engine death ticks held back so far (dying boss bodies kept past Bedrock's 20 ticks).
[[nodiscard]] int heldDeathTicksForTest();
/// Removes the lifecycle overrides of a type (fixture overrides live only while a test runs).
void clearLifecycleForTest(std::string const& entityId);
/// SpawnPlacements#checkSpawnRules (SPAWNER) as the sinister spawner applies it.
[[nodiscard]] bool spawnRulesAllowForTest(BlockSource& region, BlockPos const& pos, bool monster);

/// Chunks that count as not loaded for the dying bosses and their death chests (`fn(dim, pos)`
/// true: not loaded; nullptr: the real chunk state again).
void setUnloadedForTest(std::function<bool(int dim, BlockPos const& pos)> fn);
/// The dying record of the boss `uid` (nullopt: none).
struct DyingInfo {
    int          deathTime = 0;
    int          finish    = 0;
    bool         bodiless  = false;
    bool         released  = false;
    int          bufferStacks = 0; ///< non-empty slots of its loot buffer
};
[[nodiscard]] std::optional<DyingInfo> dyingForTest(std::int64_t uid);
/// Death chests waiting for their chunk.
[[nodiscard]] int pendingDepositsForTest();
/// The world save / load of the dying bosses and waiting death chests (and the chests waiting for
/// their container): write them to the world DB, forget them in memory, read them back.
void saveDeathsForTest();
void forgetDeathsForTest();
void loadDeathsForTest();
/// Sets the animation length of the dying record `uid` (a test resuming a saved death).
void setDeathFinishForTest(std::int64_t uid, int finish);
/// Drops the dying record `uid` without its death chest.
void dropDyingForTest(std::int64_t uid);

} // namespace gf::feature::boss
