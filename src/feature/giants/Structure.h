// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U29 (giants): gloamingforest:giant_house's layout (Structure.cpp), for the generator and the
// selftests.

#include "feature/structure/Structure.h"
#include "feature/structure/bases/Component.h"

namespace gf::feature::giants {

/// The cloud castle's piece type (U27's CloudCastleComponent).
inline constexpr char kCloudCastlePiece[] = "gloamingforest:GloamingClCa";
/// The saved form of CloudCastleComponent(1, x, y, z).
[[nodiscard]] structure::Json cloudCastleTag(int x, int y, int z);
/// Whether the cloud castle piece type is registered (U27 merged).
[[nodiscard]] bool cloudCastleAvailable();
/// LandmarkStructure#generatePieces of giant_house at start (x, y, z): the castle at y + 168 and
/// its children (`contextRandom` = Structure.GenerationContext#random()). False without the pieces.
bool layoutGiantHouse(structure::RandomSource& contextRandom, int x, int y, int z, structure::bases::PieceList& pieces);
/// Registers the F6 generator once the piece type exists.
void registerGiantHouse();

} // namespace gf::feature::giants
