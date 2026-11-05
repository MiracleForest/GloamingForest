// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/HydraLairComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// HydraLairComponent ("twilightforest:TFHydra", U24): a HollowHillComponent of size 2 with the
// hydra lair's own postProcess (domes of stone and grass, the speleothem lattice, the boss
// spawner at local (27, 3, 27)).

#include <cstdint>
#include <memory>
#include <vector>

#include "feature/structure/bases/HollowHill.h"

class BlockSource;

namespace gf::feature::hydra {

class HydraLair : public structure::bases::HollowHill {
public:
    using Region       = structure::Region;
    using RandomSource = structure::RandomSource;
    using Piece        = structure::Piece;
    using Json         = structure::Json;

    /// HydraLairComponent(i, x, y, z, config): HollowHillComponent(TFHydra, i, 2, x, y + 2, z).
    HydraLair(int genDepth, int x, int y, int z);

    void addChildren(Piece& start, std::vector<std::unique_ptr<Piece>>& pieces, RandomSource& random) override;
    void postProcess(Region& region, RandomSource& random, core::BlockBox const& chunkBox, BlockPos const& pivot) override;

private:
    void generateSpeleothems(Region& region, RandomSource& random, core::BlockBox const& writeableBounds);
};

/// Builds the lair piece of a start at (x, y, z) and post-processes it into `region`, writing only
/// inside `clip` (selftests: the golden volume hash). Returns the piece's box.
core::BlockBox placeLairForTest(BlockSource& region, core::BlockBox const& clip, int x, int y, int z, std::int64_t seed);

} // namespace gf::feature::hydra
