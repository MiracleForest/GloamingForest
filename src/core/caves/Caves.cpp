// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/YetiCaveStructure.java
//   src/main/java/twilightforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/twilightforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/twilightforest/world/components/structures/trollcave/CloudComponent.java
//   src/main/java/twilightforest/entity/projectile/FallingIce.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/caves/Caves.h"

#include <algorithm>
#include <cmath>

namespace gf::core::caves {

namespace {

/// Mth.clampedMap(double, ...).
double clampedMap(double v, double fromMin, double fromMax, double toMin, double toMax) {
    double t = (v - fromMin) / (fromMax - fromMin);
    if (t < 0.0) return toMin;
    if (t > 1.0) return toMax;
    return toMin + t * (toMax - toMin);
}

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Mth.floor(float).
int floorF(float v) {
    int i = static_cast<int>(v);
    return v < static_cast<float>(i) ? i - 1 : i;
}

/// Mth.ceil(float).
int ceilF(float v) {
    int i = static_cast<int>(v);
    return v > static_cast<float>(i) ? i + 1 : i;
}

} // namespace

double absDiffMin(double max, int centerX, int centerZ, int x, int z) {
    return std::min(std::min(std::abs(static_cast<double>(x) - centerX), std::abs(static_cast<double>(z) - centerZ)), max);
}

double absDiffMax(double max, int centerX, int centerZ, int x, int z) {
    return std::min(std::max(std::abs(static_cast<double>(x) - centerX), std::abs(static_cast<double>(z) - centerZ)), max);
}

double yClampedGradient(int y, int fromY, int toY, double fromValue, double toValue) {
    return clampedMap(static_cast<double>(y), static_cast<double>(fromY), static_cast<double>(toY), fromValue, toValue);
}

double yetiCaveTerraformer(int cx, int cz, int x, int y, int z) {
    constexpr int riser = 5;
    // exterior "cut pyramid", trapezoid prism
    double exteriorTrapezoidSq = -1.0 * absDiffMax(40, cx, cz, x, z)
                               + (yClampedGradient(y, 0, 24 + riser, 40, 32) + yClampedGradient(y, 21 + riser, 22 + riser, 0, -100000));
    // floor and ceiling
    constexpr int deltaToGround = 8;
    constexpr int deltaToAir    = 4;
    constexpr int yPosTop       = 14 + riser;
    constexpr int yPosBottom    = -3 + riser;
    double        floorCeiling  = 32.0
                           * std::max(yClampedGradient(y, yPosTop - deltaToAir, yPosTop + deltaToGround, -1, 1),
                                      yClampedGradient(y, yPosBottom - deltaToGround, yPosBottom + deltaToAir, 1, -1))
                       + clampD(0.0 + absDiffMin(64, cx, cz, x, z), 1, 64);
    double walls          = yClampedGradient(y, -3 + riser, 21 + riser, -9.5, -2.5);
    // entrances (+ shaped) and the square interior "room" box, as a negative
    double interiorCarver = std::min(absDiffMin(64, cx, cz, x, z), -20.0 + absDiffMax(64, cx, cz, x, z));
    double shapedInterior = walls + interiorCarver;
    double interior       = std::max(floorCeiling, shapedInterior);
    double entranceSlope  = yClampedGradient(y, -8, 16 + riser, -46, -48) + absDiffMax(64, cx, cz, x, z);
    double smoothed       = std::max(interior, static_cast<double>(1 / 64.0F) * (static_cast<double>(15.0F) + entranceSlope));
    // DensityFunctions.lerp(delta, zero(), max) = delta * max
    double delta          = clampD(static_cast<double>(0.020833334F) * exteriorTrapezoidSq, 0, 1);
    if (delta == 0.0) return 0.0;
    return delta * (16.0 * smoothed);
}

double hollowDistance(int x, int y, int z, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    int ex = std::min(x - minX, maxX - x);
    int ey = std::min((y - minY) * 2, maxY - y);
    int ez = std::min(z - minZ, maxZ - z);
    return std::sqrt(static_cast<double>(ex * ey * ez));
}

std::vector<CloudColumn> generateCloud(int chunkX, int chunkZ, int hx, int hz, int cloudHeight, jrand::RandomSource& random) {
    bool isCenter = hx == 0 && hz == 0;
    int  regionX  = (chunkX + 8) >> 4;
    int  regionZ  = (chunkZ + 8) >> 4;

    auto seed = static_cast<std::uint64_t>(static_cast<std::int64_t>(regionX) * 3129871LL ^ static_cast<std::int64_t>(regionZ) * 116129781LL);
    seed      = seed * seed * 42317861ULL + seed * 7ULL;
    auto s    = static_cast<std::int64_t>(seed);

    int num0 = 5 * static_cast<int>(s >> 12 & 3LL) - 4 * static_cast<int>(s >> 15 & 3LL);
    int num1 = 4 * static_cast<int>(s >> 18 & 3LL) - 5 * static_cast<int>(s >> 21 & 3LL);
    int num2 = 5 * static_cast<int>(s >> 9 & 3LL) - 4 * static_cast<int>(s >> 6 & 3LL);
    int num3 = 4 * static_cast<int>(s >> 3 & 3LL) - 5 * static_cast<int>(s & 3LL);

    std::vector<CloudColumn> out;
    for (int bx = 0; bx < 4; bx++) {
        int dx  = bx * 4 - hx - 2;
        int dx2 = dx + num0;
        int dx3 = dx + num2;
        for (int bz = 0; bz < 4; bz++) {
            int dz  = bz * 4 - hz - 2;
            int dz2 = dz + num1;
            int dz3 = dz + num3;

            // take the minimum distance to any center (Mth.sqrt(float))
            float dist0 = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(dx * dx + dz * dz)))) / 4.0F;
            float dist2 = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(dx2 * dx2 + dz2 * dz2)))) / 3.5F;
            float dist3 = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(dx3 * dx3 + dz3 * dz3)))) / 4.5F;

            double dist = std::min(dist0, std::min(dist2, dist3));

            float  pr = random.nextFloat();
            double cv = dist - 7.0F - static_cast<double>(pr * 3.0F);

            // randomize depth and height
            int y     = cloudHeight;
            int depth = 4;
            if (!isCenter && pr < 0.1F) y++;
            if (pr > 0.6F) depth++;
            if (pr > 0.9F) depth++;

            bool full   = dist < 7 || cv < static_cast<double>(0.05F);
            bool fringe = !full && (dist < 8 || cv < static_cast<double>(1.0F));
            if (!full && !fringe) continue;
            for (int sx = 0; sx < 4; sx++)
                for (int sz = 0; sz < 4; sz++) out.push_back(CloudColumn{bx * 4 + sx, bz * 4 + sz, y, depth, full});
        }
    }
    return out;
}

float fallingIceDamage(float dist, int difficulty) {
    static constexpr float kDamagePerDifficulty[] = {0.0F, 0.5F, 1.0F, 2.0F};
    int realDist = ceilF(dist - 5.0F);
    if (realDist < 0) return -1.0F;
    difficulty = std::clamp(difficulty, 0, 3);
    return static_cast<float>(std::min(floorF(static_cast<float>(realDist) * kDamagePerDifficulty[difficulty]), 100));
}

int rawBrightness(int blockLight, int skyLight, int skyDarken) { return std::max(blockLight, skyLight - skyDarken); }

} // namespace gf::core::caves
