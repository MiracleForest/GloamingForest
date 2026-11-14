// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U25 (stronghold): the trophy pedestal and the stronghold shield (Blocks.cpp).

class Block;
class BlockPos;
class BlockSource;

namespace gf::feature::stronghold::blocks {

inline constexpr char kPedestal[] = "gloamingforest:trophy_pedestal";
inline constexpr char kShield[]   = "gloamingforest:stronghold_shield";

/// TrophyPedestalBlock#neighborChanged at `pos` (true: the pedestal activated now).
bool neighborChanged(BlockSource& region, BlockPos const& pos);
/// StrongholdShieldBlock#getDestroyProgress's face rule: whether mining from `face` (FacingID)
/// can break the shield.
[[nodiscard]] bool canBreakShieldFrom(Block const& shield, int face);

void install();

} // namespace gf::feature::stronghold::blocks
