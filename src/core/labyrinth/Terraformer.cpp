// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/LabyrinthStructure.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MinotaurMazeComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/labyrinth/Terraformer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

#include "core/jrand/JavaOrder.h"

namespace gf::core::labyrinth {

namespace {

constexpr float kPi = 3.1415927F; // Mth.PI

float  clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Mth.clampedMap(double, double, double, double, double).
double clampedMap(double v, double fromMin, double fromMax, double toMin, double toMax) {
    double t = (v - fromMin) / (fromMax - fromMin);
    if (t < 0.0) return toMin;
    if (t > 1.0) return toMax;
    return toMin + t * (toMax - toMin);
}

} // namespace

IPos boxCenter(BlockBox const& b) {
    return {b.minX + (b.maxX - b.minX + 1) / 2, b.minY + (b.maxY - b.minY + 1) / 2, b.minZ + (b.maxZ - b.minZ + 1) / 2};
}

double hollowHill(float centerX, float bottomY, float centerZ, float radius, float heightScale, int x, int y, int z) {
    float dX     = static_cast<float>(x) - centerX;
    float dY     = static_cast<float>(y) - bottomY;
    float dZ     = static_cast<float>(z) - centerZ;
    float dist   = std::sqrt(dX * dX + dZ * dZ);
    float height = jrand::mthCos(dist / radius * kPi) * radius * 0.3333333334F;
    float norm   = clampF(dist / std::fabs(radius), 0.0F, 1.0F);
    if (norm >= 1.0F) return 0.0;
    return clampF(height * heightScale - dY, -1.0F, 1.0F);
}

double focused(float centerX, float bottomY, float centerZ, float radius, float nearValue, float farValue, int x, int y, int z) {
    float dX   = centerX - static_cast<float>(x);
    float dY   = bottomY - static_cast<float>(y);
    float dZ   = centerZ - static_cast<float>(z);
    float dist = std::sqrt(dX * dX + dY * dY + dZ * dZ);
    return clampedMap(dist, 0.0, radius, nearValue, farValue);
}

double absDiffMin(double max, double centerX, double centerZ, int x, int z) {
    return std::min(std::min(std::fabs(x - centerX), std::fabs(z - centerZ)), max);
}

double absDiffMax(double max, double centerX, double centerZ, int x, int z) {
    return std::min(std::max(std::fabs(x - centerX), std::fabs(z - centerZ)), max);
}

double yClampedGradient(int fromY, int toY, double fromValue, double toValue, int y) { return clampedMap(y, fromY, toY, fromValue, toValue); }

double terraformer(BlockBox const& structureBox, int x, int y, int z) {
    constexpr float radius = kMoundRadius;
    IPos const      c      = boxCenter(structureBox);
    int const       yCeil  = c.y;

    // Main mound density field. All values above mount surface are 0 while other values under the mound are 1.
    double hillMound = clampD(hollowHill(static_cast<float>(c.x + 1), static_cast<float>(c.y + 7), static_cast<float>(c.z) + 2.0F, radius, 0.8F, x, y, z), 0.0, 2.0);
    // the field above, inverted: positive terrain above the hill mound
    double ceilingCapped = yClampedGradient(5, 6, -1.0, 1.0, y);
    // Floor leading up inwards, offering chance for terrain to slop upwards if otherwise submerged
    IPos const  pos = {c.x + 1, c.y, c.z + 1};
    float const px  = static_cast<float>(pos.x) + 0.5F;
    float const pz  = static_cast<float>(pos.z) + 0.5F;
    double innerFloor = yClampedGradient(-4, 3, 26.0, -1.0, y) + -1.0 * absDiffMax(32.0, px, pz, x, z);
    // Interior entrances
    double entrances = std::max(ceilingCapped, -2.0 + absDiffMin(32.0, px, pz, x, z));
    // the interior carved out (negative past 0)
    double interior = clampD(std::max(entrances, innerFloor), 0.0, 1.0);
    // FocusedDensityFunction.fromPos(hillCenter.atY(yCeilingFocus), radius * 0.7f, radius, 0)
    double mask           = focused(static_cast<float>(c.x) + 0.5F, static_cast<float>(yCeil) + 0.5F, static_cast<float>(c.z) + 0.5F, radius * 0.7F, radius, 0.0F, x, y, z);
    double maskClamped    = clampD(mask, 0.0, 1.0);
    double interiorMasked = maskClamped == 0.0 ? 0.0 : maskClamped * (interior - 0.0) + 0.0; // DensityFunctions.lerp(mask, zero, interior)
    return std::min(hillMound, interiorMasked);
}

long long mazeSeed(BlockBox const& box) {
    // Java: minX * 90342903L + minY * 90342903L ^ minZ (+ binds tighter than ^)
    auto const k = 90342903ULL;
    auto const a = static_cast<unsigned long long>(static_cast<long long>(box.minX)) * k + static_cast<unsigned long long>(static_cast<long long>(box.minY)) * k;
    return static_cast<long long>(a ^ static_cast<unsigned long long>(static_cast<long long>(box.minZ)));
}

bool isNearRoom(int dx, int dz, int const* rcoords, int count, int range) {
    // if proposed coordinates are covering the origin, return true to stop the room from causing the maze to fail
    if (dx == 1 && dz == 1) return true;
    for (int i = 0; i < count; i++) {
        int rx = rcoords[i * 2];
        int rz = rcoords[i * 2 + 1];
        if (rx == 0 && rz == 0) continue;
        if (std::abs(dx - rx) < range && std::abs(dz - rz) < range) return true;
    }
    return false;
}

char const* roomType(int i, int level) {
    if (i == 0) return "twilightforest:TFMMR";
    if (i == 1) return level == 1 ? "twilightforest:TFMMRE" : "twilightforest:TFMMRB";
    if (i == 2 || i == 3) return level == 1 ? "twilightforest:TFMMRC" : "twilightforest:TFMMMR";
    if (i == 4) return level == 1 ? "twilightforest:TFMMRF" : "twilightforest:TFMMRV";
    return "twilightforest:TFMMRSC";
}

char const* deadEndType(int t, bool tripwire, int level) {
    switch (t) {
    case 1: return "twilightforest:TFMMDEC";
    case 2: return tripwire ? "twilightforest:TFMMDETC" : "twilightforest:TFMMDETrC";
    case 3: return "twilightforest:TFMMDET";
    case 4: return "twilightforest:TFMMDEF";
    case 5: return "twilightforest:TFMMDEFL";
    case 6: return "twilightforest:TFMMDEP";
    case 7: return level == 1 ? "twilightforest:TFMMDER" : "twilightforest:TFMMDES";
    default: return "twilightforest:TFMMDE"; // blank with fence doorway
    }
}

char const* corridorType(int t, int level) {
    switch (t) {
    case 1: return "twilightforest:TFMMC";
    case 2: return "twilightforest:TFMMCIF";
    case 4: return level == 1 ? "twilightforest:TFMMCR" : "twilightforest:TFMMCS";
    default: return nullptr; // 3: painting (none), 0: nothing
    }
}

float weaponModifier(char const* id) {
    std::string_view s(id ? id : "");
    if (s == "minecraft:golden_axe" || s == "twilightforest:gold_minotaur_axe") return 6.0F;
    if (s == "twilightforest:diamond_minotaur_axe") return 9.0F;
    return 0.0F;
}

char const* minotaurAxe(int roll, float effectiveDifficulty) {
    float additionalDiff = effectiveDifficulty + 1;
    int   result         = static_cast<int>(static_cast<float>(roll) / additionalDiff);
    return result == 0 ? "twilightforest:gold_minotaur_axe" : "minecraft:golden_axe";
}

float mazeSlimeHealth(int size) {
    float base = static_cast<float>(size * size);
    return base + base * 2.0F; // AttributeModifier ADD_MULTIPLIED_BASE 2
}

float mazeSlimeSpeed(int size) { return 0.2F + 0.1F * static_cast<float>(size); }

int mazeSlimeXp(int size) { return size + 3; }

int mazeSlimeSize(int roll, bool boosted) {
    int i = roll;
    if (i < 2 && boosted) ++i;
    return 1 << i;
}

float slamDamage(float attackDamage) { return attackDamage * 0.5F; }

} // namespace gf::core::labyrinth
