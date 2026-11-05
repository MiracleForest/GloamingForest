// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Extension added by U2 (terrain), rule R18 (folded by A8 in W2b): two read-only questions about
// F5's generator pipeline that worldgen's contract cannot answer yet.
//
// - What runs between two providers: the terrain generator hands the block grid it computed in the
//   noise step to the surface step (and the surface step's to the blankets) instead of reading the
//   chunk back, which is only right when nothing else can write the chunk in between: no
//   Stage::Noise step (between setTerrain's provider and setSurface's), no Stage::Surface step and
//   no blanket registered under an id that sorts before the reader (F5 runs the blankets in id
//   order) between setSurface's provider and the blanket.
// - Fluids in Bedrock's liquid layer: a Java block that is waterlogged ("waterlogged=true") keeps
//   its water in the subchunk's extra block layer in Bedrock, which worldgen::Chunk::getBlock does
//   not show. TF's carver asks BlockState#getFluidState, so it must see that water.

#include <string>
#include <string_view>
#include <vector>

#include "feature/worldgen/Worldgen.h"

class Block;

namespace gf::terrain::ext {

/// The ids of the steps registered for `stage`, in the order F5 runs them.
[[nodiscard]] std::vector<std::string> stageStepIds(worldgen::Stage stage);
/// The ids of the registered chunk blankets, in the order F5 runs them.
[[nodiscard]] std::vector<std::string> blanketIds();

/// Whether `chunk` is a chunk of the game being generated (it has Bedrock's liquid layer).
[[nodiscard]] bool hasLiquidLayer(worldgen::Chunk const& chunk);
/// The block of the liquid (extra) layer at a local position of a chunk the game is generating
/// (nullptr: no such layer there, or the chunk is not a game chunk, e.g. an in-memory test chunk,
/// whose blocks have no liquid layer).
[[nodiscard]] Block const* liquidLayerBlock(worldgen::Chunk const& chunk, int lx, int y, int lz);
/// Empties the liquid layer at a local position (Java replacing a waterlogged state replaces its
/// fluid too; Bedrock's setBlock keeps the liquid layer). Nothing for a chunk without one.
void clearLiquidLayer(worldgen::Chunk& chunk, int lx, int y, int lz, Block const& air);

} // namespace gf::terrain::ext
