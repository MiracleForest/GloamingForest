// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/gloamingforest/events/TravellersGearEvents.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U12 (tgmove): the twelve Travellers' Gear movement modifiers, server side. Internal to the unit
// (rule R2: no other unit includes it); the selftests and the client half use it.
//
// Java's movement is client authoritative; Bedrock's is server authoritative (the server simulates
// every player from its input and corrects a client that moved elsewhere; e2e: a jump the server
// did not make is put back 2 ticks later). So both simulations of a player apply each modifier:
// with the client mod the local player applies double jump, sidestep, glide, step up, water walk,
// unrestrained, swift swim, agile ranger and straight ahead in gf::client::movement
// (src/client/tgmove) and sends upstream's packets as the channels tgmove.double_jump / sidestep /
// gradual_glide; the server pushes the double jump and the sidestep (SetActorMotionPacket), applies
// the same glide, water walk, step height, slowdown immunity, Depth Strider, item use slowdown and
// speed to its copy of the player, runs upstream's anti-abuse validator on refused packets and keeps
// what only the server has: Jump Boost II (high jump), the fall damage of a glide, the double
// jump's safe fall +2, slimy soles' cancelled landing and bounce, the wing state
// (tgmove.wings_state), the aquatic agility air and mining speed, and the movement food
// exhaustion of efficient eater. Mobs wearing the gear glide, bounce, jump high, run straight
// ahead and ignore block slowdowns as in upstream (updateOtherModifiers runs for every LivingEntity).
// Vanilla clients get the fallbacks of docs/spec/tgmove.md section 4 (jump in the air -> double
// jump by motion packet, sneak in the air -> Slow Falling glide).

#include <cstdint>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/tgmove/TgMove.h"

class Actor;
class Mob;
class Player;
class Block;
class ItemStack;
class Vec2;
class Vec3;

namespace gf::feature::tgmove {

/// The client config section `tgmove` (GloamingClientConfig.travellersWingsGradualGlide, R13).
struct Config {
    int  version                     = 1;
    bool travellersWingsGradualGlide = true;
};

/// The movement modifiers and the armour slot (0 head .. 3 feet) of their equipment_slots group.
enum class Mod { DoubleJump, SideStep, GradualGlide, HighJump, AgileRanger, StepUp, StraightAhead, SlimySoles, Unrestrained, WaterWalk, SwiftSwim, AquaticAgility, EfficientEater };
[[nodiscard]] char const* modId(Mod m);
[[nodiscard]] int         modSlot(Mod m);

/// TravellersModifiersManager#isModifierActive: on the stack in its slot, not a spectator, the
/// piece not broken (Travellers' gear at max damage - 1).
[[nodiscard]] bool active(Player const& player, Mod m);
/// The same for any living entity (mobs: the modifiers of the stack in the modifier's slot).
[[nodiscard]] bool activeOn(Actor const& entity, Mod m);
/// Every active modifier of `entity` at once (bit 1 << Mod), one gear lookup per armour slot: the
/// per-tick logic reads this mask instead of asking for each modifier.
[[nodiscard]] unsigned modMask(Actor const& entity);
[[nodiscard]] constexpr bool has(unsigned mask, Mod m) { return (mask >> static_cast<unsigned>(m)) & 1u; }
/// Whether `stack` is broken Travellers' gear.
[[nodiscard]] bool brokenGear(ItemStack const& stack);

/// A hidden effect of the player's own that one of ours replaced (Java keeps it as the hidden
/// effect of the stronger one and brings it back when ours ends).
struct SavedEffect {
    int       amplifier = 0;
    long long endTick   = 0;
    bool      visible   = true;
    bool      ambient   = false;
};

/// The server's per-entity state (upstream's data attachments, by ActorUniqueID; never a pointer).
struct PlayerState {
    bool                    hasDoubleJump = false;
    core::tgmove::Validator doubleJumpValidator;
    core::tgmove::Validator sidestepValidator;
    bool                    safeFall = false; ///< boots_double_jump_safe_fall_distance (+2)
    bool                    gliding  = false; ///< is_gradually_gliding
    core::tgmove::Slimy     slimy;
    core::tgmove::Wings     wings;
    float                   walkSpeed      = 0;
    double                  fallVy         = 0; ///< the y velocity before the last landing
    bool                    lastOnGround   = true;
    bool                    landedByDamage = false; ///< the combat hook bounced this landing
    long long               lastSplash     = 0;     ///< last_tick_water_walking
    bool                    snapBackPending = false; ///< a validator strike waits for the next move
    // vanilla fallbacks
    bool airborne          = false; ///< from the input edges
    bool slowFalling       = false; ///< our Slow Falling glide is on
    int  lastJumpPressTick = -100;  ///< last_jump_key_press_time (tickCount), the creative fly guard
    float     pendingRefund   = 0; ///< efficient eater: half a ground jump's exhaustion, to take back
    long long pendingRefundAt = 0;
    // engine state this unit changed, with the value it replaced (restored when the modifier ends)
    bool                       highJump      = false;
    std::optional<SavedEffect> savedJumpBoost;
    bool                       straightAhead = false;
    bool                       immunity      = false; ///< our 5 slowdown immunity entries are in
    bool                       immunityAdded = false; ///< we created the component
    std::optional<float>       stepBase;
    float                      stepSet = 0;
    std::optional<int>         depthBase; ///< the Depth Strider level before ours (-1: no component)
    int                        depthSet  = 0;
    std::optional<float>       agileBase; ///< the item use slowdown before agile ranger
    float                      agileSet  = 0;
    int                        lastAir   = -1;
    double                     lastX = 0, lastY = 0, lastZ = 0;
    bool                       hasLast = false;
    float                      lastExhaustion = -1; ///< efficient eater: the exhaustion after the last tick
    float                      lastSaturation = -1; ///< efficient eater: the saturation after the last tick (a wrap of the exhaustion takes it)
    float                      lastFood       = -1; ///< efficient eater: the food after the last tick
    float                      refundDebt     = 0;  ///< efficient eater: refund the exhaustion could not take yet (it wrapped)
    // unrestrained on the ground (BlockHooks): what the engine's last step left to put right
    double                     unrRescale = 1.0;   ///< the friction / slowdown of the last step -> upstream's 0.6
    double                     unrVyPre   = 0;     ///< the y velocity the last step started with
    bool                       unrSlime   = false; ///< the last step was on a slime block
    bool                       unrHoney   = false; ///< the last step was on a honey block
    double                     unrJumpLost = 0;    ///< the height a honey jump made up in the last step
    double                     unrJumpAdded = 0;   ///< the y velocity the last step's honey jump make-up added (server: sent to the companion)
    bool                       unrHoneyByServer = false; ///< client mod with a server running the mod: the server makes the honey jump up (tgmove.honey_jump)
    double                     unrLastAccelSlow = 1.0; ///< mobs: the engine's acceleration factor of the last step
    double                     unrStepX = 0, unrStepZ = 0; ///< mobs: the last step's velocity (acceleration estimate)
    double                     unrLastDrag = 0;    ///< mobs: the last step's friction x 0.91 x slowdown (0: no ground step)
    double                     unrLastFriction = 0.6;
    core::tgmove::Velocity     unrAccel;           ///< mobs: the input acceleration estimated from the last step
    core::tgmove::Velocity     unrAccelRaw;        ///< mobs: the last step's raw estimate
    bool                       unrAccelRawValid = false;
    // companion packets the server copy refused when they came: retried on the next ticks before
    // they count as a validator strike (the packet can come before the move it follows)
    int                        pendingJumpTries     = 0;
    int                        pendingSidestepTries = 0;
    bool                       pendingSidestepLeft  = false;
    bool                       loggedAgile          = false; ///< the e2e line of the input while drawing
};
/// The state of `entity` (created on first use).
[[nodiscard]] PlayerState& stateOf(Actor const& entity);
/// Puts back what this unit changed on `entity` and drops its state (tests, disconnect, respawn).
void forget(Actor& entity);

/// TravellersGearLogic#performDoubleJump on the server. `vanilla`: a vanilla client's jump edge
/// (its ground state comes from the input edges). The server pushes the jump either way.
bool performDoubleJump(Player& player, bool vanilla);
/// TravellersGearLogic#tryPerformSidestep on the server.
bool tryPerformSidestep(Player& player, bool left);
/// A refused companion packet (handleDoubleJumpAbuse / handleSidestepAbuse); the verdict.
core::tgmove::Abuse handleAbuse(Player& player, bool sidestep);
/// Whether the anti-abuse validator runs (upstream: dedicated servers only; the client build's
/// integrated server skips it). Tests force it.
[[nodiscard]] bool validatorEnabled();
void               forceValidator(std::optional<bool> on);

/// One server tick of `player` (TravellersGearEvents#tickMovementModifiers, updateOtherModifiers).
void tickPlayer(Player& player);
/// One server tick of a mob wearing Travellers' gear (updateOtherModifiers for LivingEntity).
void tickMob(Mob& mob);
/// The fall damage hook (players and mobs): false cancels the damage (slimy soles), `amount` is
/// the remaining damage.
bool onFall(Actor& entity, float& amount);
/// A vanilla client's input edge (F12's movement contract): the double jump fallback, the creative
/// fly guard, slimy soles' cancelled bounce and efficient eater's jump exhaustion.
void onInputEdge(Player& player, int edge);

/// Sends the wing state of `entity` to the companions tracking it (TravellersWingsStatePacket).
void syncWings(Actor& entity);
/// The voice pitch of a player (LivingEntity#getVoicePitch).
[[nodiscard]] float voicePitch();

/// WaterWalkTransformer: the y of the water surface (the half-block stable shape of a water source
/// whose block above is not water) that `actor`'s box (feet `feet`, y velocity `vy`) lands on this
/// tick, if any. The client mod and the server's copy of a companion apply it alike.
[[nodiscard]] std::optional<double> waterSurface(Actor& actor, Vec3 const& feet, double vy);

/// Unrestrained's slowdown immunity (cobweb, sweet berry bush, powder snow, honey block, soul sand)
/// in or out of `a`'s simulation, leaving entries others added; client mod and server alike.
void applyImmunity(Actor& a, PlayerState& st, bool want);
/// Depth Strider III (swift swim; ground speed while water walking) on `a` while `on`, the level
/// it replaced back after; client mod and server alike.
void applyDepthStrider(Actor& a, PlayerState& st, bool on);

/// Agile ranger's items (Java's ProjectileWeaponItem and the whitelist tag, not the blacklist).
[[nodiscard]] bool agileRangerItem(ItemStack const& stack);
/// Applies (or puts back) agile ranger's x5 on the item use slowdown of `player`'s simulation; the
/// client mod and the server's copy call it alike. Returns whether it is on.
/// `forceOff` puts the slowdown back whatever the player does (the state is dropped).
bool applyAgileRanger(Player& player, std::optional<float>& base, float& set, bool forceOff = false);

/// Unrestrained on the ground (BlockHooks#resetBlockFrictionWithUnrestrained, #resetSlimeMomentum-
/// WithUnrestrained, #stopBouncing; EntityHooks#resetFactorWithUnrestrained): the velocity `v` the
/// engine kept, turned into the one to step with so that the step moves and slows as on a block of
/// friction 0.6 with no slime or block slowdown and no slime bounce of a walk. `move` is the input
/// (MoveInputComponent: x strafe, y forward); without it (mobs) the input acceleration is estimated
/// from the last step. Client mod and server copy alike, before the engine's step.
[[nodiscard]] Vec3 applyUnrestrained(Actor& a, PlayerState& st, bool active, Vec3 v, bool onGround, bool steppingCarefully, Vec2 const* move);
/// The velocity an input change makes in one engine step (Entity#moveRelative): upstream changes the
/// input (straight ahead's strafe / 1.4, the glide's unsneaked input) where Bedrock reads it after
/// any hook, so both simulations add the difference of the accelerations of `gloamingMove` and
/// `engineMove` (x strafe, y forward) instead: on the ground speed x the block's friction factor,
/// in the air the flying speed (0.02, 0.026 sprinting).
/// `unrestrained`: the ground step's friction is upstream's 0.6 (applyUnrestrained puts the engine's right),
/// so the input change accelerates as on a block of friction 0.6.
[[nodiscard]] Vec3 inputDelta(Actor& a, Vec2 const& engineMove, Vec2 const& gloamingMove, bool onGround, bool sprinting, bool unrestrained = false);
/// Bedrock's item use slowdown of the input (measured: a held key's input is 0.122 long while a bow
/// is drawn; Java's is 0.2, which agile ranger's x5 cancels).
inline constexpr double kBedrockItemUseSlowdown = 0.122;
/// handleAgileRanger's input: Bedrock's input without its item use slowdown `slowdown` (the
/// slowdown of the current use; at most length 1).
[[nodiscard]] Vec2 agileRangerInput(Vec2 const& move, double slowdown = kBedrockItemUseSlowdown);
/// The item use slowdown of the current use as the input shows it: the engine's input `move` over
/// the raw input `raw` (the keys or the stick, before any slowdown); kBedrockItemUseSlowdown when
/// the raw input is too short to tell or the ratio is out of range (sneaking, no input).
[[nodiscard]] double itemUseSlowdownOf(Vec2 const& move, Vec2 const& raw);
/// The friction the engine gives the block at `pos` (Block friction; 0.6 for most blocks).
[[nodiscard]] double blockFriction(Block const& block);
/// The engine's own slowdowns of a ground step on `blockId`, which unrestrained undoes (1 = none):
/// `drag` multiplies the step's velocity (Bedrock's slime and honey blocks: Java's SlimeBlock#stepOn
/// factor), `accel` the input acceleration (soul sand). Measured by tgmove.unrestrained_blocks.
struct EngineSlowdown {
    double drag  = 1.0;
    double accel = 1.0;
};
[[nodiscard]] EngineSlowdown engineSlowdown(std::string const& blockId, double vy, bool steppingCarefully);
/// Bedrock's soul sand: the walk's input acceleration x this (measured: 0.544 of the walking speed
/// on grass; a slide on it is not slowed).
inline constexpr double kSoulSandAccel = 0.544;
/// Bedrock's honey block jump: 0.6 of the jump power (measured: 0.514 blocks high, 1.252 on grass).
inline constexpr double kHoneyJumpFactor = 0.6;


/// A companion's tgmove.double_jump / tgmove.sidestep (PerformDoubleJumpPacket / PerformSidestep-
/// Packet#handle): performed now, or retried on the next ticks, then a validator strike.
void onDoubleJumpPacket(Player& player);
void onSidestepPacket(Player& player, bool left);
/// The retries of refused packets (each server tick, before tickPlayer).
void processPending(Player& player);
inline constexpr int kPacketRetryTicks = 3;

/// The entity data key (F8's gf::entity::data) where the server keeps last_damage_armor_time (the
/// game time armour last took damage) for U11's auto repair, which reads it by this string.
inline constexpr char kLastDamageArmorTimeKey[] = "gloamingforest:last_damage_armor_time";
/// The entity tag that marks a leashed mob as free of Java's stay-close pathing
/// (leashed_pathfinder_override; U21 adds it by this string to the lich tower's fenced mobs).
inline constexpr char kLeashOverrideTag[] = "gloamingforest:leashed_pathfinder_override";

/// Server game time (Level#getCurrentTick).
[[nodiscard]] long long gameTime();

} // namespace gf::feature::tgmove
