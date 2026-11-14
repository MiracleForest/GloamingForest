// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/HydraHeadContainer.java
//   src/main/java/gloamingforest/entity/boss/Hydra.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/hydra/Head.h"

#include <cmath>

#include "core/jrand/JavaOrder.h"

namespace gf::core::hydra {

namespace {

constexpr float kPi = 3.1415927F; // Mth.PI

struct StateInfo {
    State            state;
    int              duration;
    State            next;
    std::string_view name;
};

constexpr StateInfo kStates[kStateCount] = {
    {State::Idle,            10, State::Idle,           "IDLE"            },
    {State::BiteBeginning,   40, State::BiteReady,      "BITE_BEGINNING"  },
    {State::BiteReady,       80, State::Biting,         "BITE_READY"      },
    {State::Biting,          7,  State::BiteEnding,     "BITING"          },
    {State::BiteEnding,      40, State::AttackCooldown, "BITE_ENDING"     },
    {State::FlameBeginning,  40, State::Flaming,        "FLAME_BEGINNING" },
    {State::Flaming,         100, State::FlameEnding,   "FLAMING"         },
    {State::FlameEnding,     30, State::AttackCooldown, "FLAME_ENDING"    },
    {State::MortarBeginning, 40, State::MortarShooting, "MORTAR_BEGINNING"},
    {State::MortarShooting,  25, State::MortarEnding,   "MORTAR_SHOOTING" },
    {State::MortarEnding,    30, State::AttackCooldown, "MORTAR_ENDING"   },
    {State::Dying,           70, State::Dead,           "DYING"           },
    {State::Dead,            20, State::Dead,           "DEAD"            },
    {State::AttackCooldown,  80, State::Idle,           "ATTACK_COOLDOWN" },
    {State::Born,            20, State::RoarStart,      "BORN"            },
    {State::RoarStart,       10, State::RoarRawr,       "ROAR_START"      },
    {State::RoarRawr,        50, State::Idle,           "ROAR_RAWR"       },
};

StateInfo const& info(State s) { return kStates[static_cast<int>(s)]; }

/// setupStateRotations, per state, heads 0..6 (a missing entry has neckLength 0: no pose).
struct Row {
    State state;
    Pose  heads[kMaxHeads];
};
// clang-format off
constexpr Row kPoses[] = {
    {State::Idle,            {{60, 0, 7, 0}, {10, 60, 9, 0}, {10, -60, 9, 0}, {50, 90, 8, 0}, {50, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::AttackCooldown,  {{60, 0, 7, 0}, {10, 60, 9, 0}, {10, -60, 9, 0}, {50, 90, 8, 0}, {50, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::FlameBeginning,  {{50, 0, 8, 0.75F}, {30, 45, 9, 0.75F}, {30, -45, 9, 0.75F}, {50, 90, 8, 0.75F}, {50, -90, 8, 0.75F}, {-10, 90, 9, 0.75F}, {-10, -90, 9, 0.75F}}},
    {State::Flaming,         {{45, 0, 8, 1}, {30, 60, 9, 1}, {30, -60, 9, 1}, {50, 90, 8, 1}, {50, -90, 8, 1}, {-10, 90, 9, 1}, {-10, -90, 9, 1}}},
    {State::FlameEnding,     {{60, 0, 7, 0}, {10, 45, 9, 0}, {10, -45, 9, 0}, {50, 90, 8, 0}, {50, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::BiteBeginning,   {{-5, 60, 5, 0.25F}, {-10, 60, 9, 0.25F}, {-10, -60, 9, 0.25F}, {}, {}, {}, {}}},
    {State::BiteReady,       {{-5, 60, 5, 1}, {-10, 60, 9, 1}, {-10, -60, 9, 1}, {}, {}, {}, {}}},
    {State::Biting,          {{-5, -30, 5, 0.2F}, {-10, -30, 5, 0.2F}, {-10, 30, 5, 0.2F}, {}, {}, {}, {}}},
    {State::BiteEnding,      {{60, 0, 7, 0}, {-10, 60, 9, 0}, {-10, -60, 9, 0}, {}, {}, {}, {}}},
    {State::MortarBeginning, {{50, 0, 8, 0.75F}, {30, 45, 9, 0.75F}, {30, -45, 9, 0.75F}, {50, 90, 8, 0.75F}, {50, -90, 8, 0.75F}, {-10, 90, 9, 0.75F}, {-10, -90, 9, 0.75F}}},
    {State::MortarShooting,  {{45, 0, 8, 1}, {30, 60, 9, 1}, {30, -60, 9, 1}, {50, 90, 8, 1}, {50, -90, 8, 1}, {-10, 90, 9, 1}, {-10, -90, 9, 1}}},
    {State::MortarEnding,    {{60, 0, 7, 0}, {10, 45, 9, 0}, {10, -45, 9, 0}, {50, 90, 8, 0}, {50, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::Dying,           {{-20, 0, 7, 0}, {-20, 60, 9, 0}, {-20, -60, 9, 0}, {-20, 90, 8, 0}, {-20, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::Dead,            {{0, 179, 4, 0}, {0, 179, 4, 0}, {0, -180, 4, 0}, {0, 179, 4, 0}, {0, -180, 4, 0}, {0, 179, 4, 0}, {0, -180, 4, 0}}},
    {State::Born,            {{60, 0, 7, 0}, {10, 60, 9, 0}, {10, -60, 9, 0}, {50, 90, 8, 0}, {50, -90, 8, 0}, {-10, 90, 9, 0}, {-10, -90, 9, 0}}},
    {State::RoarStart,       {{60, 0, 7, 0.25F}, {10, 60, 9, 0.25F}, {10, -60, 9, 0.25F}, {50, 90, 8, 0.25F}, {50, -90, 8, 0.25F}, {-10, 90, 9, 0.25F}, {-10, -90, 9, 0.25F}}},
    {State::RoarRawr,        {{60, 0, 9, 1}, {10, 60, 11, 1}, {10, -60, 11, 1}, {50, 90, 10, 1}, {50, -90, 10, 1}, {-10, 90, 11, 1}, {-10, -90, 11, 1}}},
};
// clang-format on

float mthSin(float v) { return jrand::mthSin(v); }
float mthCos(float v) { return jrand::mthCos(v); }

/// Entity#setXRot (1.21.1): finite values are clamped to [-90, 90] after `% 360`.
float clampPitch(float x) {
    if (!std::isfinite(x)) return 0.0F;
    float m = std::fmod(x, 360.0F);
    return m < -90.0F ? -90.0F : (m > 90.0F ? 90.0F : m);
}

} // namespace

int   duration(State s) { return info(s).duration; }
State automaticNext(State s) { return info(s).next; }

std::string_view name(State s) { return info(s).name; }

std::optional<State> stateFromName(std::string_view n) {
    for (auto const& i : kStates)
        if (i.name == n) return i.state;
    return std::nullopt;
}

Pose pose(int head, State state) {
    if (head < 0 || head >= kMaxHeads) return {};
    for (auto const& r : kPoses)
        if (r.state == state) {
            Pose p = r.heads[head];
            if (p.neckLength != 0) return p;
            break;
        }
    return pose(head, State::Idle);
}

Head::Head(int number, bool startActive) : num(number) {
    if (startActive) {
        prevState = currentState = State::Idle;
        ticksNeeded = ticksProgress = 60;
    } else {
        prevState = currentState = State::Dead;
        ticksNeeded = ticksProgress = 20;
    }
}

bool Head::isIdle() const { return currentState == State::Idle && (!nextState || *nextState == State::Idle); }

bool Head::isAttacking() const {
    switch (currentState) {
    case State::BiteBeginning:
    case State::BiteReady:
    case State::Biting:
    case State::FlameBeginning:
    case State::Flaming:
    case State::MortarBeginning:
    case State::MortarShooting: return true;
    default: return false;
    }
}

bool Head::isBiting() const {
    return currentState == State::BiteBeginning || currentState == State::BiteReady || currentState == State::Biting
        || (nextState && *nextState == State::BiteBeginning);
}

bool Head::advanceRespawnCounter() {
    if (currentState == State::Dead && respawnCounter > -1) {
        if (--respawnCounter <= 0) {
            setNextState(State::Born);
            damageTaken = 0;
            endCurrentAction();
            respawnCounter = -1;
            return true;
        }
    }
    return false;
}

void Head::advanceHeadState() {
    if (++ticksProgress >= ticksNeeded) {
        State myNext;
        if (!nextState) {
            myNext = automaticNext(currentState);
            // when returning from a secondary attack, no attack cooldown
            if (myNext != currentState && isSecondaryAttacking && myNext == State::AttackCooldown) {
                isSecondaryAttacking = false;
                myNext               = State::Idle;
            }
        } else {
            myNext = *nextState;
            nextState.reset();
        }
        ticksNeeded   = duration(myNext);
        ticksProgress = 0;
        prevState     = currentState;
        currentState  = myNext;
    }
}

float Head::currentNeckLength() const { return clampedLerp(pose(num, prevState).neckLength, pose(num, currentState).neckLength, progress()); }
float Head::currentXRotation() const { return clampedLerp(pose(num, prevState).xRot, pose(num, currentState).xRot, progress()); }
float Head::currentYRotation() const { return clampedLerp(pose(num, prevState).yRot, pose(num, currentState).yRot, progress()); }
float Head::currentMouthOpen() const { return clampedLerp(pose(num, prevState).mouthOpen, pose(num, currentState).mouthOpen, progress()); }

// ---- geometry --------------------------------------------------------------------------------------

V3 xRot(V3 v, float angle) {
    float  f  = mthCos(angle);
    float  f1 = mthSin(angle);
    double y  = v.y * f + v.z * f1;
    double z  = v.z * f - v.y * f1;
    return {v.x, y, z};
}

V3 yRot(V3 v, float angle) {
    float  f  = mthCos(angle);
    float  f1 = mthSin(angle);
    double x  = v.x * f + v.z * f1;
    double z  = v.z * f - v.x * f1;
    return {x, v.y, z};
}

V3 viewVector(float xRotDeg, float yRotDeg) {
    float f  = xRotDeg * (kPi / 180.0F);
    float f1 = -yRotDeg * (kPi / 180.0F);
    float f2 = mthCos(f1);
    float f3 = mthSin(f1);
    float f4 = mthCos(f);
    float f5 = mthSin(f);
    return {static_cast<double>(f3 * f4), static_cast<double>(-f5), static_cast<double>(f2 * f4)};
}

float wrapDegrees(float deg) {
    float f = std::fmod(deg, 360.0F);
    if (f >= 180.0F) f -= 360.0F;
    if (f < -180.0F) f += 360.0F;
    return f;
}

float updateRotation(float current, float intended, float increment) {
    float delta = wrapDegrees(intended - current);
    if (delta > increment) delta = increment;
    if (delta < -increment) delta = -increment;
    return wrapDegrees(current + delta);
}

float rotlerp(float current, float target, float maxStep) {
    float f = wrapDegrees(target - current);
    if (f > maxStep) f = maxStep;
    if (f < -maxStep) f = -maxStep;
    return current + f;
}

float clampedLerp(float start, float end, float delta) {
    if (delta < 0.0F) return start;
    if (delta > 1.0F) return end;
    return start + delta * (end - start);
}

Facing faceVec(V3 head, float headYRot, float headXRot, V3 target, float yawConstraint, float pitchConstraint) {
    double xOffset  = target.x - head.x;
    double zOffset  = target.z - head.z;
    double yOffset  = (head.y + 1.0) - target.y;
    double distance = std::sqrt(static_cast<float>(xOffset * xOffset + zOffset * zOffset));
    float  xyAngle  = static_cast<float>((std::atan2(zOffset, xOffset) * 180.0) / 3.141592653589793) - 90.0F;
    float  zdAngle  = static_cast<float>(-((std::atan2(yOffset, distance) * 180.0) / 3.141592653589793));
    Facing out;
    out.xRot = clampPitch(-updateRotation(headXRot, zdAngle, pitchConstraint));
    out.yRot = updateRotation(headYRot, xyAngle, yawConstraint);
    return out;
}

float periodX(int head) { return (head == 0 || head == 3) ? 20.0F : ((head == 1 || head == 4) ? 5.0F : 7.0F); }
float periodY(int head) { return (head == 0 || head == 4) ? 10.0F : ((head == 1 || head == 6) ? 6.0F : 5.0F); }

V3 headPosition(Head const& head, V3 hydra, float yBodyRot, int tickCount) {
    float neckLength = head.currentNeckLength();
    float xRotation  = head.currentXRotation();
    float yRotation  = head.currentYRotation();
    float xSwing     = mthSin(static_cast<float>(tickCount) / periodX(head.num)) * 3.0F;
    float ySwing     = mthSin(static_cast<float>(tickCount) / periodY(head.num)) * 5.0F;
    if (head.isDead()) xSwing = ySwing = 0;
    V3 v{0, 0, neckLength};
    v = xRot(v, (xRotation * kPi + xSwing) / 180.0F);
    v = yRot(v, (-(yBodyRot + yRotation + ySwing) * kPi) / 180.0F);
    return {hydra.x + v.x, hydra.y + v.y + 3, hydra.z + v.z};
}

V3 neckAnchor(int head, V3 hydra, float yBodyRot) {
    static constexpr V3    kVectors[kMaxHeads]   = {{0, 3, -1}, {-1, 3, 3}, {1, 3, 3}, {-1, 2.5, 3}, {1, 2.5, 3}, {-1, 2, 5}, {1, 2, 5}};
    static constexpr float kRotations[kMaxHeads] = {0, 90, -90, 135, -135, 135, -135};
    if (head < 0 || head >= kMaxHeads) return hydra;
    V3 v = yRot(kVectors[head], (-(yBodyRot + kRotations[head]) * kPi) / 180.0F);
    return {hydra.x + v.x, hydra.y + v.y, hydra.z + v.z};
}

std::array<PartPlace, 5> neckPositions(V3 start, float startYaw, V3 headPos, float headYRot, float headXRot) {
    double endX     = headPos.x;
    double endY     = headPos.y - 0.5F;
    double endZ     = headPos.z;
    float  endYaw   = headYRot;
    float  endPitch = headXRot;
    while (startYaw - endYaw < -180.0F) endYaw -= 360.0F;
    while (startYaw - endYaw >= 180.0F) endYaw += 360.0F;
    while (0.0F - endPitch < -180.0F) endPitch -= 360.0F;
    while (0.0F - endPitch >= 180.0F) endPitch += 360.0F;
    if (endPitch > 0) {
        // looking down: don't raise the first neck position
        V3 v = yRot(V3{0.0, 0.0, -1.8}, (-endYaw * 3.141593F) / 180.0F);
        endX += v.x;
        endY += v.y;
        endZ += v.z;
    } else {
        // looking up: lower it or it goes through the crest
        V3 look = viewVector(headXRot, headYRot);
        endX -= look.x;
        endY -= look.y;
        endZ -= look.z;
    }
    std::array<PartPlace, 5> out;
    static constexpr float kFactors[5] = {0.0F, 0.25F, 0.5F, 0.75F, 1.0F};
    for (int i = 0; i < 5; ++i) {
        float f    = kFactors[i];
        out[i].pos = {endX + (start.x - endX) * f, endY + (start.y - endY) * f, endZ + (start.z - endZ) * f};
        out[i].yRot = endYaw + (startYaw - endYaw) * f;
        out[i].xRot = clampPitch(endPitch + (0.0F - endPitch) * f);
    }
    return out;
}

V3 bodyPosition(V3 hydra, float yBodyRot) {
    float angle = ((yBodyRot + 180.0F) * kPi) / 180.0F;
    return {hydra.x - mthSin(angle) * 3.0, hydra.y + 0.1, hydra.z + mthCos(angle) * 3.0};
}

V3 tailPosition(V3 hydra, float yBodyRot) {
    float angle = ((yBodyRot + 180.0F) * kPi) / 180.0F;
    return {hydra.x - mthSin(angle) * 10.5, hydra.y + 0.1, hydra.z + mthCos(angle) * 10.5};
}

float tickBodyTurn(float yBodyRot, float entityYRot) {
    // f = wrapDegrees(yRot - yBodyRot) is 0 for a still hydra
    float f1 = wrapDegrees(entityYRot - yBodyRot);
    if (f1 < -75.0F) f1 = -75.0F;
    if (f1 >= 75.0F) f1 = 75.0F;
    float body = entityYRot - f1;
    if (f1 * f1 > 2500.0F) body += f1 * 0.2F;
    return body;
}

int countActiveHeads(std::array<Head, kMaxHeads> const& heads) {
    int n = 0;
    for (auto const& h : heads)
        if (!h.isDead()) ++n;
    return n;
}

bool tooManyAttacking(std::array<Head, kMaxHeads> const& heads, int testHead, bool hard) {
    int other = 0;
    for (int i = 0; i < kMaxHeads; ++i) {
        if (i == testHead || !heads[i].isAttacking()) continue;
        ++other;
        if (heads[i].isBiting()) other += 2; // biting heads count triple
    }
    float factor = hard ? 0.5F : 0.3F;
    return static_cast<float>(other) >= 1.0F + static_cast<float>(countActiveHeads(heads)) * factor;
}

bool otherHeadsBiting(std::array<Head, kMaxHeads> const& heads, int testHead) {
    for (int i = 0; i < kMaxHeads; ++i)
        if (i != testHead && heads[i].isBiting()) return true;
    return false;
}

std::uint8_t activeMask(std::array<Head, kMaxHeads> const& heads) {
    std::uint8_t m = 0;
    for (int i = 0; i < kMaxHeads; ++i)
        if (heads[i].isActive()) m |= static_cast<std::uint8_t>(1 << i);
    return m;
}

void activateHeadsOnLoad(std::array<Head, kMaxHeads>& heads, std::uint8_t mask) {
    for (int i = 0; i < kMaxHeads; ++i)
        if ((mask & (1 << i)) != 0) {
            heads[i].setNextState(State::Idle);
            heads[i].endCurrentAction();
        }
}

} // namespace gf::core::hydra
