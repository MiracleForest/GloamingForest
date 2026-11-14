// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/gloamingforest/client/event/TravellersClientEvents.java
//   src/main/java/gloamingforest/events/TravellersGearEvents.java
//   src/main/java/gloamingforest/components/entity/TravellersWingsAttachment.java
//   src/main/java/gloamingforest/components/entity/TravellersWingsAnimAttachment.java
//   src/main/java/gloamingforest/components/entity/SlimySolesAttachment.java
//   src/main/java/gloamingforest/client/model/armor/TravellersWingsModel.java
//   src/main/java/gloamingforest/util/TFMathUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent rules of U12 (Travellers' Gear movement modifiers): the numbers and state
// machines of upstream's TravellersGearLogic / TravellersClientEvents / TravellersWingsModel, shared by
// the server module (src/feature/tgmove), the client mod (src/client/tgmove) and the gtests.

#include <optional>
#include <string_view>

namespace gf::core::tgmove {

// ---- constants (upstream 4.8) ----------------------------------------------------------------------

inline constexpr double kGravity                 = 0.08;  ///< LivingEntity#getGravity of players
inline constexpr double kJumpStrength            = 0.42;  ///< generic.jump_strength default
inline constexpr double kSprintJumpBoost         = 0.2;   ///< LivingEntity#jumpFromGround while sprinting
inline constexpr double kSidestepPush            = 1.6;   ///< TravellersGearLogic#performSidestep ("5 blocks")
inline constexpr int    kSidestepDoubleTapTicks  = 4;     ///< tickCount - lastWalkingTime < 4
inline constexpr int    kSidestepCooldown        = 40;    ///< side_step's sidestep_cooldown
inline constexpr double kGradualGlideMultiplier  = 1.0 - 1.0 / 6.0; ///< gradually_gliding_multiplier (0.8333333)
inline constexpr int    kHighJumpAmplifier       = 1;     ///< high_jump_amplifier of the wings (Jump Boost II)
inline constexpr double kStepUp                  = 0.5;   ///< travellers_gear.boots_high_step
inline constexpr double kStraightAheadMultiplier = 1.4;   ///< straight_ahead_multiplier
inline constexpr double kSlimySolesCoefficient   = 0.5;   ///< slimy_soles_coefficient
inline constexpr double kAgileRangerModifier     = 5.0;   ///< agile_ranger_modifier
inline constexpr double kWaterWalkMaxSubmerged   = 0.4;   ///< WATER_WALKING_MAX_SUBMERGED_HEIGHT
inline constexpr double kWaterWalkSurface        = 0.5;   ///< LiquidBlock.STABLE_SHAPE height (8/16)
inline constexpr double kDoubleJumpSafeFall      = 2.0;   ///< travellers_gear.boots_double_jump_safe_fall_distance
inline constexpr double kSafeFallDistance        = 3.0;   ///< generic.safe_fall_distance default
inline constexpr int    kOxygenBonus             = 3;     ///< travellers_gear.goggles_aquatic_agility_oxygen
inline constexpr double kSubmergedMiningBonus    = 4.0;   ///< add_multiplied_total: x5
inline constexpr int    kSwiftSwimDepthStrider   = 3;     ///< water_movement_efficiency +1 = Depth Strider III
inline constexpr int    kCreativeFlyTicks        = 6;     ///< avoidCreativeFly window
inline constexpr int    kValidatorResetTicks     = 45;    ///< validateMovement: a gap that resets the count
inline constexpr int    kValidatorSnapBack       = 1;     ///< count > 1 snaps the player back
inline constexpr int    kValidatorDisconnect     = 5;     ///< count >= 5 disconnects
inline constexpr int    kDoubleJumpDuration      = 15;    ///< TravellersWingsAttachment.DOUBLE_JUMP_DURATION
inline constexpr int    kSidestepDuration        = 11;    ///< TravellersWingsAttachment.SIDESTEP_DURATION
inline constexpr float  kFallSlowDistance        = 2.3f;  ///< determineWingState's fall distance split
inline constexpr int    kWaterSplashPeriod       = 3;     ///< processWaterWalking: gameTime % 3 == 1

// ---- jumps -----------------------------------------------------------------------------------

/// LivingEntity#getJumpPower: jump strength x block jump factor plus Jump Boost (amplifier < 0: none).
[[nodiscard]] double jumpPower(int jumpBoostAmplifier, double blockJumpFactor = 1.0);

struct Velocity {
    double x = 0, y = 0, z = 0;
    friend bool operator==(Velocity const&, Velocity const&) = default;
};

/// LivingEntity#jumpFromGround: y = max(power, y), plus 0.2 towards `yawDeg` when sprinting.
[[nodiscard]] Velocity jumpFromGround(Velocity v, double power, bool sprinting, double yawDeg);

/// performDoubleJump's slimy-soles boost: y = sqrt(y^2 + boost^2) when the boost is not 0.
[[nodiscard]] double boostedJump(double vy, double boost);

/// Java's fall damage before armour (ceil(distance - safe) x multiplier); <= 0: none.
[[nodiscard]] double fallDamage(double fallDistance, double safeFallDistance, double multiplier = 1.0);

/// Why performDoubleJump refuses (TravellersGearLogic#performDoubleJump), all false = allowed.
struct DoubleJumpState {
    bool hasDoubleJump = false;
    bool fallFlying    = false; ///< elytra
    bool onClimbable   = false;
    bool onGround      = false;
    bool swimming      = false;
    bool flying        = false; ///< creative / ability flight
    bool inLiquid      = false;
    bool passenger     = false;
};
[[nodiscard]] bool canDoubleJump(DoubleJumpState const& s);

/// tickMovementModifiers: the new has_double_jump (nullopt: unchanged).
[[nodiscard]] std::optional<bool> refreshDoubleJump(bool modifierActive, bool onGround, bool inLiquid, bool onClimbable);

/// TravellersClientEvents#handleDoubleJump: a may-fly player that pressed jump within the last 6
/// ticks is starting creative flight, not a double jump.
[[nodiscard]] bool avoidCreativeFly(bool mayFly, int tickCount, int lastJumpKeyPressTime);

/// The double jump sound's pitch: (voicePitch - 1) x 1.1 + 0.98.
[[nodiscard]] float doubleJumpPitch(float voicePitch);

// ---- sidestep --------------------------------------------------------------------------------

/// The client's double-tap detector (TravellersClientEvents#handleSidestep; the attachments
/// last_horizontal_impulse, last_non_horizontal_impulse (the last non-zero one) and
/// last_horizontal_walking_time).
struct SidestepTap {
    float lastImpulse        = 0;
    float lastNonZeroImpulse = 0;
    int   lastWalkingTime    = 0;
};
/// One movement-input update: the sidestep it triggers (true = left) or nullopt. `leftImpulse` is
/// Java's Input.leftImpulse (positive = left). Off the ground nothing is recorded (upstream returns first).
[[nodiscard]] std::optional<bool> detectSidestep(SidestepTap& tap, float leftImpulse, int tickCount, bool onGround);

/// tryPerformSidestep's cooldown: more than `cooldown` ticks since the last sidestep.
[[nodiscard]] bool sidestepReady(long long gameTime, long long lastSidestepTime, long long cooldown);
/// performSidestep's push (x, z), `yawDeg` the player's yaw.
[[nodiscard]] Velocity sidestepPush(double yawDeg, bool left);

// ---- gradual glide ---------------------------------------------------------------------------

/// updateGradualGlideState: gliding when the config default equals the sneak key, falling and
/// in the air.
[[nodiscard]] bool wantsGlide(bool manualGlideConfig, bool sneakHeld, double vy, bool onGround);
/// travellersWingsGradualGlide: the new y velocity (vy x multiplier) when it applies.
[[nodiscard]] std::optional<double> glide(double vy, double multiplier, bool active, bool gliding, bool fallFlying);
/// The fall distance a glide leaves: v^2 / 2 / g.
[[nodiscard]] float glideFallDistance(double vy, double gravity = kGravity);
/// speedUpControlledWhileSneaking: impulses / 0.2, then (Java's getInputVector) at most length 1.
/// Java applies the 0.3 sneak factor before upstream's / 0.2. `engineScalesLater`: the engine applies
/// its sneak factor after these impulses (the impulses are the raw input): then the result is
/// 1 / sneakFactor of the unit input, so the engine's factor brings it back to upstream's full input.
void unsneakImpulse(float& forward, float& left, bool engineScalesLater = false, float sneakFactor = 0.3f);

// ---- unrestrained (BlockHooks / EntityHooks) -------------------------------------------------------

inline constexpr double kUnrestrainedFriction = 0.6;   ///< resetBlockFrictionWithUnrestrained
inline constexpr double kSlimeBounceStop      = -0.08; ///< stopBouncing: vy > -0.08 -> max(0, vy)
inline constexpr double kGroundAccel          = 0.21600002; ///< LivingEntity#getFrictionInfluencedSpeed

/// Java's ground acceleration factor of a block with friction `f`: 0.21600002 / f^3.
[[nodiscard]] double groundAccelFactor(double friction);
/// Entity#moveRelative's input acceleration (x, z) before the friction factor: the input
/// (strafe = Java's xxa, positive to the left; forward = zza) at most length 1, x `speed`, turned
/// by `yawDeg`.
[[nodiscard]] Velocity inputAcceleration(double strafe, double forward, double yawDeg, double speed);
/// SlimeBlock#stepOn's horizontal factor (0.4 + |vy| x 0.2 while |vy| < 0.1 and not stepping
/// carefully, else 1), which resetSlimeMomentumWithUnrestrained turns off.
[[nodiscard]] double slimeStepFactor(double vy, bool steppingCarefully);
/// One ground step of an unrestrained entity, before the engine's step. The engine multiplies the
/// horizontal velocity of the step by `friction` x 0.91 x `engineSlowdown` (slime's step-on, a speed
/// factor), where upstream's unrestrained has 0.6 x 0.91 and no slowdown, and adds the input acceleration
/// with `friction`'s factor, where upstream has 0.6's. `stored` is the velocity the engine kept, `rescale`
/// the factor the previous step left to restore (1 = none), `accel` the input acceleration
/// (inputAcceleration) and `engineAccelSlowdown` the engine's factor on it (soul sand). Returns the
/// velocity to step with (y unchanged); `nextRescale` is what the
/// next call restores. Off the ground (or `active` false) only the previous step is restored.
struct UnrestrainedStep {
    Velocity velocity;
    double   nextRescale = 1.0;
};
[[nodiscard]] UnrestrainedStep unrestrainedStep(Velocity stored, double rescale, Velocity accel, double friction, double engineSlowdown, bool onGround,
                                                bool active, double engineAccelSlowdown = 1.0);
/// The jump off a block whose jump factor the engine applies (the honey block), which
/// resetFactorWithUnrestrained turns off: from the y velocity the engine kept after the jump step
/// (Java: (power - g) x 0.98), the one to step with now so that the height lost in the jump step is
/// made up (`lost`, to take off the next step's velocity again as lost x 0.98); nullopt when the
/// engine's jump was not weaker than `power`.
struct JumpMakeUp {
    double vy   = 0;
    double lost = 0;
};
[[nodiscard]] std::optional<JumpMakeUp> unrestrainedJump(double vyAfterJumpStep, double power, double gravity = kGravity);
/// The honey jump made up late (tgmove.honey_jump on the client mod): `lost` is the power the
/// engine's jump lacked (JumpMakeUp::lost); the client hears of it `stepsLate` steps after the step
/// the server made it up in. `add` goes on the y velocity now, `deficit` (the height behind the full
/// jump, made up in this step) comes off the next step's velocity x 0.98. With 0 steps late it is
/// what unrestrainedJump does (add = lost x 0.98 + lost, deficit = lost).
struct HoneyCatchUp {
    double add     = 0;
    double deficit = 0;
};
[[nodiscard]] HoneyCatchUp honeyCatchUp(double lost, int stepsLate);
/// stopBouncing after the engine's slime landing: the y velocity upstream keeps (0 instead of a bounce of
/// a landing slower than 0.08), or nullopt when the bounce stays.
[[nodiscard]] std::optional<double> slimeBounceStop(double vyBeforeLanding, double vyAfter);

// ---- slimy soles -----------------------------------------------------------------------------

/// SlimySolesAttachment.
struct Slimy {
    double bounceVelocity          = 0;
    double doubleJumpBoostVelocity = 0;
    bool   forceBounce             = false;
    bool   hasBounced              = false;
};
/// reduceSlimySolesFallDamage: whether the landing is cancelled into a bounce.
[[nodiscard]] bool slimyCancelsFall(bool sneaking, bool active, double fallDamage, bool forceBounce);
/// The cancelled landing: bounce = -vy x sqrt(coefficient), also the next double jump's boost.
void slimyLand(Slimy& s, double vyAtLanding, double coefficient);
/// travellersBootsSlimySolesBounce (each tick): the new y velocity when the bounce happens now.
[[nodiscard]] std::optional<double> slimyBounce(Slimy& s, double vy);
/// cancelSlimySolesJump: a normal jump ends the bouncing.
void slimyJump(Slimy& s);
/// travellersBootsSlimySolesParticles: the slime block particle count (0: none).
[[nodiscard]] int slimyParticleCount(double bounceVelocity);

// ---- water walk ------------------------------------------------------------------------------

/// processWaterWalking: water is standable while the submerged height is under 0.4 and the
/// wearer does not sneak.
[[nodiscard]] bool waterStandable(double submergedHeight, bool sneaking);
/// Java's fluid height of a water block with Bedrock's liquid_depth (0 = source: 8/9).
[[nodiscard]] double waterHeight(int liquidDepth);
/// Whether a splash shows this tick (gameTime % 3 == 1, not twice in a row, moving).
[[nodiscard]] bool waterSplash(long long gameTime, long long lastSplashTime, double horizontalSpeed);

// ---- validation --------------------------------------------------------------------------------

/// The double_jump_validator / sidestep_validator attachments with their *_last_check.
struct Validator {
    int count     = 0;
    int lastCheck = 0;
};
enum class Abuse { Accept, SnapBack, Disconnect };
/// TravellersGearLogic#validateMovement for one refused packet (dedicated servers only).
[[nodiscard]] Abuse validate(Validator& v, int tickCount, bool fallFlying);

// ---- wings state -------------------------------------------------------------------------------

enum class WingState { Idle, Walk, Sprint, FallSlow, FallFast, Swim, Ride, DoubleJump, Sidestep };
[[nodiscard]] std::string_view wingStateName(WingState s);
[[nodiscard]] std::optional<WingState> wingStateFromName(std::string_view name);

/// TravellersWingsAttachment.
struct Wings {
    WingState state                   = WingState::Idle;
    bool      sidestepLeft            = false;
    int       doubleJumpTimer         = 0;
    int       sidestepTimer           = 0;
    long long lastSidestepTime        = -1000000; ///< long-ago: ready at once
    bool      shouldPlayCooldownSound = false;
};
struct WingInputs {
    bool   passenger    = false;
    bool   swimming     = false;
    bool   onGround     = true;
    bool   inLiquid     = false;
    bool   flying       = false;
    float  fallDistance = 0;
    double vy           = 0;
    bool   sprinting    = false;
    float  walkSpeed    = 0; ///< walkAnimation.speed()
};
/// determineWingState (server, every tick); true when the state changed (the state packet goes out).
bool determineWingState(Wings& w, WingInputs const& in);
/// Starts a double jump / sidestep animation (performDoubleJump / performSidestep).
void startDoubleJump(Wings& w);
void startSidestep(Wings& w, bool left);

/// WalkAnimationState#update(min(distance x 4, 1), 0.4) of LivingEntity#calculateEntityAnimation.
[[nodiscard]] float walkAnimation(float speed, double horizontalDistance);

// ---- wings animation (client model) -----------------------------------------------------------

/// TFMathUtil#interpolateToTarget.
[[nodiscard]] double interpolateToTarget(double old, double target, double dtTicks, double tau);

/// TravellersWingsAnimAttachment (`phase` starts at a random value in [0, 2 pi): seedPhase).
struct WingsAnim {
    double phase      = 0;
    double oldAge     = 0;
    float  xRotOld    = 0;
    float  yRotOldL   = 0;
    float  yRotOldR   = 0;
    float  zRotOld    = 0;
};
/// TravellersWingsAnimAttachment's initial accumulatedPhase: random01 in [0, 1) -> [0, 2 pi).
void seedPhase(WingsAnim& a, double random01);

/// The wing bases' rotations (radians) and the crouch drop (model pixels) for one frame.
struct WingPose {
    float rightX = 0, rightY = 0, rightZ = 0;
    float leftX = 0, leftY = 0, leftZ = 0;
    float drop = 0;
};
/// TravellersWingsModel#setupModelAnimations for `w` at `ageInTicks`.
[[nodiscard]] WingPose animateWings(WingsAnim& a, Wings const& w, double ageInTicks, float walkSpeed, bool crouching);

// ---- input modifiers (client) -----------------------------------------------------------------

/// handleStraightAhead: the speed multiplier while moving forward, the strafe impulse / it.
[[nodiscard]] double straightAheadMultiplier(bool active, double multiplier, float forwardImpulse);
/// handleStraightAhead's input from Bedrock's: the engine hands a key input already normalized
/// (0.707, 0.707 for W + A); Java divides the raw impulse (1, 1) by the multiplier and normalizes
/// after (getInputVector). Returns upstream's input (x strafe, y forward) at most length 1.
struct Input2 {
    float strafe = 0, forward = 0;
};
[[nodiscard]] Input2 straightAheadInput(float strafe, float forward, double multiplier);
/// The FOV factor (AbstractClientPlayer#getFieldOfViewModifier's walking part) that upstream keeps
/// unchanged by straight ahead: Java's (speed / walkSpeed + 1) / 2.
[[nodiscard]] float speedFovFactor(double speed, double walkSpeed);
/// PlayerHooks#straightAheadNullify / Restore: the factor that turns the FOV of `speed` (which
/// includes straight ahead's x`multiplier`) into the FOV without it; 1 when `multiplier` is 1.
[[nodiscard]] float straightAheadFovCompensation(double speed, double walkSpeed, double multiplier);

/// Efficient eater: the exhaustion to take back this tick, half of the movement exhaustion that the
/// game added. `expectedMovement` is Java's movement exhaustion of the move, `added` what the
/// exhaustion value grew by this tick (all sources; a wrap past 4 adds 4 back). Never more than
/// half of what was added.
[[nodiscard]] float efficientEaterRefund(float expectedMovement, float added);

/// The air a breath costs with an oxygen bonus (LivingEntity#decreaseAirSupply): true = keep the
/// air (random in [0, bonus] > 0).
[[nodiscard]] bool keepsAir(int oxygenBonus, int randomInclusive);

} // namespace gf::core::tgmove
