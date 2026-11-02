// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/landmarks/LegacyLandmarkPlacements.java
//   src/main/java/twilightforest/util/iterators/XZQuadrantIterator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/landmark/Grid.h"

#include <cmath>
#include <limits>

namespace gf::core::landmark {

// ---- java.util.Random ----------------------------------------------------------------------------

namespace {
constexpr std::uint64_t kMultiplier = 0x5DEECE66DULL;
constexpr std::uint64_t kAddend     = 0xBULL;
constexpr std::uint64_t kMask       = (1ULL << 48) - 1;
} // namespace

void JavaRandom::setSeed(std::int64_t seed) { mSeed = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kMask; }

int JavaRandom::next(int bits) {
    mSeed = (mSeed * kMultiplier + kAddend) & kMask;
    // (int)(seed >>> (48 - bits)) on the 48-bit value: the top `bits` bits, as a signed int
    return static_cast<int>(static_cast<std::int64_t>(mSeed) >> (48 - bits));
}

int JavaRandom::nextInt() { return next(32); }

int JavaRandom::nextInt(int bound) {
    if (bound <= 0) return 0;
    if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
    int bits, val;
    do {
        bits = next(31);
        val  = bits % bound;
    } while (static_cast<int>(static_cast<unsigned>(bits) - static_cast<unsigned>(val) + static_cast<unsigned>(bound - 1)) < 0);
    return val;
}

std::int64_t JavaRandom::nextLong() {
    auto hi = static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32))) << 32;
    return static_cast<std::int64_t>(hi + static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32))));
}

float JavaRandom::nextFloat() { return static_cast<float>(next(24)) * (1.0f / static_cast<float>(1 << 24)); }

double JavaRandom::nextDouble() {
    // BitRandomSource#nextDouble: Minecraft multiplies by (double)1.110223E-16F, a float constant
    // (not java.util.Random's 2^-53)
    auto hi = static_cast<std::int64_t>(next(26)) << 27;
    return static_cast<double>(hi + next(27)) * static_cast<double>(1.110223E-16F);
}

// ---- grid helpers --------------------------------------------------------------------------------

int roundRegion(int chunk) {
    // Math.round(float): floor(a + 0.5), exact for every float in double arithmetic
    float  a = static_cast<float>(chunk) / 16.0f;
    double r = std::floor(static_cast<double>(a) + 0.5);
    if (r >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (r <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return static_cast<int>(r);
}

bool blockNearCenter(int blockX, int blockZ, int range) {
    for (int x = -range; x <= range; x++) {
        for (int z = -range; z <= range; z++) {
            if (chunkHasCenter(blockX >> ((4 + x) & 31), blockZ >> ((4 + z) & 31))) return true;
        }
    }
    return false;
}

bool blockIsInCenter(int blockX, int blockZ) { return chunkHasCenter(blockX >> 4, blockZ >> 4); }

ColumnXZ biomeSampleColumn(int chunkX, int chunkZ) {
    auto edge = [](int chunk) {
        auto c = static_cast<unsigned>(roundRegion(chunk)) * 16u;
        return static_cast<int>((c << 4) + 8u);
    };
    return {edge(chunkX), edge(chunkZ)};
}

std::vector<std::pair<std::string, std::string>> const& biomeLandmarks() {
    // BIOME_2_STRUCTURES (DENSE_MUSHROOM_FOREST -> MUSHROOM_TOWER is commented out in TF)
    static std::vector<std::pair<std::string, std::string>> const rows = {
        {"twilightforest:enchanted_forest", "twilightforest:quest_grove"},
        {"twilightforest:lake", "twilightforest:quest_island"},
        {"twilightforest:swamp", "twilightforest:labyrinth"},
        {"twilightforest:fire_swamp", "twilightforest:hydra_lair"},
        {"twilightforest:dark_forest", "twilightforest:knight_stronghold"},
        {"twilightforest:dark_forest_center", "twilightforest:dark_tower"},
        {"twilightforest:snowy_forest", "twilightforest:yeti_cave"},
        {"twilightforest:glacier", "twilightforest:aurora_palace"},
        {"twilightforest:highlands", "twilightforest:troll_cave"},
        {"twilightforest:final_plateau", "twilightforest:final_castle"},
    };
    return rows;
}

std::string biomeLandmark(std::string const& biomeId) {
    for (auto const& [biome, landmark] : biomeLandmarks())
        if (biome == biomeId) return landmark;
    return {};
}

std::vector<std::pair<std::string, int>> const& varietyLandmarks() {
    static std::vector<std::pair<std::string, int>> const list = {
        {"twilightforest:small_hollow_hill", 6},
        {"twilightforest:medium_hollow_hill", 3},
        {"twilightforest:large_hollow_hill", 1},
        {"twilightforest:hedge_maze", 2},
        {"twilightforest:naga_courtyard", 2},
        {"twilightforest:lich_tower", 2},
    };
    return list;
}

std::vector<ColumnXZ> centerScanner(int blockX, int blockZ, int gridSearchRadius) {
    // new XZQuadrantIterator<>((x >> 4) & ~0b1111, (z >> 4) & ~0b1111, false, radius, 16, getNearestCenterXZ)
    int const xConstant = (blockX >> 4) & ~0b1111;
    int const zConstant = (blockZ >> 4) & ~0b1111;
    int const radius    = gridSearchRadius;
    int const spacing   = 16;
    int       dX = 0, dZ = 0, cardinal = -1; // skipCenter = false

    auto hasNext  = [&] { return dX + 1 <= radius; };
    auto generate = [&](int ox, int oz) {
        return nearestCenter(static_cast<int>(static_cast<unsigned>(xConstant) + static_cast<unsigned>(ox)),
                             static_cast<int>(static_cast<unsigned>(zConstant) + static_cast<unsigned>(oz)));
    };
    auto tickXZ = [&] {
        if (!hasNext()) return;
        if (dZ < radius) {
            dZ++;
        } else {
            dZ = 1;
            dX++;
        }
    };

    std::vector<ColumnXZ> out;
    while (hasNext()) {
        switch (cardinal) {
        case 0:
            cardinal++;
            out.push_back(generate(dZ * spacing, -dX * spacing));
            break;
        case 1:
            cardinal++;
            out.push_back(generate(-dX * spacing, -dZ * spacing));
            break;
        case 2:
            cardinal++;
            out.push_back(generate(-dZ * spacing, dX * spacing));
            break;
        default: { // -1 or 3
            cardinal = 0;
            auto ret = generate(dX * spacing, dZ * spacing);
            tickXZ();
            out.push_back(ret);
            break;
        }
        }
    }
    return out;
}

bool isUnimplementedKey(std::string const& key) {
    return key == "twilightforest:quest_island" || key == "twilightforest:druid_grove" || key == "twilightforest:floating_ruins"
        || key == "twilightforest:world_tree";
}

} // namespace gf::core::landmark
