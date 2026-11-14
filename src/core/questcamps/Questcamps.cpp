// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/passive/QuestRam.java
//   src/main/java/gloamingforest/client/model/entity/QuestRamModel.java
//   src/main/java/gloamingforest/world/components/feature/templates/TemplateFeature.java
//   src/main/java/gloamingforest/world/components/feature/templates/GraveyardFeature.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/questcamps/Questcamps.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace gf::core::questcamps {

namespace {
// DyeColor's textureDiffuseColor values (1.21.1)
constexpr std::array<unsigned, 16> kDiffuse = {0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
                                               0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};
} // namespace

int dyeId(std::string_view name) {
    for (int i = 0; i < 16; ++i)
        if (kDyeNames[static_cast<std::size_t>(i)] == name) return i;
    return -1;
}

unsigned textureDiffuse(int id) { return id >= 0 && id < 16 ? kDiffuse[static_cast<std::size_t>(id)] : 0xFFFFFFu; }

unsigned sheepColor(int id) {
    if (id == 0) return 0xE6E6E6u; // -1644826
    unsigned c = textureDiffuse(id);
    auto     f = [](unsigned v) { return static_cast<unsigned>(std::floor(static_cast<float>(v) * 0.75F)); };
    return f((c >> 16) & 0xFF) << 16 | f((c >> 8) & 0xFF) << 8 | f(c & 0xFF);
}

int countColors(int flags) { return std::popcount(static_cast<unsigned>(flags)); }

ModelPose modelPose(int flags) {
    ModelPose p;
    int       count = countColors(flags);
    p.count         = count;
    p.headZ         = static_cast<float>(-count - 11);
    p.neckZ         = static_cast<float>(-count - 11);
    p.frontTorsoZ   = static_cast<float>(-count);
    p.backTorsoZ    = static_cast<float>(count);
    p.backLegZ      = static_cast<float>(9 + count);
    p.frontLegZ     = static_cast<float>(-11 - count);
    int segmentOffset = 0;
    for (int color : kColorOrder) {
        auto i = static_cast<std::size_t>(color);
        if (colorPresent(flags, color)) {
            p.visible[i]  = true;
            p.segmentZ[i] = segmentOffset - count;
            segmentOffset += 2;
        } else {
            p.visible[i] = false;
        }
    }
    return p;
}

std::optional<Ground> averageGround(std::vector<int> const& heights, bool mathRound) {
    if (heights.empty()) return std::nullopt;
    // StatsAccumulator: Welford's running mean and sum of squared deltas
    double mean = 0, m2 = 0;
    long   n    = 0;
    int    max  = heights.front();
    for (int h : heights) {
        ++n;
        double delta = h - mean;
        mean += delta / static_cast<double>(n);
        m2 += delta * (h - mean);
        max = std::max(max, h);
    }
    double deviation = n == 1 ? 0.0 : std::sqrt(std::max(0.0, m2) / static_cast<double>(n));
    if (deviation > 2.0) return std::nullopt;
    Ground g;
    g.mean      = mean;
    g.deviation = deviation;
    g.maxY      = max;
    // Math.round(double) is floor(x + 0.5); (int)(mean + 0.5) truncates toward zero (same for x >= 0)
    g.baseY = mathRound ? static_cast<int>(std::floor(mean + 0.5)) : static_cast<int>(mean + 0.5);
    return g;
}

Vec3i rotatedSize(Vec3i size, Rot rotation) {
    if (rotation == Rot::Cw90 || rotation == Rot::Ccw90) return Vec3i{size.z, size.y, size.x};
    return size;
}

Vec3i rotate(Vec3i p, Rot rotation) {
    switch (rotation) {
    case Rot::Cw90: return Vec3i{-p.z, p.y, p.x};
    case Rot::Cw180: return Vec3i{-p.x, p.y, -p.z};
    case Rot::Ccw90: return Vec3i{p.z, p.y, -p.x};
    default: return p;
    }
}

GraveGrid graveGrid(Vec3i transformedSize, Vec3i transformedGraveSize, Rot rotation, Mir mirror) {
    GraveGrid g;
    g.size      = Vec3i{transformedSize.x - 1, transformedSize.y, transformedSize.z - 1};
    g.graveSize = Vec3i{transformedGraveSize.x - 1, transformedGraveSize.y, transformedGraveSize.z - 1};
    g.start     = Vec3i{1, 1, 0};
    Vec3i end{g.start.x + g.size.x, g.start.y, g.start.z + g.size.z};
    Vec3i inner{g.start.x + 2, g.start.y, g.start.z + 2};
    Vec3i bound{end.x - 2, end.y, end.z - 2};
    g.innerSize = Vec3i{bound.x - inner.x, bound.y - inner.y, bound.z - inner.z};
    int fx = (rotation == Rot::Cw180 ? g.graveSize.x : 0) + (mirror == Mir::FrontBack ? transformedGraveSize.x - 1 : 0) * (rotation == Rot::Cw180 ? -1 : 1);
    int fz = (rotation == Rot::Ccw90 ? g.graveSize.z : 0) + (mirror == Mir::FrontBack ? transformedGraveSize.z - 1 : 0) * (rotation == Rot::Ccw90 ? -1 : 1);
    g.fixed = Vec3i{inner.x + fx, inner.y, inner.z + fz};
    Vec3i fixedSize{g.innerSize.x - g.graveSize.x, g.innerSize.y, g.innerSize.z - g.graveSize.z};
    int   stepX = (rotation == Rot::Cw90 || rotation == Rot::Ccw90) ? 2 : 5;
    int   stepZ = (rotation == Rot::None || rotation == Rot::Cw180) ? 2 : 5;
    for (int x = 0; x <= fixedSize.x; x += stepX)
        for (int z = 0; z <= fixedSize.z; z += stepZ) {
            if (x == g.innerSize.x / 2 || z == g.innerSize.z / 2) continue;
            g.cells.emplace_back(x, z);
        }
    return g;
}

} // namespace gf::core::questcamps
