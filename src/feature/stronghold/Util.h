// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U25 (stronghold): small helpers shared by the unit's mobs (Util.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"

class Actor;
class Mob;
class Player;

namespace gf::feature::stronghold {

namespace id {
inline constexpr char KnightPhantom[]     = "twilightforest:knight_phantom";
inline constexpr char ThrownWep[]         = "twilightforest:thrown_wep";
inline constexpr char LowerGoblinKnight[] = "twilightforest:lower_goblin_knight";
inline constexpr char UpperGoblinKnight[] = "twilightforest:upper_goblin_knight";
inline constexpr char BlockChainGoblin[]  = "twilightforest:blockchain_goblin";
inline constexpr char HelmetCrab[]        = "twilightforest:helmet_crab";
inline constexpr char Spawner[]           = "twilightforest:knight_phantom_boss_spawner";
inline constexpr char Stronghold[]        = "twilightforest:knight_stronghold";
} // namespace id

/// Level#getRandom (the unit's own LegacyRandom; selftests reseed it).
[[nodiscard]] core::jrand::RandomSource& levelRandom();
void                                     seedLevelRandomForTest(std::int64_t seed);

/// level.getEntitiesOfClass(type, actor.getBoundingBox().inflate(r)): the live actors of `type`
/// whose box meets the inflated box ("" = any type).
[[nodiscard]] std::vector<Actor*> actorsAround(Actor& actor, std::string const& type, double inflate);
/// The same around a box given by its corners.
[[nodiscard]] std::vector<Actor*> actorsInBox(int dim, Vec3 const& min, Vec3 const& max, std::string const& type);

/// CombatRules#getDamageAfterAbsorb as a factor: 1 - clamp(armor - damage / (2 + toughness / 4),
/// armor * 0.2, 20) / 25.
[[nodiscard]] float armorFactor(float damage, float armor, float toughness);

/// Entity#playSound at the actor (the entity's sound source).
void playSound(Actor& actor, std::string const& event, float volume, float pitch);

/// Mob#getVoicePitch: (r1 - r2) * 0.2 + 1 (adults).
[[nodiscard]] float voicePitch(core::jrand::RandomSource& random);

/// Whether the actor's main hand holds `itemId`.
[[nodiscard]] bool holds(Actor const& actor, std::string const& itemId);

} // namespace gf::feature::stronghold
