// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ColorHandler.java
//   src/main/java/twilightforest/util/ColorUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The colour of a falling_dust particle (Java's FallingDustParticle.Provider): a FallingBlock's
// getDustColor, else BlockColors#getColor(state, level, pos), which calls the block's BlockColor
// with a null level and position and tint index 0 (vanilla's BlockColors.createDefault and TF's
// ColorHandler#registerBlockColors), and falls back to the map colour for a block without one.
// -1 (a BlockColor that needs a level, or tint index 1) draws the particle white.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

#include "feature/fx/FxInternal.h"

namespace gf::fx {

namespace {

// net.minecraft.world.level.FoliageColor / GrassColor (1.21.1), with a null level
constexpr int kFoliageDefault   = 0x48B518; // FoliageColor.getDefaultColor() 4764952
constexpr int kFoliageEvergreen = 0x619961; // FoliageColor.getEvergreenColor() 6396257
constexpr int kFoliageBirch     = 0x80A755; // FoliageColor.getBirchColor() 8431445
constexpr int kFoliageMangrove  = 0x92C648; // FoliageColor.getMangroveColor() 9619016
// GrassColor.getDefaultColor() = get(0.5, 1.0): the grass colormap's pixel (127, 127)
constexpr int kGrassDefault = 0x7CBB6C;
constexpr int kLilyPad      = 7455580; // the lily pad's (and TF's huge lily pad's) colour without a level

// ColorUtil#rgb
int rgb(float r, float g, float b) {
    return ((static_cast<int>(r * 255.f + 0.5f) & 0xFF) << 16) | ((static_cast<int>(g * 255.f + 0.5f) & 0xFF) << 8)
         | (static_cast<int>(b * 255.f + 0.5f) & 0xFF);
}

// ColorUtil#hsvToRGB
int hsvToRgb(float hue, float saturation, float value) {
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

// ColorUtil#rgbToHSV (Java's `1 / 2` is an integer division: 0)
std::array<float, 3> rgbToHsv(int r, int g, int b) {
    float       h    = 0, s;
    float const rabs = static_cast<float>(r) / 255.0f;
    float const gabs = static_cast<float>(g) / 255.0f;
    float const babs = static_cast<float>(b) / 255.0f;
    float const v    = std::max(rabs, std::max(gabs, babs));
    float const diff = v - std::min(rabs, std::min(gabs, babs));
    auto        diffc = [&](float c) { return (v - c) / 6 / diff + 0; };
    if (diff == 0) {
        h = s = 0;
    } else {
        s              = diff / v;
        float const rr = diffc(rabs);
        float const gg = diffc(gabs);
        float const bb = diffc(babs);
        if (rabs == v) h = bb - gg;
        else if (gabs == v) h = (1.0f / 3.0f) + rr - bb;
        else if (babs == v) h = (2.0f / 3.0f) + gg - rr;
        if (h < 0) h += 1;
        else if (h > 1) h -= 1;
    }
    return {h, s, v};
}

// TF's aurora block without a level: hsvToRGB(0.45, 1, 1)
int auroraColor() { return hsvToRgb(0.45f, 1.0f, 1.0f); }

// TF's aurora pillar, slab and auroralized glass: the aurora colour paled
int auroraPaleColor() {
    int const normal = auroraColor();
    auto      hsb    = rgbToHsv((normal >> 16) & 255, (normal >> 8) & 255, normal & 255);
    return hsvToRgb(hsb[0], hsb[1] * 0.5f, std::min(hsb[2] + 0.4f, 0.9f));
}

// RedStoneWireBlock#getColorForPower
int redstoneColor(int power) {
    float const f  = static_cast<float>(power) / 15.0f;
    float const f1 = f * 0.6f + (f > 0.0f ? 0.4f : 0.3f);
    float const f2 = std::clamp(f * f * 0.7f - 0.5f, 0.0f, 1.0f);
    float const f3 = std::clamp(f * f * 0.6f - 0.7f, 0.0f, 1.0f);
    // FastColor.ARGB32.color(Mth.floor(f1 * 255), Mth.floor(f2 * 255), Mth.floor(f3 * 255))
    return (static_cast<int>(std::floor(f1 * 255.0f)) << 16) | (static_cast<int>(std::floor(f2 * 255.0f)) << 8)
         | static_cast<int>(std::floor(f3 * 255.0f));
}

// StemBlock's colour by age: FastColor.ARGB32.color(age * 32, 255 - age * 8, age * 4)
int stemColor(int age) { return ((age * 32) << 16) | ((255 - age * 8) << 8) | (age * 4); }

enum class Rule { Fixed, Redstone, Stem };
struct Entry {
    Rule rule  = Rule::Fixed;
    int  color = -1;
};

std::unordered_map<std::string, Entry> const& table() {
    static std::unordered_map<std::string, Entry> const m = [] {
        std::unordered_map<std::string, Entry> t;
        auto fixed = [&](std::initializer_list<char const*> ids, int c) {
            for (auto const* id : ids) t[id] = Entry{Rule::Fixed, c};
        };
        // vanilla (BlockColors.createDefault), Bedrock's ids
        fixed({"minecraft:large_fern", "minecraft:tall_grass", "minecraft:grass_block", "minecraft:fern", "minecraft:short_grass"}, kGrassDefault);
        fixed({"minecraft:pink_petals"}, -1); // tint index 0
        fixed({"minecraft:spruce_leaves"}, kFoliageEvergreen);
        fixed({"minecraft:birch_leaves"}, kFoliageBirch);
        fixed({"minecraft:oak_leaves", "minecraft:jungle_leaves", "minecraft:acacia_leaves", "minecraft:dark_oak_leaves", "minecraft:vine"}, kFoliageDefault);
        fixed({"minecraft:mangrove_leaves"}, kFoliageMangrove);
        fixed({"minecraft:water", "minecraft:flowing_water", "minecraft:bubble_column"}, -1);
        fixed({"minecraft:reeds", "minecraft:sugar_cane"}, -1);
        fixed({"minecraft:waterlily", "minecraft:lily_pad"}, kLilyPad);
        t["minecraft:redstone_wire"] = Entry{Rule::Redstone};
        t["minecraft:melon_stem"]    = Entry{Rule::Stem};
        t["minecraft:pumpkin_stem"]  = Entry{Rule::Stem};
        // TF (ColorHandler#registerBlockColors), with a null getter and position, tint index 0
        fixed({"twilightforest:aurora_block"}, auroraColor());
        fixed({"twilightforest:aurora_pillar", "twilightforest:aurora_slab", "twilightforest:auroralized_glass"}, auroraPaleColor());
        fixed({"twilightforest:smoker", "twilightforest:fire_jet"}, kGrassDefault); // the short grass colour
        fixed({"twilightforest:huge_lily_pad"}, kLilyPad);
        fixed({"twilightforest:time_leaves"}, 106 << 16 | 156 << 8 | 23);
        fixed({"twilightforest:transformation_leaves"}, 108 << 16 | 204 << 8 | 234);
        fixed({"twilightforest:mining_leaves"}, 252 << 16 | 241 << 8 | 68);
        fixed({"twilightforest:sorting_leaves"}, 54 << 16 | 76 << 8 | 3);
        fixed({"twilightforest:towerwood", "twilightforest:cracked_towerwood", "twilightforest:infested_towerwood", "twilightforest:mossy_towerwood"}, -1);
        fixed({"twilightforest:twilight_oak_leaves", "twilightforest:dark_leaves", "twilightforest:hardened_dark_leaves", "twilightforest:giant_leaves", "twilightforest:fallen_leaves", "twilightforest:rainbow_oak_leaves"}, kFoliageDefault);
        fixed({"twilightforest:canopy_leaves", "twilightforest:beanstalk_leaves", "twilightforest:thorn_leaves"}, kFoliageEvergreen);
        fixed({"twilightforest:mangrove_leaves"}, kFoliageBirch);
        fixed({"twilightforest:fiddlehead", "twilightforest:potted_fiddlehead"}, kGrassDefault);
        for (auto const* wood : {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "vangrove", "cherry", "twilight_oak", "canopy", "mangrove", "dark", "time", "transformation", "mining", "sorting"}) {
            t[std::string("twilightforest:hollow_") + wood + "_log_horizontal"] = Entry{Rule::Fixed, -1};
            t[std::string("twilightforest:hollow_") + wood + "_log_climbable"]  = Entry{Rule::Fixed, -1};
        }
        for (auto const* stem : {"crimson", "warped"}) {
            t[std::string("twilightforest:hollow_") + stem + "_stem_horizontal"] = Entry{Rule::Fixed, -1};
            t[std::string("twilightforest:hollow_") + stem + "_stem_climbable"]  = Entry{Rule::Fixed, -1};
        }
        fixed({"twilightforest:twilight_portal_miniature_structure", "twilightforest:naga_courtyard_miniature_structure"}, -1);
        fixed({"twilightforest:pink_castle_rune_brick", "twilightforest:pink_castle_door"}, 0xFF00FF);
        fixed({"twilightforest:blue_castle_rune_brick", "twilightforest:blue_castle_door"}, 0x00FFFF);
        fixed({"twilightforest:yellow_castle_rune_brick", "twilightforest:yellow_castle_door"}, 0xFFFF00);
        fixed({"twilightforest:violet_castle_rune_brick", "twilightforest:violet_castle_door"}, 0x4B0082);
        fixed({"twilightforest:violet_force_field"}, 0x5C1074);
        fixed({"twilightforest:pink_force_field"}, 0xFA057E);
        fixed({"twilightforest:orange_force_field"}, 0xFF5B02);
        fixed({"twilightforest:green_force_field"}, 0x89E701);
        fixed({"twilightforest:blue_force_field"}, 0x0DDEFF);
        return t;
    }();
    return m;
}

} // namespace

std::optional<int> fallingDustColor(std::string const& blockId, std::function<std::optional<int>(std::string_view)> const& intState) {
    // FallingBlock#getDustColor overrides (ColoredFallingBlock): sand, red sand, gravel
    if (blockId == "minecraft:sand") return 14406560;
    if (blockId == "minecraft:red_sand") return 11098145;
    if (blockId == "minecraft:gravel") return -8356741 & 0xFFFFFF;
    auto const& t  = table();
    auto        it = t.find(blockId);
    if (it == t.end()) return std::nullopt; // no BlockColor: the map colour
    switch (it->second.rule) {
    case Rule::Redstone: return redstoneColor(std::clamp(intState ? intState("redstone_signal").value_or(0) : 0, 0, 15));
    case Rule::Stem: {
        // Bedrock's stems are Java's stem and attached stem in one (facing_direction 2..5: attached)
        int const facing = intState ? intState("facing_direction").value_or(0) : 0;
        if (facing >= 2) return 14731036; // AttachedStemBlock's colour
        return stemColor(std::clamp(intState ? intState("growth").value_or(0) : 0, 0, 7));
    }
    default: return it->second.color;
    }
}

Rgba dustRgb(int color) {
    if (color == -1) return Rgba{1.f, 1.f, 1.f, 1.f};
    return Rgba{static_cast<float>((color >> 16) & 0xFF) / 255.f, static_cast<float>((color >> 8) & 0xFF) / 255.f, static_cast<float>(color & 0xFF) / 255.f, 1.f};
}

} // namespace gf::fx
