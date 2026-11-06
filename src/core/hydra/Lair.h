// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/HydraLairStructure.java
//   src/main/java/twilightforest/world/components/structures/HydraLairComponent.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/world/components/structures/util/TriangularLatticeConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent maths of the hydra lair (U24): the structure terraformer
// (HydraLairStructure#getStructureTerraformer with HollowHillFunction and FocusedDensityFunction)
// and the speleothem lattice of its speleothem config (structure_speleothem_settings/hydra_lair:
// spacing 4.5, x_offset 3.897186, x_spacing 7.794372, z_offset 2.2498753, z_spacing 4.5).

#include <vector>

#include "core/GlobalPos.h"

namespace gf::core::hydra {

/// BoundingBox#getCenter.
struct IPos {
    int x = 0, y = 0, z = 0;
};
[[nodiscard]] IPos boxCenter(BlockBox const& box);

/// HollowHillFunction(center + 0.5, radius, heightScale) at a block.
[[nodiscard]] double hollowHillFunction(IPos const& center, float radius, float heightScale, int x, int y, int z);
/// FocusedDensityFunction(center + 0.5, radius, near, far) at a block.
[[nodiscard]] double focusedDensity(IPos const& center, float radius, float nearValue, float farValue, int x, int y, int z);

/// HydraLairStructure#getStructureTerraformer: the density the lair adds at (x, y, z), for the
/// structure start's box (8 x lair; positive fills, negative carves).
[[nodiscard]] double lairTerraformer(BlockBox const& structureBox, int x, int y, int z);

/// TriangularLatticeConfig#boundedGrid of the hydra lair's lattice over `bounds` at height `y`:
/// the positions in Java's order (two zipped rectangle lattices).
[[nodiscard]] std::vector<IPos> lairLattice(BlockBox const& bounds, int y);

/// The hydra lair's speleothem chances (twilight/stalactites/hydra_lair).
inline constexpr float kStalactiteChance = 1.0F;
inline constexpr float kStalagmiteChance = 0.0625F;

} // namespace gf::core::hydra
