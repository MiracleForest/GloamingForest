#pragma once

// F10-internal parts of gf::loot (the interpreter, the death pipeline, selftests).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/combat/Combat.h"
#include "feature/ext/combat/LootFill.h" // gf::loot::fill (LootTable#fill)
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
// LootTable#fill (gf::loot::fill) is declared in feature/ext/combat/LootFill.h (units use it).

/// The number of players taking part in the fight against `boss` (gf::boss::participants), or
/// the selftest override.
[[nodiscard]] int qualifiedPlayers(Actor const* boss);
/// Selftests: pretends every boss fight has `n` qualified players (-1: back to gf::boss).
void setTestParticipants(int n);
/// The `bosses` config's multiplayerFightAdjuster adds loot (MORE_LOOT / MORE_LOOT_AND_HEALTH);
/// selftests may force it.
[[nodiscard]] bool adjustsLootRolls();
/// Forgets the cached adjuster (the configs were reloaded).
void invalidateAdjuster();
void               setTestAdjustsLoot(std::optional<bool> value);

/// Collects the item entities the game spawns on this thread while it lives (the death
/// pipeline wraps a mob's `die` in one): Java's Entity#captureDrops.
class DropCapture {
public:
    DropCapture();
    ~DropCapture();
    DropCapture(DropCapture const&)            = delete;
    DropCapture& operator=(DropCapture const&) = delete;
    /// ActorUniqueIDs of the item entities spawned so far.
    [[nodiscard]] std::vector<std::int64_t> const& ids() const { return mIds; }

private:
    std::vector<std::int64_t>  mIds;
    std::vector<std::int64_t>* mPrevious;
};
/// An item entity was spawned (the Spawner::spawnItem hook): recorded by the active DropCapture,
/// or for a mob that died this tick when `spawner` is that mob.
void captureSpawnedItem(Actor const& item, Actor const* spawner);

/// A mob died (the death pipeline, right after the game's `die`): its Java loot table (TF
/// entities) drops now; on the next tick, once the game has spawned the mob's own loot, the
/// onEntityDrops handlers see every item entity of the death (the Java table's, `gameDrops` from
/// `die`, and the later ones with the mob as the spawner); `killerId` is the killing player's
/// ActorUniqueID (0: none).
void onMobDied(Actor& dead, combat::DamageInfo const& damage, std::int64_t killerId, std::vector<std::int64_t> const& gameDrops = {});
/// Deaths whose drop merge waits for the next tick (selftests).
[[nodiscard]] std::size_t pendingDeathCount();
/// Player#getLuck (the luck attribute; 0 for anything else or nullptr).
[[nodiscard]] float luckOf(Actor* actor);

/// Rolls the Java table placeContainer left in the vanilla container at `pos` into it (the opener
/// in the loot context); true when there was one.
bool unpackVanilla(BlockSource& region, BlockPos const& pos, Actor* opener);
/// The Java table and seed placeContainer left in the vanilla container at `pos` (not unpacked yet).
[[nodiscard]] std::optional<std::pair<std::string, std::int64_t>> pendingVanilla(BlockSource& region, BlockPos const& pos);

/// The user data tag of a Bedrock storage item naming its dynamic container
/// (StorageItemComponentTags::bundleIDTag(), exported by the server binary only: the selftest
/// combat.loot_set_contents checks the value on BDS).
inline constexpr char kBundleIdTag[] = "bundle_id";

/// Java's Math.round(float) (NumberProvider#getInt): half up, saturated, NaN -> 0.
[[nodiscard]] int javaRound(float value);
/// The stacks a set_contents function put into a Bedrock storage item (a bundle), nullopt when
/// the stack has no bundle id (selftests).
[[nodiscard]] std::optional<std::vector<ItemStack>> bundleContents(ItemStack const& stack);

/// The size of a slime-like entity (Java's Slime#getSize): Bedrock keeps it in the variant (1, 2,
/// 4) of slimes and magma cubes, and TF's maze slime does the same; <= 0 when unknown.
[[nodiscard]] int slimeSize(Actor const& actor);
/// EnchantmentHelper#selectEnchantment over the enchantments of `options` (a tag "#...", an id, a
/// list; null: the registry): the Java ids and levels picked (selftests).
[[nodiscard]] std::vector<std::pair<std::string, int>> selectEnchantment(RandomSource& random, ItemStack const& stack, int level, Json const& options);
/// EntityPredicate#matches as the interpreter evaluates it (selftests).
[[nodiscard]] bool entityMatches(Actor* actor, Json const& predicate);

/// Java enchantment id -> Bedrock Enchant::Type value (-1 unknown).
[[nodiscard]] int bedrockEnchant(std::string const& javaId);
/// The level of a Java enchantment on a stack (vanilla on Bedrock enchants, TF ones through gf::enchant).
[[nodiscard]] int enchantLevel(ItemStack const& stack, std::string const& javaId);

} // namespace gf::loot
