// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/HydraLairStructure.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/util/iterators/RectangleLatticeIterator.java
//   src/main/java/twilightforest/util/iterators/ZippedIterator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/hydra/Lair.h"

#include <algorithm>
#include <cmath>

#include "core/jrand/JavaOrder.h"

namespace gf::core::hydra {

namespace {

constexpr float kPi = 3.1415927F;

float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Mth.clampedMap(double, ...).
double clampedMap(double v, double fromMin, double fromMax, double toMin, double toMax) {
    double t = (v - fromMin) / (fromMax - fromMin);
    if (t < 0.0) return toMin;
    if (t > 1.0) return toMax;
    return toMin + t * (toMax - toMin);
}

/// Mth.positiveModulo(float, float).
float positiveModulo(float numerator, float denominator) { return std::fmod(std::fmod(numerator, denominator) + denominator, denominator); }

int floorF(float v) {
    int i = static_cast<int>(v);
    return v < static_cast<float>(i) ? i - 1 : i;
}

struct RectLattice {
    int   yLevel, startX, startZ, countX, countZ;
    float xSpacing, zSpacing, xOffset, zOffset;
    int   lx = 0, lz = 0;

    static int nearestIndex(float spacing, int i) {
        float fi = static_cast<float>(i);
        return floorF((fi - positiveModulo(fi, spacing)) / spacing);
    }
    RectLattice(BlockBox const& b, int y, float xs, float zs, float xo, float zo) : yLevel(y), xSpacing(xs), zSpacing(zs), xOffset(xo), zOffset(zo) {
        startX = nearestIndex(xSpacing, static_cast<int>(static_cast<float>(b.minX) - xOffset));
        startZ = nearestIndex(zSpacing, static_cast<int>(static_cast<float>(b.minZ) - zOffset));
        countX = nearestIndex(xSpacing, static_cast<int>(static_cast<float>(b.maxX + 1) - xOffset)) - startX;
        countZ = nearestIndex(zSpacing, static_cast<int>(static_cast<float>(b.maxZ + 1) - zOffset)) - startZ;
    }
    [[nodiscard]] bool hasNext() const { return lx < countX; }
    IPos               next() {
        IPos p{static_cast<int>(xOffset + static_cast<float>(startX + lx) * xSpacing), yLevel, static_cast<int>(zOffset + static_cast<float>(startZ + lz) * zSpacing)};
        if (lz + 1 < countZ) lz++;
        else {
            lz = 0;
            lx++;
        }
        return p;
    }
};

} // namespace

IPos boxCenter(BlockBox const& b) {
    return {b.minX + (b.maxX - b.minX + 1) / 2, b.minY + (b.maxY - b.minY + 1) / 2, b.minZ + (b.maxZ - b.minZ + 1) / 2};
}

double hollowHillFunction(IPos const& c, float radius, float heightScale, int x, int y, int z) {
    float dX     = static_cast<float>(x) - (static_cast<float>(c.x) + 0.5F);
    float dY     = static_cast<float>(y) - (static_cast<float>(c.y) + 0.5F);
    float dZ     = static_cast<float>(z) - (static_cast<float>(c.z) + 0.5F);
    float dist   = std::sqrt(dX * dX + dZ * dZ);
    float height = jrand::mthCos(dist / radius * kPi) * radius * 0.3333333334F;
    float norm   = clampF(dist / std::fabs(radius), 0.0F, 1.0F);
    if (norm >= 1.0F) return 0.0;
    return clampF(height * heightScale - dY, -1.0F, 1.0F);
}

double focusedDensity(IPos const& c, float radius, float nearValue, float farValue, int x, int y, int z) {
    float dX   = (static_cast<float>(c.x) + 0.5F) - static_cast<float>(x);
    float dY   = (static_cast<float>(c.y) + 0.5F) - static_cast<float>(y);
    float dZ   = (static_cast<float>(c.z) + 0.5F) - static_cast<float>(z);
    float dist = std::sqrt(dX * dX + dY * dY + dZ * dZ);
    return clampedMap(dist, 0.0, radius, nearValue, farValue);
}

double lairTerraformer(BlockBox const& box, int x, int y, int z) {
    constexpr int   hillSize    = 2;
    constexpr float radius      = (hillSize * 4 + 0.8F) * 8;
    constexpr float radiusInner = radius - 8;
    int const       width       = std::min(box.maxX - box.minX + 1, box.maxZ - box.minZ + 1);
    int const       yCeiling    = box.minY;
    IPos const      center      = boxCenter(box);

    double hillMound    = clampD(hollowHillFunction({center.x, yCeiling + 8, center.z}, radius, 0.7F, x, y, z), 0.0, 1.0);
    double innerFloor   = clampedMap(y, -1.0, 0.0, 1.0, -4.0); // DensityFunctions.yClampedGradient(-1, 0, 1, -4)
    double innerCeiling = -1.0 * hollowHillFunction({center.x, yCeiling + 6, center.z}, radiusInner, 0.675F, x, y, z);
    double interior     = std::max(innerCeiling, innerFloor);
    double interiorMask = focusedDensity({center.x, yCeiling, center.z}, radiusInner * 0.52F, -radiusInner, 1.0F, x, y, z);
    double masked       = std::max(interiorMask, interior);
    double hollowHill   = std::min(hillMound, masked);
    int    down         = static_cast<int>(std::ceil(radius * 0.1F)); // Mth.ceil(radius * 0.1)
    double sphere       = clampD(focusedDensity({center.x, center.y - down, center.z}, static_cast<float>(width) * 0.5F + 5, static_cast<float>(width) * 0.25F, 0.0F, x, y, z), 0.0, 1.0);
    double cutout       = clampD(focusedDensity({center.x - 16, center.y, center.z - 16}, 23.0F, -23.0F, 0.0F, x, y, z), -4.0, 0.0);
    double hillMasked   = sphere * hollowHill;
    double lair         = hillMasked + clampD(std::max(cutout, innerFloor), -2.0, 0.0);
    return 8.0 * lair;
}

std::vector<IPos> lairLattice(BlockBox const& bounds, int y) {
    // structure_speleothem_settings/hydra_lair
    constexpr float xSpacing = 7.794372F, zSpacing = 4.5F, xOffset = 3.897186F, zOffset = 2.2498753F;
    RectLattice       first(bounds, y, xSpacing, zSpacing, 0, 0);
    RectLattice       second(bounds, y, xSpacing, zSpacing, xOffset, zOffset);
    std::vector<IPos> out;
    bool              firstNext = first.hasNext();
    while (first.hasNext() || second.hasNext()) {
        RectLattice& current = firstNext ? first : second;
        RectLattice& other   = firstNext ? second : first;
        if (other.hasNext()) firstNext = !firstNext;
        out.push_back(current.next());
    }
    return out;
}

} // namespace gf::core::hydra
