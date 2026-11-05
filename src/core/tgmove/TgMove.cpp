// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/client/model/armor/TravellersWingsModel.java
//   src/main/java/twilightforest/util/TFMathUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/tgmove/TgMove.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace gf::core::tgmove {

namespace {
constexpr double kPi      = std::numbers::pi;
constexpr float  kAngle10 = static_cast<float>(kPi / 18.0); ///< ANGLE_10_DEG
constexpr float  kBigSwing   = 15.0f;
constexpr float  kSmallSwing = 8.0f;
constexpr double kTau        = 4.0;

double toRad(double deg) { return deg * kPi / 180.0; }

int signum(float f) { return f > 0 ? 1 : (f < 0 ? -1 : 0); }

struct Rot {
    float x, y, z;
};

/// TravellersWingsModel#calculateRotations: advances the phase and returns sin / swing + offsets.
Rot calculateRotations(WingsAnim& a, double dt, float divisor, float xOff, float yOff, float zOff, float swing) {
    a.phase += dt / divisor;
    auto s = static_cast<float>(std::sin(a.phase));
    return {s / swing + xOff, s / swing + yOff, s / swing + zOff};
}
} // namespace

// ---- jumps -------------------------------------------------------------------------------------

double jumpPower(int jumpBoostAmplifier, double blockJumpFactor) {
    double p = kJumpStrength * blockJumpFactor;
    if (jumpBoostAmplifier >= 0) p += 0.1 * (jumpBoostAmplifier + 1);
    return p;
}

Velocity jumpFromGround(Velocity v, double power, bool sprinting, double yawDeg) {
    if (power <= 1.0e-5) return v;
    v.y = std::max(power, v.y);
    if (sprinting) {
        auto f = static_cast<float>(toRad(yawDeg));
        v.x += -std::sin(f) * kSprintJumpBoost;
        v.z += std::cos(f) * kSprintJumpBoost;
    }
    return v;
}

double boostedJump(double vy, double boost) { return boost != 0 ? std::sqrt(vy * vy + boost * boost) : vy; }

double fallDamage(double fallDistance, double safeFallDistance, double multiplier) {
    return std::ceil((fallDistance - safeFallDistance) * multiplier);
}

bool canDoubleJump(DoubleJumpState const& s) {
    return s.hasDoubleJump && !s.fallFlying && !s.onClimbable && !s.onGround && !s.swimming && !s.flying && !s.inLiquid && !s.passenger;
}

std::optional<bool> refreshDoubleJump(bool modifierActive, bool onGround, bool inLiquid, bool onClimbable) {
    if (!modifierActive) return false;
    if (onGround || inLiquid || onClimbable) return true;
    return std::nullopt;
}

bool avoidCreativeFly(bool mayFly, int tickCount, int lastJumpKeyPressTime) {
    return mayFly && tickCount - lastJumpKeyPressTime <= kCreativeFlyTicks;
}

float doubleJumpPitch(float voicePitch) {
    constexpr float shift = 0.1f;
    return (voicePitch - 1) * (1 + shift) + (1 - shift * 0.2f);
}

// ---- sidestep ----------------------------------------------------------------------------------

std::optional<bool> detectSidestep(SidestepTap& tap, float leftImpulse, int tickCount, bool onGround) {
    if (!onGround) return std::nullopt;
    bool lastImpulseZero      = tap.lastImpulse == 0;
    bool sameImpulseDirection = signum(tap.lastNonZeroImpulse) == signum(leftImpulse);
    bool hasDoubleTapped      = tickCount - tap.lastWalkingTime < kSidestepDoubleTapTicks;
    std::optional<bool> out;
    if (lastImpulseZero && sameImpulseDirection && hasDoubleTapped && leftImpulse != 0) out = leftImpulse > 0;
    tap.lastImpulse = leftImpulse;
    if (leftImpulse != 0) {
        tap.lastWalkingTime    = tickCount;
        tap.lastNonZeroImpulse = leftImpulse;
    }
    return out;
}

bool sidestepReady(long long gameTime, long long lastSidestepTime, long long cooldown) { return gameTime - lastSidestepTime > cooldown; }

Velocity sidestepPush(double yawDeg, bool left) {
    double rot = left ? -kPi / 2 : kPi / 2;
    return {-std::sin(toRad(yawDeg) + rot) * kSidestepPush, 0, std::cos(toRad(yawDeg) + rot) * kSidestepPush};
}

// ---- gradual glide ---------------------------------------------------------------------------

bool wantsGlide(bool manualGlideConfig, bool sneakHeld, double vy, bool onGround) {
    return manualGlideConfig == sneakHeld && vy < 0 && !onGround;
}

std::optional<double> glide(double vy, double multiplier, bool active, bool gliding, bool fallFlying) {
    if (!active || vy >= 0 || fallFlying || !gliding) return std::nullopt;
    return vy * multiplier;
}

float glideFallDistance(double vy, double gravity) { return static_cast<float>(vy * vy / 2 / gravity); }

void unsneakImpulse(float& forward, float& left, bool engineScalesLater, float sneakFactor) {
    if (!engineScalesLater) { // Java: the impulses already carry the 0.3
        forward /= 0.2f;
        left /= 0.2f;
    }
    float len2 = forward * forward + left * left;
    if (len2 > 1.0f) {
        float len = std::sqrt(len2);
        forward /= len;
        left /= len;
    }
    if (engineScalesLater && sneakFactor > 0) { // the engine's factor comes after: undo it in advance
        forward /= sneakFactor;
        left /= sneakFactor;
    }
}

// ---- unrestrained ------------------------------------------------------------------------------

double groundAccelFactor(double friction) {
    if (friction <= 0) return 0;
    return kGroundAccel / (friction * friction * friction);
}

Velocity inputAcceleration(double strafe, double forward, double yawDeg, double speed) {
    double len2 = strafe * strafe + forward * forward;
    if (len2 < 1.0e-7) return {};
    if (len2 > 1.0) {
        double len = std::sqrt(len2);
        strafe /= len;
        forward /= len;
    }
    strafe *= speed;
    forward *= speed;
    double s = std::sin(toRad(yawDeg)), c = std::cos(toRad(yawDeg));
    return {strafe * c - forward * s, 0, forward * c + strafe * s};
}

double slimeStepFactor(double vy, bool steppingCarefully) {
    double a = std::abs(vy);
    return a < 0.1 && !steppingCarefully ? 0.4 + a * 0.2 : 1.0;
}

UnrestrainedStep unrestrainedStep(Velocity stored, double rescale, Velocity accel, double friction, double engineSlowdown, bool onGround, bool active,
                                  double engineAccelSlowdown) {
    UnrestrainedStep r;
    r.velocity = stored;
    // the previous step's drag: the engine's friction (and slowdown) -> TF's 0.6
    r.velocity.x *= rescale;
    r.velocity.z *= rescale;
    if (!active || !onGround || friction <= 0 || engineSlowdown <= 0) return r;
    double extra = groundAccelFactor(kUnrestrainedFriction) - groundAccelFactor(friction) * engineAccelSlowdown;
    r.velocity.x += accel.x * extra;
    r.velocity.z += accel.z * extra;
    r.nextRescale = kUnrestrainedFriction / (friction * engineSlowdown);
    if (std::abs(r.nextRescale - 1.0) < 1e-9) r.nextRescale = 1.0;
    return r;
}

std::optional<JumpMakeUp> unrestrainedJump(double vyAfterJumpStep, double power, double gravity) {
    double enginePower = vyAfterJumpStep / 0.98 + gravity;
    double lost        = power - enginePower;
    if (lost <= 0.01) return std::nullopt;
    return JumpMakeUp{(power - gravity) * 0.98 + lost, lost};
}

std::optional<double> slimeBounceStop(double vyBeforeLanding, double vyAfter) {
    // a bounce of the landing (at most as fast as the landing), not a jump off the slime
    if (vyBeforeLanding > kSlimeBounceStop && vyAfter > 0 && vyAfter <= std::max(-vyBeforeLanding, 0.0) + 0.02) return std::max(0.0, vyBeforeLanding);
    return std::nullopt;
}

// ---- slimy soles -----------------------------------------------------------------------------

bool slimyCancelsFall(bool sneaking, bool active, double fallDamage, bool forceBounce) {
    return !sneaking && active && (fallDamage > 0 || forceBounce);
}

void slimyLand(Slimy& s, double vyAtLanding, double coefficient) {
    s.bounceVelocity          = -vyAtLanding * std::sqrt(coefficient);
    s.doubleJumpBoostVelocity = s.bounceVelocity;
    s.hasBounced              = false;
}

std::optional<double> slimyBounce(Slimy& s, double vy) {
    if (s.bounceVelocity == 0 || s.hasBounced) return std::nullopt;
    s.hasBounced  = true;
    double out    = std::sqrt(vy * vy + s.bounceVelocity * s.bounceVelocity);
    s.forceBounce = std::abs(out) > 0.25;
    return out;
}

void slimyJump(Slimy& s) {
    s.bounceVelocity = 0;
    s.forceBounce    = false;
}

int slimyParticleCount(double bounceVelocity) {
    if (bounceVelocity <= 0) return 0;
    double intensity = std::min(0.2 + bounceVelocity, 2.5);
    return static_cast<int>(40 * intensity);
}

// ---- water walk ------------------------------------------------------------------------------

bool waterStandable(double submergedHeight, bool sneaking) { return submergedHeight < kWaterWalkMaxSubmerged && !sneaking; }

double waterHeight(int liquidDepth) {
    if (liquidDepth <= 0 || liquidDepth >= 8) return 8.0 / 9.0;
    return (8.0 - liquidDepth) / 9.0;
}

bool waterSplash(long long gameTime, long long lastSplashTime, double horizontalSpeed) {
    if (gameTime % kWaterSplashPeriod != 1) return false;
    return lastSplashTime + 1 != gameTime && horizontalSpeed >= 0.01;
}

// ---- validation --------------------------------------------------------------------------------

Abuse validate(Validator& v, int tickCount, bool fallFlying) {
    int count = v.count;
    int diff  = tickCount - v.lastCheck;
    if (diff >= kValidatorResetTicks && !fallFlying) count = -1;
    v.lastCheck = tickCount;
    if (count >= kValidatorDisconnect) return Abuse::Disconnect;
    v.count = count + 1;
    return count > kValidatorSnapBack ? Abuse::SnapBack : Abuse::Accept;
}

// ---- wings state -------------------------------------------------------------------------------

namespace {
constexpr std::array<std::string_view, 9> kStateNames{"idle", "walk", "sprint", "fall_slow", "fall_fast", "swim", "ride", "double_jump", "sidestep"};
}

std::string_view wingStateName(WingState s) { return kStateNames[static_cast<std::size_t>(s)]; }

std::optional<WingState> wingStateFromName(std::string_view name) {
    for (std::size_t i = 0; i < kStateNames.size(); ++i)
        if (kStateNames[i] == name) return static_cast<WingState>(i);
    return std::nullopt;
}

bool determineWingState(Wings& w, WingInputs const& in) {
    WingState next   = WingState::Idle;
    bool      locked = false;
    if (w.state == WingState::DoubleJump) {
        ++w.doubleJumpTimer;
        if (w.doubleJumpTimer < kDoubleJumpDuration) {
            locked = true;
            next   = WingState::DoubleJump;
        }
    } else if (w.state == WingState::Sidestep) {
        ++w.sidestepTimer;
        if (w.sidestepTimer < kSidestepDuration) {
            locked = true;
            next   = WingState::Sidestep;
        }
    } else {
        w.doubleJumpTimer = 0;
        w.sidestepTimer   = 0;
    }
    if (!locked) {
        if (in.passenger) next = WingState::Ride;
        else if (in.swimming) next = WingState::Swim;
        else if (!in.onGround && !in.inLiquid && in.fallDistance < kFallSlowDistance && !in.flying) next = WingState::FallSlow;
        else if (in.vy < 0 && in.fallDistance > kFallSlowDistance) next = WingState::FallFast;
        else if (in.sprinting) next = WingState::Sprint;
        else if (in.walkSpeed > 0.1f) next = WingState::Walk;
    }
    if (next == w.state) return false;
    w.state = next;
    return true;
}

void startDoubleJump(Wings& w) {
    w.state           = WingState::DoubleJump;
    w.doubleJumpTimer = 0;
}

void startSidestep(Wings& w, bool left) {
    w.state         = WingState::Sidestep;
    w.sidestepLeft  = left;
    w.sidestepTimer = 0;
}

float walkAnimation(float speed, double horizontalDistance) {
    auto target = static_cast<float>(std::min(horizontalDistance * 4.0, 1.0));
    return speed + (target - speed) * 0.4f;
}

// ---- wings animation ---------------------------------------------------------------------------

double interpolateToTarget(double old, double target, double dtTicks, double tau) {
    return target - (target - old) * std::exp(-dtTicks / tau);
}

void seedPhase(WingsAnim& a, double random01) { a.phase = std::clamp(random01, 0.0, 1.0) * 2 * kPi; }

WingPose animateWings(WingsAnim& a, Wings const& w, double ageInTicks, float walkSpeed, bool crouching) {
    double   dt = ageInTicks - a.oldAge;
    WingPose p;
    p.drop       = crouching ? 2.0f : 0.0f;
    double speed = kTau;
    Rot    right{};
    switch (w.state) {
    case WingState::DoubleJump:
        right = {-0.4f, -0.8f, -0.1f};
        speed = kTau - 1;
        break;
    case WingState::Ride: right = calculateRotations(a, dt, 10.0f, kAngle10 * 3, -0.6f, -0.3f, kBigSwing); break;
    case WingState::Swim: right = calculateRotations(a, dt, 17.0f, kAngle10 * 4, -1.0f, -0.5f, kBigSwing); break;
    case WingState::FallSlow: right = calculateRotations(a, dt, 17.0f, kAngle10 * 5, -1.1f, -0.1f, kBigSwing); break;
    case WingState::FallFast: right = calculateRotations(a, dt, 2.0f, kAngle10 * 4, -1.1f, -0.3f, kSmallSwing); break;
    case WingState::Sprint: right = calculateRotations(a, dt, 2.0f, kAngle10 * 3, -0.3f, 0.0f, kBigSwing); break;
    case WingState::Sidestep:
        right = calculateRotations(a, dt, 2.0f, kAngle10 * 3, w.sidestepLeft ? -0.6f : 0.4f, 0.0f, kBigSwing);
        break;
    default: {
        float divisor = walkSpeed > 0.1f ? 4.0f : 20.0f;
        right         = calculateRotations(a, dt, divisor, kAngle10 * 3, -0.6f, -0.3f, kBigSwing);
    }
    }
    p.rightX = static_cast<float>(interpolateToTarget(a.xRotOld, right.x, dt, speed));
    p.rightY = static_cast<float>(interpolateToTarget(a.yRotOldR, right.y, dt, speed));
    p.rightZ = static_cast<float>(interpolateToTarget(a.zRotOld, right.z, dt, speed));
    float targetLeftY = w.state == WingState::Sidestep
                          ? calculateRotations(a, dt, 2.0f, kAngle10 * 3, w.sidestepLeft ? -0.4f : 0.6f, 0.0f, kBigSwing).y
                          : -right.y;
    p.leftX = p.rightX;
    p.leftY = static_cast<float>(interpolateToTarget(a.yRotOldL, targetLeftY, dt, speed));
    p.leftZ = -p.rightZ;

    a.phase    = std::fmod(a.phase, 2 * kPi);
    a.oldAge   = ageInTicks;
    a.xRotOld  = p.rightX;
    a.yRotOldR = p.rightY;
    a.yRotOldL = p.leftY;
    a.zRotOld  = p.rightZ;
    return p;
}

// ---- input modifiers ---------------------------------------------------------------------------

double straightAheadMultiplier(bool active, double multiplier, float forwardImpulse) {
    if (!active || forwardImpulse <= 0) return 1.0;
    return multiplier;
}

float speedFovFactor(double speed, double walkSpeed) {
    if (walkSpeed <= 0) return 1.0f;
    return static_cast<float>((speed / walkSpeed + 1.0) / 2.0);
}

float straightAheadFovCompensation(double speed, double walkSpeed, double multiplier) {
    if (multiplier <= 0 || multiplier == 1.0 || speed <= 0) return 1.0f;
    float with = speedFovFactor(speed, walkSpeed), without = speedFovFactor(speed / multiplier, walkSpeed);
    return with > 0 ? without / with : 1.0f;
}

float efficientEaterRefund(float expectedMovement, float added) {
    if (added < 0) added += 4.0f; // the exhaustion passed 4 and the game took 4 off (food / saturation)
    if (expectedMovement <= 0 || added <= 0) return 0.0f;
    return std::min(expectedMovement, added) / 2.0f;
}

bool keepsAir(int oxygenBonus, int randomInclusive) { return oxygenBonus > 0 && randomInclusive > 0; }

} // namespace gf::core::tgmove
