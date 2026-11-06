// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Extension added by U2 (terrain), rule R18 (folded by A8 in W2b): NeoForge's
// PieceBeardifierModifier for structure pieces, which the beardifier of the terrain generator asks
// per piece (core/terrain/Beard.h has TF 4.8's answers for its own piece types).
//
// The beardifier resolves a piece of a start (landmark::ext::StartPiece) in this order:
//   1. a function registered here for the piece's type id (a structure unit's piece whose beard
//      depends on something the generic rules cannot see; tests);
//   2. TF 4.8's table (core::terrain::beard::tfPieceBeard: TFStructureComponentOld's NONE, the
//      overrides of the hedge maze, lich tower, dark tower entrance, stronghold, troll cave, final
//      castle dungeon, courtyard terrace and the new lich tower, and TwilightJigsawPiece's
//      "terrain_adaptation" / "ground_offset" tag);
//   3. the loaded piece (structure::loadPiece) when it derives from F7's bases::ComponentOld or
//      bases::JigsawPiece: their getBeardifierBox / getTerrainAdjustment / getGroundLevelDelta;
//   4. otherwise a plain piece: its bounding box with the structure's terrain_adaptation, ground
//      delta 0 (vanilla Beardifier).
//
// Registering is meant for install() (server thread); the beardifier reads the registry on the
// generation threads.

#include <functional>
#include <optional>
#include <string>

#include "core/GlobalPos.h"
#include "feature/ext/landmark/LandmarkExt.h"

namespace gf::terrain::ext {

/// A piece's answer to the beardifier.
struct PieceBeard {
    /// true: the piece is a PieceBeardifierModifier (the fields below); false: a plain piece (its
    /// bounding box with the structure's adaptation, ground delta 0).
    bool          modifier = true;
    std::string   adjustment = "none"; ///< "none", "bury", "beard_thin", "beard_box", "encapsulate" (any case)
    core::BlockBox box;                ///< getBeardifierBox
    int           groundLevelDelta = 0;
};
using PieceBeardFn = std::function<PieceBeard(landmark::ext::StartPiece const& piece)>;

/// Registers (or with an empty function removes) the answer for pieces of `typeId`
/// ("twilightforest:TFHedge"; matched without case).
void registerPieceBeard(std::string const& typeId, PieceBeardFn fn);

/// The beardifier's answer for one piece, resolved as described above.
[[nodiscard]] PieceBeard pieceBeard(landmark::ext::StartPiece const& piece);

} // namespace gf::terrain::ext
