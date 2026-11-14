// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/SimplexNoiseHelper.java
//   src/main/java/gloamingforest/util/ColorUtil.java (rgbToHSV, hsvToRGB)
//   src/main/java/gloamingforest/client/event/ColorHandler.java (aurora block colours)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
// SimplexNoiseHelper's 3D simplex noise is Stefan Gustavson's public domain example code.
#include "core/aurora/AuroraColor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gf::core::aurora {

namespace {

struct Grad {
    float x, y, z;
};
constexpr Grad grad3[] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                          {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

constexpr short p[] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240,
    21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,
    237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111,
    229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,
    209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217,
    226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,
    223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,
    108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241,
    81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,
    150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180};

struct Perm {
    short perm[512];
    short permMod12[512];
    Perm() {
        for (int i = 0; i < 512; i++) {
            perm[i]      = p[i & 255];
            permMod12[i] = static_cast<short>(perm[i] % 12);
        }
    }
};
Perm const& tables() {
    static Perm const t;
    return t;
}

constexpr float F3 = 1.0f / 3.0f;
constexpr float G3 = 1.0f / 6.0f;

int fastfloor(float x) {
    int xi = static_cast<int>(x);
    return x < static_cast<float>(xi) ? xi - 1 : xi;
}
float dot(Grad const& g, float x, float y, float z) { return g.x * x + g.y * y + g.z * z; }

/// ColorUtil#rgb: each channel (int)(c * 255 + 0.5) & 0xFF.
unsigned rgb(float r, float g, float b) {
    auto c = [](float v) { return static_cast<unsigned>(static_cast<int>((v * 255.0F) + 0.5F) & 0xFF); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

/// Java's float `%` (fmod semantics).
float jmod(float a, float b) { return std::fmod(a, b); }

} // namespace

float simplex3(float xin, float yin, float zin) {
    auto const& t = tables();
    float       n0, n1, n2, n3;
    float       s  = (xin + yin + zin) * F3;
    int         i  = fastfloor(xin + s);
    int         j  = fastfloor(yin + s);
    int         k  = fastfloor(zin + s);
    float       tt = static_cast<float>(i + j + k) * G3;
    float       X0 = static_cast<float>(i) - tt;
    float       Y0 = static_cast<float>(j) - tt;
    float       Z0 = static_cast<float>(k) - tt;
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
    float x1 = x0 - static_cast<float>(i1) + G3;
    float y1 = y0 - static_cast<float>(j1) + G3;
    float z1 = z0 - static_cast<float>(k1) + G3;
    float x2 = x0 - static_cast<float>(i2) + 2.0f * G3;
    float y2 = y0 - static_cast<float>(j2) + 2.0f * G3;
    float z2 = z0 - static_cast<float>(k2) + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3;
    float y3 = y0 - 1.0f + 3.0f * G3;
    float z3 = z0 - 1.0f + 3.0f * G3;
    int   ii = i & 255;
    int   jj = j & 255;
    int   kk = k & 255;
    int   gi0 = t.permMod12[ii + t.perm[jj + t.perm[kk]]];
    int   gi1 = t.permMod12[ii + i1 + t.perm[jj + j1 + t.perm[kk + k1]]];
    int   gi2 = t.permMod12[ii + i2 + t.perm[jj + j2 + t.perm[kk + k2]]];
    int   gi3 = t.permMod12[ii + 1 + t.perm[jj + 1 + t.perm[kk + 1]]];
    float t0  = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 < 0) n0 = 0.0f;
    else {
        t0 *= t0;
        n0 = t0 * t0 * dot(grad3[gi0], x0, y0, z0);
    }
    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 < 0) n1 = 0.0f;
    else {
        t1 *= t1;
        n1 = t1 * t1 * dot(grad3[gi1], x1, y1, z1);
    }
    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 < 0) n2 = 0.0f;
    else {
        t2 *= t2;
        n2 = t2 * t2 * dot(grad3[gi2], x2, y2, z2);
    }
    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 < 0) n3 = 0.0f;
    else {
        t3 *= t3;
        n3 = t3 * t3 * dot(grad3[gi3], x3, y3, z3);
    }
    return 32.0f * (n0 + n1 + n2 + n3);
}

float fractalNoiseSum(int iteration, float size, int x, int y, int z) {
    if (iteration == 0) return 0;
    float off = static_cast<float>(iteration) * size;
    float n   = simplex3((static_cast<float>(x) + off) / size, (static_cast<float>(y) + off) / size, (static_cast<float>(z) + off) / size);
    return ((n + 1.0f) * 0.5f) + fractalNoiseSum(iteration - 1, size, x, y, z);
}

float fractalNoise(int iterations, float size, int x, int y, int z) { return fractalNoiseSum(iterations, size, x, y, z) / static_cast<float>(iterations); }

float rippleFractalNoise(int iterations, float size, int x, int y, int z, float minimum, float maximum, float frequency) {
    float i = maximum - minimum;
    return std::fabs(jmod(fractalNoiseSum(iterations, size, x, y, z) * frequency, 2 * i) - i) + minimum;
}

int calcVariant(int x, int y, int z, int variants) {
    // Mth.floor(Mth.positiveModulo(fractalNoise(3, 48, pos) * 120, variants)) (float overloads)
    float v   = fractalNoise(3, 48.0f, x, y, z) * 120.0f;
    float d   = static_cast<float>(variants);
    float mod = jmod(jmod(v, d) + d, d);
    int   i   = static_cast<int>(mod);
    return mod < static_cast<float>(i) ? i - 1 : i;
}

unsigned hsvToRGB(float hue, float saturation, float value) {
    float const normalizedHue = hue - static_cast<float>(std::floor(static_cast<double>(hue)));
    int const   h             = static_cast<int>(normalizedHue * 6);
    float const f             = normalizedHue * 6 - static_cast<float>(h);
    float const pp            = value * (1 - saturation);
    float const q             = value * (1 - f * saturation);
    float const tt            = value * (1 - (1 - f) * saturation);
    switch (h) {
    case 0: return rgb(value, tt, pp);
    case 1: return rgb(q, value, pp);
    case 2: return rgb(pp, value, tt);
    case 3: return rgb(pp, q, value);
    case 4: return rgb(tt, pp, value);
    default: return rgb(value, pp, q); // 5 (upstream throws on anything else; unreachable for finite input)
    }
}

std::array<float, 3> rgbToHSV(int r, int g, int b) {
    float h = 0;
    float s;
    float rabs = static_cast<float>(r) / 255.0F;
    float gabs = static_cast<float>(g) / 255.0F;
    float babs = static_cast<float>(b) / 255.0F;
    float v    = std::max(rabs, std::max(gabs, babs));
    float diff = v - std::min(rabs, std::min(gabs, babs));
    // upstream: c -> (v - c) / 6 / diff + 1 / 2, where 1 / 2 is an int division (0)
    auto diffc = [&](float c) { return (v - c) / 6 / diff + 0; };
    if (diff == 0) {
        h = s = 0;
    } else {
        s        = diff / v;
        float rr = diffc(rabs);
        float gg = diffc(gabs);
        float bb = diffc(babs);
        if (rabs == v) h = bb - gg;
        else if (gabs == v) h = (1.0F / 3.0F) + rr - bb;
        else if (babs == v) h = (2.0F / 3.0F) + gg - rr;
        if (h < 0) h += 1;
        else if (h > 1) h -= 1;
    }
    return {h, s, v};
}

unsigned auroraBlockColor(int x, int y, int z) {
    // pos.above(128)
    return hsvToRGB(rippleFractalNoise(2, 128.0f, x, y + 128, z, 0.37f, 0.67f, 1.5f), 1.0f, 1.0f);
}

unsigned auroraBlockItemColor() { return hsvToRGB(0.45F, 1.0f, 1.0f); }

unsigned auroraPaleColor(unsigned c) {
    int  red   = static_cast<int>((c >> 16) & 255);
    int  green = static_cast<int>((c >> 8) & 255);
    int  blue  = static_cast<int>(c & 255);
    auto hsb   = rgbToHSV(red, green, blue);
    return hsvToRGB(hsb[0], hsb[1] * 0.5F, std::min(hsb[2] + 0.4F, 0.9F));
}

} // namespace gf::core::aurora
