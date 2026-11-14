#pragma once

// Contract "loot" (header owned by F0, implemented by F10; rule R5).
//
// A loot table interpreter for upstream's 634 tables (imported JSON) with the vanilla functions,
// conditions and number providers they use plus upstream's multiplayer_addition, multiplayer_rolls,
// looting_rolls and mod_exists; units register their own upstream conditions / functions / global
// modifiers by id (is_minion U21, giant_pick_used U29, fiery_pick_smelting U7, ...).
//
//     auto items = gf::loot::roll("gloamingforest:chests/hill_1", ctx);
//     gf::loot::placeContainer(region, pos, "minecraft:chest", 2, "gloamingforest:chests/hill_1", seed);

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::loot {

using Json = nlohmann::json;
using core::jrand::RandomSource;

/// Java's LootParams / LootContext (every member optional).
struct LootContext {
    BlockSource*     region   = nullptr;
    Vec3             origin{};
    Actor*           thisEntity = nullptr;   ///< the dead mob / the opener
    Player*          player     = nullptr;   ///< last damage player / chest opener
    Actor*           attacker   = nullptr;
    Actor*           direct     = nullptr;
    std::string      damageType;
    ItemStack const* tool  = nullptr;
    Block const*     block = nullptr;
    float            luck  = 0.f;
    std::int64_t     seed  = 0;              ///< 0 = random
};

/// LootItemFunction: returns the changed stack.
using FunctionFn = std::function<ItemStack(ItemStack stack, LootContext const& ctx, Json const& config, RandomSource& random)>;
/// LootItemCondition.
using ConditionFn = std::function<bool(LootContext const& ctx, Json const& config, RandomSource& random)>;
/// Global loot modifier (NeoForge IGlobalLootModifier): edits the rolled items of any table.
using ModifierFn = std::function<void(std::vector<ItemStack>& loot, LootContext const& ctx, std::string const& tableId)>;
/// Entity drops after the loot table rolled (the death pipeline's drops for mobs).
using EntityDropsFn = std::function<void(Actor& dead, std::vector<ItemStack>& drops, LootContext const& ctx)>;

/// Rolls table `tableId` ("gloamingforest:entities/naga"); empty when unknown (one warning).
[[nodiscard]] std::vector<ItemStack> roll(std::string const& tableId, LootContext const& ctx);
void registerFunction(std::string const& id, FunctionFn fn);
void registerCondition(std::string const& id, ConditionFn fn);
void addGlobalModifier(std::string const& id, ModifierFn fn);
/// Places a container block (chest-like, `facing` as FacingID) with a loot table that rolls when
/// first opened (native loot field / gf::inventory setLootTable). False on failure.
bool placeContainer(BlockSource& region, BlockPos const& pos, std::string const& blockId, int facing, std::string const& tableId, std::int64_t seed);
void onEntityDrops(EntityDropsFn fn);

} // namespace gf::loot
