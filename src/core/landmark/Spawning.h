// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/HollowHillStructure.java
//   src/main/java/gloamingforest/events/EntityEvents.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The landmarks' controlled spawn areas (F6, pure, gtested): EntityEvents#gatherPotentialSpawns asks
// HollowHillStructure#canSpawnMob before it picks a hollow hill's monster list, so its monsters spawn
// in the half dome of the start box only (feature/landmark/Starts.cpp, ext::potentialSpawns).

#include "core/GlobalPos.h"

namespace gf::core::landmark {

/// HollowHillStructure#canSpawnMob(spawnPos, structureStartBox): "cuts" the start box into a half
/// dome. Each axis is Mth.inverseLerp(float) of the position between the box's min and max (x and z
/// scaled to -1..1, y kept at 0..1, in float), and the position counts while Mth.length(hX, hY, hZ)
/// (double: 1.21.1's only three-argument overload) < 0.975f. A box one block wide on an axis divides
/// by zero there, as in Java.
[[nodiscard]] bool hollowHillCanSpawnMob(BlockBox const& startBox, int x, int y, int z);

} // namespace gf::core::landmark
