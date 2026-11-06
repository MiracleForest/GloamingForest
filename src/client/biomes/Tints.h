#pragma once

// U1 `biomes`, client build: the unit's internal interface to its tints (Tints.cpp), for its
// selftests. Other units use feature/ext/biomes/Biomes.h.

#ifdef GF_CLIENT

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "feature/ext/ui/Client.h"

class Biome;
class BlockPos;
class BlockSource;

namespace gf::client::biomes {

/// TF's grass / foliage colour at `pos`: with `region`, the biomes of the blend's columns read from
/// it; without, recognised by the game's colour (see Tints.cpp).
[[nodiscard]] unsigned grassAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game);
[[nodiscard]] unsigned foliageAt(BlockSource* region, BlockPos const& pos, tint::GameColors const& game);

/// TF's water colour at `pos` (BiomeColors.getAverageWaterColor: every column's biome water colour,
/// blended as the grass), the biomes read from `region`; nullopt outside TF's biomes. `game` is the
/// game's colour, used for a column of another biome.
[[nodiscard]] std::optional<unsigned> waterAt(BlockSource& region, BlockPos const& pos, unsigned game);

/// The vanilla blocks the tints are registered for.
[[nodiscard]] std::span<char const* const> grassBlocks();
[[nodiscard]] std::span<char const* const> foliageBlocks();
/// TF's own blocks drawn with the biome grass colour that are owned by other units (ColorHandler:
/// the fiddlehead and its potted form), and the textures of their tinted faces (the client gives
/// those material instances the grass tint method, feature/ext/biomes/VanillaTint.cpp).
[[nodiscard]] std::span<char const* const> tfGrassBlocks();
[[nodiscard]] std::span<char const* const> tfGrassTextures();

/// The namespaced id of a biome ("minecraft:plains", "twilightforest:swamp"), cached per biome object
/// on this thread. An entry is checked against the biome's name hash on every use, so a biome of
/// another world (a remote join reuses the address) is looked up again.
[[nodiscard]] std::string const& biomeIdOf(Biome const& biome);
/// Whether `biomeId` is one of TF's 22 biomes.
[[nodiscard]] bool isTfBiome(std::string_view biomeId);

} // namespace gf::client::biomes

#endif
