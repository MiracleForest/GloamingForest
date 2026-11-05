// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
//   src/main/resources/assets/twilightforest/shaders/core/aurora/aurora.fsh
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The aurora shader evaluated on the CPU (RenderDragon has no GLSL): K.jpg's OpenSimplex2 as the
// fragment shader writes it, the 16-step ray march, the colour and Minecraft's fog.glsl, in GLSL's
// float arithmetic.

#include "core/atmosphere/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "core/jrand/BiomeManager.h"

namespace gf::core::atmosphere {

int tickAurora(int aurora, bool inAuroraBiome, bool listEmpty) {
    if (listEmpty) return 0;
    return std::clamp(aurora + (inAuroraBiome ? 1 : -1), 0, kAuroraMax);
}

float auroraAlpha(int lastAurora, int aurora, float partialTicks) {
    float v = static_cast<float>(lastAurora) + partialTicks * static_cast<float>(aurora - lastAurora);
    return v / 60.0f * 0.5f;
}

int auroraSeed(std::int64_t worldSeed) {
    auto zoom = biome::obfuscateSeed(worldSeed);
    int  i    = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(zoom))); // (int) long
    // Mth.abs(int): Integer.MIN_VALUE stays negative
    return i == std::numeric_limits<int>::min() ? i : std::abs(i);
}

namespace {

struct V3 {
    float x, y, z;
};
struct V4 {
    float x, y, z, w;
};

float glslMod(float x, float y) { return x - y * std::floor(x / y); }
float glslRound(float x) { return std::round(x); }
float glslSign(float x) { return x > 0 ? 1.0f : (x < 0 ? -1.0f : 0.0f); }
float glslStep(float edge, float x) { return x < edge ? 0.0f : 1.0f; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

V4 permute(V4 t) {
    return {t.x * (t.x * 34.0f + 133.0f), t.y * (t.y * 34.0f + 133.0f), t.z * (t.z * 34.0f + 133.0f), t.w * (t.w * 34.0f + 133.0f)};
}
V4 mod289(V4 t) { return {glslMod(t.x, 289.0f), glslMod(t.y, 289.0f), glslMod(t.z, 289.0f), glslMod(t.w, 289.0f)}; }

// Gradient set is a normalized expanded rhombic dodecahedron
V3 grad(float hash) {
    V3 cube{glslMod(std::floor(hash / 1.0f), 2.0f) * 2.0f - 1.0f, glslMod(std::floor(hash / 2.0f), 2.0f) * 2.0f - 1.0f,
            glslMod(std::floor(hash / 4.0f), 2.0f) * 2.0f - 1.0f};
    V3  cuboct = cube;
    int idx    = static_cast<int>(hash * (1.0f / 16.0f));
    if (idx == 0) cuboct.x = 0.0f;
    else if (idx == 1) cuboct.y = 0.0f;
    else cuboct.z = 0.0f;
    float type = glslMod(std::floor(hash / 8.0f), 2.0f);
    V3    cr{cube.y * cuboct.z - cube.z * cuboct.y, cube.z * cuboct.x - cube.x * cuboct.z, cube.x * cuboct.y - cube.y * cuboct.x};
    V3    rhomb{(1.0f - type) * cube.x + type * (cuboct.x + cr.x), (1.0f - type) * cube.y + type * (cuboct.y + cr.y),
                (1.0f - type) * cube.z + type * (cuboct.z + cr.z)};
    V3    g{cuboct.x * 1.22474487139f + rhomb.x, cuboct.y * 1.22474487139f + rhomb.y, cuboct.z * 1.22474487139f + rhomb.z};
    float k = (1.0f - 0.042942436724648037f * type) * 32.80201376986577f;
    return {g.x * k, g.y * k, g.z * k};
}

/// The half-lattice point closest to X along the edge (v1, d1) and its neighbour (v2, d2).
void lattice(V3 X, V3& v1, V3& d1, V3& v2, V3& d2) {
    v1 = {glslRound(X.x), glslRound(X.y), glslRound(X.z)};
    d1 = {X.x - v1.x, X.y - v1.y, X.z - v1.z};
    V3 s{std::abs(d1.x), std::abs(d1.y), std::abs(d1.z)};
    // step(max(score.yzx, score.zxy), score)
    V3 dir{glslStep(std::max(s.y, s.z), s.x), glslStep(std::max(s.z, s.x), s.y), glslStep(std::max(s.x, s.y), s.z)};
    v2 = {v1.x + dir.x * glslSign(d1.x), v1.y + dir.y * glslSign(d1.y), v1.z + dir.z * glslSign(d1.z)};
    d2 = {X.x - v2.x, X.y - v2.y, X.z - v2.z};
}

float openSimplex2Base(V3 X) {
    V3 v1, d1, v2, d2, v3, d3, v4, d4;
    lattice(X, v1, d1, v2, d2);
    lattice({X.x + 144.5f, X.y + 144.5f, X.z + 144.5f}, v3, d3, v4, d4);
    V4 hashes = permute(mod289({v1.x, v2.x, v3.x, v4.x}));
    hashes    = permute(mod289({hashes.x + v1.y, hashes.y + v2.y, hashes.z + v3.y, hashes.w + v4.y}));
    hashes    = permute(mod289({hashes.x + v1.z, hashes.y + v2.z, hashes.z + v3.z, hashes.w + v4.z}));
    hashes    = {glslMod(hashes.x, 48.0f), glslMod(hashes.y, 48.0f), glslMod(hashes.z, 48.0f), glslMod(hashes.w, 48.0f)};
    float a[4]{std::max(0.5f - dot(d1, d1), 0.0f), std::max(0.5f - dot(d2, d2), 0.0f), std::max(0.5f - dot(d3, d3), 0.0f), std::max(0.5f - dot(d4, d4), 0.0f)};
    V3    ds[4]{d1, d2, d3, d4};
    float hs[4]{hashes.x, hashes.y, hashes.z, hashes.w};
    float value = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float aa = a[i] * a[i];
        value += aa * aa * dot(ds[i], grad(hs[i]));
    }
    return value;
}

} // namespace

float openSimplex2ImproveXY(float x, float y, float z) {
    // orthonormalMap * X (GLSL mat3 columns)
    V3 c0{0.788675134594813f, -0.211324865405187f, -0.577350269189626f};
    V3 c1{-0.211324865405187f, 0.788675134594813f, -0.577350269189626f};
    V3 c2{0.577350269189626f, 0.577350269189626f, 0.577350269189626f};
    V3 r{c0.x * x + c1.x * y + c2.x * z, c0.y * x + c1.y * y + c2.y * z, c0.z * x + c1.z * y + c2.z * z};
    return openSimplex2Base(r);
}

float linearFogFade(float distance, float fogStart, float fogEnd) {
    if (distance <= fogStart) return 1.0f;
    if (distance >= fogEnd) return 0.0f;
    // smoothstep(fogEnd, fogStart, distance)
    float t = std::clamp((distance - fogEnd) / (fogStart - fogEnd), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

namespace {

float genNoise(float x, float z, float speed, AuroraUniforms const& u) {
    // (SeedContext / 360) and (SeedContext % 360) are int operations
    float xx = x + u.camX + static_cast<float>(u.seed / 360);
    float zz = z + u.camZ + static_cast<float>(u.seed % 360);
    return openSimplex2ImproveXY(xx / 512.0f, zz / 512.0f, u.gameTime * speed);
}

float floatToOneOrZero(float value) { return glslStep(0.000001f, value); }

float fixNoise(float noise) {
    float absNoise = std::abs(noise);
    float swap     = floatToOneOrZero(absNoise - 0.2f);
    noise          = (1.0f - swap) * (1.0f + absNoise * 5.0f) + swap * -1.0f;
    noise          = std::clamp((noise + 1.0f) / 2.0f - 0.5f, 0.0f, 1.0f);
    return (1.0f - noise) * floatToOneOrZero(noise);
}

float rayMarch(V3 origin, V3 direction, AuroraUniforms const& u) {
    float noise = 0.0f;
    for (int i = 0; i < 16; ++i) {
        float t = (static_cast<float>(i) / 16.0f) * 0.35f;
        V3    p{origin.x + t * direction.x, origin.y + t * direction.y, origin.z + t * direction.z};
        float fade = i == 0 ? 1.0f : (static_cast<float>(16 - i) / 16.0f) * 0.65f;
        noise += fixNoise(genNoise(p.x, p.z, 22.5f, u)) * fade;
    }
    return noise;
}

} // namespace

AuroraMesh auroraMesh(float planeHeight, float fogEnd, int rings, int segments) {
    return AuroraMesh{std::max(rings, 1), std::max(segments, 3), planeHeight, std::max(fogEnd, 1.0f) * 2.75f};
}

std::pair<float, float> auroraMeshPoint(AuroraMesh const& m, int ring, int segment) {
    float const h        = std::max(std::abs(m.height), 8.0f);
    float const maxAngle = std::atan(m.extent / h);
    int const   rr       = std::clamp(ring, 0, m.rings);
    float const r        = std::min(m.extent, h * std::tan(maxAngle * static_cast<float>(rr) / static_cast<float>(m.rings)));
    float const a        = 6.28318530718f * static_cast<float>(((segment % m.segments) + m.segments) % m.segments) / static_cast<float>(m.segments);
    return {r * std::cos(a), r * std::sin(a)};
}

AuroraFragment auroraFragment(float px, float pz, AuroraUniforms const& u) {
    float ux = px * 2.0f - 1.0f, uy = pz * 2.0f - 1.0f; // uv = pixelPos.xz * 2.0 - 1.0
    float noise      = rayMarch({ux, 0.0f, uy}, {ux, 1.0f, uy}, u);
    float colorNoise = genNoise(ux, uy, 720.0f, u);
    colorNoise       = ((colorNoise + 1.0f) / 2.0f) * 0.5f;
    Rgb   color{0.0f, 0.5f + colorNoise, 1.0f - colorNoise};
    float len     = std::sqrt(px * px + pz * pz);
    float fogFade = linearFogFade(len / 2.75f, u.fogStart, u.fogEnd);
    // linear_fog(color, length(pixelPos.xz / 2.5), ...)
    float d = len / 2.5f;
    if (d > u.fogStart) {
        float fogValue = d < u.fogEnd ? [&] {
            float t = std::clamp((d - u.fogStart) / (u.fogEnd - u.fogStart), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }()
                                      : 1.0f;
        float m        = fogValue * u.fogAlpha;
        color          = {color.r + (u.fogColor.r - color.r) * m, color.g + (u.fogColor.g - color.g) * m, color.b + (u.fogColor.b - color.b) * m};
    }
    return {color, std::max(0.0f, noise) * fogFade};
}

} // namespace gf::core::atmosphere
