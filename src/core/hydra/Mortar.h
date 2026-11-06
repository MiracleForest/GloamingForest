// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/projectile/HydraMortar.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hydra mortar's hit boxes (U24, pure C++). HydraMortar's EntityDimensions are 0.75 x 0.75 and
// its getPickRadius is 1.5: a player's pick (melee, the crosshair) uses the box inflated by 1.5,
// which the Bedrock entity's custom_hit_test (3.75 x 3.75) carries. A Java projectile
// (ProjectileUtil#getEntityHitResult) only inflates the box by 0.3; Bedrock projectiles use the
// custom hit test too, so the port refuses their hits that would miss the Java box.

#pragma once

#include "core/hydra/Head.h"

namespace gf::core::hydra {

inline constexpr double kMortarSize         = 0.75; ///< HydraMortar's EntityDimensions
inline constexpr double kMortarPickRadius   = 1.5;  ///< HydraMortar#getPickRadius
inline constexpr double kProjectileInflate  = 0.3;  ///< ProjectileUtil#getEntityHitResult's inflation

struct Box {
    V3 min;
    V3 max;
};

/// The mortar's Java box at feet position `pos`, inflated by `inflate`.
[[nodiscard]] Box mortarBox(V3 pos, double inflate);

/// AABB#clip: whether the segment `from` -> `to` meets `box` (a segment starting inside counts).
[[nodiscard]] bool segmentHits(Box const& box, V3 from, V3 to);

struct Segment {
    V3 from;
    V3 to;
};

/// The line of motion of a projectile that Bedrock reports hitting the mortar at `pos` with motion
/// `delta` (`facing`: its look, used when the motion is about 0). A Bedrock projectile stops on the
/// 3.75 pick box, up to about 1.9 blocks before the mortar's centre, and may report its motion
/// before or after its move (or zeroed): the segment runs along the motion (or the look) far enough
/// both ways to cross the whole pick box (pick radius + size + one step), so a projectile aimed at
/// the Java box meets it wherever Bedrock stopped it. Zero motion and zero facing give an empty
/// segment at `pos`.
[[nodiscard]] Segment projectilePath(V3 pos, V3 delta, V3 facing);

/// Entity#getLookAngle of a Java projectile moving `motion`: Projectile#updateRotation (and
/// AbstractArrow#shoot) set yRot = atan2(dx, dz) and xRot = atan2(dy, horizontal) in degrees, the
/// mirror of a mob's convention, so calculateViewVector gives the unit motion with x and y negated.
/// Zero motion gives zero.
[[nodiscard]] V3 projectileLook(V3 motion);

/// Whether a projectile at `pos` moving `delta` (look `facing`) would hit the Java projectile box
/// (0.3 inflation) of the mortar with feet at `mortarFeet`.
[[nodiscard]] bool projectileReachesJavaBox(V3 mortarFeet, V3 pos, V3 delta, V3 facing);

} // namespace gf::core::hydra