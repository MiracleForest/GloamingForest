// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/TwilightForestRenderInfo.java
//   src/main/java/twilightforest/client/renderer/TFSkyRenderer.java
//   src/main/java/twilightforest/client/event/FogHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Dimension effects, fog and stars (with vanilla 1.21.1's DimensionType#timeOfDay,
// FogRenderer#setupFog / setupColor and LevelRenderer#drawStars, which TF's code builds on).

#include "core/atmosphere/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/jrand/JRand.h"

namespace gf::core::atmosphere {

namespace {
constexpr float kPi = std::numbers::pi_v<float>;

float lerp(float t, float a, float b) { return a + t * (b - a); }

/// The lerp factor of `frames` frames of the per-frame factor k: 1 - (1 - k)^frames (k for one frame).
float rate(float k, float frames) { return frames == 1.0f ? k : 1.0f - std::pow(1.0f - k, std::max(frames, 0.0f)); }
} // namespace

// ---- biome look --------------------------------------------------------------------------------------

std::vector<BiomeLook> const& biomeLooks() {
    static std::vector<BiomeLook> const looks{
        {"twilightforest:clearing", 0x20224A, 0xC0FFD8},
        {"twilightforest:dark_forest", 0x000000, 0x000000},
        {"twilightforest:dark_forest_center", 0x000000, 0x493000},
        {"twilightforest:dense_forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:dense_mushroom_forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:enchanted_forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:final_plateau", 0x20224A, 0xC0FFD8},
        {"twilightforest:fire_swamp", 0x20224A, 0x380A00},
        {"twilightforest:firefly_forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:glacier", 0x130D28, 0x361F88},
        {"twilightforest:highlands", 0x20224A, 0xC0FFD8},
        {"twilightforest:highlands_underground", 0x20224A, 0xC0FFD8},
        {"twilightforest:lake", 0x20224A, 0xC0FFD8},
        {"twilightforest:mushroom_forest", 0x20224A, 0xC0FFD8},
        {"twilightforest:oak_savannah", 0x20224A, 0xC0FFD8},
        {"twilightforest:snowy_forest", 0x808080, 0xFFFFFF},
        {"twilightforest:spooky_forest", 0x20224A, 0x827391},
        {"twilightforest:stream", 0x20224A, 0xC0FFD8},
        {"twilightforest:swamp", 0x002112, 0x003F21},
        {"twilightforest:thornlands", 0x20224A, 0xC0FFD8},
        {"twilightforest:underground", 0x20224A, 0xC0FFD8},
    };
    return looks;
}

std::optional<BiomeLook> biomeLook(std::string_view biomeId) {
    for (auto const& l : biomeLooks())
        if (biomeId == l.id) return l;
    return std::nullopt;
}

Rgb rgbOf(unsigned rgb) {
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f, static_cast<float>(rgb & 0xFF) / 255.f};
}

// ---- dimension effects ---------------------------------------------------------------------------------

float timeOfDay(long long dayTime) {
    double d0 = static_cast<double>(dayTime) / 24000.0 - 0.25;
    d0 -= std::floor(d0); // Mth.frac
    double d1 = 0.5 - std::cos(d0 * std::numbers::pi) / 2.0;
    return static_cast<float>(d0 * 2.0 + d1) / 3.0f;
}

float daylight(float tod) { return std::clamp(std::cos(tod * (kPi * 2.0f)) * 2.0f + 0.5f, 0.0f, 1.0f); }

Rgb brightnessDependentFogColor(Rgb fog, float d) {
    return {fog.r * (d * 0.94f + 0.06f), fog.g * (d * 0.94f + 0.06f), fog.b * (d * 0.91f + 0.09f)};
}

Rgb fogColor(Rgb biomeFog, Rgb biomeSky, float d, int renderDistanceChunks) {
    float f = 0.25f + 0.75f * static_cast<float>(renderDistanceChunks) / 32.0f;
    f       = 1.0f - static_cast<float>(std::pow(static_cast<double>(f), 0.25));
    Rgb sky{biomeSky.r * d, biomeSky.g * d, biomeSky.b * d}; // ClientLevel#getSkyColor (clear weather)
    Rgb fog = brightnessDependentFogColor(biomeFog, d);
    // TF's getSunriseColor is null: no sunrise blend
    return {fog.r + (sky.r - fog.r) * f, fog.g + (sky.g - fog.g) * f, fog.b + (sky.b - fog.b) * f};
}

bool isSpooky(std::string_view biomeId) { return biomeId == "twilightforest:spooky_forest"; }

bool isFoggyAt(std::string_view biomeId, double playerY) {
    bool spooky = isSpooky(biomeId);
    if (playerY > 20 && !spooky) return false; // above the dark forest canopy
    return spooky || biomeId == "twilightforest:dark_forest" || biomeId == "twilightforest:dark_forest_center";
}

std::string birthdaySplash(int year) {
    int         n      = year - 2011;
    int         mod100 = ((n % 100) + 100) % 100, mod10 = ((n % 10) + 10) % 10;
    char const* suffix = (mod100 >= 11 && mod100 <= 13) ? "th" : mod10 == 1 ? "st" : mod10 == 2 ? "nd" : mod10 == 3 ? "rd" : "th";
    return "Happy " + std::to_string(n) + suffix + " birthday to the Twilight Forest!";
}

// ---- fog ------------------------------------------------------------------------------------------------

FogPlanes vanillaFog(bool sky, float renderDistance, bool foggy) {
    if (sky) return {0.0f, renderDistance}; // FOG_SKY comes before the near fog
    if (foggy) return {renderDistance * 0.05f, std::min(renderDistance, 192.0f) * 0.5f};
    float f = std::clamp(renderDistance / 10.0f, 4.0f, 64.0f);
    return {renderDistance - f, renderDistance};
}

std::optional<FogPlanes> FogSmoother::sky(FogPlanes vanilla, bool spooky, bool chunkLoaded, float frames) {
    if (mSkyLoaded) {
        float far  = spooky ? vanilla.end * 0.5f : vanilla.end;
        float near = spooky ? 0.0f : vanilla.start;
        mSky.end   = lerp(rate(0.003f, frames), mSky.end, far);
        mSky.start = lerp(rate(0.003f * (mSky.start < near ? 0.5f : 2.0f), frames), mSky.start, near);
        return mSky;
    }
    if (chunkLoaded) { // the first-time set up once the player's chunk is loaded (vanilla this frame)
        mSkyLoaded = true;
        mSky.end   = spooky ? vanilla.end * 0.5f : vanilla.end;
        mSky.start = spooky ? 0.0f : vanilla.start;
    }
    return std::nullopt;
}

std::optional<FogPlanes> FogSmoother::terrain(FogPlanes vanilla, bool spooky, bool chunkLoaded, float frames) {
    if (mTerrainLoaded) {
        float far      = spooky ? vanilla.end * 0.5f : vanilla.end;
        float near     = spooky ? far * 0.75f : vanilla.start;
        mTerrain.end   = lerp(rate(0.003f, frames), mTerrain.end, far);
        mTerrain.start = lerp(rate(0.003f * (mTerrain.start < near ? 0.5f : 2.0f), frames), mTerrain.start, near);
        return mTerrain;
    }
    if (mSkyLoaded || chunkLoaded) {
        mTerrainLoaded = true;
        mTerrain.end   = spooky ? vanilla.end * 0.5f : vanilla.end;
        mTerrain.start = spooky ? mTerrain.end * 0.75f : vanilla.start;
    }
    return std::nullopt;
}

void FogSmoother::reset() {
    mSkyLoaded     = false;
    mTerrainLoaded = false;
}

// ---- stars ----------------------------------------------------------------------------------------------

namespace {
Vec3f add(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

/// JOML Quaternionf#rotateTo((0, 0, -1), dir) as a rotation of `v` (Rodrigues' formula; `dir` unit).
Vec3f rotateFromMinusZ(Vec3f dir, Vec3f v) {
    // a = (0, 0, -1): axis = a x dir, cos = a . dir
    Vec3f axis{dir.y, -dir.x, 0.0f};
    float c = -dir.z;
    float s = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    if (s < 1e-6f) {
        if (c > 0) return v;
        // opposite directions: JOML turns 180 degrees about (0, fromDir.z, -fromDir.y) = the y axis
        return {-v.x, v.y, -v.z};
    }
    Vec3f k{axis.x / s, axis.y / s, axis.z / s};
    // v cos + (k x v) sin + k (k . v)(1 - cos)
    Vec3f kxv{k.y * v.z - k.z * v.y, k.z * v.x - k.x * v.z, k.x * v.y - k.y * v.x};
    float kv = k.x * v.x + k.y * v.y + k.z * v.z;
    return {v.x * c + kxv.x * s + k.x * kv * (1 - c), v.y * c + kxv.y * s + k.y * kv * (1 - c), v.z * c + kxv.z * s + k.z * kv * (1 - c)};
}
} // namespace

std::vector<std::array<Vec3f, 4>> starQuads() {
    std::vector<std::array<Vec3f, 4>> out;
    jrand::LegacyRandom               random(10842);
    for (int i = 0; i < 3000; ++i) { // TF: 1500 -> 3000
        float f1 = random.nextFloat() * 2.0f - 1.0f;
        float f2 = random.nextFloat() * 2.0f - 1.0f;
        float f3 = random.nextFloat() * 2.0f - 1.0f;
        float f4 = 0.15f + random.nextFloat() * 0.1f;
        float f5 = f1 * f1 + f2 * f2 + f3 * f3;
        if (f5 <= 0.010000001f || f5 >= 1.0f) continue;
        float len = std::sqrt(f5);
        Vec3f dir{f1 / len, f2 / len, f3 / len};
        Vec3f s{dir.x * 100.0f, dir.y * 100.0f, dir.z * 100.0f};
        float f6 = static_cast<float>(random.nextDouble() * static_cast<double>(kPi) * 2.0);
        float cs = std::cos(f6), sn = std::sin(f6);
        auto  corner = [&](float x, float y) {
            Vec3f r{x * cs - y * sn, x * sn + y * cs, 0.0f}; // rotateZ(f6)
            return rotateFromMinusZ(dir, r);
        };
        std::array<Vec3f, 4> q{};
        // Vector3f#add works in place: every vertex is the sum of the corners before it
        s    = add(s, corner(f4, -f4));
        q[0] = s;
        s    = add(s, corner(f4, f4));
        q[1] = s;
        s    = add(s, corner(-f4, f4));
        q[2] = s;
        s    = add(s, corner(-f4, -f4));
        q[3] = s;
        out.push_back(q);
    }
    return out;
}

Vec3f rotateStar(Vec3f v, float tod) {
    // Axis.XP.rotationDegrees(timeOfDay * 360) first (innermost), then Axis.YP.rotationDegrees(-90)
    float a = tod * kPi * 2.0f;
    float ca = std::cos(a), sa = std::sin(a);
    Vec3f x{v.x, v.y * ca - v.z * sa, v.y * sa + v.z * ca};
    float b = -kPi / 2.0f;
    float cb = std::cos(b), sb = std::sin(b);
    return {x.x * cb + x.z * sb, x.y, -x.x * sb + x.z * cb};
}

} // namespace gf::core::atmosphere
