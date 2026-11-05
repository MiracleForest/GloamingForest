#pragma once

// U1 `biomes`, client build: the unit's internal interface to its tints (Tints.cpp), for its
// selftests. Other units use feature/ext/biomes/Biomes.h.

#ifdef GF_CLIENT

#include <span>

#include "feature/ext/ui/Client.h"

class BlockPos;
class BlockSource;

namespace gf::client::biomes {

/// TF's grass / foliage colour at `pos`: with `region`, the biomes of the blend's columns read from
/// it; without, recognised by the game's colour (see Tints.cpp).
[[nodiscard]] unsigned grassAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game);
[[nodiscard]] unsigned foliageAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game);

/// The vanilla blocks the tints are registered for.
[[nodiscard]] std::span<char const* const> grassBlocks();
[[nodiscard]] std::span<char const* const> foliageBlocks();

} // namespace gf::client::biomes

#endif
