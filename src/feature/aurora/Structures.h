// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's landmarks: the aurora palace and the mushroom tower (layouts shared by the generators and
// the selftests).

#include "feature/structure/bases/Component.h"

class Block;
class Player;

namespace gf::feature::aurora {

inline constexpr char kAuroraPalace[]  = "gloamingforest:aurora_palace";
inline constexpr char kMushroomTower[] = "gloamingforest:mushroom_tower";

/// AuroraPalaceStructure#getFirstPiece + LandmarkStructure#generatePieces at start (x, y, z).
void layoutAuroraPalace(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                        structure::bases::PieceList& pieces);
/// MushroomTowerStructure#getFirstPiece + LandmarkStructure#generatePieces at start (x, y, z).
void layoutMushroomTower(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                         structure::bases::PieceList& pieces);

/// The aurora block with upstream's texture variant at (x, y, z) (NoiseVaryingModel: core::aurora::calcVariant),
/// as the `gloamingforest:variant` state; nullptr before the block registry exists.
[[nodiscard]] Block const* auroraBlockAt(int x, int y, int z);
/// Gives every plain aurora block in `piece` and `sbb` the variant of its position (the pieces call it
/// at the end of postProcess).
void applyAuroraVariants(structure::Region& region, structure::BlockBox const& piece, structure::BlockBox const& sbb);
/// The aurora block's placement variant and its 10-tick mining with progress_glacier.
void installAuroraBlocks();
/// Selftests: whether the player's 10-tick aurora block mining is running.
[[nodiscard]] bool isMiningAuroraBlockForTest(Player const& player);

} // namespace gf::feature::aurora
