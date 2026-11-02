// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/placements/LandmarkGridPlacement.java
//   src/main/java/twilightforest/world/components/structures/placements/AvoidLandmarkGridPlacement.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The vanilla parts (StructurePlacement, RandomSpreadStructurePlacement, WorldgenRandom seeding)
// follow Minecraft 1.21.1's behaviour, which TF's placements extend.
#include "core/landmark/Placement.h"

#include <cmath>
#include <cstdlib>

#include "core/landmark/Grid.h"
#include "core/landmark/JavaMath.h"
#include "core/landmark/Landmark.h"

namespace gf::core::landmark::placement {

namespace {

using java::ladd;
using java::lmul;

int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

/// WorldgenRandom#setLargeFeatureWithSalt.
void setLargeFeatureWithSalt(JavaRandom& r, std::int64_t levelSeed, int regionX, int regionZ, int salt) {
    r.setSeed(ladd(ladd(ladd(lmul(regionX, 341873128712LL), lmul(regionZ, 132897987541LL)), levelSeed), salt));
}

/// WorldgenRandom#setLargeFeatureSeed.
void setLargeFeatureSeed(JavaRandom& r, std::int64_t baseSeed, int chunkX, int chunkZ) {
    r.setSeed(baseSeed);
    std::int64_t i = r.nextLong();
    std::int64_t j = r.nextLong();
    r.setSeed(lmul(chunkX, i) ^ lmul(chunkZ, j) ^ baseSeed);
}

int evaluate(Spread spread, JavaRandom& r, int limit) {
    if (limit <= 0) return 0;
    if (spread == Spread::Triangular) {
        int a = r.nextInt(limit);
        int b = r.nextInt(limit);
        return (a + b) / 2;
    }
    return r.nextInt(limit);
}

/// The frequency reducers of StructurePlacement (shouldGenerate(levelSeed, salt, regionX, regionZ, p)).
bool shouldGenerate(Reduction method, std::int64_t levelSeed, int salt, int regionX, int regionZ, float probability) {
    JavaRandom r(0);
    switch (method) {
    case Reduction::Default:
        // probabilityReducer passes its arguments on in this order: setLargeFeatureWithSalt(levelSeed,
        // salt, regionX, regionZ), i.e. the salt lands in the regionX slot (vanilla's own order)
        setLargeFeatureWithSalt(r, levelSeed, salt, regionX, regionZ);
        return r.nextFloat() < probability;
    case Reduction::LegacyType1: { // legacyPillagerOutpostReducer
        int i = regionX >> 4;
        int j = regionZ >> 4;
        r.setSeed(static_cast<std::int64_t>(i ^ static_cast<int>(static_cast<unsigned>(j) << 4)) ^ levelSeed);
        (void)r.nextInt();
        int bound = static_cast<int>(1.0f / probability);
        return bound > 0 && r.nextInt(bound) == 0;
    }
    case Reduction::LegacyType2: // legacyArbitrarySaltProbabilityReducer
        setLargeFeatureWithSalt(r, levelSeed, regionX, regionZ, 10387320);
        return r.nextFloat() < probability;
    case Reduction::LegacyType3: // legacyProbabilityReducerWithDouble
        setLargeFeatureSeed(r, levelSeed, regionX, regionZ);
        return r.nextDouble() < static_cast<double>(probability);
    }
    return true;
}

} // namespace

Placement Placement::fromJson(Json const& j) {
    Placement p;
    if (!j.is_object()) return p;
    p.type = j.value("type", std::string{});
    if (p.type == "twilightforest:landmark_grid") p.kind = Kind::LandmarkGrid;
    else if (p.type == "twilightforest:avoid_landmark_grid") p.kind = Kind::AvoidLandmarkGrid;
    else if (p.type == "minecraft:random_spread") p.kind = Kind::RandomSpread;
    p.gridLock = j.value("structure_grid_lock", std::string{});
    if (auto it = j.find("locate_offset"); it != j.end() && it->is_array() && it->size() == 3)
        for (int i = 0; i < 3; ++i) p.locateOffset[i] = (*it)[i].is_number_integer() ? (*it)[i].get<int>() : 0;
    auto method = j.value("frequency_reduction_method", std::string{"default"});
    if (method == "legacy_type_1") p.reduction = Reduction::LegacyType1;
    else if (method == "legacy_type_2") p.reduction = Reduction::LegacyType2;
    else if (method == "legacy_type_3") p.reduction = Reduction::LegacyType3;
    p.frequency = j.value("frequency", 1.0f);
    p.salt      = j.value("salt", 0);
    if (auto it = j.find("exclusion_zone"); it != j.end() && it->is_object())
        p.exclusionZone = ExclusionZone{it->value("other_set", std::string{}), it->value("chunk_count", 0)};
    p.spacing    = j.value("spacing", 0);
    p.separation = j.value("separation", 0);
    p.spread     = j.value("spread_type", std::string{"linear"}) == "triangular" ? Spread::Triangular : Spread::Linear;
    if (auto it = j.find("avoid_additional_structures"); it != j.end() && it->is_object())
        for (auto const& [set, range] : it->items())
            if (range.is_number_integer()) p.avoidAdditional.emplace_back(set, range.get<int>());
    return p;
}

ChunkXZ potentialStructureChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ) {
    if (p.spacing <= 0) return {chunkX, chunkZ};
    int        i = floorDiv(chunkX, p.spacing);
    int        j = floorDiv(chunkZ, p.spacing);
    JavaRandom r(0);
    setLargeFeatureWithSalt(r, seed, i, j, p.salt);
    int k  = p.spacing - p.separation;
    int l  = evaluate(p.spread, r, k);
    int i1 = evaluate(p.spread, r, k);
    return {i * p.spacing + l, j * p.spacing + i1};
}

bool isPlacementChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, VarietyFn const& variety) {
    switch (p.kind) {
    case Kind::LandmarkGrid:
        if (!chunkHasCenter(chunkX, chunkZ)) return false;
        return p.gridLock.empty() || (variety ? variety(chunkX, chunkZ) : pickVariety(seed, chunkX, chunkZ)) == p.gridLock;
    case Kind::AvoidLandmarkGrid: {
        // copy of super
        auto chunk = potentialStructureChunk(p, seed, chunkX, chunkZ);
        if (chunk.x != chunkX || chunk.z != chunkZ) return false;
        // Feature Center, turned into the feature offset from the chunk's corner
        auto center = nearestCenter(chunkX, chunkZ);
        int  offX   = std::abs(center.x - chunkX * 16);
        int  offZ   = std::abs(center.z - chunkZ * 16);
        int  size   = 80;
        return offX >= size || offZ >= size;
    }
    case Kind::RandomSpread: {
        auto chunk = potentialStructureChunk(p, seed, chunkX, chunkZ);
        return chunk.x == chunkX && chunk.z == chunkZ;
    }
    case Kind::Unknown: break;
    }
    return false;
}

bool applyAdditionalChunkRestrictions(Placement const& p, std::int64_t seed, int chunkX, int chunkZ) {
    return !(p.frequency < 1.0f) || shouldGenerate(p.reduction, seed, p.salt, chunkX, chunkZ, p.frequency);
}

bool hasStructureChunkInRange(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, int range, VarietyFn const& variety, SetLookup const& sets, int depth) {
    for (int i = chunkX - range; i <= chunkX + range; ++i)
        for (int j = chunkZ - range; j <= chunkZ + range; ++j)
            if (isStructureChunk(p, seed, i, j, variety, sets, depth)) return true;
    return false;
}

bool isStructureChunk(Placement const& p, std::int64_t seed, int chunkX, int chunkZ, VarietyFn const& variety, SetLookup const& sets, int depth) {
    if (!isPlacementChunk(p, seed, chunkX, chunkZ, variety)) return false;
    if (!applyAdditionalChunkRestrictions(p, seed, chunkX, chunkZ)) return false;
    if (depth > 4) return true; // a cycle of exclusion zones: vanilla would recurse forever
    // applyInteractionsWithOtherStructures: the exclusion zone ...
    if (p.exclusionZone && sets) {
        if (auto const* other = sets(p.exclusionZone->otherSet))
            if (hasStructureChunkInRange(*other, seed, chunkX, chunkZ, p.exclusionZone->chunkCount, variety, sets, depth + 1)) return false;
    }
    // ... and AvoidLandmarkGridPlacement's avoid_additional_structures
    if (p.kind == Kind::AvoidLandmarkGrid && sets) {
        for (auto const& [setId, range] : p.avoidAdditional) {
            if (auto const* other = sets(setId))
                if (hasStructureChunkInRange(*other, seed, chunkX, chunkZ, range, variety, sets, depth + 1)) return false;
        }
    }
    return true;
}

std::pair<int, int> locatePos(Placement const& p, int chunkX, int chunkZ) {
    return {chunkX * 16 + p.locateOffset[0], chunkZ * 16 + p.locateOffset[2]};
}

int tryWeighted(std::int64_t seed, int chunkX, int chunkZ, std::vector<int> const& weights, std::function<bool(int index)> const& tryOne) {
    if (weights.empty()) return -1;
    if (weights.size() == 1) return tryOne(0) ? 0 : -1;
    std::vector<int> remaining;
    int              total = 0;
    for (int i = 0; i < static_cast<int>(weights.size()); ++i) {
        remaining.push_back(i);
        total += weights[i];
    }
    JavaRandom r(0);
    setLargeFeatureSeed(r, seed, chunkX, chunkZ);
    while (!remaining.empty() && total > 0) {
        int j = r.nextInt(total);
        int k = 0;
        for (int idx : remaining) {
            j -= weights[idx];
            if (j < 0) break;
            ++k;
        }
        if (k >= static_cast<int>(remaining.size())) k = static_cast<int>(remaining.size()) - 1;
        int chosen = remaining[k];
        if (tryOne(chosen)) return chosen;
        remaining.erase(remaining.begin() + k);
        total -= weights[chosen];
    }
    return -1;
}

} // namespace gf::core::landmark::placement
