// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/passive/Bird.java
//   src/main/java/gloamingforest/entity/passive/FlyingBird.java
//   src/main/java/gloamingforest/entity/passive/Bighorn.java
//   src/main/java/gloamingforest/entity/passive/DwarfRabbit.java
//   src/main/java/gloamingforest/entity/passive/TinyBirdVariant.java
//   src/main/java/gloamingforest/entity/passive/DwarfRabbitVariant.java
//   src/main/java/gloamingforest/entity/monster/Kobold.java
//   src/main/java/gloamingforest/entity/monster/MosquitoSwarm.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U16's pure rules (no game types): the numbers and random draws of upstream's passive and ambient
// fauna, shared by src/feature/fauna and gtested in tests/fauna.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::fauna {

using jrand::RandomSource;

// ---- dye colours (DyeColor ids, = Bedrock's minecraft:color index) ------------------------------

inline constexpr int kDyeCount = 16;
inline constexpr int kBrown    = 12;

/// DyeColor#getName by id ("white" .. "black"); "" outside 0..15.
[[nodiscard]] std::string_view dyeName(int id);
/// The id of a dye name, nullopt when unknown.
[[nodiscard]] std::optional<int> dyeByName(std::string_view name);
/// The DyeColor of a Java / Bedrock dye item ("minecraft:red_dye"), nullopt for other items.
[[nodiscard]] std::optional<int> dyeOfItem(std::string_view itemId);
/// "minecraft:<colour>_wool".
[[nodiscard]] std::string woolOf(int dye);

/// Sheep#getOffspringColor: the dye a crafting grid holding one dye of each parent makes (vanilla's
/// two-dye shapeless recipes); nullopt when no recipe matches (the caller picks a parent at random).
[[nodiscard]] std::optional<int> mixDyes(int a, int b);
/// Sheep#getOffspringColor with the fallback draw (`random.nextBoolean() ? a : b`).
[[nodiscard]] int offspringColor(int a, int b, RandomSource& random);

/// Bighorn#getRandomFleeceColor: brown half of the time, else any of the 16 colours.
[[nodiscard]] int randomFleeceColor(RandomSource& random);
/// Bighorn#getDefaultLootTable: the sheared table, or the fleece colour's (white for unknown ids).
[[nodiscard]] std::string bighornLootTable(bool sheared, int color);
/// Sheep#shear: the number of wool items (1 + nextInt(3)).
[[nodiscard]] int shearCount(RandomSource& random);

// ---- variants (TinyBirdVariant / DwarfRabbitVariant registries) -------------------------------

struct Variant {
    std::string              id;       ///< "gloamingforest:blue"
    std::string              texture;  ///< the variant's texture
    bool                     anyBiome = true; ///< no "biomes" field
    std::vector<std::string> biomes;   ///< biome ids ("#tag" entries resolved by the caller)
};
/// TinyBirdVariant#getVariant / DwarfRabbitVariant#getVariant: uniform among the variants whose
/// biome set is empty or holds `biome`; `fallback` when none.
[[nodiscard]] int pickVariant(std::vector<Variant> const& variants, std::string_view biome, int fallback, RandomSource& random);
/// DwarfRabbitVariant#getRandomCommonVariant: uniform among the variants without a biome set.
[[nodiscard]] int pickCommonVariant(std::vector<Variant> const& variants, int fallback, RandomSource& random);
/// DwarfRabbit#getBreedOffspring: a random common variant 1 time in 20, else one parent's at random.
[[nodiscard]] int rabbitOffspringVariant(std::vector<Variant> const& variants, int self, int mate, int fallback, RandomSource& random);
/// The index of a variant id, nullopt when unknown.
[[nodiscard]] std::optional<int> variantIndex(std::vector<Variant> const& variants, std::string_view id);

// ---- birds ----------------------------------------------------------------------------------

/// Bird's flap state (aiStep; the renderer's bob).
struct BirdFlap {
    float flapLength    = 0.0F;
    float flapIntensity = 0.0F;
    float flapSpeed     = 1.0F;
    /// Bird#aiStep's flap part, once per tick.
    void step(bool onGround);
    /// BirdRenderer#getBob without interpolation: (sin(flapLength) + 1) * flapIntensity.
    [[nodiscard]] float bob() const;
};

/// FlyingBird#customServerAiStep's new target position: x + rand(7) - rand(7), y + rand(6) - (2 | 4),
/// z + rand(7) - rand(7) (BlockPos.containing of the bird's position plus those).
[[nodiscard]] std::array<int, 3> nextFlightTarget(double x, double y, double z, int flightTime, RandomSource& random);
/// The steered motion towards the target block (+0.5, +0.1, +0.5): motion + (signum(d) * (0.5, 0.7,
/// 0.5) - motion) * 0.1.
[[nodiscard]] std::array<double, 3> steerTowards(std::array<double, 3> motion, std::array<double, 3> pos, std::array<int, 3> target);
/// The yaw (degrees) of a motion: atan2(z, x) - 90.
[[nodiscard]] float yawOf(double mx, double mz);
/// Mth.wrapDegrees.
[[nodiscard]] float wrapDegrees(float degrees);

// ---- kobold / mosquito ---------------------------------------------------------------------------

/// Bedrock / Java difficulty ids: 0 peaceful, 1 easy, 2 normal, 3 hard.
/// Kobold#difficultyTime: the base eating delay (easy 400, normal 200, hard 100, else 200).
[[nodiscard]] int koboldEatDelay(int difficulty);
/// MosquitoSwarm#doHurtTarget: the hunger duration in seconds (easy 7, hard 30, else 15).
[[nodiscard]] int mosquitoHungerSeconds(int difficulty);

// ---- parrots ---------------------------------------------------------------------------------

/// Parrot#getPitch: (r1 - r2) * 0.2 + 1.
[[nodiscard]] float parrotPitch(RandomSource& random);
/// LivingEntity#getVoicePitch of an adult: (r1 - r2) * 0.2 + 1; babies (r1 - r2) * 0.2 + 1.5.
[[nodiscard]] float voicePitch(RandomSource& random, bool baby);

} // namespace gf::core::fauna
