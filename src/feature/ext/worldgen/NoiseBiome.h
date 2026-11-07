#pragma once

// Contract extension of "worldgen" (F5, rule R18; to be folded into Worldgen.h by A8): the raw
// quart biome of the biome source.

#include <string>

namespace gf::worldgen::ext {

/// The raw biome of the source at quart (4-block) coordinates, without BiomeManager's zoom
/// (BiomeSource#getNoiseBiome, as Structure#isValidBiome and TF's findBiomeHorizontal read it). The
/// quart y is clamped to the dimension's range as ChunkAccess#getNoiseBiome does ("" when no source
/// is set).
[[nodiscard]] std::string noiseBiomeAt(int quartX, int quartY, int quartZ);

} // namespace gf::worldgen::ext
