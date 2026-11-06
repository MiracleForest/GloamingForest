// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's mobs: shared helpers of Mobs.cpp (the ice mobs) and SnowQueen.cpp.

#include <optional>
#include <string>

#include "core/jrand/JRand.h"

class Actor;
class Mob;
class BlockPos;
class BlockSource;
class Vec3;

namespace gf::feature::aurora {

inline constexpr char kSnowQueen[]       = "twilightforest:snow_queen";
inline constexpr char kSnowGuardian[]    = "twilightforest:snow_guardian";
inline constexpr char kStableIceCore[]   = "twilightforest:stable_ice_core";
inline constexpr char kUnstableIceCore[] = "twilightforest:unstable_ice_core";
inline constexpr char kIceCrystal[]      = "twilightforest:ice_crystal";

/// The unit's random source for TF's gameplay draws (server thread).
[[nodiscard]] core::jrand::RandomSource& rng();
/// (random.nextFloat() - random.nextFloat()).
[[nodiscard]] float rr();
[[nodiscard]] Vec3  feet(Actor const& actor);
/// BaseIceMob: the #minecraft:snow_golem_melts biomes and TF's oak savannah and fire swamp.
[[nodiscard]] bool meltsSnowIn(std::string const& biomeId);
/// The mobGriefing gamerule (EventHooks#canEntityGrief).
[[nodiscard]] bool mobGriefing();
/// UnstableIceCore#transformBlocks around `at` (range 4 + (r - r) x 2): stained glass / terracotta.
void transformBlocksAt(BlockSource& region, BlockPos const& at);
/// Java's MapColor#col of a vanilla block id (transformBlocks' table), nullopt when not listed.
[[nodiscard]] std::optional<unsigned> javaMapColor(std::string const& id);
/// IceCrystal#setToDieIn30Seconds.
void markSummonedCrystal(Actor& crystal);
/// Test hook: the crystal's age (ticks lived since it was summoned).
void setCrystalAgeForTest(Actor& crystal, int age);
/// Test hook: the crystal's maxCrystalAge (0: not summoned, it stays).
int crystalMaxAgeForTest(Actor& crystal);

/// Entity#getEyeHeight of TF's dimensions (1.53 / 1.35 / 2.125): Bedrock puts a mob's eyes at 0.9 x its
/// height; the offsets component's eye offset is set to Java's value (every tick: the engine may
/// recompute it with the box).
void applyJavaEyeHeight(Actor& actor);

/// Whether F8's per-actor state of `actor` holds native goals (it is dropped by F8's sweep some
/// time after the actor stops resolving, e.g. in an unloaded chunk: the goals must come back).
[[nodiscard]] bool nativeGoalsPresent(Actor& actor);
/// Test hook: forgets F8's goals, move control and spawn mark of `actor`, as F8's sweep does.
void dropNativeStateForTest(Actor& actor);

void installMobs();
void installSnowQueen();

// selftest access to the Snow Queen's (unsaved) fight state
[[nodiscard]] int queenPhaseForTest(Actor& queen); ///< 0 summon, 1 drop, 2 beam; -1 before her first tick
void              setQueenPhaseForTest(Actor& queen, int phase);
[[nodiscard]] int queenSummonsForTest(Actor& queen);
[[nodiscard]] int queenDropsForTest(Actor& queen, int* maxDrops);
[[nodiscard]] int queenBeamDamageForTest(Actor& queen);
[[nodiscard]] bool queenBreathingForTest(Actor& queen);
/// HoverBeamGoal#doRayAttack once, along the queen's view.
void rayAttackForTest(Mob& queen);
[[nodiscard]] int pierceLevelForTest(Actor& projectile);

} // namespace gf::feature::aurora
