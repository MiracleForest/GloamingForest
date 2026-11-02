#pragma once

// Block families (F11), client mod: the game's own biome foliage colour at a position, for the tint
// functions of TF's canopy and mangrove leaves (ColorHandler.CANOPY_COLORIZER / MANGROVE_COLORIZER
// of BiomeColors.getAverageFoliageColor). Client build only.

#ifdef GF_CLIENT

#include <optional>

class BlockPos;

namespace gf::client::families {

/// The foliage colour (0xRRGGBB) the game tints a block with at `pos` (Bedrock's
/// "default_foliage" tint method: the biomes' foliage colours blended around the position, as
/// Java's BiomeColors.getAverageFoliageColor). Only inside a tint function (gf::client::tint, F12):
/// it asks the tint resolver of the chunk mesh being built on this thread; nullopt elsewhere.
[[nodiscard]] std::optional<unsigned> biomeFoliageAt(BlockPos const& pos);

} // namespace gf::client::families

#endif // GF_CLIENT
