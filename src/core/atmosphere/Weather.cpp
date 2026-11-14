// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/renderer/GloamingWeatherRenderer.java
//   src/main/java/gloamingforest/util/IntervalUtils.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/atmosphere/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/jrand/JRand.h"

namespace gf::core::atmosphere {

char const* textureOf(Effect effect) {
    switch (effect) {
    case Effect::Blizzard:
        return "textures/gloamingforest/environment/blizzard";
    case Effect::Mosquito:
        return "textures/gloamingforest/environment/mosquitoes";
    case Effect::Ashes:
        return "textures/gloamingforest/environment/ashes";
    case Effect::DarkStream:
        return "textures/gloamingforest/environment/darkstream";
    case Effect::BigRain:
        return "textures/gloamingforest/environment/bigrain";
    case Effect::Sparkles:
        return "textures/gloamingforest/environment/sparkles";
    default:
        return "";
    }
}

char const* nameOf(Effect effect) {
    switch (effect) {
    case Effect::Blizzard:
        return "blizzard";
    case Effect::Mosquito:
        return "mosquito";
    case Effect::Ashes:
        return "ashes";
    case Effect::DarkStream:
        return "dark_stream";
    case Effect::BigRain:
        return "big_rain";
    case Effect::Sparkles:
        return "sparkles";
    default:
        return "none";
    }
}

Effect effectFor(std::string_view enforcement, bool darkRoll) {
    if (enforcement == "gloamingforest:frost") return Effect::Blizzard;
    if (enforcement == "gloamingforest:hunger") return Effect::Mosquito;
    if (enforcement == "gloamingforest:fire") return Effect::Ashes;
    if (enforcement == "gloamingforest:darkness") return darkRoll ? Effect::DarkStream : Effect::None;
    if (enforcement == "gloamingforest:acid_rain") return Effect::BigRain;
    return Effect::None;
}

std::int64_t columnSeed(int x, int z) {
    auto lx = static_cast<std::int64_t>(x), lz = static_cast<std::int64_t>(z);
    // Java longs wrap: unsigned arithmetic, then back
    auto a = static_cast<std::uint64_t>(lx) * static_cast<std::uint64_t>(lx) * 3121u + static_cast<std::uint64_t>(lx) * 45238971u;
    auto b = static_cast<std::uint64_t>(lz) * static_cast<std::uint64_t>(lz) * 418711u + static_cast<std::uint64_t>(lz) * 13761u;
    return static_cast<std::int64_t>(a ^ b);
}

std::pair<float, float> rainOffset(int dx, int dz) {
    // rainxs[i << 5 | j] = -f1 / f2, rainzs = f / f2 with f = j - 16 (x), f1 = i - 16 (z); times 0.5
    float f  = static_cast<float>(dx);
    float f1 = static_cast<float>(dz);
    float f2 = std::sqrt(f * f + f1 * f1);
    if (f2 == 0.0f) return {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    return {-f1 / f2 * 0.5f, f / f2 * 0.5f};
}

namespace {

int floorInt(double v) { return static_cast<int>(std::floor(v)); }

/// renderEffect's quad: corners around the column's centre, v from the column's ends.
Column columnOf(ColumnInput const& in, Effect effect, float countFactor, float uFactor, float vFactor, Rgb color, float alpha, bool fullBright) {
    int  px = floorInt(in.camX), pz = floorInt(in.camZ);
    auto [rainX, rainZ] = rainOffset(in.x - px, in.z - pz);
    Column c;
    c.effect     = effect;
    c.x0         = static_cast<float>(in.x - in.camX - rainX + 0.5);
    c.z0         = static_cast<float>(in.z - in.camZ - rainZ + 0.5);
    c.x1         = static_cast<float>(in.x - in.camX + rainX + 0.5);
    c.z1         = static_cast<float>(in.z - in.camZ + rainZ + 0.5);
    c.y0         = static_cast<float>(in.minY - in.camY);
    c.y1         = static_cast<float>(in.maxY - in.camY);
    c.u0         = 0.0f + uFactor;
    c.u1         = 1.0f + uFactor;
    c.v0         = static_cast<float>(in.minY) * 0.25f + countFactor + vFactor;
    c.v1         = static_cast<float>(in.maxY) * 0.25f + countFactor + vFactor;
    c.color      = color;
    c.alpha      = alpha;
    c.fullBright = fullBright;
    return c;
}

float distanceAlpha(ColumnInput const& in) {
    double xRange = static_cast<double>(static_cast<float>(in.x) + 0.5f) - in.camX;
    double zRange = static_cast<double>(static_cast<float>(in.z) + 0.5f) - in.camZ;
    float  d      = std::sqrt(static_cast<float>(xRange * xRange + zRange * zRange)) / static_cast<float>(in.range);
    return (1.0f - d * d) * 0.3f + 0.5f;
}

} // namespace

std::optional<std::pair<int, int>> lockedSpan(int py, int range, int minBuild) {
    int minY = std::max(py - range, minBuild);
    int maxY = std::max(py + range, minBuild);
    if (minY == maxY) return std::nullopt;
    return std::pair{minY, maxY};
}

std::optional<Column> lockedColumn(ColumnInput const& in, std::string_view enforcement) {
    jrand::LegacyRandom random(0);
    random.setSeed(columnSeed(in.x, in.z));
    bool   dark   = enforcement == "gloamingforest:darkness" && random.nextBoolean();
    Effect effect = effectFor(enforcement, dark);
    if (effect == Effect::None) return std::nullopt;
    if (in.x == floorInt(in.camX) && in.z == floorInt(in.camZ)) return std::nullopt; // NaN corners
    float       combined = static_cast<float>(in.ticks) + in.partialTicks;
    float const alpha    = distanceAlpha(in);
    switch (effect) {
    case Effect::Blizzard: {
        float count = (static_cast<float>(in.ticks & 511) + in.partialTicks) / 512.0f;
        float u     = random.nextFloat();
        u += combined * 0.05f * static_cast<float>(random.nextGaussian());
        float v = random.nextFloat();
        v += combined * 0.0025f * static_cast<float>(random.nextGaussian());
        return columnOf(in, effect, count, u, v, {1, 1, 1}, alpha, true);
    }
    case Effect::Mosquito: {
        float u = random.nextFloat();
        u += combined * 0.03f * static_cast<float>(random.nextGaussian());
        float v = random.nextFloat();
        v += combined * 0.003f * static_cast<float>(random.nextGaussian());
        float r = random.nextFloat() * 0.3f;
        float g = random.nextFloat() * 0.3f;
        float b = random.nextFloat() * 0.3f;
        return columnOf(in, effect, 0.0f, u, v, {r, g, b}, 1.0f, true);
    }
    case Effect::Ashes: {
        float count = -(static_cast<float>(in.ticks & 1023) + in.partialTicks) / 1024.0f;
        float u     = random.nextFloat();
        u += combined * 0.0025f * static_cast<float>(random.nextGaussian());
        float v = random.nextFloat();
        v += combined * 0.005f * static_cast<float>(random.nextGaussian());
        float grey = random.nextFloat() * 0.2f + 0.8f;
        return columnOf(in, effect, count, u, v, {grey, grey, grey}, alpha, true);
    }
    case Effect::DarkStream: {
        float count = -(static_cast<float>(in.ticks & 511) + in.partialTicks) / 512.0f;
        float v     = random.nextFloat();
        v += combined * 0.005f * static_cast<float>(random.nextGaussian());
        return columnOf(in, effect, count, 0.0f, v, {1, 1, 1}, alpha, true); // no moving horizontally
    }
    case Effect::BigRain: {
        // Java ints wrap: (ticks + x*x*3121 + x*45238971 + z*z*418711 + z*13761) & 31
        auto  ux   = static_cast<std::uint32_t>(in.x), uz = static_cast<std::uint32_t>(in.z);
        auto  sum  = static_cast<std::uint32_t>(in.ticks) + ux * ux * 3121u + ux * 45238971u + uz * uz * 418711u + uz * 13761u;
        float count = (static_cast<float>(sum & 31u) + in.partialTicks) / 32.0f;
        count *= 3.0f + random.nextFloat();
        float u = random.nextFloat();
        float v = random.nextFloat();
        return columnOf(in, effect, count, u, v, {1, 1, 1}, alpha, false); // the world's light
    }
    default:
        return std::nullopt;
    }
}

std::optional<Column> sparkleColumn(ColumnInput const& in) {
    if (in.x == floorInt(in.camX) && in.z == floorInt(in.camZ)) return std::nullopt; // NaN corners
    jrand::LegacyRandom random(0);
    random.setSeed(columnSeed(in.x, in.z));
    float combined = static_cast<float>(in.ticks) + in.partialTicks;
    float count    = (static_cast<float>(in.ticks & 511) + in.partialTicks) / 512.0f;
    float u        = random.nextFloat();
    u += combined * 0.02f * static_cast<float>(random.nextGaussian());
    float v = random.nextFloat();
    v += combined * 0.02f * static_cast<float>(random.nextGaussian());
    double xRange = in.x + 0.5 - in.camX;
    double zRange = in.z + 0.5 - in.camZ;
    float  d      = std::sqrt(static_cast<float>(xRange * xRange + zRange * zRange)) / static_cast<float>(in.range);
    float  alpha  = (1.0f - d * d) * 0.3f + 0.5f;
    alpha *= random.nextFloat();
    return columnOf(in, Effect::Sparkles, count, u, v, {1, 1, 1}, alpha, true);
}

std::vector<Interval> mergeAndSortIntervals(std::vector<Interval> intervals) {
    if (intervals.empty()) return intervals;
    std::stable_sort(intervals.begin(), intervals.end(), [](Interval const& a, Interval const& b) { return a.first < b.first; });
    std::vector<Interval> merged;
    Interval              current = intervals.front();
    for (std::size_t i = 1; i < intervals.size(); ++i) {
        auto const& next = intervals[i];
        if (current.second >= next.first) current = {current.first, std::max(current.second, next.second)};
        else {
            merged.push_back(current);
            current = next;
        }
    }
    merged.push_back(current);
    return merged;
}

std::vector<Interval> subtractIntervals(std::vector<Interval> sortedA, std::vector<Interval> const& sortedB) {
    std::vector<Interval> result;
    std::size_t           i = 0, j = 0;
    while (i < sortedA.size() && j < sortedB.size()) {
        auto const a = sortedA[i];
        auto const b = sortedB[j];
        if (a.second <= b.first) {
            result.push_back(a);
            ++i;
        } else if (a.first >= b.second) {
            ++j;
        } else {
            if (a.first < b.first) result.emplace_back(a.first, b.first);
            if (a.second <= b.second) ++i;
            else {
                sortedA[i] = {b.second, a.second};
                ++j;
            }
        }
    }
    while (i < sortedA.size()) result.push_back(sortedA[i++]);
    return result;
}

BlockBox cameraBox(int px, int py, int pz, int range) { return {px - range, py - range, pz - range, px + range, py + 2 * range, pz + range}; }

std::vector<Interval> sparkleIntervals(int x, int z, BlockBox const& pBox, std::vector<std::pair<BlockBox, bool>> const& boxes) {
    std::vector<Interval> prot, unprot;
    for (auto const& [box, isProtected] : boxes) {
        if (!box.intersects(pBox)) continue;
        // BoundingBox#intersects(x, z, x, z): the column only
        if (!(box.maxX >= x && box.minX <= x && box.maxZ >= z && box.minZ <= z)) continue;
        int rainMin = std::max(pBox.minY, box.minY);
        int rainMax = std::min(pBox.maxY, box.maxY + 1); // + 1: renderEffect draws rainMin..rainMax as floats
        if (rainMin < rainMax) (isProtected ? prot : unprot).emplace_back(rainMin, rainMax);
    }
    return subtractIntervals(mergeAndSortIntervals(std::move(prot)), mergeAndSortIntervals(std::move(unprot)));
}

float tickUrGhastRain(float rain, bool alive) { return alive ? std::min(1.0f, rain + 0.1f) : std::max(0.0f, rain - 0.02f); }

float rainLevel(float levelRain, float urGhastRain, bool fancy) { return std::max(levelRain, urGhastRain) / (fancy ? 1.0f : 2.0f); }

int splashCount(float level, bool decreasedParticles) { return static_cast<int>(100.0f * level * level) / (decreasedParticles ? 2 : 1); }

float fluidHeight(int depth, bool sameAbove) {
    if (sameAbove) return 1.0f;
    int amount = depth >= 8 ? 8 : 8 - std::clamp(depth, 0, 7);
    return static_cast<float>(amount) / 9.0f;
}

std::int64_t rainTickSeed(int ticks) { return static_cast<std::int64_t>(ticks) * 312987231LL; }

} // namespace gf::core::atmosphere
