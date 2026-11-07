// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/LabyrinthStructure.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent maths of the labyrinth (U23): LabyrinthStructure#getStructureTerraformer,
// the mound over the maze with its two crossing entrance trenches, and the small helpers of the
// maze layout (the room and dead end rules of MinotaurMazeComponent).

#include "core/GlobalPos.h"

namespace gf::core::labyrinth {

/// BoundingBox#getCenter.
struct IPos {
    int x = 0, y = 0, z = 0;
};
[[nodiscard]] IPos boxCenter(BlockBox const& box);

/// HollowHillFunction(centerX, bottomY, centerZ, radius, heightScale) at a block (float maths with
/// the Mth.cos table, as TF).
[[nodiscard]] double hollowHill(float centerX, float bottomY, float centerZ, float radius, float heightScale, int x, int y, int z);
/// FocusedDensityFunction(centerX, bottomY, centerZ, radius, near, far) at a block.
[[nodiscard]] double focused(float centerX, float bottomY, float centerZ, float radius, float nearValue, float farValue, int x, int y, int z);
/// AbsoluteDifferenceFunction.Min / Max(max, centerX, centerZ) at a block.
[[nodiscard]] double absDiffMin(double max, double centerX, double centerZ, int x, int z);
[[nodiscard]] double absDiffMax(double max, double centerX, double centerZ, int x, int z);
/// DensityFunctions.yClampedGradient(fromY, toY, fromValue, toValue) at height y.
[[nodiscard]] double yClampedGradient(int fromY, int toY, double fromValue, double toValue, int y);

/// LabyrinthStructure#getStructureTerraformer at (x, y, z) for the structure start's box.
[[nodiscard]] double terraformer(BlockBox const& structureBox, int x, int y, int z);

/// The radius the terraformer reaches around the box center (35, the mound's radius).
inline constexpr float kMoundRadius = 35.0F;

/// MinotaurMazeComponent: 22 x 22 cells of 5 blocks, radius 55, diameter 110.
inline constexpr int kMazeSize     = 22;
inline constexpr int kMazeRadius   = static_cast<int>(kMazeSize * 2.5);
inline constexpr int kMazeDiameter = kMazeSize * 5;
inline constexpr int kRooms        = 7;

/// MinotaurMazeComponent#getSeed: minX * 90342903L + minY * 90342903L ^ minZ.
[[nodiscard]] long long mazeSeed(BlockBox const& box);

/// MinotaurMazeComponent#isNearRoom(dx, dz, rcoords, range) over the first `count` room pairs.
[[nodiscard]] bool isNearRoom(int dx, int dz, int const* rcoords, int count, int range);

/// The piece type of room `i` of a maze level (MinotaurMazeComponent#makeRoom).
[[nodiscard]] char const* roomType(int index, int level);
/// The piece type of dead end decoration `decorationType` (nextInt(8); case 2 reads the extra
/// boolean `tripwire`).
[[nodiscard]] char const* deadEndType(int decorationType, bool tripwire, int level);
/// The piece type of corridor decoration `decorationType` (nextInt(5)); nullptr: none.
[[nodiscard]] char const* corridorType(int decorationType, int level);

// ---- mob rules ----------------------------------------------------------------------------------------

/// The ATTACK_DAMAGE modifier of a held axe: AxeItem.createAttributes(tier, 6, -3.x) = 6 + the
/// tier's bonus (golden axe and gold minotaur axe 6, diamond minotaur axe 9; 0 for anything else).
[[nodiscard]] float weaponModifier(char const* itemId);
/// Minotaur#populateDefaultEquipmentSlots: "twilightforest:gold_minotaur_axe" when
/// (int)(roll / (effectiveDifficulty + 1)) == 0 (roll = nextInt(10)), else "minecraft:golden_axe".
[[nodiscard]] char const* minotaurAxe(int roll, float effectiveDifficulty);
/// MazeSlime#setSize: MAX_HEALTH size^2 with the double_health modifier (+2 x base) = 3 size^2.
[[nodiscard]] float mazeSlimeHealth(int size);
/// Slime#setSize: MOVEMENT_SPEED 0.2 + 0.1 size, ATTACK_DAMAGE size; MazeSlime xp size + 3.
[[nodiscard]] float mazeSlimeSpeed(int size);
[[nodiscard]] int   mazeSlimeXp(int size);
/// Slime#finalizeSpawn's size from nextInt(3) = `roll` and the boost (raised once when roll < 2).
[[nodiscard]] int mazeSlimeSize(int roll, bool boosted);
/// GroundAttackGoal: the slam damage (attack damage x 0.5).
[[nodiscard]] float slamDamage(float attackDamage);
/// SlimeBlock#stepOn as a walking-speed factor: a walker on a block of friction `friction` moves each
/// tick by v' = (v + a) x friction x 0.91 at steady state; stepOn also multiplies the speed by
/// 0.4 + |dy| x 0.2 (dy: the speed bounceUp leaves on the ground, gravity's 0.0784). The factor is the
/// ratio of both steady speeds, (1 - k) / (1 - k x m) with k = friction x 0.91 and m the stepOn factor
/// (about 0.39 on the slime block's 0.8). Used for players whose movement the server cannot change.
[[nodiscard]] float slimeStepSpeedFactor(float friction);

} // namespace gf::core::labyrinth
