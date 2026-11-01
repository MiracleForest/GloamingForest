// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded into the contract by A8
// in W2b). What TF's pieces do with a WorldGenLevel that the contract's Piece / Region do not
// carry:
//   - LiquidSettings.IGNORE_WATERLOGGING on a template placement (LichTowerWingRoom U21 sets it;
//     a pool entry's ignore_world_waterlog sets it on jigsaw pieces): template blocks then keep
//     their own waterlogged state instead of taking the world's water;
//   - a waterlogged state set by code (state.setValue(WATERLOGGED, true)): Bedrock keeps the water
//     in the block's liquid layer, which a Block permutation cannot carry (Piece::placeBlock with
//     the id form and {"waterlogged": true} does the same);
//   - WorldGenRegion#getRandom (world.getRandom()): the region's own random, separate from the
//     piece's.

#include <string>
#include <vector>

#include "feature/structure/Structure.h"

namespace gf::structure::ext {

/// The TemplateSettings / bases::PlaceSettings processors entry that stands for
/// setLiquidSettings(LiquidSettings.IGNORE_WATERLOGGING) (it is not a processor).
inline constexpr char kIgnoreWaterlogging[] = "gf:liquid_settings/ignore_waterlogging";
/// setLiquidSettings(IGNORE_WATERLOGGING) / (APPLY_WATERLOGGING) on a settings' processor list.
void setIgnoreWaterlogging(std::vector<std::string>& processors, bool ignore);
[[nodiscard]] bool ignoresWaterlogging(std::vector<std::string> const& processors);

/// Water in (true) or out of (false) the liquid layer of the block at `pos` (Java: the block's
/// WATERLOGGED property). During generation the water goes in when the chunk is loaded.
void setWaterlogged(Region& region, BlockPos const& pos, bool waterlogged);
/// Level#getFluidState(pos) is a water source (the block or its liquid layer).
[[nodiscard]] bool isWaterSource(Region& region, BlockPos const& pos);

/// WorldGenRegion#getRandom: the region's random (world.getRandom() in TF's pieces and features):
/// Minecraft's Xoroshiro128++ RandomState random "minecraft:worldgen_region_random" at the
/// region's center chunk, bit for bit (the TF dimension's noise settings use the Xoroshiro source).
/// Everything decorating the same region draws from the same sequence.
[[nodiscard]] RandomSource& worldRandom(Region& region);
/// A new WorldGenRegion starts: its random starts over (Java makes one per region). F5's region
/// (one per chunk decoration) calls this when it is created, so a region object that reuses the
/// address of an earlier one for the same chunk does not continue the old sequence; the kit's own
/// regions (BlockSourceRegion, the selftests' memory regions) do it themselves.
void regionStarted(Region const& region);

} // namespace gf::structure::ext
