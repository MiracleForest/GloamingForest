// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/HydraHeadContainer.java
//   src/main/java/gloamingforest/entity/boss/Hydra.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent part of the Hydra (U24): HydraHeadContainer's state machine and pose
// tables, and the geometry of the heads, necks, body and tail (Hydra#aiStep,
// HydraHeadContainer#setHeadPosition / #setNeckPosition / #faceVec). Pure C++ (gf-core), gtested
// in tests/hydra.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gf::core::hydra {

inline constexpr int   kMaxHeads          = 7;    ///< Hydra.MAX_HEADS
inline constexpr int   kHeadRespawnTicks  = 140;  ///< Hydra.HEAD_RESPAWN_TICKS
inline constexpr int   kHeadMaxDamage     = 120;  ///< Hydra.HEAD_MAX_DAMAGE
inline constexpr float kArmorMultiplier   = 8.0F; ///< Hydra.ARMOR_MULTIPLIER
inline constexpr int   kTicksBeforeHeal   = 1000; ///< Hydra.TICKS_BEFORE_HEALING
inline constexpr int   kMaxHealth         = 360;  ///< Hydra.MAX_HEALTH
inline constexpr double kMovementSpeed    = 0.28; ///< Attributes.MOVEMENT_SPEED
inline constexpr int   kXpReward          = 511;
inline constexpr int   kMaxHeadXRot       = 500;  ///< Hydra#getMaxHeadXRot
inline constexpr int   kHomeRadius        = 20;
inline constexpr unsigned kBarColor       = 0x05EBB9;
inline constexpr int   kFlameDamage       = 19;   ///< HydraHeadContainer.FLAME_DAMAGE
inline constexpr int   kFlameBurnSeconds  = 3;    ///< FLAME_BURN_FACTOR
inline constexpr int   kBiteDamage        = 48;   ///< BITE_DAMAGE
inline constexpr int   kSecondaryFlameChance  = 10;
inline constexpr int   kSecondaryMortarChance = 16;
inline constexpr int   kBiteChance        = 10;
inline constexpr int   kFlameChance       = 100;
inline constexpr int   kMortarChance      = 160;
/// Part actors, in Java's order (Hydra's constructor) without the two legs: body, tail, then per
/// head the head and its five necks. upstream registers a left and a right leg part (2 x 3) but never
/// positions them (they stay at the world origin and render nothing); no actor stands for them here.
inline constexpr int   kBodyParts         = 2;
inline constexpr int   kPartsPerHead      = 6;
inline constexpr int   kPartCount         = kBodyParts + kMaxHeads * kPartsPerHead; // 44
[[nodiscard]] constexpr int headPartIndex(int head) { return kBodyParts + head * kPartsPerHead; }
[[nodiscard]] constexpr int neckPartIndex(int head, int neck) { return headPartIndex(head) + 1 + neck; }

/// HydraHeadContainer.State (ordinal order matters: the head's synced DATA_STATE).
enum class State : std::uint8_t {
    Idle,
    BiteBeginning,
    BiteReady,
    Biting,
    BiteEnding,
    FlameBeginning,
    Flaming,
    FlameEnding,
    MortarBeginning,
    MortarShooting,
    MortarEnding,
    Dying,
    Dead,
    AttackCooldown,
    Born,
    RoarStart,
    RoarRawr,
};
inline constexpr int kStateCount = 17;

[[nodiscard]] int              duration(State s);
/// State.NEXT_STATE.
[[nodiscard]] State            automaticNext(State s);
[[nodiscard]] std::string_view name(State s);
[[nodiscard]] std::optional<State> stateFromName(std::string_view name);

/// One pose of setupStateRotations: (xRotation, yRotation, neckLength, mouthOpen).
struct Pose {
    float xRot       = 0;
    float yRot       = 0;
    float neckLength = 0;
    float mouthOpen  = 0;
};
/// The pose of `head` in `state`. Heads 3..6 have no bite poses (they never bite; Java would throw
/// on unboxing): the IDLE pose stands in.
[[nodiscard]] Pose pose(int head, State state);

/// HydraHeadContainer's state data (the server side of one head).
struct Head {
    int                  num = 0;
    State                prevState    = State::Idle;
    State                currentState = State::Idle;
    std::optional<State> nextState; ///< nullopt: NEXT_AUTOMATIC
    int                  ticksNeeded   = 60;
    int                  ticksProgress = 60;
    int                  damageTaken   = 0;
    int                  respawnCounter = -1;
    int                  deathTime      = 0; ///< animateHeadDeath's counter
    bool                 isSecondaryAttacking = false;

    Head() = default;
    /// HydraHeadContainer(hydra, number, startActive).
    Head(int number, bool startActive);

    [[nodiscard]] bool isActive() const { return currentState != State::Dead; }
    [[nodiscard]] bool isDead() const { return currentState == State::Dying || currentState == State::Dead; }
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] bool isAttacking() const;
    [[nodiscard]] bool isBiting() const;
    [[nodiscard]] bool canRespawn() const { return currentState == State::Dead && respawnCounter == -1; }

    void setNextState(State s) { nextState = s; }
    void endCurrentAction() { ticksProgress = ticksNeeded; }
    /// advanceRespawnCounter: true when the head was told to be BORN this tick.
    bool advanceRespawnCounter();
    /// advanceHeadState.
    void advanceHeadState();
    /// addDamage: `(int) damageAmount`.
    void addDamage(float amount) { damageTaken += static_cast<int>(amount); }

    [[nodiscard]] float progress() const { return static_cast<float>(ticksProgress) / static_cast<float>(ticksNeeded); }
    [[nodiscard]] float currentNeckLength() const;
    [[nodiscard]] float currentXRotation() const;
    [[nodiscard]] float currentYRotation() const;
    [[nodiscard]] float currentMouthOpen() const;
};

// ---- geometry --------------------------------------------------------------------------------------

struct V3 {
    double x = 0, y = 0, z = 0;
};
/// Java's Vec3#xRot / #yRot (Mth.cos / Mth.sin of a float angle in radians).
[[nodiscard]] V3 xRot(V3 v, float angle);
[[nodiscard]] V3 yRot(V3 v, float angle);
/// Entity#calculateViewVector(xRot, yRot): the look vector of a pitch / yaw in degrees.
[[nodiscard]] V3 viewVector(float xRotDeg, float yRotDeg);
/// Mth.wrapDegrees(float).
[[nodiscard]] float wrapDegrees(float deg);
/// HydraHeadContainer#updateRotation.
[[nodiscard]] float updateRotation(float current, float intended, float increment);
/// Mth.rotLerp-free Mob#rotlerp (Mob#lookAt's step).
[[nodiscard]] float rotlerp(float current, float target, float maxStep);
/// Mth.clampedLerp(float).
[[nodiscard]] float clampedLerp(float start, float end, float delta);

/// HydraHeadContainer#faceVec: the head's new (yaw, pitch) facing (x, y, z) from `head` (the head
/// entity's position, its yaw and pitch).
struct Facing {
    float yRot = 0;
    float xRot = 0;
};
[[nodiscard]] Facing faceVec(V3 head, float headYRot, float headXRot, V3 target, float yawConstraint, float pitchConstraint);

/// The swing periods of setHeadPosition.
[[nodiscard]] float periodX(int head);
[[nodiscard]] float periodY(int head);
/// HydraHeadContainer#setHeadPosition: where the head entity stands (from the hydra's position).
[[nodiscard]] V3 headPosition(Head const& head, V3 hydra, float yBodyRot, int tickCount);
/// The neck anchor of setNeckPosition() (from the hydra's position).
[[nodiscard]] V3 neckAnchor(int head, V3 hydra, float yBodyRot);

/// One neck part's placement.
struct PartPlace {
    V3    pos;
    float yRot = 0;
    float xRot = 0;
};
/// setNeckPosition(start, startYaw): the five neck parts between the head end and the anchor.
[[nodiscard]] std::array<PartPlace, 5> neckPositions(V3 start, float startYaw, V3 headPos, float headYRot, float headXRot);

/// Hydra#aiStep: the body (3 blocks behind) and the tail (10.5 behind), at y + 0.1.
[[nodiscard]] V3 bodyPosition(V3 hydra, float yBodyRot);
[[nodiscard]] V3 tailPosition(V3 hydra, float yBodyRot);

/// Hydra#tickHeadTurn with the LivingEntity#tick argument of a hydra that does not move
/// (yRot = yBodyRot): the new yBodyRot after one tick.
[[nodiscard]] float tickBodyTurn(float yBodyRot, float entityYRot);

/// The "too many heads attacking" limit: other attacks (biting counts 3) >= 1 + active x factor.
[[nodiscard]] bool tooManyAttacking(std::array<Head, kMaxHeads> const& heads, int testHead, bool hard);
[[nodiscard]] int  countActiveHeads(std::array<Head, kMaxHeads> const& heads);
[[nodiscard]] bool otherHeadsBiting(std::array<Head, kMaxHeads> const& heads, int testHead);

/// Hydra's save byte NumHeads (bit i: head i active) and activateHeadsOnLoad.
[[nodiscard]] std::uint8_t activeMask(std::array<Head, kMaxHeads> const& heads);
void                       activateHeadsOnLoad(std::array<Head, kMaxHeads>& heads, std::uint8_t mask);

} // namespace gf::core::hydra
