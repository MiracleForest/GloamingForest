// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's mobs: shared helpers of Mobs.cpp (the ice mobs) and SnowQueen.cpp.

#include <string>

#include "core/jrand/JRand.h"

class Actor;
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
/// The mobGriefing gamerule (EventHooks#canEntityGrief).
[[nodiscard]] bool mobGriefing();
/// The three snow guardian particles per tick around the eyes (BaseIceMob, SnowQueen#spawnParticles).
void snowParticles(Actor& self, float eyeHeight, int count);
/// IceCrystal#setToDieIn30Seconds.
void markSummonedCrystal(Actor& crystal);

void installMobs();
void installSnowQueen();

// selftest access to the Snow Queen's (unsaved) fight state
[[nodiscard]] int queenPhaseForTest(Actor& queen); ///< 0 summon, 1 drop, 2 beam; -1 before her first tick
void              setQueenPhaseForTest(Actor& queen, int phase);
[[nodiscard]] int queenSummonsForTest(Actor& queen);
[[nodiscard]] int queenDropsForTest(Actor& queen, int* maxDrops);
[[nodiscard]] int queenBeamDamageForTest(Actor& queen);

} // namespace gf::feature::aurora
