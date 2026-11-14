// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/ColorHandler.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Block families (F11): the leaf colours upstream's ColorHandler computes from the position alone (time,
// transformation, mining and sorting leaves fade between two colours, rainbow oak leaves), as
// 0xRRGGBB. The client mod tints those leaves with them (src/client/families, gf::client::tint);
// without a world (an item, a vanilla client: tools/families pre-tints the textures) Java uses the
// colour at fade 0 (rainbow oak: FoliageColor.getDefaultColor). Canopy and mangrove leaves blend
// the biome foliage colour at the position (CANOPY_COLORIZER / MANGROVE_COLORIZER, blendOf): the
// client mod applies them to the game's own foliage tint there; without a world Java uses
// FoliageColor.getEvergreenColor / getBirchColor (Blend::withoutWorld: the client mod's colour when
// it can't ask the game for the foliage colour; a vanilla client: Bedrock's evergreen / birch tint
// methods). No game headers: both builds include it (the server's selftests check the values).

#include <cstdint>
#include <optional>
#include <string_view>

namespace gf::feature::families::leafcolors {

/// Java's int arithmetic (wrapping, two's complement bits) of a x*a + y*b + z*c.
[[nodiscard]] constexpr std::uint32_t wrap(int x, int y, int z, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    return static_cast<std::uint32_t>(x) * a + static_cast<std::uint32_t>(y) * b + static_cast<std::uint32_t>(z) * c;
}

/// ColorHandler's seasonal fade of the magic leaves: `fade` from the position folded to 0..255,
/// the colour between `spring` (fade 0) and `fall` (255) truncated per channel as Java's (int).
[[nodiscard]] constexpr unsigned fadeColor(std::uint32_t fade, unsigned spring, unsigned fall) {
    if ((fade & 256u) != 0) fade = 255u - (fade & 255u);
    fade &= 255u;
    float s = static_cast<float>(255u - fade) / 255.0f;
    float f = static_cast<float>(fade) / 255.0f;
    auto  c = [&](int shift) {
        return static_cast<unsigned>(static_cast<int>(s * static_cast<float>((spring >> shift) & 0xFF) + f * static_cast<float>((fall >> shift) & 0xFF))) << shift;
    };
    return c(16) | c(8) | c(0);
}

/// GloamingBlocks.TIME_LEAVES: (106, 156, 23) to (251, 108, 27), fade x*16 + y*16 + z*16.
[[nodiscard]] constexpr unsigned time(int x, int y, int z) { return fadeColor(wrap(x, y, z, 16, 16, 16), 0x6A9C17, 0xFB6C1B); }
/// GloamingBlocks.TRANSFORMATION_LEAVES: (108, 204, 234) to (96, 107, 121), fade x*27 + y*63 + z*39.
[[nodiscard]] constexpr unsigned transformation(int x, int y, int z) { return fadeColor(wrap(x, y, z, 27, 63, 39), 0x6CCCEA, 0x606B79); }
/// GloamingBlocks.MINING_LEAVES: (252, 241, 68) to (237, 172, 9), fade x*31 + y*33 + z*32.
[[nodiscard]] constexpr unsigned mining(int x, int y, int z) { return fadeColor(wrap(x, y, z, 31, 33, 32), 0xFCF144, 0xEDAC09); }
/// GloamingBlocks.SORTING_LEAVES: (54, 76, 3) to (168, 199, 43), fade x*63 + y*63 + z*63.
[[nodiscard]] constexpr unsigned sorting(int x, int y, int z) { return fadeColor(wrap(x, y, z, 63, 63, 63), 0x364C03, 0xA8C72B); }

/// GloamingBlocks.RAINBOW_OAK_LEAVES ("RAINBOW!"): red from x and y, green from y and z (inverted), blue
/// from x and z, with Java's int bits (green ^ 255 is not masked: its high bits reach the red
/// channel as they do in Java).
[[nodiscard]] constexpr unsigned rainbow(int x, int y, int z) {
    std::uint32_t red = wrap(x, y, 0, 32, 16, 0);
    if ((red & 256u) != 0) red = 255u - (red & 255u);
    red &= 255u;
    std::uint32_t green = wrap(0, y, z, 0, 32, 16);
    if ((green & 256u) != 0) green = 255u - (green & 255u);
    green ^= 255u;
    std::uint32_t blue = wrap(x, 0, z, 16, 0, 32);
    if ((blue & 256u) != 0) blue = 255u - (blue & 255u);
    blue &= 255u;
    return ((red << 16) | (green << 8) | blue) & 0xFFFFFFu;
}

/// ColorHandler.CANOPY_COLORIZER: GloamingBlocks.CANOPY_LEAVES blend the biome foliage colour
/// (BiomeColors.getAverageFoliageColor) with 0x469A66, ((foliage & 0xFEFEFE) + 0x469A66) / 2.
[[nodiscard]] constexpr unsigned canopy(unsigned foliage) { return (((foliage & 0xFEFEFEu) + 0x469A66u) / 2u) & 0xFFFFFFu; }
/// ColorHandler.MANGROVE_COLORIZER: GloamingBlocks.MANGROVE_LEAVES, ((foliage & 0xFEFEFE) + 0xC0E694) / 2.
[[nodiscard]] constexpr unsigned mangrove(unsigned foliage) { return (((foliage & 0xFEFEFEu) + 0xC0E694u) / 2u) & 0xFFFFFFu; }

/// FoliageColor.getEvergreenColor (-10380959): canopy leaves without a world.
inline constexpr unsigned kEvergreen = 0x619961;
/// FoliageColor.getBirchColor (-8345771): mangrove leaves without a world.
inline constexpr unsigned kBirch = 0x80A755;

using BlendFn = unsigned (*)(unsigned foliage);
/// ColorHandler's colour of canopy or mangrove leaves: the colorizer of the biome foliage colour at
/// the position, and the colour without a world or position (getter == null || pos == null).
struct Blend {
    BlendFn  fn;
    unsigned withoutWorld;
};
/// The blend ColorHandler applies to an upstream leaf block id (nullopt: none; those leaves take the
/// foliage colour as it is, or one of `of`).
[[nodiscard]] constexpr std::optional<Blend> blendOf(std::string_view id) {
    if (id == "gloamingforest:canopy_leaves") return Blend{&canopy, kEvergreen};
    if (id == "gloamingforest:mangrove_leaves") return Blend{&mangrove, kBirch};
    return std::nullopt;
}

using ColorFn = unsigned (*)(int x, int y, int z);
/// The position colour of an upstream leaf block id (nullopt: not one ColorHandler colours by position
/// alone).
[[nodiscard]] constexpr std::optional<ColorFn> of(std::string_view id) {
    if (id == "gloamingforest:time_leaves") return &time;
    if (id == "gloamingforest:transformation_leaves") return &transformation;
    if (id == "gloamingforest:mining_leaves") return &mining;
    if (id == "gloamingforest:sorting_leaves") return &sorting;
    if (id == "gloamingforest:rainbow_oak_leaves") return &rainbow;
    return std::nullopt;
}

} // namespace gf::feature::families::leafcolors
