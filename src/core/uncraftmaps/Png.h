// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// A minimal PNG writer for /gloamingforest biomepng (U13): 8-bit RGBA, zlib with stored
// (uncompressed) deflate blocks. Pure C++, no game code.

#include <cstdint>
#include <string>
#include <vector>

namespace gf::core::uncraftmaps {

/// CRC-32 (PNG / zlib polynomial) of `data`.
[[nodiscard]] std::uint32_t crc32(std::uint8_t const* data, std::size_t size, std::uint32_t crc = 0);
/// Adler-32 of `data`.
[[nodiscard]] std::uint32_t adler32(std::uint8_t const* data, std::size_t size);
/// The PNG file of a `width` x `height` image; `argb` holds width * height pixels, row by row.
[[nodiscard]] std::vector<std::uint8_t> encodePng(int width, int height, std::vector<std::uint32_t> const& argb);

} // namespace gf::core::uncraftmaps
