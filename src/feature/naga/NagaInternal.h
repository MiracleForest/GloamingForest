// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/Naga.java
//   src/main/java/twilightforest/entity/ai/goal/NagaMovementPattern.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U20 internals shared by the Naga, the courtyard and their selftests (not a contract).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"
#include "core/naga/Courtyard.h"
#include "feature/entity/Entity.h"
#include "feature/structure/Structure.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;

namespace gf::feature::naga {

inline constexpr char kNagaId[]     = "twilightforest:naga";
inline constexpr char kSpawnerId[]  = "twilightforest:naga_boss_spawner";
inline constexpr char kCourtyard[]  = "twilightforest:naga_courtyard";
inline constexpr int  kMaxSegments  = 12;
inline constexpr int  kXzHomeBounds = 46;
inline constexpr int  kYHomeBounds  = 7;
inline constexpr int  kHomeRadius   = 40;
inline constexpr int  kDeathAnimationDuration = 24;
inline constexpr int  kDeathParticlesDuration = 100;
inline constexpr int  kTicksBeforeHealing     = 600;
inline constexpr double kDefaultSpeed         = 0.5;

/// NagaMovementPattern.MovementState.
enum class MovementState { Intimidate, Crumble, Charge, StunlessCharge, Circle, Daze };
[[nodiscard]] char const* stateName(MovementState state);

/// NagaSegment (a TFPart: an F8 part actor).
struct Segment {
    gf::entity::PartHandle part;
    Vec3                   pos{};
    float                  yRot = 0.0f, xRot = 0.0f;
    bool                   active       = false;
    int                    deathCounter = 0;
};

/// Everything a live Naga keeps (by ActorUniqueID, rule R16).
struct NagaState {
    std::int64_t                      uid = 0;
    core::jrand::LegacyRandom         random{0};
    std::array<Segment, kMaxSegments> segments{};
    int                               currentSegmentCount = 0;
    float                             healthPerSegment    = 12.0f;
    int                               ticksSinceDamaged   = 0;
    int                               damageDuringCurrentStun = 0;
    bool                              dazed = false, charging = false, stunless = false;
    // NagaMovementPattern
    MovementState state          = MovementState::Circle;
    int           stateCounter   = 15;
    bool          clockwise      = false;
    bool          stunCalculated = false;
    // the navigation (PathNavigation's moveTo / isDone / stop / isStuck)
    bool  navActive = false;
    Vec3  navDest{};
    float navSpeed  = 0.0f; ///< Mob#speed's modifier: 0 until the first path
    bool  initialized = false; ///< onSpawn (or the reattach after a reload) ran for this state
    int   navTicks  = 0;
    Vec3  stuckCheckPos{};
    bool  stuck     = false;
    // LivingEntity#xxa of NagaMoveControl
    float xxa = 0.0f;
    // HurtByTargetGoal
    std::int64_t lastHurtBy = 0;
    int          lastHurtByTimestamp = 0;
    std::uint64_t lastSeen = 0;
    int           tickCount = 0;
};

/// The state of a Naga (created on demand; nullptr for other actors).
[[nodiscard]] NagaState* stateOf(Actor& naga);
/// The Naga's movement pattern state (selftests).
void seedNagaForTest(Actor& naga, std::int64_t seed);
/// Forgets the U20 state of a Naga, as when its chunk stayed unloaded longer than the state lives
/// while F8 kept its own (selftests: the reattach path).
void dropStateForTest(Actor& naga);
/// NagaMovementPattern#doDaze / #doCircle / #forceCircle (selftests drive the shield cases).
void doDaze(Mob& naga);
void doCircle(Mob& naga);
/// The cooldown category of an item (Item#getCooldownCategory; its path when it has none).
[[nodiscard]] std::string shieldCooldownCategory(ItemStack const& stack);
/// Naga#doHurtTarget (selftests).
bool nagaHurtTarget(Mob& naga, Actor& target);
/// Naga#isMobWithinHomeArea.
[[nodiscard]] bool withinHome(Mob& naga, Actor& entity);
/// Naga#setSegmentsPerHealth's formula.
[[nodiscard]] int segmentsFor(float health, float healthPerSegment);
/// BlockBehaviour#getFriction (Java's values: 0.6 by default, ice 0.98, slime 0.8).
[[nodiscard]] float blockFriction(Block const& block);
/// EntityUtil#canDestroyBlock for the Naga (selftests).
[[nodiscard]] bool nagaCanDestroyBlock(BlockSource& region, BlockPos const& pos);

// ---- courtyard (Courtyard.cpp) ------------------------------------------------------------------

void installCourtyard();
/// The pieces of a courtyard start at (x, y, z) (LandmarkStructure's center): CourtyardMain from
/// `firstPieceRandom`, its children from `contextRandom`.
[[nodiscard]] std::vector<std::unique_ptr<structure::Piece>> courtyardPieces(structure::RandomSource& firstPieceRandom,
                                                                            structure::RandomSource& contextRandom, int x, int y, int z);
[[nodiscard]] std::unique_ptr<structure::Piece> makePiece(core::naga::PieceRecord const& record);

// ---- entity (Naga.cpp) --------------------------------------------------------------------------

void installNaga();
void uninstallNaga();

} // namespace gf::feature::naga
