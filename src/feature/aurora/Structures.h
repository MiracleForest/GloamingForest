// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's landmarks: the aurora palace and the mushroom tower (layouts shared by the generators and
// the selftests).

#include "feature/structure/bases/Component.h"

namespace gf::feature::aurora {

inline constexpr char kAuroraPalace[]  = "twilightforest:aurora_palace";
inline constexpr char kMushroomTower[] = "twilightforest:mushroom_tower";

/// AuroraPalaceStructure#getFirstPiece + LandmarkStructure#generatePieces at start (x, y, z).
void layoutAuroraPalace(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                        structure::bases::PieceList& pieces);
/// MushroomTowerStructure#getFirstPiece + LandmarkStructure#generatePieces at start (x, y, z).
void layoutMushroomTower(structure::RandomSource& firstPieceRandom, structure::RandomSource& contextRandom, int x, int y, int z,
                         structure::bases::PieceList& pieces);

} // namespace gf::feature::aurora
