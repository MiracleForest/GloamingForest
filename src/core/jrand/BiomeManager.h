#pragma once

// Java-parity BiomeManager of Minecraft 1.21.1 (net.minecraft.world.level.biome.BiomeManager): the
// "biome zoom" that turns the quart (4x4x4) noise biomes of a chunk into the biome of a block. Every
// biome read at a block position in Java (Level#getBiome, WorldGenRegion#getBiome: the biome filter
// of placed features, freeze_top_layer, the spawners, upstream's blanket processors) picks one of the 8
// quarts around the position by a seeded, jittered distance. Pure C++ (gf-core).
//
//     auto zoom = gf::core::biome::obfuscateSeed(worldSeed);           // once per world
//     auto q    = gf::core::biome::zoom(zoom, x, y, z);                // the quart to read
//     auto id   = noiseBiome(q.x, q.y, q.z);                           // chunk / biome source

#include <array>
#include <cstdint>
#include <string_view>

namespace gf::core::biome {

/// A quart position (block >> 2).
struct Quart {
    int  x = 0, y = 0, z = 0;
    bool operator==(Quart const&) const = default;
};

/// SHA-256 of `data` (FIPS 180-4).
[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::string_view data);

/// BiomeManager#obfuscateSeed: Guava's Hashing.sha256().hashLong(seed).asLong(), i.e. the SHA-256 of
/// the seed's 8 little-endian bytes, whose first 8 bytes are read back little-endian.
[[nodiscard]] std::int64_t obfuscateSeed(std::int64_t seed);

/// LinearCongruentialGenerator#next(left, right): left * (left * 6364136223846793005 + 1442695040888963407) + right.
[[nodiscard]] std::int64_t lcgNext(std::int64_t left, std::int64_t right);

/// BiomeManager#getFiddle.
[[nodiscard]] double fiddle(std::int64_t seed);

/// BiomeManager#getFiddledDistance.
[[nodiscard]] double fiddledDistance(std::int64_t zoomSeed, int x, int y, int z, double xNoise, double yNoise, double zNoise);

/// BiomeManager#getBiome(BlockPos) without the final lookup: the quart whose noise biome is the
/// biome of block (x, y, z) for the zoom seed `zoomSeed` (obfuscateSeed of the world seed).
[[nodiscard]] Quart zoom(std::int64_t zoomSeed, int x, int y, int z);

} // namespace gf::core::biome
