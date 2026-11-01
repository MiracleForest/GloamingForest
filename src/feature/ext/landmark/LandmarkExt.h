// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/asmhooks/WorldgenHooks.java
//   src/main/java/twilightforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/util/WorldUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Extension of the "landmark" contract (rule R18, folded by A8 in W2b). The contract header
// (feature/landmark/Landmark.h) has no entry point for these, which other units need:
//
//   - terrain (U2): the structure terraformers summed into the density, like TF's beardifier hook
//     (WorldgenHooks#gatherCustomTerrain / getCustomDensity), and the surface height a landmark
//     uses to adjust its elevation (DecorationClearance#adjustForTerrain);
//   - features (F5): AvoidLandmarkModifier at block precision (DecorationKind has no vegetation and
//     no additional clearance);
//   - structure units (U18-U30): the terraformer shapes TF's structures use (inverted pyramid,
//     box density);
//   - maps / commands (U13, U14): locating landmarks (WorldUtil#findNearestMapLandmark) and the
//     protected boxes the client weather renderer draws (U15, StructureProtectionPacket).
//
// Persisted pieces: a start's pieces are saved as Java's StructurePiece#createTag does, one object
// per piece: {"id": type id, "BB": [minX, minY, minZ, maxX, maxY, maxZ], "O": 2D direction data
// value (S 0, W 1, N 2, E 3, -1 none), "GD": gen depth, "gf_mirror", "gf_rotation"} plus the
// piece's own Piece::save() fields merged in; gf::structure::loadPiece receives that object.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/landmark/Landmark.h"

namespace gf::landmark::ext {

// ---- terrain -----------------------------------------------------------------------------------------

/// The sum of the terraformers (Generator::terraform and addTerraformer) of every start whose box
/// covers column (x, z) of `dimension`: add it to the terrain density (beardifier equivalent).
[[nodiscard]] double terraformDensity(int dimension, int x, int y, int z);
/// Whether any start with a terraformer covers chunk (chunkX, chunkZ) (skip terraformDensity when not).
[[nodiscard]] bool hasTerraformers(int dimension, int chunkX, int chunkZ);
/// The first occupied height of a column for landmark elevation (ChunkGenerator#getFirstOccupiedHeight
/// with WORLD_SURFACE_WG). Registered by the terrain provider; until then landmarks sit at sea level.
using HeightFn = std::function<std::optional<int>(int dimension, int x, int z)>;
void setHeightProvider(HeightFn fn);

/// CustomDensitySource#getInvertedPyramidTerraformer for `start` at a position.
[[nodiscard]] double invertedPyramid(Start const& start, int yOffset, int horizontalPadding, int x, int y, int z);
/// BoxDensityFunction#combine(boxes, dYMin, dYMax, adjustment) at a position (adjustment: "bury",
/// "beard_thin", "beard_box", "encapsulate", "none").
[[nodiscard]] double boxDensity(std::vector<BlockBox> const& boxes, int dYMin, int dYMax, std::string const& adjustment, int x, int y, int z);

// ---- features ----------------------------------------------------------------------------------------

/// What a placed feature occupies (AvoidLandmarkModifier codec fields).
struct Occupancy {
    bool                     surface             = false;
    bool                     underground         = false;
    bool                     vegetation          = false;
    int                      additionalClearance = 0;
    std::vector<std::string> structuresAllowed; ///< structure keys the feature may overlap
};
/// AvoidLandmarkModifier#getPositions: whether a feature at (x, y, z) is suppressed by a start
/// referencing its chunk.
[[nodiscard]] bool featureBlocked(int dimension, int x, int y, int z, Occupancy const& occupancy);

// ---- spawns ------------------------------------------------------------------------------------------

/// EntityEvents#gatherPotentialSpawns: the controlled spawn list (SpawnerData JSON objects
/// {"type", "weight", "minCount", "maxCount"}) for `category` ("monster", "ambient",
/// "water_creature", ...) at a position; nullopt to keep the biome's list.
[[nodiscard]] std::optional<std::vector<nlohmann::json>> potentialSpawns(int dimension, std::string const& category, int x, int y, int z);

// ---- locating ----------------------------------------------------------------------------------------

struct Located {
    std::string key;
    GlobalPos   pos; ///< landmark center / locate position (y 0 for grid landmarks)
};
/// ChunkGenerator#findNearestMapStructure with TF's hook, for structure `key` (or any member of tag
/// "#twilightforest:landmark") around (x, z): first vanilla's ring search over the random spread
/// sets (`searchRadius` rings of their spacing; the first ring with a hit returns), else
/// WorldUtil#findNearestMapLandmark over the landmark grid sets (`searchRadius` cells of 16
/// chunks, as TF passes vanilla's radius on). /locate passes 100.
[[nodiscard]] std::optional<Located> locate(int dimension, std::string const& keyOrTag, int x, int z, int searchRadius, bool skipKnownStructures = false);

#ifdef GF_CLIENT
namespace client {
/// The structure boxes the server last sent (StructureProtectionPacket): (box, protected) pairs;
/// empty for "all clear". TFWeatherRenderer draws the protection rain inside them.
[[nodiscard]] std::vector<std::pair<BlockBox, bool>> structureProtection();
/// The area protection boxes shown now (AreaProtectionPacket), with ticks left.
[[nodiscard]] std::vector<std::pair<BlockBox, int>> areaProtection();
} // namespace client
#endif

} // namespace gf::landmark::ext
