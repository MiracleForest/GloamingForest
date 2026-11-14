// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/BiomeColorAlgorithms.java
//   src/main/java/gloamingforest/client/FoliageColorHandler.java
//   src/main/java/gloamingforest/GloamingEnumExtensions.java
//   src/main/java/gloamingforest/util/ColorUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The colour-map constants: upstream's flat swamp and dark forest colours are
// ((map & 0xFEFEFE) + tint) / 2, so only the map value's upper 7 bits per channel matter. The dark
// forest's are vanilla's forest colours (GrassColor.get(0.7, 0.8) = #79C05A, FoliageColor.get(0.7,
// 0.8) = #59AE30); the swamp's are recovered from the colours upstream's biome JSON stores for the swamp
// (#5C694E grass, #496137 foliage: BiomeMaker writes the modifier's result), which also reproduces
// the dark forest's JSON colours from the known values (tests/biomes/ColorsTest.cpp checks all four).
#include "core/biomes/Colors.h"

#include <cmath>
#include <cstdint>

#include "core/landmark/Landmark.h"
#include "core/noise/PerlinSimplex.h"

#pragma clang fp contract(off)

namespace gf::core::biomes::colors {

namespace {

constexpr std::string_view kEnchanted        = "gloamingforest:enchanted_forest";
constexpr std::string_view kSwamp            = "gloamingforest:swamp";
constexpr std::string_view kDarkForest       = "gloamingforest:dark_forest";
constexpr std::string_view kDarkForestCenter = "gloamingforest:dark_forest_center";
constexpr std::string_view kSpooky           = "gloamingforest:spooky_forest";

/// Biome.TEMPERATURE_NOISE.getValue(x * 0.0225, z * 0.0225, false).
double temperatureNoise(double x, double z) {
    return core::noise::PerlinSimplexNoise::temperatureNoise().getValue(x * 0.0225, z * 0.0225, false);
}

/// Java int multiplication / shift on wrapping 32-bit values.
std::int32_t imul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b)); }

} // namespace

int blendColors(int a, int b, double ratio) {
    std::int32_t const mask1 = 0x00FF00FF;
    std::int32_t const mask2 = static_cast<std::int32_t>(0xFF00FF00u);
    std::int32_t       f2    = static_cast<std::int32_t>(256 * ratio);
    std::int32_t       f1    = 256 - f2;
    std::int32_t       lo    = static_cast<std::int32_t>(std::uint32_t(imul(a & mask1, f1)) + std::uint32_t(imul(b & mask1, f2)));
    std::int32_t       hi    = static_cast<std::int32_t>(std::uint32_t(imul(a & mask2, f1)) + std::uint32_t(imul(b & mask2, f2)));
    return ((lo >> 8) & mask1) | ((hi >> 8) & mask2);
}

int enchanted(int originalColor, int x, int z) {
    auto center = core::landmark::nearestCenter(x / 16, z / 16); // Java int division (toward zero)
    int  cx     = center.x;
    int  cz     = center.z;
    // (int) Mth.sqrt(int): the int sum widened to float, (float) Math.sqrt(double), truncated
    int  sum    = (cx - x) * (cx - x) + (cz - z) * (cz - z);
    int  dist   = static_cast<int>(static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(sum)))));
    int  color  = dist * 16;
    color      %= 512;
    if (color > 255) color = 511 - color;
    color = 255 - color;
    return (originalColor & 0xFFFF00) + color;
}

int swamp(bool foliage) { return (((foliage ? kFoliageSwampMap : kGrassSwampMap) & 0xFEFEFE) + 0x4E0E4E) / 2; }

int darkForest(bool foliage) { return (((foliage ? kFoliageForestMap : kGrassForestMap) & 0xFEFEFE) + 0x1E0E4E) / 2; }

int darkForestCenterGrass(double x, double z) { return temperatureNoise(x, z) < -0.2 ? 0x667540 : 0x554114; }

int darkForestCenterFoliage(double x, double z) {
    // (noise + 1) / 2 is never below -0.1 in upstream: always 0xE94E14 (kept as upstream has it)
    double noise = (temperatureNoise(x, z) + 1.0) / 2.0;
    return noise < -0.1 ? 0xF9821E : 0xE94E14;
}

double spookyNoise(double x, double z) { return (temperatureNoise(x, z) + 1.0) / 2.0; }

int spookyGrass(double x, double z) {
    double noise = spookyNoise(x, z);
    return blendColors(0xC43323, 0x5BC423, noise > 0.6 ? noise * 0.1 : noise);
}

int spookyFoliage(double x, double z) {
    double noise = spookyNoise(x, z);
    return blendColors(0xFF0101, 0x49FF01, noise > 0.6 ? noise * 0.2 : noise);
}

bool isPositional(std::string_view id) { return id == kEnchanted || id == kDarkForestCenter || id == kSpooky; }

bool hasModifier(std::string_view id) { return isPositional(id) || id == kSwamp || id == kDarkForest; }

std::optional<int> grass(std::string_view id, int base, double x, double z) {
    if (id == kEnchanted) return enchanted(base, static_cast<int>(x), static_cast<int>(z));
    if (id == kSwamp) return swamp(false);
    if (id == kDarkForest) return darkForest(false);
    if (id == kDarkForestCenter) return darkForestCenterGrass(x, z);
    if (id == kSpooky) return spookyGrass(x, z);
    return std::nullopt;
}

std::optional<int> foliage(std::string_view id, int base, double x, double z) {
    if (id == kSpooky) return spookyFoliage(x, z);
    if (id == kEnchanted) return enchanted(base, static_cast<int>(x), static_cast<int>(z));
    if (id == kDarkForestCenter) return darkForestCenterFoliage(x, z);
    if (id == kDarkForest) return darkForest(true);
    if (id == kSwamp) return swamp(true);
    return std::nullopt;
}

namespace {

struct StaticColours {
    std::string_view id;
    int              grass, foliage;
};

// upstream's biome JSON (src/generated/resources/data/gloamingforest/worldgen/biome/*.json): the explicit
// colours of the fire swamp and the snowy forest; the others the colour map at (temperature,
// downfall) as GrassColor.get / FoliageColor.get read it (Java float climate widened to double).
// The biomes with a modifier (enchanted forest, dark forest centre, spooky forest, swamp, dark
// forest) are not here: their modifier gives every colour.
constexpr StaticColours kStatic[] = {
    {"gloamingforest:clearing", 0x92BC58, 0x77AB2F},              // 0.8, 0.4
    {"gloamingforest:dense_forest", 0x79BF5A, 0x59AE30},          // 0.7, 0.8
    {"gloamingforest:dense_mushroom_forest", 0x61C64B, 0x3BB61E}, // 0.8, 1.0
    {"gloamingforest:final_plateau", 0xA6BB4D, 0x91A922},         // 1.0, 0.2
    {"gloamingforest:fire_swamp", 0x572E23, 0x64260F},            // JSON
    {"gloamingforest:firefly_forest", 0x7CBB6C, 0x5BAB46},        // 0.5, 1.0
    {"gloamingforest:forest", 0x8DB870, 0x71A74D},                // 0.5, 0.5
    {"gloamingforest:glacier", 0x84B491, 0x66A275},               // 0.08, 0.1
    {"gloamingforest:highlands", 0x88B777, 0x6AA755},             // 0.4, 0.7
    {"gloamingforest:highlands_underground", 0x96B47F, 0x7BA35F}, // 0.35, 0.0
    {"gloamingforest:lake", 0x70C05B, 0x4CB031},                  // 0.66, 1.0
    {"gloamingforest:mushroom_forest", 0x71C34F, 0x4EB224},       // 0.8, 0.8
    {"gloamingforest:oak_savannah", 0xB8B55B, 0xA6A432},          // 0.9, 0.0
    {"gloamingforest:snowy_forest", 0xFFFFFF, 0xFFFFFF},          // JSON
    {"gloamingforest:stream", 0x9BB574, 0x83A352},                // 0.5, 0.1
    {"gloamingforest:thornlands", 0x8FB582, 0x74A362},            // 0.3, 0.2
    {"gloamingforest:underground", 0xABB568, 0x97A342},           // 0.7, 0.0
};

StaticColours const* staticOf(std::string_view id) {
    for (auto const& s : kStatic)
        if (s.id == id) return &s;
    return nullptr;
}

/// upstream's JSON grass colour before the modifier (BiomeMaker): the enchanted forest's 0x00FFFF.
constexpr int kEnchantedJson = 0x00FFFF;

struct WaterColours {
    std::string_view id;
    int              water, fog;
};
// upstream's biome JSON effects.water_color / water_fog_color where they are not 0x3F76E4 / 0x050533
constexpr WaterColours kWater[] = {
    {"gloamingforest:dense_forest", 0x005522, 0x005522},
    {"gloamingforest:fire_swamp", 0x6C2C2C, 0x6C2C2C},
    {"gloamingforest:spooky_forest", 0xBC8857, 0xBC8857},
    {"gloamingforest:swamp", 0x95B55F, 0x95B55F},
};

bool isGloaming(std::string_view id) { return staticOf(id) != nullptr || hasModifier(id); }

} // namespace

std::optional<int> staticGrass(std::string_view id) {
    auto const* s = staticOf(id);
    return s ? std::optional<int>(s->grass) : std::nullopt;
}

std::optional<int> staticFoliage(std::string_view id) {
    auto const* s = staticOf(id);
    return s ? std::optional<int>(s->foliage) : std::nullopt;
}

std::optional<int> grassAt(std::string_view id, double x, double z) {
    if (auto v = grass(id, kEnchantedJson, x, z)) return v;
    return staticGrass(id);
}

std::optional<int> foliageAt(std::string_view id, double x, double z) {
    if (auto v = foliage(id, kEnchantedJson, x, z)) return v;
    return staticFoliage(id);
}

std::optional<int> water(std::string_view id) {
    if (!isGloaming(id)) return std::nullopt;
    for (auto const& w : kWater)
        if (w.id == id) return w.water;
    return 0x3F76E4;
}

std::optional<int> waterFog(std::string_view id) {
    if (!isGloaming(id)) return std::nullopt;
    for (auto const& w : kWater)
        if (w.id == id) return w.fog;
    return 0x050533;
}

bool closerWaterFog(std::string_view id) { return id == kSpooky || id == kSwamp || id == "gloamingforest:fire_swamp"; }

int blend(int x, int z, int radius, std::function<int(int, int)> const& colorAt) {
    if (radius <= 0) return colorAt(x, z);
    int n = (radius * 2 + 1) * (radius * 2 + 1);
    int r = 0, g = 0, b = 0;
    // Cursor3D order (x fastest, then z) does not change the sums
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int c  = colorAt(x + dx, z + dz);
            r     += (c & 0xFF0000) >> 16;
            g     += (c & 0xFF00) >> 8;
            b     += c & 0xFF;
        }
    }
    return (r / n & 0xFF) << 16 | (g / n & 0xFF) << 8 | (b / n & 0xFF);
}

} // namespace gf::core::biomes::colors
