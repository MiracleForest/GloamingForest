// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
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
// (src/client/tgmove) and sends TF's packets as the channels tgmove.double_jump / sidestep /
// gradual_glide; the server pushes the double jump and the sidestep (SetActorMotionPacket), applies
// the same glide, water walk, step height, slowdown immunity, Depth Strider, item use slowdown and
// speed to its copy of the player, runs TF's anti-abuse validator on refused packets and keeps
// what only the server has: Jump Boost II (high jump), the fall damage of a glide, the double
// jump's safe fall +2, slimy soles' cancelled landing and bounce, the wing state
// (tgmove.wings_state), the aquatic agility air and mining speed, and the movement food
// exhaustion of efficient eater. Mobs wearing the gear glide, bounce, jump high, run straight
// ahead and ignore block slowdowns as in TF (updateOtherModifiers runs for every LivingEntity).
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
class ItemStack;
class Vec3;

namespace gf::feature::tgmove {

/// The client config section `tgmove` (TFClientConfig.travellersWingsGradualGlide, R13).
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

/// The server's per-entity state (TF's data attachments, by ActorUniqueID; never a pointer).
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
/// Whether the anti-abuse validator runs (TF: dedicated servers only; the client build's
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
bool applyAgileRanger(Player& player, std::optional<float>& base, float& set);

/// The entity data key (F8's gf::entity::data) where the server keeps last_damage_armor_time (the
/// game time armour last took damage) for U11's auto repair, which reads it by this string.
inline constexpr char kLastDamageArmorTimeKey[] = "twilightforest:last_damage_armor_time";
/// The entity tag that marks a leashed mob as free of Java's stay-close pathing
/// (leashed_pathfinder_override; U21 adds it by this string to the lich tower's fenced mobs).
inline constexpr char kLeashOverrideTag[] = "twilightforest:leashed_pathfinder_override";

/// Server game time (Level#getCurrentTick).
[[nodiscard]] long long gameTime();

} // namespace gf::feature::tgmove
