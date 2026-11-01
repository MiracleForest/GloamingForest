// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/HollowHillComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18).
// HollowHillComponent ("twilightforest:TFHill"): the hollow hill interior (water drained, a
// hexagonal lattice of stalactites, stalagmites, chests and spawners from a
// structure_speleothem_settings config). The hollow hill landmark and the yeti cave (U27) build on it.

#include <string>

#include "feature/structure/bases/ComponentOld.h"

namespace gf::structure::bases {

class HollowHill : public ComponentOld {
public:
    /// HollowHillComponent(type, i, size, x, y, z, config): hill size 1..3 (small, medium, large),
    /// oriented SOUTH; `speleothemConfigId` names a twilight/structure_speleothem_settings entry.
    HollowHill(std::string typeId, int genDepth, int size, int x, int y, int z, std::string speleothemConfigId);

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    /// "hillSize", "config_id" plus ComponentOld's fields.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    /// drainWater: replaces liquids inside the circle (radiusSq around the center) with `airBlock`
    /// down to `maxDepth`, and the liquid bottom with `undergroundBlock`.
    static void drainWater(BlockBox const& chunkBox, BlockBox const& structureBox, Region& region, int maxDepth, Block const& airBlock, int xCenter,
                           int zCenter, double radiusSq, Block const& undergroundBlock);
    [[nodiscard]] static float getDistSqFromCenter(BlockPos const& center, BlockPos const& to);

    /// The spawner mobs of each hill level (entity ids, consuming `random` as Java).
    [[nodiscard]] virtual std::string getLevel1Mob(RandomSource& random) const;
    [[nodiscard]] virtual std::string getLevel2Mob(RandomSource& random) const;
    [[nodiscard]] virtual std::string getLevel3Mob(RandomSource& random) const;

protected:
    /// generateSpeleothem: a stalactite (`hanging`) or stalagmite at `pos` with its own RNG
    /// (seed + x * z), skipped on spawners.
    void generateSpeleothem(Region& region, BlockPos const& pos, BlockBox const& sbb, bool hanging);
    [[nodiscard]] std::string getMobID(RandomSource& random) const;
    [[nodiscard]] std::string getMobID(RandomSource& random, int level) const;

    int         hillSize = 1;
    int         radius   = 0; ///< (hillSize * 2 + 1) * 8 - 6
    int         hdiam    = 0; ///< (hillSize * 2 + 1) * 16
    std::string speleothemConfigId;
};

} // namespace gf::structure::bases
