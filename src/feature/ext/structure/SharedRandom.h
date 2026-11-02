// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded into the contract by A8
// in W2b). A template placement that draws from the caller's random, as TF's template features
// do: TemplateFeature#place (stone circle, druid hut with its basement, grove ruins, ...) and
// GraveyardFeature#place give their StructurePlaceSettings the feature's random (setRandom) and
// pass that same random to StructureTemplate#placeInWorld, then StructureTemplate#filterBlocks for
// the data markers, the markers' handling, postPlacement and further placements keep drawing from
// it. The contract's templates::place only takes TemplateSettings::seed, which starts a random of
// its own for each placement, so the draws of the feature around the placement could not continue
// the placement's sequence.
//
//     StructurePlaceSettings settings = new StructurePlaceSettings().setMirror(mirror)
//         .setRotation(rotation).setBoundingBox(box).setRandom(random);
//     template.placeInWorld(world, placementPos, placementPos, settings, random, Block.UPDATE_CLIENTS);
//     for (StructureBlockInfo info : template.filterBlocks(placementPos, settings, Blocks.STRUCTURE_BLOCK)) ...
// is
//     gf::structure::ext::placeTemplate(id, region, placementPos, placementPos, settings, random, 2);
//     for (auto const& [pos, label] : gf::structure::ext::filterDataMarkers(id, placementPos, settings, random)) ...

#include <string>
#include <utility>
#include <vector>

#include "feature/structure/Structure.h"

namespace gf::structure::ext {

/// StructureTemplate#placeInWorld(level, offset, pos, settings.setRandom(random), random, flags) for
/// template `id`: every processor draws from `random` (StructurePlaceSettings#getRandom), after the
/// palette pick (getRandomPalette: random.nextInt(1)); the blocks are processed in the order Java
/// keeps a loaded template's blocks, all of them (settings.clip only limits what is placed); every
/// randomizable container with block entity data placed draws its LootTableSeed from `random` in
/// placement order. settings.seed is not used. The data markers are not handled: the caller lists
/// them with filterDataMarkers (Java's filterBlocks) as TF's features do. False when the template is
/// missing.
bool placeTemplate(std::string const& id, Region& region, BlockPos const& offset, BlockPos const& pos, TemplateSettings const& settings,
                   RandomSource& random, int flags = 2);

/// StructureTemplate#filterBlocks(offset, settings.setRandom(random), Blocks.STRUCTURE_BLOCK) for the
/// DATA structure blocks of template `id`: draws getRandomPalette's random.nextInt(1), then returns
/// each data marker (world position, metadata label) inside settings.clip, in Java's order (by y,
/// then x, then z of the template). Empty when the template is missing.
[[nodiscard]] std::vector<std::pair<BlockPos, std::string>> filterDataMarkers(std::string const& id, BlockPos const& offset,
                                                                            TemplateSettings const& settings, RandomSource& random);

} // namespace gf::structure::ext
