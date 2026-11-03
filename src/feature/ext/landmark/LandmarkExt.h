// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/asmhooks/WorldgenHooks.java
//   src/main/java/twilightforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/util/WorldUtil.java
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java
//   src/main/java/twilightforest/world/components/structures/CustomDensitySource.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Extension of the "landmark" contract (rule R18, folded by A8 in W2b). The contract header
// (feature/landmark/Landmark.h) has no entry point for these, which other units need:
//
//   - terrain (U2): the structure terraformers summed into the density, like TF's beardifier hook
//     (WorldgenHooks#gatherCustomTerrain / getCustomDensity), and the column heights a landmark
//     uses to adjust its elevation (DecorationClearance#adjustForTerrain, WorldUtil#adjustForTerrain);
//   - structure layouts (U18-U30): Structure.GenerationContext#random() and the terrain height
//     queries of the findGenerationPoint / adjustForTerrain overrides;
//   - features (F5): AvoidLandmarkModifier at block precision (DecorationKind has no vegetation and
//     no additional clearance);
//   - structure units (U18-U30): the terraformer shapes TF's structures use (inverted pyramid,
//     box density);
//   - maps / commands (U13, U14): locating landmarks (WorldUtil#findNearestMapLandmark) and the
//     protected boxes the client weather renderer draws (U15, StructureProtectionPacket);
//   - structure units and the structure kit's commands (U18-U30, F7's structure_util display_pieces
//     and count_template): a start's pieces with their type ids and saved tags
//     (StructureStart#getPieces; the contract's Start holds their boxes only).
//
// Persisted pieces: a start's pieces are saved as Java's StructurePiece#createTag does, one object
// per piece: {"id": type id, "BB": [minX, minY, minZ, maxX, maxY, maxZ], "O": 2D direction data
// value (S 0, W 1, N 2, E 3, -1 none), "GD": gen depth, "gf_mirror", "gf_rotation"} plus the
// piece's own Piece::save() fields merged in; gf::structure::loadPiece receives that object. A piece
// whose type has no factory yet (a W1 unit's, before it registers) keeps the tag it was saved with:
// it is written back unchanged and startPieces returns it.
//
// Reserved sites: before a structure unit registers its generator, F6 stores a start for every site
// its structure sets choose, without pieces (Start::generated false, the clearance square as its
// box). Such a start is no StructureStart#isValid start: protection, conquest, terraforming, the
// controlled spawns and spawn overrides, featureBlocked and landmark::decorationAllowed skip it
// (landmark::startAt and startsIntersecting still list it, without pieceBoxes). Once the generator
// exists, the pieces are rebuilt from its deterministic layout when the start chunk is evaluated
// again or a chunk the site reaches generates.
//
// Selftests remove the starts they force with feature/ext/landmark/LandmarkTest.h (ScopedStart,
// forgetStartForTest), which also describes the gf_test fixture landmarks.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"
#include "feature/landmark/Landmark.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::landmark::ext {

// ---- terrain -----------------------------------------------------------------------------------------

/// WorldgenHooks#gatherCustomTerrain + getCustomDensity for one chunk: the terraformers
/// (Generator::terraform and addTerraformer) of every start referencing chunk (chunkX, chunkZ) of
/// `dimension` (its box intersects the chunk: ChunkGenerator#createReferences), gathered once; the
/// function returns their sum at a position of that chunk, the whole chunk included (TF evaluates
/// a start's terraformer everywhere in the chunks it references, not only inside its box). Add it
/// to the terrain density (beardifier equivalent). Empty function: no terraformer reaches the chunk.
using ChunkTerraformer = std::function<double(int x, int y, int z)>;
[[nodiscard]] ChunkTerraformer chunkTerraformer(int dimension, int chunkX, int chunkZ);
/// chunkTerraformer(dimension, x >> 4, z >> 4) at one position. The chunk's terraformer is gathered
/// once and kept per thread while the queries stay in that chunk (and no start, generator or
/// terraformer changes), so per-cell calls cost one lookup; chunkTerraformer per chunk is still the
/// cheapest.
[[nodiscard]] double terraformDensity(int dimension, int x, int y, int z);
/// Whether any start with a terraformer references chunk (chunkX, chunkZ) (skip the terraformers when not).
[[nodiscard]] bool hasTerraformers(int dimension, int chunkX, int chunkZ);

/// The first occupied height of a column (ChunkGenerator#getFirstOccupiedHeight: the heightmap's
/// base height - 1) for heightmap `type`. Registered by the terrain provider (U2); until then the
/// height queries below answer nullopt and landmarks sit at sea level.
using HeightFn = std::function<std::optional<int>(int dimension, int x, int z, worldgen::Heightmap type)>;
void setHeightProvider(HeightFn fn);
/// ChunkGenerator#getFirstOccupiedHeight(x, z, type, ...) through the registered provider.
[[nodiscard]] std::optional<int> firstOccupiedHeight(int dimension, int x, int z, worldgen::Heightmap type = worldgen::Heightmap::WorldSurfaceWg);
/// WorldUtil#adjustForTerrain(context, xMin, zMin, xMax, zMax, gridLength, type): the weighted height
/// of a gridLength x gridLength sample grid over the area (FallenTrunkStructure, TemplatePoolInstance).
[[nodiscard]] std::optional<int> adjustForTerrain(int dimension, int xMin, int zMin, int xMax, int zMax, int gridLength, worldgen::Heightmap type);
/// WorldUtil#adjustForTerrain(context, xInCenterChunk, zInCenterChunk, radiusFromCenterChunk,
/// gridLength): the chunk of (x, z) widened by the radius, WORLD_SURFACE_WG (CampStructure 12, 3; the
/// lich tower 32, 4, hedge maze 24, 4 and naga courtyard 40, 4 (+2) are applied by F6 itself).
[[nodiscard]] std::optional<int> adjustForTerrain(int dimension, int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength);
/// ChunkGenerator#getSeaLevel of a dimension (0 in the Twilight Forest).
[[nodiscard]] int seaLevel(int dimension);

// ---- layouts -----------------------------------------------------------------------------------------
//
// A Generator::layout runs Java's Structure#findGenerationPoint and the GenerationStub's piece
// generator in one call. Its LayoutContext::random is LandmarkStructure#findGenerationPoint's
// getFirstPiece random, RandomSource.create(seed + chunkX * 25117L + chunkZ * 151121L). Everything
// Java draws from Structure.GenerationContext#random() instead (LandmarkStructure
// #generateFromStartingPiece's addChildren, the lich tower's jigsaws and yard, the
// findGenerationPoint overrides of CampStructure, HollowTreeStructure and FallenTrunkStructure)
// comes from contextRandom(ctx.start): create it once at the start of the layout and use that one
// source for both parts, as Java shares one GenerationContext. LayoutContext::start arrives with
// center (x, y, z) set the way LandmarkStructure#findGenerationPoint sets it (the chunk origin + 7
// with center_in_chunk, y by the structure's adjustForTerrain); a layout that finds its own point
// (camps, hollow trees, fallen trunks) writes it to start.center, which is then the position
// Structure#findValidGenerationPoint checks the biome at.

/// Structure.GenerationContext#makeRandom(seed, chunkPos): WorldgenRandom(LegacyRandomSource(0))
/// with setLargeFeatureSeed(seed, chunkX, chunkZ), for the start's seed and start chunk
/// (start.data["chunk"]; else the chunk of start.center).
[[nodiscard]] std::unique_ptr<core::jrand::WorldgenRandom> contextRandom(Start const& start);

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

// ---- pieces ------------------------------------------------------------------------------------------

/// A piece of a stored start (StructureStart#getPieces), in placement order.
struct StartPiece {
    std::string type; ///< the piece type id (StructurePieceType: "twilightforest:TFHill", ...)
    BlockBox    box;
    /// StructurePiece#createTag (the persisted shape above): from the piece in memory, else the tag it
    /// was last saved with (its type has no factory yet), else {"id", "BB"}.
    nlohmann::json tag;
    /// Whether protection covers the piece, as isProtectedFor and the structure protection sync decide
    /// it: Generator::pieceProtected at the piece's box center when the generator has one, else the
    /// piece's own ProgressionPiece#isComponentProtected.
    bool           protectedPiece = true;
    /// The piece's spawn list index (-1: none), as potentialSpawns picks it: Generator::spawnIndex at
    /// the piece's box center when the generator has one, else the piece's own
    /// SpawnIndexProvider#getSpawnIndex. (Per position the rule may differ inside one piece: ask
    /// potentialSpawns for a position.)
    int            spawnIndex     = -1;
};
/// The pieces of `start` with their type ids and saved tags: the stored start of its key, dimension
/// and start chunk (start.data["chunk"]); none for a reserved site. A Start the store does not hold
/// gives its pieceBoxes, with type "" and tag {"BB"}; its flags are then unknown and set to "nothing
/// claimed" (protectedPiece false, spawnIndex -1): a caller building protection or spawns from them
/// skips pieces whose type is empty. landmark::startAt's start carries the same tags in
/// data["pieces"] (the one-start lookup of commands: F7's structure_util reads them there);
/// startsIntersecting's starts do not (it runs per feature placement): ask this.
///
/// Cost of landmark::startAt: it saves every piece of the start it finds (StructurePiece#createTag, a
/// JSON object per piece) on each call. It is meant for one-off lookups on the server thread
/// (commands, tests, an interaction). Per-tick or per-block callers (a player's position every tick,
/// a feature placement) use startsIntersecting(dim, chunk) and Start::box / pieceBoxes, and call
/// startPieces only for the start they act on.
[[nodiscard]] std::vector<StartPiece> startPieces(Start const& start);

// ---- spawns ------------------------------------------------------------------------------------------

/// EntityEvents#gatherPotentialSpawns: the controlled spawn list (SpawnerData JSON objects
/// {"type", "weight", "minCount", "maxCount"}) for `category` ("monster", "ambient",
/// "water_creature", ...) at a position; nullopt to keep the biome's list. For a ControlledSpawns
/// start reaching the position's chunk: the ambient / water lists, an empty list for the other
/// non-monster categories, and for monsters (not once conquered) the labelled list of the spawn
/// index at the position: Generator::spawnIndex when the generator has one (a landmark's own lookup:
/// the dark tower's floors), else EntityEvents#getSpawnListIndexAt (the highest
/// structure::bases::Component::getSpawnIndex of the pieces holding the position); -1: nullopt.
///
/// Hollow hills (U18): HollowHillStructure#canSpawnMob is applied here, keyed on the structure type
/// as Java's `instanceof` is: for a start whose descriptor type is twilightforest:hollow_hill, a
/// position outside the half dome of the start box (core/landmark/Spawning.h,
/// hollowHillCanSpawnMob(start.box, x, y, z)) gets nullopt before the spawn index is asked, so the
/// hill's empty "full" spawn override applies there. U18 registers no hook for it: its layout gives
/// the HollowHillComponent (a bases::Component, spawn index 0) TF's box, so the start box (the union
/// of the pieces, terrain adaptation none) is Java's start.getBoundingBox(), and it leaves
/// Generator::spawnIndex unset (that would replace the piece lookup).
[[nodiscard]] std::optional<std::vector<nlohmann::json>> potentialSpawns(int dimension, std::string const& category, int x, int y, int z);

// ---- locating ----------------------------------------------------------------------------------------

struct Located {
    std::string key;
    GlobalPos   pos; ///< landmark center / locate position (y 0 for grid landmarks)
};
/// ChunkGenerator#findNearestMapStructure with TF's hook, for structure `key` (or "#" and a
/// structure tag of gf::tags, e.g. "#twilightforest:landmark": any member with a landmark
/// descriptor) around (x, z): first vanilla's ring search over the random spread
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
