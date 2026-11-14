// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/projectile/ChainBlock.java
//   src/main/java/gloamingforest/entity/projectile/CubeOfAnnihilation.java
//   src/main/java/gloamingforest/entity/projectile/IceBomb.java
//   src/main/java/gloamingforest/entity/projectile/SeekerArrow.java
//   src/main/java/gloamingforest/item/CrumbleHornItem.java
//   src/main/java/gloamingforest/item/OreMagnetItem.java
//   src/main/java/gloamingforest/item/TripleBowItem.java
//   src/main/java/gloamingforest/item/recipe/MoonwormQueenRepairRecipe.java
//   src/main/java/gloamingforest/util/iterators/VoxelBresenhamIterator.java
//   src/main/java/gloamingforest/util/WorldUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/weapons/Weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gf::core::weapons {

namespace {
constexpr float kDegToRad = 0.017453292F;
constexpr float kPi       = 3.1415927F;
} // namespace

double length(V3 const& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

V3 normalize(V3 const& v) {
    double d = length(v);
    return d < 1.0E-5 ? V3{} : V3{v.x / d, v.y / d, v.z / d};
}

double dot(V3 const& a, V3 const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3     scale(V3 const& v, double f) { return {v.x * f, v.y * f, v.z * f}; }
V3     add(V3 const& a, V3 const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

V3 yRot(V3 const& v, float angle) {
    float  c = std::cos(angle), s = std::sin(angle);
    return {v.x * c + v.z * s, v.y, v.z * c - v.x * s};
}

I3 containing(V3 const& v) {
    return {static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z))};
}

V3 viewVector(float xRot, float yRot) {
    float f  = xRot * kDegToRad;
    float f1 = -yRot * kDegToRad;
    float f2 = std::cos(f1), f3 = std::sin(f1), f4 = std::cos(f), f5 = std::sin(f);
    return {static_cast<double>(f3 * f4), static_cast<double>(-f5), static_cast<double>(f2 * f4)};
}

V3 offsetLook(float yRot, float xRot, float yawOffset, float pitchOffset) {
    float var2 = std::cos(-(yRot + yawOffset) * kDegToRad - kPi);
    float var3 = std::sin(-(yRot + yawOffset) * kDegToRad - kPi);
    float var4 = -std::cos(-(xRot + pitchOffset) * kDegToRad);
    float var5 = std::sin(-(xRot + pitchOffset) * kDegToRad);
    return {static_cast<double>(var3 * var4), static_cast<double>(var5), static_cast<double>(var2 * var4)};
}

std::array<std::pair<float, float>, 9> const& magnetTries() {
    static constexpr float kWiggle = 10.0F;
    static std::array<std::pair<float, float>, 9> const tries{{
        {0, 0},
        {kWiggle, 0},
        {kWiggle, kWiggle},
        {0, kWiggle},
        {-kWiggle, kWiggle},
        {-kWiggle, 0},
        {-kWiggle, -kWiggle},
        {0, -kWiggle},
        {kWiggle, -kWiggle},
    }};
    return tries;
}

std::vector<I3> voxelLine(I3 a, I3 b) {
    int xVec = b.x - a.x, yVec = b.y - a.y, zVec = b.z - a.z;
    int absDx = std::abs(xVec), absDy = std::abs(yVec), absDz = std::abs(zVec);
    int xInc = xVec < 0 ? -1 : 1, yInc = yVec < 0 ? -1 : 1, zInc = zVec < 0 ? -1 : 1;
    int ddx = absDx << 1, ddy = absDy << 1, ddz = absDz << 1;
    int err1 = 0, err2 = 0, len = 0, axis = 0;
    if (absDx >= absDy && absDx >= absDz) {
        err1 = ddy - absDx, err2 = ddz - absDx, axis = 0, len = absDx + 1;
    } else if (absDy >= absDx && absDy >= absDz) {
        err1 = ddx - absDy, err2 = ddz - absDy, axis = 1, len = absDy + 1;
    } else {
        err1 = ddy - absDz, err2 = ddx - absDz, axis = 2, len = absDz + 1;
    }
    std::vector<I3> out;
    out.reserve(static_cast<std::size_t>(len));
    I3 v = a;
    for (int i = 0; i < len; ++i) {
        out.push_back(v);
        switch (axis) {
        case 0:
            if (err1 > 0) v.y += yInc, err1 -= ddx;
            if (err2 > 0) v.z += zInc, err2 -= ddx;
            err1 += ddy, err2 += ddz, v.x += xInc;
            break;
        case 1:
            if (err1 > 0) v.x += xInc, err1 -= ddy;
            if (err2 > 0) v.z += zInc, err2 -= ddy;
            err1 += ddx, err2 += ddz, v.y += yInc;
            break;
        default:
            if (err1 > 0) v.y += yInc, err1 -= ddz;
            if (err2 > 0) v.x += xInc, err2 -= ddz;
            err1 += ddy, err2 += ddx, v.z += zInc;
            break;
        }
    }
    return out;
}

namespace {
void vein(I3 here, std::function<bool(I3 const&)> const& isOre, std::set<I3>& out, std::size_t limit) {
    if (out.contains(here) || out.size() >= limit) return;
    if (!isOre(here)) return;
    out.insert(here);
    static constexpr I3 kDirs[] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (auto const& d : kDirs) vein({here.x + d.x, here.y + d.y, here.z + d.z}, isOre, out, limit);
}
} // namespace

std::set<I3> findVein(I3 start, std::function<bool(I3 const&)> const& isOre, std::size_t limit) {
    std::set<I3> out;
    vein(start, isOre, out, limit);
    return out;
}

float bowPower(int useTicks) {
    float f = static_cast<float>(useTicks) / 20.0F;
    f       = (f * f + f * 2.0F) / 3.0F;
    return std::min(f, 1.0F);
}

std::optional<V3> seekerSteer(V3 const& course, V3 const& toTarget) {
    constexpr double kSeekFactor = 0.8, kThreshold = 0.5;
    V3     target    = scale(toTarget, kSeekFactor);
    double courseLen = length(course);
    double targetLen = length(target);
    double totalLen  = std::sqrt(courseLen * courseLen + targetLen * targetLen);
    double cosine    = dot(course, target) / (courseLen * targetLen);
    if (!(cosine > kThreshold)) return std::nullopt;
    V3 m = add(scale(course, courseLen / totalLen), scale(target, courseLen / totalLen));
    return V3{m.x, m.y + static_cast<double>(0.045F), m.z};
}

std::pair<V3, V3> seekerBox(V3 const& pos, V3 const& motion) {
    constexpr double kSeekDistance = 5.0;
    constexpr double kSeekAngle    = 3.141592653589793 / 6.0;
    V3               lo = pos, hi = pos;
    for (float a : {static_cast<float>(kSeekAngle), static_cast<float>(-kSeekAngle)}) {
        V3 c = yRot(scale(motion, kSeekDistance), a);
        V3 p = add(pos, c);
        lo   = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi   = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    lo.y -= kSeekDistance * 0.5;
    hi.y += kSeekDistance * 0.5;
    return {lo, hi};
}

V3 chainBounce(V3 v, int face) {
    constexpr double kBounce = 0.6;
    v = scale(v, kBounce);
    switch (face) {
    case 0:
        if (v.y > 0) v.y *= -kBounce;
        break;
    case 1:
        if (v.y < 0) v.y *= -kBounce;
        break;
    case 2:
        if (v.z > 0) v.z *= -kBounce;
        break;
    case 3:
        if (v.z < 0) v.z *= -kBounce;
        break;
    case 4:
        if (v.x > 0) v.x *= -kBounce;
        break;
    case 5:
        if (v.x < 0) v.x *= -kBounce;
        break;
    default: break;
    }
    return v;
}

V3 chainReturn(V3 const& saved, V3 const& toOwnerEye, int tickCount, double gravity) {
    V3    back = normalize(toOwnerEye);
    float age  = std::min(static_cast<float>(tickCount) * 0.03F, 1.0F);
    return {saved.x * (1.0 - age) + back.x * 2.0F * age, saved.y * (1.0 - age) + back.y * 2.0F * age - gravity, saved.z * (1.0 - age) + back.z * 2.0F * age};
}

V3 cubeVelocity(V3 const& centre, V3 const& dest) {
    V3    v{dest.x - centre.x, dest.y - centre.y, dest.z - centre.z};
    float speed    = std::sqrt(static_cast<float>(v.x * v.x + v.y * v.y + v.z * v.z));
    float maxSpeed = 0.5F;
    if (speed > maxSpeed) v = {v.x / (speed / maxSpeed), v.y / (speed / maxSpeed), v.z / (speed / maxSpeed)};
    // else: Java's `getDeltaMovement().multiply(0.5, ...)` result is discarded (no slow-down)
    return v;
}

int moonwormRepairDamage(int damage, int berries) { return damage - berries * 64; }

std::pair<I3, I3> crumbleBox(I3 center) {
    // AABB.encapsulatingFullBlocks(center - 2, center + 1) ends at center + 2, and
    // WorldUtil#getAllInBB includes the box's max corner: 5 x 5 x 5 positions
    constexpr int r = 2;
    return {{center.x - r, center.y - r, center.z - r}, {center.x + r, center.y + r, center.z + r}};
}

std::vector<I3> allInBox(V3 const& min, V3 const& max) {
    // WorldUtil#getAllInBB: (int) casts (truncation toward zero), both ends included
    int x0 = static_cast<int>(min.x), y0 = static_cast<int>(min.y), z0 = static_cast<int>(min.z);
    int x1 = static_cast<int>(max.x), y1 = static_cast<int>(max.y), z1 = static_cast<int>(max.z);
    std::vector<I3> out;
    for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z)
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
            for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) out.push_back({x, y, z});
    return out;
}

std::vector<I3> iceBombOffsets(int range) {
    std::vector<I3> out;
    for (int x = -range; x <= range; ++x)
        for (int z = -range; z <= range; ++z) {
            if (std::abs(x) == range && std::abs(z) == range) continue;
            for (int y = -range; y <= range; ++y) out.push_back({x, y, z});
        }
    return out;
}

std::array<double, 3> tripleBowLift() { return {0.0075 * 20.0 * -1, 0.0, 0.0075 * 20.0}; }

} // namespace gf::core::weapons
