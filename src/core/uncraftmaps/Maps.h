// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/MagicMapItem.java
//   src/main/java/twilightforest/item/MazeMapItem.java
//   src/main/java/twilightforest/command/MapBiomesCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The game-independent maths of TF's maps (U13): the map centres, the pixel scans of the magic
// and maze map updates, Java's MapColor shading, map decoration coordinates and the biomepng
// command's colours, number format and file name.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace gf::core::uncraftmaps {

/// Java's int division (truncates towards zero), as `Mth.floor(viewer.getX() - centerX) / bpp`.
[[nodiscard]] constexpr int javaDiv(int a, int b) { return b == 0 ? 0 : a / b; }

/// MagicMapItem#getMagicMapCenter: the 2048-block magic map grid, round((x - 1024) / 2048) * 2048 + 1024.
[[nodiscard]] std::pair<int, int> magicMapCenter(int x, int z);
/// MazeMapItem#createMapData: floor((x + 64) / mapSize) * mapSize + mapSize / 2 - 64 (mapSize = 128 << scale).
[[nodiscard]] std::pair<int, int> mazeMapCenter(int x, int z, int scale);

/// The pixel of a viewer on a map: floor(pos - center) / blocksPerPixel + 64 (Java int division).
[[nodiscard]] int viewerPixel(double pos, int center, int blocksPerPixel);

/// MapColor.Brightness: LOW (0) 180, NORMAL (1) 220, HIGH (2) 255, LOWEST (3) 135.
[[nodiscard]] int brightnessModifier(int brightness);
/// MapColor#calculateRGBColor as ARGB: each channel of `rgb` times the brightness modifier / 255.
[[nodiscard]] std::uint32_t shade(std::uint32_t rgb, int brightness);
/// ARGB <-> ABGR (Bedrock's map pixels are ABGR).
[[nodiscard]] constexpr std::uint32_t argbToAbgr(std::uint32_t c) {
    return (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
}

/// MagicMapItem#update's pixel loop: calls `visit(xPixel, zPixel)` for every pixel the update writes
/// (inside the 128 x 128 map, within the 32-pixel view radius of the viewer pixel, the fuzzy edge
/// beyond radius - 2 on the checkerboard), in Java's order (x outer, z inner).
inline constexpr int kMagicBlocksPerPixel = 16;
inline constexpr int kMagicBiomesPerPixel = 4;
inline constexpr int kMagicViewRadius     = 512 / kMagicBlocksPerPixel;
void magicMapScan(int viewerX, int viewerZ, std::function<void(int xPixel, int zPixel)> const& visit);
/// The block a magic map samples for biome cache cell `i` (x or z) of a map centred at `center`:
/// ((center / 16 - 64) * 4) * 4 + i * 4. Pixel p uses cell p * 4, the stream check cell p * 4 + 1.
[[nodiscard]] int magicSampleBlock(int center, int cell);
/// The block of a magic map pixel (the landmark check): (center / 16 + pixel - 64) * 16.
[[nodiscard]] int magicPixelBlock(int center, int pixel);

/// MazeMapItem#update's loop (blocks per pixel `bpp`, view radius 16, 8 with a ceiling): every call
/// advances `step`; a column is scanned when (xPixel & 15) == (step & 15) or the column before
/// changed a pixel. `colour(x, z)` gives the pixel's colour (nullopt: the chunk is empty, nothing is
/// written); `write(x, z, colour)` stores it and returns true when the pixel changed. zPixel -1 is
/// sampled (Java reads it) but never written.
void mazeMapScan(int viewerX, int viewerZ, int radius, int step, std::function<std::optional<std::uint32_t>(int xPixel, int zPixel)> const& colour, std::function<bool(int xPixel, int zPixel, std::uint32_t colour)> const& write);

/// MapItemSavedData#addDecoration's placement of a fixed decoration: the map coordinates (bytes,
/// -128..127: 2 per pixel) and the rotation (0..15) of world (x, z) on a map of `scale` centred at
/// (centerX, centerZ); nullopt when it lies outside the map (Java removes it).
struct DecorationPos {
    int x = 0, z = 0, rotation = 0;
    friend bool operator==(DecorationPos const&, DecorationPos const&) = default;
};
[[nodiscard]] std::optional<DecorationPos> decorationPos(double x, double z, int centerX, int centerZ, int scale, double yRot);
/// MagicMapItem#makeName: "<asset id>_<x>_<z>" (the decoration id; the conquered list holds the same
/// with the map coordinates).
[[nodiscard]] std::string decorationName(std::string const& assetId, int x, int z);

// ---- biomepng (MapBiomesCommand) ----------------------------------------------------------------------

/// The fixed colour of a TF biome (ARGB, opaque); nullopt for any other biome.
[[nodiscard]] std::optional<std::uint32_t> biomePngColor(std::string const& biomeId);
/// MapBiomesCommand#getMapColor of a magic_map_color entry (rgb): ABGR with alpha (red and blue
/// swapped, as Java's BiomeMapColor then takes it for ARGB).
[[nodiscard]] std::uint32_t biomePngDataMapColor(std::uint32_t rgb);
/// java.text.DecimalFormat("#.00") with its default HALF_EVEN rounding ("0" -> ".00", 12.345 -> "12.34").
[[nodiscard]] std::string decimalFormat00(double value);
/// The PNG's path below the world folder: generated/biomemaps/<seed>/biome_map-<seed>-(<startX>.<startZ>)-(<endX>.<endZ>).png
[[nodiscard]] std::string biomePngPath(std::int64_t seed, int startX, int startZ, int width, int height);
/// What createMap does before and while sampling (NativeImage's checks): the failure, if any, and
/// the rows that send a progress line before it.
struct BiomePngPlan {
    bool fails       = false; ///< command.failed somewhere
    int  progressRows = 0;    ///< progress lines sent (before a failure, or all of them)
    int  completedRows = 0;   ///< rows sampled (x)
};
[[nodiscard]] BiomePngPlan biomePngPlan(int width, int height);

} // namespace gf::core::uncraftmaps
