// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/MagicMapItem.java
//   src/main/java/gloamingforest/item/MazeMapItem.java
//   src/main/java/gloamingforest/command/MapBiomesCommand.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/uncraftmaps/Maps.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace gf::core::uncraftmaps {

std::pair<int, int> magicMapCenter(int x, int z) {
    constexpr int mapSize = 2048;
    // (int) Math.round((double) (x - 1024) / mapSize)
    auto round = [](double v) { return static_cast<int>(std::floor(v + 0.5)); };
    int  roundX = round(static_cast<double>(x - 1024) / mapSize);
    int  roundZ = round(static_cast<double>(z - 1024) / mapSize);
    return {roundX * mapSize + 1024, roundZ * mapSize + 1024};
}

std::pair<int, int> mazeMapCenter(int x, int z, int scale) {
    int mapSize = 128 * (1 << scale);
    int roundX  = static_cast<int>(std::floor((x + 64.0) / static_cast<double>(mapSize)));
    int roundZ  = static_cast<int>(std::floor((z + 64.0) / static_cast<double>(mapSize)));
    return {roundX * mapSize + mapSize / 2 - 64, roundZ * mapSize + mapSize / 2 - 64};
}

int viewerPixel(double pos, int center, int blocksPerPixel) {
    return javaDiv(static_cast<int>(std::floor(pos - static_cast<double>(center))), blocksPerPixel) + 64;
}

int brightnessModifier(int brightness) {
    switch (brightness) {
    case 0: return 180;
    case 1: return 220;
    case 2: return 255;
    case 3: return 135;
    default: return 220;
    }
}

std::uint32_t shade(std::uint32_t rgb, int brightness) {
    unsigned i = static_cast<unsigned>(brightnessModifier(brightness));
    unsigned r = ((rgb >> 16) & 0xFFu) * i / 255u;
    unsigned g = ((rgb >> 8) & 0xFFu) * i / 255u;
    unsigned b = (rgb & 0xFFu) * i / 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void magicMapScan(int viewerX, int viewerZ, std::function<void(int, int)> const& visit) {
    int const r = kMagicViewRadius;
    for (int xPixel = viewerX - r + 1; xPixel < viewerX + r; ++xPixel) {
        for (int zPixel = viewerZ - r - 1; zPixel < viewerZ + r; ++zPixel) {
            if (xPixel < 0 || zPixel < 0 || xPixel >= 128 || zPixel >= 128) continue;
            int  dx         = xPixel - viewerX;
            int  dz         = zPixel - viewerZ;
            bool shouldFuzz = dx * dx + dz * dz > (r - 2) * (r - 2);
            if (dx * dx + dz * dz < r * r && (!shouldFuzz || ((xPixel + zPixel) & 1) != 0)) visit(xPixel, zPixel);
        }
    }
}

int magicSampleBlock(int center, int cell) {
    int start = (javaDiv(center, kMagicBlocksPerPixel) - 64) * kMagicBiomesPerPixel;
    return start * kMagicBiomesPerPixel + cell * kMagicBiomesPerPixel;
}

int magicPixelBlock(int center, int pixel) { return (javaDiv(center, kMagicBlocksPerPixel) + pixel - 64) * kMagicBlocksPerPixel; }

void mazeMapScan(
    int                                                              viewerX,
    int                                                              viewerZ,
    int                                                              radius,
    int                                                              step,
    std::function<std::optional<std::uint32_t>(int, int)> const&    colour,
    std::function<bool(int, int, std::uint32_t)> const&              write
) {
    bool flag = false;
    for (int xPixel = viewerX - radius + 1; xPixel < viewerX + radius; ++xPixel) {
        if ((xPixel & 15) != (step & 15) && !flag) continue;
        flag = false;
        for (int zPixel = viewerZ - radius - 1; zPixel < viewerZ + radius; ++zPixel) {
            if (xPixel < 0 || zPixel < -1 || xPixel >= 128 || zPixel >= 128) continue;
            int  dx         = xPixel - viewerX;
            int  dz         = zPixel - viewerZ;
            bool shouldFuzz = dx * dx + dz * dz > (radius - 2) * (radius - 2);
            auto c          = colour(xPixel, zPixel);
            if (!c) continue; // an empty chunk: nothing
            if (zPixel >= 0 && dx * dx + dz * dz < radius * radius && (!shouldFuzz || ((xPixel + zPixel) & 1) != 0)) {
                if (write(xPixel, zPixel, *c)) flag = true;
            }
        }
    }
}

std::optional<DecorationPos> decorationPos(double x, double z, int centerX, int centerZ, int scale, double yRot) {
    int   i  = 1 << scale;
    float f  = static_cast<float>(x - static_cast<double>(centerX)) / static_cast<float>(i);
    float f1 = static_cast<float>(z - static_cast<double>(centerZ)) / static_cast<float>(i);
    auto  b0 = static_cast<std::int8_t>(static_cast<int>(static_cast<double>(f * 2.0f) + 0.5));
    auto  b1 = static_cast<std::int8_t>(static_cast<int>(static_cast<double>(f1 * 2.0f) + 0.5));
    if (!(f >= -63.0f && f1 >= -63.0f && f <= 63.0f && f1 <= 63.0f)) return std::nullopt;
    yRot += yRot < 0.0 ? -8.0 : 8.0;
    auto b2 = static_cast<std::int8_t>(static_cast<int>(yRot * 16.0 / 360.0));
    return DecorationPos{b0, b1, b2};
}

std::string decorationName(std::string const& assetId, int x, int z) { return assetId + "_" + std::to_string(x) + "_" + std::to_string(z); }

std::optional<std::uint32_t> biomePngColor(std::string const& biomeId) {
    static std::unordered_map<std::string, std::uint32_t> const colours{
        {"gloamingforest:stream", 0x0000FF},          {"gloamingforest:lake", 0x0000FF},
        {"gloamingforest:clearing", 0x84F582},        {"gloamingforest:oak_savannah", 0xEFF582},
        {"gloamingforest:forest", 0x00FF00},          {"gloamingforest:dense_forest", 0x00AA00},
        {"gloamingforest:firefly_forest", 0x58FC66},  {"gloamingforest:enchanted_forest", 0x00FFFF},
        {"gloamingforest:spooky_forest", 0x7700FF},   {"gloamingforest:mushroom_forest", 0xCC008B},
        {"gloamingforest:dense_mushroom_forest", 0xB830B8}, {"gloamingforest:swamp", 0x00CCBB},
        {"gloamingforest:fire_swamp", 0x8C0000},      {"gloamingforest:dark_forest", 0x193D0D},
        {"gloamingforest:dark_forest_center", 0x9D4F00}, {"gloamingforest:snowy_forest", 0xFFFFFF},
        {"gloamingforest:glacier", 0x82BFF5},         {"gloamingforest:highlands", 0x644100},
        {"gloamingforest:thornlands", 0x80645A},      {"gloamingforest:final_plateau", 0x808080},
    };
    auto it = colours.find(biomeId);
    if (it == colours.end()) return std::nullopt;
    return 0xFF000000u | it->second;
}

std::uint32_t biomePngDataMapColor(std::uint32_t rgb) {
    std::uint32_t j = (rgb >> 16) & 0xFFu, k = (rgb >> 8) & 0xFFu, l = rgb & 0xFFu;
    return 0xFF000000u | (l << 16) | (k << 8) | j;
}

std::string decimalFormat00(double value) {
    // DecimalFormat rounds the double's exact decimal value (HALF_EVEN): 0.125 is exact and goes to
    // .12, 0.135 lies below .135 and goes to .13. The exact digits come from printf.
    if (!std::isfinite(value)) return value != value ? "NaN" : (value < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E");
    bool neg = std::signbit(value);
    char exact[512];
    std::snprintf(exact, sizeof(exact), "%.80f", std::fabs(value));
    std::string s(exact);
    auto        dot   = s.find('.');
    std::string whole = s.substr(0, dot);
    std::string frac  = s.substr(dot + 1);
    // the kept digits (whole + 2 decimals) as a number string, and the rest
    std::string kept = whole + frac.substr(0, 2);
    std::string rest = frac.substr(2);
    bool        up   = false;
    if (!rest.empty() && rest[0] > '5') up = true;
    else if (!rest.empty() && rest[0] == '5') {
        bool beyond = rest.find_first_not_of('0', 1) != std::string::npos;
        up          = beyond || ((kept.back() - '0') % 2 == 1);
    }
    if (up) {
        int i = static_cast<int>(kept.size()) - 1;
        while (i >= 0 && kept[static_cast<std::size_t>(i)] == '9') kept[static_cast<std::size_t>(i--)] = '0';
        if (i < 0) kept.insert(kept.begin(), '1');
        else ++kept[static_cast<std::size_t>(i)];
    }
    std::string intPart = kept.substr(0, kept.size() - 2);
    std::string decPart = kept.substr(kept.size() - 2);
    auto        nz      = intPart.find_first_not_of('0');
    intPart             = nz == std::string::npos ? std::string() : intPart.substr(nz); // "#": no leading zero
    bool zero = intPart.empty() && decPart == "00";
    return (neg && !zero ? "-" : "") + intPart + "." + decPart;
}

std::string biomePngPath(std::int64_t seed, int startX, int startZ, int width, int height) {
    auto s = std::to_string(seed);
    return "generated/biomemaps/" + s + "/biome_map-" + s + "-(" + std::to_string(startX) + "." + std::to_string(startZ) + ")-("
         + std::to_string(startX + width) + "." + std::to_string(startZ + height) + ").png";
}

BiomePngPlan biomePngPlan(int width, int height) {
    BiomePngPlan p;
    if (width <= 0 || height <= 0) { // new NativeImage(width, height): "Invalid texture size"
        p.fails = true;
        return p;
    }
    int progressUpdate = height / 8;
    for (int x = 0; x < height; ++x) {
        // setPixelRGBA(x, z) for z in 0..width-1: x must be < width, z < height
        if (x >= width || width > height) {
            p.fails = true;
            return p;
        }
        p.completedRows = x + 1;
        if (progressUpdate == 0) { // x % 0
            p.fails = true;
            return p;
        }
        if (x % progressUpdate == 0) ++p.progressRows;
    }
    return p;
}

} // namespace gf::core::uncraftmaps
