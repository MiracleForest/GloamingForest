// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/landmarks/LegacyLandmarkPlacements.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The pure landmark grid (contract "landmark", F6). Java's int and long arithmetic wraps on
// overflow: core/landmark/JavaMath.h computes in unsigned types so the C++ results stay bit exact.
#include "core/landmark/Landmark.h"

#include <cstdlib>

#include "core/landmark/Grid.h"
#include "core/landmark/JavaMath.h"

namespace gf::core::landmark {

using java::iadd;
using java::imul;
using java::ishl;
using java::ladd;
using java::lmul;

ColumnXZ nearestCenter(int chunkX, int chunkZ) {
    // generate random number for the whole biome area
    int regionX = iadd(chunkX, 8) >> 4;
    int regionZ = iadd(chunkZ, 8) >> 4;

    std::int64_t seed = lmul(regionX, 3129871LL) ^ lmul(regionZ, 116129781LL);
    seed              = ladd(lmul(lmul(seed, seed), 42317861LL), lmul(seed, 7LL));

    int num0 = static_cast<int>((seed >> 12) & 3LL);
    int num1 = static_cast<int>((seed >> 15) & 3LL);
    int num2 = static_cast<int>((seed >> 18) & 3LL);
    int num3 = static_cast<int>((seed >> 21) & 3LL);

    // slightly randomize center of biome (+/- 3)
    int centerX = 8 + num0 - num1;
    int centerZ = 8 + num2 - num3;

    // centers are offset strangely depending on +/-
    int ccz;
    if (regionZ >= 0) {
        ccz = iadd(imul(iadd(imul(regionZ, 16), centerZ - 8), 16), 8);
    } else {
        ccz = iadd(imul(iadd(imul(regionZ, 16), (16 - centerZ) - 8), 16), 9);
    }

    int ccx;
    if (regionX >= 0) {
        ccx = iadd(imul(iadd(imul(regionX, 16), centerX - 8), 16), 8);
    } else {
        ccx = iadd(imul(iadd(imul(regionX, 16), (16 - centerX) - 8), 16), 9);
    }

    return {ccx, ccz};
}

bool chunkHasCenter(int chunkX, int chunkZ) {
    auto c = nearestCenter(chunkX, chunkZ);
    return chunkX == (c.x >> 4) && chunkZ == (c.z >> 4);
}

int manhattanDistanceFromCenter(int chunkX, int chunkZ) {
    auto c      = nearestCenter(chunkX, chunkZ);
    int  deltaX = std::abs(chunkX - (c.x >> 4));
    int  deltaZ = std::abs(chunkZ - (c.z >> 4));
    return deltaX + deltaZ;
}

std::string pickVariety(std::int64_t seed, int chunkX, int chunkZ) {
    // set the chunkX and chunkZ to the center of the biome in case they arent already
    chunkX = imul(roundRegion(chunkX), 16);
    chunkZ = imul(roundRegion(chunkZ), 16);

    int regionOffsetX = std::abs((iadd(chunkX, 64) >> 4) % 8);
    int regionOffsetZ = std::abs((iadd(chunkZ, 64) >> 4) % 8);

    // plant two lich towers near the center of each 2048x2048 map area
    if ((regionOffsetX == 4 && regionOffsetZ == 5) || (regionOffsetX == 4 && regionOffsetZ == 3)) return "twilightforest:lich_tower";

    // also two nagas
    if ((regionOffsetX == 5 && regionOffsetZ == 4) || (regionOffsetX == 3 && regionOffsetZ == 4)) return "twilightforest:naga_courtyard";

    // okay, well that takes care of most special cases: VARIETY_LANDMARKS.getRandomValue(new
    // LegacyRandomSource(seed + chunkX * 25117L + chunkZ * 151121L)) (WeightedRandom#getRandomItem)
    auto const& list  = varietyLandmarks();
    int         total = 0;
    for (auto const& [key, weight] : list) total += weight;
    if (total <= 0) return "twilightforest:small_hollow_hill";
    JavaRandom random(java::chunkSeed(seed, chunkX, chunkZ));
    int        index = random.nextInt(total);
    for (auto const& [key, weight] : list) {
        index -= weight;
        if (index < 0) return key;
    }
    return "twilightforest:small_hollow_hill";
}

std::string pickForChunk(std::int64_t seed, int chunkX, int chunkZ, BiomeAtFn const& biomeAt) {
    // set the chunkX and chunkZ to the center of the biome
    chunkX = imul(roundRegion(chunkX), 16);
    chunkZ = imul(roundRegion(chunkZ), 16);

    // what biome is at the center of the chunk?
    if (biomeAt) {
        auto biome = biomeAt(iadd(ishl(chunkX, 4), 8), iadd(ishl(chunkZ, 4), 8));
        if (!biome.empty()) {
            auto landmark = biomeLandmark(biome);
            if (!landmark.empty()) return landmark;
        }
    }
    return pickVariety(seed, chunkX, chunkZ);
}

} // namespace gf::core::landmark
