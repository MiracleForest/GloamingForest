// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/projectile/ChainBlock.java
//   src/main/java/gloamingforest/entity/projectile/CubeOfAnnihilation.java
//   src/main/java/gloamingforest/entity/projectile/SeekerArrow.java
//   src/main/java/gloamingforest/item/OreMagnetItem.java
//   src/main/java/gloamingforest/item/TripleBowItem.java
//   src/main/java/gloamingforest/item/recipe/MoonwormQueenRepairRecipe.java
//   src/main/java/gloamingforest/util/VoxelBresenhamIterator.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U8 (weapons): the pure math of upstream's weapons and throwables (no game types), gtested in
// tests/weapons.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace gf::core::weapons {

struct V3 {
    double x = 0, y = 0, z = 0;
    friend bool operator==(V3 const&, V3 const&) = default;
};
struct I3 {
    int x = 0, y = 0, z = 0;
    friend auto operator<=>(I3 const&, I3 const&) = default;
};

[[nodiscard]] double length(V3 const& v);
[[nodiscard]] V3     normalize(V3 const& v);
[[nodiscard]] double dot(V3 const& a, V3 const& b);
[[nodiscard]] V3     scale(V3 const& v, double f);
[[nodiscard]] V3     add(V3 const& a, V3 const& b);
/// Vec3#yRot (radians).
[[nodiscard]] V3 yRot(V3 const& v, float angle);
/// Mth.floor of each component (BlockPos.containing).
[[nodiscard]] I3 containing(V3 const& v);

/// Entity#calculateViewVector(xRot, yRot) (degrees): Java's look vector.
[[nodiscard]] V3 viewVector(float xRot, float yRot);
/// OreMagnetItem#getOffsetLook: the look vector turned by yaw / pitch offsets (degrees, Mth float math).
[[nodiscard]] V3 offsetLook(float yRot, float xRot, float yawOffset, float pitchOffset);
/// The order OreMagnetItem#releaseUsing tries (yaw, pitch) offsets until something moved.
[[nodiscard]] std::array<std::pair<float, float>, 9> const& magnetTries();

/// VoxelBresenhamIterator: the block positions from `a` to `b` (both included).
[[nodiscard]] std::vector<I3> voxelLine(I3 a, I3 b);

/// OreMagnetItem#findVein: the connected positions of the ore (`isOre`), at most 24, in Java's
/// recursion order (Direction.values(): down, up, north, south, west, east).
[[nodiscard]] std::set<I3> findVein(I3 start, std::function<bool(I3 const&)> const& isOre, std::size_t limit = 24);

/// BowItem#getPowerForTime.
[[nodiscard]] float bowPower(int useTicks);

/// SeekerArrow's steering: nullopt when the target is too far off course (cosine <= 0.5: give up).
[[nodiscard]] std::optional<V3> seekerSteer(V3 const& course, V3 const& toTarget);
/// SeekerArrow#updateTarget's selection box (min, max) around position `pos` for motion `motion`.
[[nodiscard]] std::pair<V3, V3> seekerBox(V3 const& pos, V3 const& motion);

/// ChainBlock#bounce: the saved velocity after hitting face `face` (FacingID: 0 down 1 up 2 north
/// 3 south 4 west 5 east).
[[nodiscard]] V3 chainBounce(V3 vel, int face);
/// ChainBlock#tick's return velocity.
[[nodiscard]] V3 chainReturn(V3 const& saved, V3 const& toOwnerEye, int tickCount, double gravity);

/// CubeOfAnnihilation#tick: the velocity toward `dest` (0.5 max speed).
[[nodiscard]] V3 cubeVelocity(V3 const& centre, V3 const& dest);

/// MoonwormQueenRepairRecipe#assemble: the new damage value.
[[nodiscard]] int moonwormRepairDamage(int damage, int berries);

/// The box CrumbleHornItem#doCrumble crumbles: [min, max] block positions around `center` (upstream
/// means 4x4x4; encapsulatingFullBlocks + getAllInBB make it center-2..center+2).
[[nodiscard]] std::pair<I3, I3> crumbleBox(I3 center);
/// WorldUtil#getAllInBB of an AABB: the block positions [floor(min), floor(max)] (Java's
/// BlockPos.betweenClosed(floor(minX), ..., floor(maxX), ...)), x fastest.
[[nodiscard]] std::vector<I3> allInBox(V3 const& min, V3 const& max);
/// IceBomb#doTerrainEffects: the offsets of radius `range` (corners skipped), in Java's order.
[[nodiscard]] std::vector<I3> iceBombOffsets(int range);

/// TripleBowItem#shoot: the vertical velocity additions of the three projectiles (j = -1, 0, 1).
[[nodiscard]] std::array<double, 3> tripleBowLift();

} // namespace gf::core::weapons
