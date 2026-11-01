#pragma once

// F10-internal parts of gf::loot (the interpreter, the death pipeline, selftests).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/combat/Combat.h"
#include "feature/loot/Loot.h"

class Container;

namespace gf::loot {

/// The parsed table `id` (data/<ns>/loot_table/<path>.json or a registered one), nullptr if none.
[[nodiscard]] Json const* table(std::string const& id);
/// Registers (or with null removes) a table by JSON (selftest fixtures).
void registerTable(std::string const& id, Json table);
/// Forgets the cached table files (a world change, /twilightforest reload).
void clearCache();

/// Rolls `table` (already parsed) without the global modifiers.
[[nodiscard]] std::vector<ItemStack> rollRaw(Json const& table, std::string const& tableId, LootContext const& ctx, RandomSource& random);
/// Rolls a table with `random` and applies the global modifiers (LootTable#getRandomItems).
[[nodiscard]] std::vector<ItemStack> rollWith(std::string const& tableId, LootContext const& ctx, RandomSource& random);
/// LootTable#fill: rolls `tableId` and spreads the stacks over the empty slots of a container of
/// `size` slots, as Java does (split stacks, shuffled slots). `get` / `set` access the slots.
void fill(
    std::string const&                                  tableId,
    LootContext const&                                  ctx,
    int                                                 size,
    std::function<ItemStack const&(int)> const&         get,
    std::function<void(int, ItemStack const&)> const&   set
);

/// The number of players taking part in the fight against `boss` (gf::boss::participants), or
/// the selftest override.
[[nodiscard]] int qualifiedPlayers(Actor const* boss);
/// Selftests: pretends every boss fight has `n` qualified players (-1: back to gf::boss).
void setTestParticipants(int n);
/// The `bosses` config's multiplayerFightAdjuster adds loot (MORE_LOOT / MORE_LOOT_AND_HEALTH);
/// selftests may force it.
[[nodiscard]] bool adjustsLootRolls();
void               setTestAdjustsLoot(std::optional<bool> value);

/// A mob died (the death pipeline): its Java loot table (TF entities) and the onEntityDrops
/// handlers; `killerId` is the killing player's ActorUniqueID (0: none).
void onMobDied(Actor& dead, combat::DamageInfo const& damage, std::int64_t killerId);

/// The Java table and seed placeContainer left in the vanilla container at `pos` (not unpacked yet).
[[nodiscard]] std::optional<std::pair<std::string, std::int64_t>> pendingVanilla(BlockSource& region, BlockPos const& pos);

/// Java enchantment id -> Bedrock Enchant::Type value (-1 unknown).
[[nodiscard]] int bedrockEnchant(std::string const& javaId);
/// The level of a Java enchantment on a stack (vanilla on Bedrock enchants, TF ones through gf::enchant).
[[nodiscard]] int enchantLevel(ItemStack const& stack, std::string const& javaId);

} // namespace gf::loot
