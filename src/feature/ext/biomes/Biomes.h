#pragma once

// U1 additions to the "worldgen" / "client.render" contracts (rule R18: new files only; to be
// folded by A8 in W2b). Other units call these by name; they include no U1 header.
//
// - Terrain blend: TF's BiomeDensitySource#sampleTerrain(blockX, blockZ), the depth / scale blend
//   of the terrain columns around a block column, which U2's density function types
//   `twilightforest:biome_driven_terrain` / `biome_driven_noise` read (the worldgen contract only
//   carries biome ids). Bit exact against the F13 density goldens (tests/biomes).
// - Key biome: the layer stack's biome of a quart column before the vertical layers.
// - Client colours (client build): TF's grass and foliage colours at a block (Biome#getGrassColor
//   with TF's grass colour modifiers, FoliageColorHandler, blended over Java's biome blend radius)
//   for owners of grass / foliage tinted blocks (F11's leaves, U3, U6), who register their tints
//   with client.render and want TF's colours instead of the game's static biome colours.
//
//     auto d = gf::biomes::ext::sampleTerrain(x, z);                 // U2: d->depth, d->scale
//     tint::registerBlock(id, [](BlockPos const& p, tint::GameColors const& g) {
//         return gf::biomes::ext::foliageColor(p, g);                // TF's foliage at p
//     });

#include <cstdint>
#include <optional>
#include <string>

class BlockPos;
class BlockSource;

namespace gf::biomes::ext {

struct DensityData {
    double depth = 0;
    double scale = 0;
};
/// BiomeDensitySource#sampleTerrain for the running world's seed (nullopt: no biome source).
[[nodiscard]] std::optional<DensityData> sampleTerrain(int blockX, int blockZ);
/// The layer stack's biome of quart column (quartX, quartZ) ("" without a biome source).
[[nodiscard]] std::string keyBiome(int quartX, int quartZ);

} // namespace gf::biomes::ext

#ifdef GF_CLIENT

#include "feature/ext/ui/Client.h"

namespace gf::biomes::ext {

/// TF's grass colour at `pos` (0xRRGGBB): Java's blend over the configured radius of each column's
/// TF biome colour (modifiers, position-dependent colours, JSON colours, colour map); the game's
/// colour (`game.grass()`) outside TF's biomes. Blocks the game draws with the biome grass / foliage
/// colour get these colours without calling this (feature/ext/biomes/VanillaTint.cpp); a tint of
/// one's own that starts from the biome colour (as TF's canopy leaves do) can call it.
[[nodiscard]] unsigned grassColor(BlockPos const& pos, gf::client::tint::GameColors const& game);
/// TF's foliage colour at `pos` (FoliageColorHandler over the game's foliage colour).
[[nodiscard]] unsigned foliageColor(BlockPos const& pos, gf::client::tint::GameColors const& game);

/// Vanilla tints (VanillaTint.cpp, an extension of client.render): F12's tint registry applied to
/// vanilla blocks the legacy tessellator draws with the biome grass or foliage colour (grass,
/// plants, oak-kin leaves, vines), which F12's PositionDependentTintResolver hook never sees.
/// The block source being tessellated while a tint function runs from that hook on this thread
/// (nullptr elsewhere, e.g. for blocks of F12's own path): read the biome at a position from it.
[[nodiscard]] BlockSource* tintRegion();
/// How many vanilla block tints the hook applied so far (selftests, e2e).
[[nodiscard]] std::uint64_t vanillaTintsApplied();
/// How many water colours of TF's biomes the water colour lookup hook answered so far.
[[nodiscard]] std::uint64_t waterColoursApplied();
/// How many water vertices of TF's biomes the water tessellation hook coloured so far.
[[nodiscard]] std::uint64_t waterDrawn();
/// How many material instances of TF's grass-tinted plants got the grass tint method.
[[nodiscard]] std::uint64_t plantTintsGiven();
/// How many times leaves in TF's biomes were drawn without Bedrock's seasons map so far.
[[nodiscard]] std::uint64_t seasonsOffApplied();

} // namespace gf::biomes::ext

#endif
