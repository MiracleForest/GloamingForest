// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerSegment.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U21 (lich): the pure parts of the lich tower generator (no game types): Java's Mth helpers the
// pieces use, the boss room's candle ring, the segment stair decay level and the room spawner
// weights. gtested in tests/lich.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::lich {

/// BlockPos#asLong (26 / 12 / 26 bits).
[[nodiscard]] inline std::int64_t blockPosAsLong(int x, int y, int z) {
    constexpr std::uint64_t kMaskXZ = (1ull << 26) - 1, kMaskY = (1ull << 12) - 1;
    std::uint64_t           l = 0;
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(x)) & kMaskXZ) << 38;
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(y)) & kMaskY);
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) & kMaskXZ) << 12;
    return static_cast<std::int64_t>(l);
}

/// Mth.SIN table lookups (65536 entries of sin(i * 2pi / 65536) as float).
[[nodiscard]] float mthSin(float v);
[[nodiscard]] float mthCos(float v);
/// Math.round(float): floor(v + 0.5).
[[nodiscard]] inline int javaRound(float v) { return static_cast<int>(std::floor(static_cast<double>(v) + 0.5)); }
/// Mth.clampedLerp(start, end, delta) (float).
[[nodiscard]] inline float clampedLerp(float start, float end, float delta) {
    if (delta < 0.0F) return start;
    if (delta > 1.0F) return end;
    return start + delta * (end - start);
}
/// Mth.ceil(double).
[[nodiscard]] inline int mthCeil(double v) {
    int i = static_cast<int>(v);
    return v > i ? i + 1 : i;
}

/// One candle of LichBossRoom#postProcess: offset from the ring centre.
struct CandleSpot {
    int x = 0, y = 0, z = 0;
};
/// The 20 candle offsets of the boss room around `center` (random seeded with center.asLong()).
[[nodiscard]] std::vector<CandleSpot> bossRoomCandles(int cx, int cy, int cz);

/// LichTowerSegment#stairDecay: the index into the 8 VerticalDecayProcessors (0.025 .. 0.2), or -1
/// for none (Mth.ceil((depth - 3) * 0.5) < 0); capped at 7 (Java caps at the length, 8, which would
/// be out of range; upstream towers never reach it: 15 segments go to depth 16 -> 7).
[[nodiscard]] inline int stairDecayLevel(int genDepth) {
    int level = mthCeil((genDepth - 3) * 0.5);
    if (level < 0) return -1;
    return level > 7 ? 7 : level;
}
/// The decay chance of a level (0.025 * (level + 1)).
[[nodiscard]] inline float stairDecayChance(int level) { return 0.025F * static_cast<float>(level + 1); }

/// LichTowerSegment#buildTowerBySegments: which slices get wings, the gallery and a mob bridge.
struct SegmentPlan {
    bool wings = false, gallery = false, mobBridge = false;
};
/// The plan for `segments` slices; `mobBridgeStart` = nextIntBetweenInclusive(0, 5), `next(a, b)`
/// draws the following countdowns (nextIntBetweenInclusive(2, 5)).
template <class NextFn>
[[nodiscard]] std::vector<SegmentPlan> planSegments(int segments, int mobBridgeStart, NextFn&& next) {
    std::vector<SegmentPlan> out;
    int                      mob = mobBridgeStart;
    for (int i = 0; i < segments; ++i) {
        out.push_back({i > (segments >> 1), i == segments - 1, mob == 0});
        mob = mob == 0 ? next(2, 5) : mob - 1;
    }
    return out;
}

} // namespace gf::core::lich
