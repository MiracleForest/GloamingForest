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
// Java's movement is client authoritative and so is Bedrock's: with the client mod the local
// player applies double jump, sidestep, glide, step up, water walk, unrestrained, swift swim, agile
// ranger and straight ahead in gf::client::movement (src/client/tgmove) and sends TF's packets as
// the channels tgmove.double_jump / sidestep / gradual_glide; the server repeats the jump / step
// (sound, particles, safe fall, wing state), runs TF's anti-abuse validator on refused ones and
// keeps everything the server owns: Jump Boost II (high jump), the fall damage of a glide, the
// double jump's safe fall +2, slimy soles' cancelled landing and bounce (SetActorMotionPacket), the
// straight ahead speed modifier, the wing state (tgmove.wings_state), the aquatic agility air,
// and the movement food exhaustion of efficient eater. Vanilla clients get the fallbacks of
// docs/spec/tgmove.md section 4 (jump in the air -> double jump by motion packet, sneak in the air
// -> Slow Falling glide).

#include <cstdint>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/tgmove/TgMove.h"

class Actor;
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
/// Whether `stack` is broken Travellers' gear.
[[nodiscard]] bool brokenGear(ItemStack const& stack);

/// The server's per-player state (TF's data attachments, by ActorUniqueID; never a Player*).
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
    long long               lastDamageArmorTime = 0;
    bool                    snapBackPending = false; ///< a validator strike waits for the next move
    // vanilla fallbacks
    bool airborne    = false; ///< from the input edges
    bool slowFalling = false; ///< our Slow Falling glide is on
    // server components and effects this unit set
    bool                 highJump      = false;
    bool                 straightAhead = false;
    bool                 immunity      = false;
    std::optional<float> stepBase;
    float                stepSet  = 0;
    int                  lastAir  = -1;
    double               lastX = 0, lastY = 0, lastZ = 0;
    bool                 hasLast = false;
};
/// The state of `player` (created on first use).
[[nodiscard]] PlayerState& stateOf(Player const& player);
/// Drops the state of `player` (tests, disconnect).
void forget(Player const& player);

/// TravellersGearLogic#performDoubleJump on the server. `sendMotion`: a vanilla client (the
/// server pushes the jump with a motion packet); a companion already jumped on its client.
bool performDoubleJump(Player& player, bool sendMotion);
/// TravellersGearLogic#tryPerformSidestep on the server.
bool tryPerformSidestep(Player& player, bool left, bool sendMotion);
/// A refused companion packet (handleDoubleJumpAbuse / handleSidestepAbuse); the verdict.
core::tgmove::Abuse handleAbuse(Player& player, bool sidestep);
/// Whether the anti-abuse validator runs (TF: dedicated servers only; the client build's
/// integrated server skips it). Tests force it.
[[nodiscard]] bool validatorEnabled();
void               forceValidator(std::optional<bool> on);

/// One server tick of `player` (TravellersGearEvents#tickMovementModifiers, updateOtherModifiers).
void tickPlayer(Player& player);
/// The fall damage hook: false cancels the damage (slimy soles), `amount` is the remaining damage.
bool onFall(Player& player, float& amount);

/// Sends the wing state of `player` to the companions tracking it (TravellersWingsStatePacket).
void syncWings(Player& player);
/// The voice pitch of a player (LivingEntity#getVoicePitch).
[[nodiscard]] float voicePitch();

/// WaterWalkTransformer: the y of the water surface (the half-block stable shape of a water source
/// whose block above is not water) that `actor`'s box (feet `feet`, y velocity `vy`) lands on this
/// tick, if any. The client mod and the server's copy of a companion apply it alike.
[[nodiscard]] std::optional<double> waterSurface(Actor& actor, Vec3 const& feet, double vy);

/// Server game time (Level#getCurrentTick).
[[nodiscard]] long long gameTime();

} // namespace gf::feature::tgmove
