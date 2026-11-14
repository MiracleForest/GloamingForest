// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/ai/goal/CubeMoveToRedstoneSymbolsGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/CubeCenterOnSymbolGoal.java
//   src/main/java/gloamingforest/entity/monster/Adherent.java (performRangedAttack)
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBossGazeboComponent.java
//   src/main/java/gloamingforest/util/BoundingBoxUtils.java (vectorsMinMax)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/finalcastle/Mobs.h"

#include <algorithm>
#include <cmath>

namespace gf::core::finalcastle {

bool isRedstoneSymbol(IPos pos, std::function<bool(IPos)> const& loaded, std::function<bool(IPos)> const& empty, std::function<bool(IPos)> const& wire) {
    if (!loaded(pos) || !empty(pos)) return false;
    // we found an air block, is it surrounded by redstone? (Direction.values())
    static constexpr int kD[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (auto const& d : kD)
        if (!wire(IPos{pos.x + d[0], pos.y + d[1], pos.z + d[2]})) return false;
    return true;
}

std::optional<IPos> searchForRedstoneSymbol(IPos cube, std::function<bool(IPos)> const& isSymbol) {
    for (int x = -16; x < 16; x++)
        for (int z = -16; z < 16; z++)
            for (int y = -5; y < 5; y++) {
                IPos p{cube.x + x, cube.y + y, cube.z + z};
                if (isSymbol(p)) return p;
            }
    return std::nullopt;
}

double distanceFromSymbol(IPos symbol, double x, double y, double z) {
    double dx = symbol.x - x + 0.5;
    double dy = symbol.y - y;
    double dz = symbol.z - z + 0.5;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool isCloseToSymbol(double distance) { return distance > 0.25F && distance < 10.0F; }

bool keepsCentering(double distance) { return distance > 0.1F && distance < 100; }

DVec centerTarget(IPos symbol) { return DVec{symbol.x + 0.5, static_cast<double>(symbol.y), symbol.z + 0.5}; }

Shot adherentShot(DVec shooter, double boltY, DVec targetFeet, double targetEye, int difficultyId) {
    double d0 = targetFeet.y + targetEye - 1.100000023841858;
    double d1 = targetFeet.x - shooter.x;
    double d2 = d0 - boltY;
    double d3 = targetFeet.z - shooter.z;
    float  f  = std::sqrt(static_cast<float>(d1 * d1 + d3 * d3)) * 0.2F;
    Shot   s;
    s.direction  = DVec{d1, d2 + f, d3};
    s.velocity   = 0.6F;
    s.inaccuracy = static_cast<float>(10 - difficultyId * 4);
    return s;
}

int rangedIntervalAfterShot(double distanceSqr, float radius, int intervalMin, int intervalMax) {
    float f = static_cast<float>(std::sqrt(distanceSqr)) / radius;
    return static_cast<int>(std::floor(f * static_cast<float>(intervalMax - intervalMin) + static_cast<float>(intervalMin)));
}

int rangedIntervalOnReset(double distanceSqr, float radius, int intervalMin, int intervalMax) {
    double delta = std::sqrt(distanceSqr) / static_cast<double>(radius);
    return static_cast<int>(std::floor(intervalMin + delta * (intervalMax - intervalMin))); // Mth.lerp
}

std::vector<DVec> wipTextPositions(IPos block) {
    std::vector<DVec> out;
    for (auto const& line : kWipLines) out.push_back(DVec{block.x + 0.5, block.y + static_cast<double>(line.yOffset), block.z + 0.5});
    return out;
}

std::optional<Box> vectorsMinMax(std::vector<DVec> const& vectors, double expand) {
    if (vectors.empty()) return std::nullopt;
    Box b{vectors[0].x, vectors[0].y, vectors[0].z, vectors[0].x, vectors[0].y, vectors[0].z};
    for (auto const& v : vectors) {
        b.minX = std::min(b.minX, v.x);
        b.minY = std::min(b.minY, v.y);
        b.minZ = std::min(b.minZ, v.z);
        b.maxX = std::max(b.maxX, v.x);
        b.maxY = std::max(b.maxY, v.y);
        b.maxZ = std::max(b.maxZ, v.z);
    }
    b.minX -= expand;
    b.minY -= expand;
    b.minZ -= expand;
    b.maxX += expand;
    b.maxY += expand;
    b.maxZ += expand;
    return b;
}

std::optional<Punch> punchFor(std::vector<DVec> const& textPositions) {
    auto box = vectorsMinMax(textPositions, 1);
    if (!box) return std::nullopt;
    Punch p;
    p.width  = ((box->maxX - box->minX) + (box->maxZ - box->minZ)) * 0.5;
    p.height = box->maxY - box->minY;
    p.pos    = DVec{(box->minX + box->maxX) / 2, box->minY, (box->minZ + box->maxZ) / 2};
    return p;
}

} // namespace gf::core::finalcastle
