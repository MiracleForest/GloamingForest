// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U8 (weapons): the unit's internal declarations shared by its files (rule R2: no other unit
// includes this). Every item / projectile registers in its own file's register function, called
// from Module.cpp's install().

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/weapons/Weapons.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;

namespace gf::feature::weapons {

using Json = nlohmann::json;
namespace cw = gf::core::weapons;

// ---- ids ----------------------------------------------------------------------------------------------
inline constexpr char kBlockAndChain[]  = "gloamingforest:block_and_chain";
inline constexpr char kChainBlock[]     = "gloamingforest:chain_block";
inline constexpr char kCrumbleHorn[]    = "gloamingforest:crumble_horn";
inline constexpr char kCube[]           = "gloamingforest:cube_of_annihilation";
inline constexpr char kCubeTalisman[]   = "gloamingforest:cube_talisman";
inline constexpr char kEnderBow[]       = "gloamingforest:ender_bow";
inline constexpr char kGlassSword[]     = "gloamingforest:glass_sword";
inline constexpr char kIceBomb[]        = "gloamingforest:ice_bomb";
inline constexpr char kThrownIce[]      = "gloamingforest:thrown_ice";
inline constexpr char kIceBow[]         = "gloamingforest:ice_bow";
inline constexpr char kIceArrow[]       = "gloamingforest:ice_arrow";
inline constexpr char kIceSword[]       = "gloamingforest:ice_sword";
inline constexpr char kShield[]         = "gloamingforest:knightmetal_shield";
inline constexpr char kLamp[]           = "gloamingforest:lamp_of_cinders";
inline constexpr char kMoonwormQueen[]  = "gloamingforest:moonworm_queen";
inline constexpr char kMoonwormShot[]   = "gloamingforest:moonworm_shot";
inline constexpr char kMoonworm[]       = "gloamingforest:moonworm";
inline constexpr char kOreMagnet[]      = "gloamingforest:ore_magnet";
inline constexpr char kFan[]            = "gloamingforest:peacock_feather_fan";
inline constexpr char kSeekerBow[]      = "gloamingforest:seeker_bow";
inline constexpr char kSeekerArrow[]    = "gloamingforest:seeker_arrow";
inline constexpr char kPowder[]         = "gloamingforest:transformation_powder";
inline constexpr char kTripleBow[]      = "gloamingforest:triple_bow";
inline constexpr char kTorchberries[]   = "gloamingforest:torchberries";

inline constexpr char kThrownKey[]        = "gloamingforest:thrown_projectile";   ///< item data: the thrown entity's uid
inline constexpr char kInfiniteGlassKey[] = "gloamingforest:infinite_glass_sword"; ///< item data: unbreakable glass sword
inline constexpr char kFanFallingKey[]    = "gf:weapons:feather_fan_falling";      ///< player data (world DB key prefix)

// ---- config (section "weapons", docs/spec/config.md) -----------------------------------------------
struct WeaponsConfig {
    int version = 1;
    /// common."Shield Parrying".parryNonGloamingAttacks
    bool parryNonGloamingAttacks = false;
    /// common."Shield Parrying".shieldParryTicksArrow
    int shieldParryTicksArrow = 40;
};
[[nodiscard]] WeaponsConfig const& config();

// ---- shared helpers (Common.cpp) ----------------------------------------------------------------------
/// Java's Entity#getLookAngle.
[[nodiscard]] Vec3 lookOf(Actor const& actor);
/// Java's eye position (Bedrock players stand at their eyes; mobs at their feet).
[[nodiscard]] Vec3 eyeOf(Actor const& actor);
[[nodiscard]] cw::V3 v3(Vec3 const& v);
[[nodiscard]] Vec3   vec(cw::V3 const& v);
/// The hand that holds `itemId` (true: main hand), nullopt when neither does.
[[nodiscard]] std::optional<bool> handHolding(Player& player, std::string const& itemId);
/// The stack in a hand, and writing it back (the player's inventory is resent).
[[nodiscard]] ItemStack handStack(Player& player, bool mainHand);
void                    setHandStack(Player& player, bool mainHand, ItemStack const& stack);
/// ItemStack#hurtAndBreak for a player (creative: nothing): true when the stack broke.
bool hurtAndBreak(ItemStack& stack, int amount, Player* player);
/// TFItemStackUtils#hurtButDontBreak: damage, but never past max - 1.
void hurtButDontBreak(ItemStack& stack, int amount, Player* player);
/// The item being used by the player (empty when none) and Java's getTicksUsingItem.
[[nodiscard]] std::string usedItemId(Player const& player);
[[nodiscard]] int         ticksUsingItem(Player const& player);
/// LevelEvent.PARTICLES_DESTROY_BLOCK of `block` at `pos`.
void destroyParticles(BlockSource& region, BlockPos const& pos, Block const& block);
/// A block's explosion resistance on Java's scale.
[[nodiscard]] float javaExplosionResistance(Block const& block);
/// Whether `actor` is a "LivingEntity" (players, mobs; not parts, items, projectiles).
[[nodiscard]] bool isLiving(Actor& actor);
/// EntityTypeTags.FREEZE_IMMUNE_ENTITY_TYPES / FREEZE_HURTS_EXTRA_TYPES of 1.21.1.
[[nodiscard]] bool freezeImmune(Actor const& actor);
[[nodiscard]] bool freezeHurtsExtra(Actor const& actor);
/// ApplyFrostedEffect#doChillAuraEffect (upstream's frosted, U10's effect, by id).
void chillAura(Actor& victim, int duration, int amplifier, bool shouldHit);
/// Whether the item in a player's hand is a shield raised to block (vanilla shield or the
/// knightmetal shield); `ticks` gets the ticks it has been used.
[[nodiscard]] bool isBlocking(Player& player, int* ticks = nullptr);
/// NeoForge's BlockEvent.BreakEvent as upstream's landmark protection answers it (F6), plus the adventure
/// mode rule (isBlockPlacingRestricted: no breaking without can_destroy).
[[nodiscard]] bool mayBreak(Player& player, BlockPos const& pos);
/// A dispensed projectile's launch velocity (world::shootFrom) handed to the projectile kit; call
/// first in the type's tick.
void applyLaunch(Actor& projectile);
/// The ticks the player has been sneaking (-1: not sneaking); call every tick (Melee.cpp does).
int trackSneak(Player& player);
/// Forgets the per-player state of the world that closed (sneak starts, fan flags).
void forgetPlayers();
void forgetFanFlags();
/// Player#disableShield: the raised shield goes down and is disabled for 100 ticks.
void disableShield(Player& player);

// ---- the unit's parts --------------------------------------------------------------------------------
void registerChain();
void registerCube();
void registerBows();
void registerSwords();
void registerShield();
void registerHorn();
void registerLamp();
void registerMoonworm();
void registerMagnet();
void registerFan();
void registerPowder();
void registerIceBomb();

// ---- selftest hooks ---------------------------------------------------------------------------------
/// CrumbleHornItem#doCrumble for `living` with `random` (seeded): the blocks crumbled.
int crumbleAround(Actor& living, ItemStack& stack, std::int64_t seed);
/// OreMagnetItem#doMagnet from `use` to `dest`: blocks moved.
int magnetLine(BlockSource& region, BlockPos const& use, BlockPos const& dest);
/// OreMagnetItem#releaseUsing for `player` (all nine tries): blocks moved.
int magnetUse(Player& player);
/// TransformPowderItem#transformEntityIfPossible: the new actor's uid (0: nothing happened).
std::int64_t transform(Actor& target, ItemStack* powder, bool shrink);
/// LampOfCindersItem#doBurnEffect.
void lampBurn(Player& player);
/// IceBomb#doTerrainEffects around `pos`.
void iceTerrain(BlockSource& region, Vec3 const& pos, int range);
/// The moonworm queen repair recipe on a grid of stacks.
[[nodiscard]] bool      moonwormRepairMatches(std::vector<ItemStack const*> const& slots);
[[nodiscard]] ItemStack moonwormRepairAssemble(std::vector<ItemStack const*> const& slots);
/// PeacockFanItem#use's server part: blocks and entities fanned.
int fanUse(Player& player);
/// Whether `player` is flagged feather_fan_falling.
[[nodiscard]] bool fanFalling(Player const& player);
/// ChainBlockItem#use / CubeOfAnnihilationItem#use on `stack` (the hand stack): the thrown entity's
/// uid (0: refused).
std::int64_t throwChain(Player& player, ItemStack& stack);
std::int64_t throwCube(Player& player, ItemStack& stack);
/// Sets the feather_fan_falling flag (selftests).
void setFanFalling(Player& player, bool on);
/// Fires a bow's release for `player` with `used` ticks (the stack in hand).
void bowRelease(Player& player, ItemStack& bow, int used);

} // namespace gf::feature::weapons
