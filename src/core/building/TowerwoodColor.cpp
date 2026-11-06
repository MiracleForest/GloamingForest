// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/SimplexNoiseHelper.java
//   src/main/java/twilightforest/util/ColorUtil.java
//   src/main/java/twilightforest/client/event/ColorHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/building/TowerwoodColor.h"

#include <array>
#include <cmath>

namespace gf::core::building {

namespace {

struct Grad {
    float x, y, z;
};

constexpr Grad kGrad3[12]{
    {1, 1, 0},
    {-1, 1, 0},
    {1, -1, 0},
    {-1, -1, 0},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
    {0, 1, 1},
    {0, -1, 1},
    {0, 1, -1},
    {0, -1, -1},
};

constexpr short kP[256]{
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142,
    8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165,
    71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,
    55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,
    18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250,
    124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189,
    28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,
    129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,
    242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,
    181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114,
    67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180,
};

struct Tables {
    std::array<short, 512> perm{}, permMod12{};
    Tables() {
        for (int i = 0; i < 512; i++) {
            perm[i]      = kP[i & 255];
            permMod12[i] = static_cast<short>(perm[i] % 12);
        }
    }
};

Tables const& tables() {
    static Tables const t;
    return t;
}

constexpr float kF3 = 1.0f / 3.0f;
constexpr float kG3 = 1.0f / 6.0f;

int fastfloor(float x) {
    int xi = static_cast<int>(x);
    return x < static_cast<float>(xi) ? xi - 1 : xi;
}

float dot(Grad const& g, float x, float y, float z) { return g.x * x + g.y * y + g.z * z; }

std::uint32_t rgb(float r, float g, float b) {
    return ((static_cast<std::uint32_t>(static_cast<int>((r * 255.0f) + 0.5f)) & 0xFF) << 16)
         | ((static_cast<std::uint32_t>(static_cast<int>((g * 255.0f) + 0.5f)) & 0xFF) << 8)
         | (static_cast<std::uint32_t>(static_cast<int>((b * 255.0f) + 0.5f)) & 0xFF);
}

} // namespace

float simplex3(float xin, float yin, float zin) {
    auto const& perm      = tables().perm;
    auto const& permMod12 = tables().permMod12;
    float       n0, n1, n2, n3;
    float       s  = (xin + yin + zin) * kF3;
    int         i  = fastfloor(xin + s);
    int         j  = fastfloor(yin + s);
    int         k  = fastfloor(zin + s);
    float       t  = static_cast<float>(i + j + k) * kG3;
    float       X0 = static_cast<float>(i) - t;
    float       Y0 = static_cast<float>(j) - t;
    float       Z0 = static_cast<float>(k) - t;
    float       x0 = xin - X0;
    float       y0 = yin - Y0;
    float       z0 = zin - Z0;
    int         i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
        } else if (x0 >= z0) {
            i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 0, k2 = 1;
        } else {
            i1 = 0, j1 = 0, k1 = 1, i2 = 1, j2 = 0, k2 = 1;
        }
    } else {
        if (y0 < z0) {
            i1 = 0, j1 = 0, k1 = 1, i2 = 0, j2 = 1, k2 = 1;
        } else if (x0 < z0) {
            i1 = 0, j1 = 1, k1 = 0, i2 = 0, j2 = 1, k2 = 1;
        } else {
            i1 = 0, j1 = 1, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
        }
    }
    float x1  = x0 - static_cast<float>(i1) + kG3;
    float y1  = y0 - static_cast<float>(j1) + kG3;
    float z1  = z0 - static_cast<float>(k1) + kG3;
    float x2  = x0 - static_cast<float>(i2) + 2.0f * kG3;
    float y2  = y0 - static_cast<float>(j2) + 2.0f * kG3;
    float z2  = z0 - static_cast<float>(k2) + 2.0f * kG3;
    float x3  = x0 - 1.0f + 3.0f * kG3;
    float y3  = y0 - 1.0f + 3.0f * kG3;
    float z3  = z0 - 1.0f + 3.0f * kG3;
    int   ii  = i & 255;
    int   jj  = j & 255;
    int   kk  = k & 255;
    int   gi0 = permMod12[ii + perm[jj + perm[kk]]];
    int   gi1 = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]];
    int   gi2 = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]];
    int   gi3 = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]];
    float t0  = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 < 0) n0 = 0.0f;
    else {
        t0 *= t0;
        n0  = t0 * t0 * dot(kGrad3[gi0], x0, y0, z0);
    }
    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 < 0) n1 = 0.0f;
    else {
        t1 *= t1;
        n1  = t1 * t1 * dot(kGrad3[gi1], x1, y1, z1);
    }
    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 < 0) n2 = 0.0f;
    else {
        t2 *= t2;
        n2  = t2 * t2 * dot(kGrad3[gi2], x2, y2, z2);
    }
    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 < 0) n3 = 0.0f;
    else {
        t3 *= t3;
        n3  = t3 * t3 * dot(kGrad3[gi3], x3, y3, z3);
    }
    return 32.0f * (n0 + n1 + n2 + n3);
}

float fractalNoise(int iteration, float size, int x, int y, int z) {
    if (iteration == 0) return 0;
    float off = static_cast<float>(iteration) * size;
    return ((simplex3((static_cast<float>(x) + off) / size, (static_cast<float>(y) + off) / size, (static_cast<float>(z) + off) / size) + 1.0f) * 0.5f)
         + fractalNoise(iteration - 1, size, x, y, z);
}

float rippleFractalNoise(int iterations, float size, int x, int y, int z, float minimum, float maximum, float frequency) {
    float i = maximum - minimum;
    return std::abs(std::fmod(fractalNoise(iterations, size, x, y, z) * frequency, 2 * i) - i) + minimum;
}

std::uint32_t hsvToRgb(float hue, float saturation, float value) {
    float const normalisedHue = hue - std::floor(hue);
    int const   h             = static_cast<int>(normalisedHue * 6);
    float const f             = normalisedHue * 6 - static_cast<float>(h);
    float const p             = value * (1 - saturation);
    float const q             = value * (1 - f * saturation);
    float const t             = value * (1 - (1 - f) * saturation);
    switch (h) {
    case 0: return rgb(value, t, p);
    case 1: return rgb(q, value, p);
    case 2: return rgb(p, value, t);
    case 3: return rgb(p, q, value);
    case 4: return rgb(t, p, value);
    default: return rgb(value, p, q);
    }
}

std::uint32_t towerwoodColor(int x, int y, int z) {
    float f = rippleFractalNoise(2, 32.0f, x, y, z, 0.4f, 1.0f, 2.0f);
    return hsvToRgb(0.1f, 1.0f - f, (f + 2.0f) / 3.0f);
}

} // namespace gf::core::building
