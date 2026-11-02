// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/placements/LandmarkGridPlacement.java
//   src/main/java/twilightforest/world/components/structures/placements/AvoidLandmarkGridPlacement.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Structure set placements of TF's structure sets (F6, pure, gtested): the two TF placement types
// twilightforest:landmark_grid and twilightforest:avoid_landmark_grid, and the vanilla parts they
// build on (StructurePlacement frequency reduction and exclusion zone, RandomSpreadStructurePlacement
// potential chunks). Read from the "placement" object of a structure set JSON.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::core::landmark::placement {

using Json = nlohmann::json;

struct ChunkXZ {
    int  x = 0, z = 0;
    friend bool operator==(ChunkXZ const&, ChunkXZ const&) = default;
};

enum class Kind { LandmarkGrid, AvoidLandmarkGrid, RandomSpread, Unknown };
/// RandomSpreadType.
enum class Spread { Linear, Triangular };
/// StructurePlacement.FrequencyReductionMethod.
enum class Reduction { Default, LegacyType1, LegacyType2, LegacyType3 };

/// StructurePlacement.ExclusionZone.
struct ExclusionZone {
    std::string otherSet;
    int         chunkCount = 0;
};

/// A structure set's placement.
struct Placement {
    Kind        kind = Kind::Unknown;
    std::string type;     ///< the JSON type id
    // landmark_grid
    std::string gridLock; ///< structure_grid_lock ("" = every landmark center)
    // StructurePlacement
    int                          locateOffset[3] = {0, 0, 0};
    Reduction                    reduction       = Reduction::Default;
    float                        frequency       = 1.0f;
    int                          salt            = 0;
    std::optional<ExclusionZone> exclusionZone;
    // RandomSpreadStructurePlacement
    int    spacing    = 0;
    int    separation = 0;
    Spread spread     = Spread::Linear;
    // AvoidLandmarkGridPlacement.AvoidAdditionalStructures (structure set -> chunk range)
    std::vector<std::pair<std::string, int>> avoidAdditional;

    /// Parses a structure set's "placement" object (unknown types: Kind::Unknown).
    [[nodiscard]] static Placement fromJson(Json const& placement);
};

/// Looks up the placement of another structure set (exclusion zones, avoided structures).
using SetLookup = std::function<Placement const*(std::string const& setId)>;
/// Chooses the variety landmark of a center chunk (LandmarkGridPlacement's lock test).
using VarietyFn = std::function<std::string(int chunkX, int chunkZ)>;

/// RandomSpreadStructurePlacement#getPotentialStructureChunk.
[[nodiscard]] ChunkXZ potentialStructureChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ);
/// The placement's own isPlacementChunk (LandmarkGridPlacement: a landmark center whose variety
/// pick is the lock; AvoidLandmarkGridPlacement: the random spread chunk at least 80 blocks from
/// the nearest landmark center on one axis; vanilla random spread).
[[nodiscard]] bool isPlacementChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, VarietyFn const& variety);
/// StructurePlacement#applyAdditionalChunkRestrictions (frequency reduction).
[[nodiscard]] bool applyAdditionalChunkRestrictions(Placement const& p, std::int64_t seed, int chunkX, int chunkZ);
/// StructurePlacement#isStructureChunk: placement chunk, frequency, exclusion zone and (avoid
/// landmark grid) the avoided structure sets.
[[nodiscard]] bool isStructureChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, VarietyFn const& variety, SetLookup const& sets, int depth = 0);
/// ChunkGeneratorStructureState#hasStructureChunkInRange.
[[nodiscard]] bool hasStructureChunkInRange(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, int range, VarietyFn const& variety, SetLookup const& sets, int depth = 0);
/// StructurePlacement#getLocatePos (the chunk's minimum corner plus locate_offset), x / z.
[[nodiscard]] std::pair<int, int> locatePos(Placement const& p, int chunkX, int chunkZ);

/// ChunkGenerator#createStructures for a set of several structures: tries them in vanilla's weighted
/// order (WorldgenRandom#setLargeFeatureSeed(seed, chunkX, chunkZ)) until `tryOne(index)` succeeds;
/// a single structure is tried alone. Returns the index that succeeded (-1: none).
int tryWeighted(std::int64_t seed, int chunkX, int chunkZ, std::vector<int> const& weights, std::function<bool(int index)> const& tryOne);

} // namespace gf::core::landmark::placement
