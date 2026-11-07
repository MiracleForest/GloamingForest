#pragma once

// U10 (provisions): the parts shared by the unit's server files and its selftests.

#include <string>

#include "nlohmann/json.hpp"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::feature::provisions {

namespace ids {
inline constexpr char kE115[]        = "twilightforest:experiment_115";
inline constexpr char kHydraChop[]   = "twilightforest:hydra_chop";
inline constexpr char kStroganoff[]  = "twilightforest:meef_stroganoff";
inline constexpr char kStaleBread[]  = "twilightforest:stale_bread";
inline constexpr char kFrosted[]     = "twilightforest:frosted";
inline constexpr char kFireReact[]   = "twilightforest:fire_react";
inline constexpr char kChillAura[]   = "twilightforest:chill_aura";
inline constexpr char kDestruction[] = "twilightforest:destruction";
inline constexpr char kRenewal[]     = "twilightforest:renewal";
inline constexpr char kSlicesEaten[] = "twilightforest:e115_slices_eaten";
inline constexpr char kHydraTrigger[] = "twilightforest:consume_hydra_chop_on_low_hunger";
inline constexpr char kStaleSandwich[] = "twilightforest:stale_sandwich";
inline constexpr char kSmashKey[]    = "twilightforest:smash_blocks";
} // namespace ids

/// The player's food level (Bedrock's hunger attribute, 0..20).
[[nodiscard]] float foodLevel(Player const& player);
void                setFoodLevel(Player& player, float value);

/// Experiment 115's block state at a position (nullopt-like: bites -1 when it is not E115).
struct E115State {
    int  bites      = -1;
    bool regenerate = false;
};
[[nodiscard]] E115State e115At(BlockSource& region, BlockPos const& pos);
[[nodiscard]] E115State e115Of(Block const& block);
/// Sets the block to E115 with the state (bites < 0 removes it).
bool setE115(BlockSource& region, BlockPos const& pos, E115State state);

/// Experiment115Item#useOn on a non-E115 block: places one slice (TF's default state) on the clicked
/// face when the target is replaceable and the block below is solid. True when placed.
bool placeE115(Player& player, BlockSource& region, BlockPos const& clicked, int face, bool creative);

/// Frosted: the entity's Java frozen ticks (0..140) as U10 keeps them.
[[nodiscard]] int  frozenTicks(Actor& actor);
/// Whether the entity can freeze (Entity#canFreeze / LivingEntity#canFreeze).
[[nodiscard]] bool canFreeze(Actor& actor);

/// Counters the selftests read.
struct Counters {
    int fireReacts    = 0;
    int chillAuras    = 0;
    int recharges     = 0;
    int smashed       = 0;
    int staleSandwich = 0;
};
[[nodiscard]] Counters& counters();

/// fire_react / chill_aura of every armour piece `victim` wears, after `attacker` damaged it.
void postAttackEffects(Actor& victim, Actor& attacker);

/// Frozen counters of entities no longer frosted thaw (every server tick).
void thawFrozen();

/// The client mod's channel of frosted entities (IceLayer): {"entities": [[unique id, amplifier], ...]},
/// the frosted living entities within kFrostedSyncRange blocks of the companion in its dimension, sent
/// when that list changes (checked every kFrostedSyncInterval ticks).
inline constexpr char kChanFrosted[]        = "provisions.frosted";
inline constexpr int  kFrostedSyncInterval  = 5;
inline constexpr float kFrostedSyncRange    = 64.0f;
/// Sends kChanFrosted to the companions whose list changed (every server tick; acts every interval).
void syncFrosted();

/// The renewal tick of one player (the main hand and the off hand).
void renewalTick(Player& player);

} // namespace gf::feature::provisions
