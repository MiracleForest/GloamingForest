// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/lich/TowerMath.h"

#include <numbers>

namespace gf::core::lich {

namespace {
float const* sinTable() {
    static std::array<float, 65536> const table = [] {
        std::array<float, 65536> t{};
        for (int i = 0; i < 65536; ++i) t[i] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return t;
    }();
    return table.data();
}
} // namespace

float mthSin(float v) { return sinTable()[static_cast<int>(v * 10430.378F) & 65535]; }
float mthCos(float v) { return sinTable()[static_cast<int>(v * 10430.378F + 16384.0F) & 65535]; }

std::vector<CandleSpot> bossRoomCandles(int cx, int cy, int cz) {
    jrand::LegacyRandom random(0);
    random.setSeed(blockPosAsLong(cx, cy, cz));
    std::vector<CandleSpot> out;
    float                   angle = 0.0F;
    for (int i = 0; i < 20; ++i) {
        angle += random.nextFloat(); // the angle grows each time: candles on all sides of the room
        float range = clampedLerp(7.0F, 11.0F, random.nextFloat());
        int   x     = javaRound(mthCos(angle) * range);
        int   z     = javaRound(mthSin(angle) * range);
        int   y     = random.nextInt(3);
        out.push_back({x, y, z});
    }
    return out;
}

} // namespace gf::core::lich
