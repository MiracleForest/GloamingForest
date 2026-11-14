#pragma once

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/ai/goal/CubeMoveToRedstoneSymbolsGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/CubeCenterOnSymbolGoal.java
//   src/main/java/gloamingforest/entity/monster/Adherent.java (performRangedAttack)
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBossGazeboComponent.java
//   src/main/java/gloamingforest/util/BoundingBoxUtils.java (vectorsMinMax)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the pure rules of the castle's mobs and of the gazebo's WIP texts (no game
// types; gtested in tests/finalcastle). The feature code (src/feature/finalcastle/Mob*.cpp,
// Wip*.cpp) asks the world through the callbacks.

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace gf::core::finalcastle {

struct IPos {
    int  x = 0, y = 0, z = 0;
    bool operator==(IPos const&) const = default;
};
struct DVec {
    double x = 0.0, y = 0.0, z = 0.0;
};

// ---- roving cube ---------------------------------------------------------------------------------

/// CubeMoveToRedstoneSymbolsGoal#isRedstoneSymbol: a loaded empty block whose six neighbours
/// (Direction.values(): down, up, north, south, west, east) are all redstone wire. upstream's comment
/// speaks of "8 redstone dust around a blank square", but the check asks every direction, so a
/// flat ring never matches (kept as upstream has it).
[[nodiscard]] bool isRedstoneSymbol(IPos pos, std::function<bool(IPos)> const& loaded, std::function<bool(IPos)> const& empty,
                                    std::function<bool(IPos)> const& wire);

/// CubeMoveToRedstoneSymbolsGoal#searchForRedstoneSymbol: x and z in -16..15, y in -5..4 around
/// the cube's block position (x outermost, then z, then y); the first symbol found.
[[nodiscard]] std::optional<IPos> searchForRedstoneSymbol(IPos cube, std::function<bool(IPos)> const& isSymbol);

/// CubeCenterOnSymbolGoal#distanceFromSymbol: from the cube's feet to the symbol block's centre
/// column at its floor (symbol - position + 0.5 on x / z).
[[nodiscard]] double distanceFromSymbol(IPos symbol, double x, double y, double z);
/// CubeCenterOnSymbolGoal#isCloseToSymbol: 0.25 < distance < 10.
[[nodiscard]] bool isCloseToSymbol(double distance);
/// CubeCenterOnSymbolGoal#canContinueToUse's test: distance > 0.1 and isCourseTraversable (< 100).
[[nodiscard]] bool keepsCentering(double distance);
/// The position CubeCenterOnSymbolGoal steers to (setWantedPosition(x + 0.5, y, z + 0.5)).
[[nodiscard]] DVec centerTarget(IPos symbol);

// ---- adherent --------------------------------------------------------------------------------------

/// Adherent#performRangedAttack's shot: the direction passed to NatureBolt#shoot, its velocity and
/// inaccuracy. `boltY` is the bolt's start height (the owner's eyes - 0.1, GloamingThrowable), `targetY`
/// the target's feet, `targetEye` its eye height (getEyeHeight), `difficultyId` Difficulty#getId
/// (0 peaceful .. 3 hard).
struct Shot {
    DVec  direction;
    float velocity   = 0.6F;
    float inaccuracy = 0.0F;
};
[[nodiscard]] Shot adherentShot(DVec shooter, double boltY, DVec targetFeet, double targetEye, int difficultyId);

/// RangedAttackGoal's attack timer: after a shot (Mth.floor(f * (max - min) + min), f the distance
/// over the radius) and when it is reset (Mth.floor(Mth.lerp(dist / radius, min, max))).
[[nodiscard]] int rangedIntervalAfterShot(double distanceSqr, float radius, int intervalMin, int intervalMax);
[[nodiscard]] int rangedIntervalOnReset(double distanceSqr, float radius, int intervalMin, int intervalMax);

// ---- the gazebo's WIP texts ------------------------------------------------------------------------

struct WipLine {
    char const* text;
    float       yOffset; ///< added to the block's y (Java float, widened to double)
};
/// FinalCastleBossGazeboComponent#willBeAddingFinalBossSoon: the four lines, top to bottom.
inline constexpr std::array<WipLine, 4> kWipLines{{
    {"Final Castle WIP.", 2.3F},
    {"Join our Discord server to", 1.3F},
    {"follow development of the mod:", 1.0F},
    {"discord.experiment115.com", 0.7F},
}};

/// The text positions at block (bx, by, bz): (bx + 0.5, by + yOffset, bz + 0.5).
[[nodiscard]] std::vector<DVec> wipTextPositions(IPos block);

struct Box {
    double minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    [[nodiscard]] bool contains(DVec const& p) const {
        return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY && p.z >= minZ && p.z <= maxZ;
    }
};
/// BoundingBoxUtils#vectorsMinMax (nullopt for no vectors).
[[nodiscard]] std::optional<Box> vectorsMinMax(std::vector<DVec> const& vectors, double expand);

/// The punch box (upstream's Interaction tagged final_castle_wip): width (xsize + zsize) * 0.5, height
/// ysize, at (centre x, minY, centre z).
struct Punch {
    DVec   pos;
    double width  = 0.0;
    double height = 0.0;
    /// The interaction's own box (width around pos.x / pos.z, height up from pos.y).
    [[nodiscard]] Box box() const {
        return Box{pos.x - width / 2, pos.y, pos.z - width / 2, pos.x + width / 2, pos.y + height, pos.z + width / 2};
    }
};
[[nodiscard]] std::optional<Punch> punchFor(std::vector<DVec> const& textPositions);

} // namespace gf::core::finalcastle
