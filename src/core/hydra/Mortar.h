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

} // namespace gf::core::hydra