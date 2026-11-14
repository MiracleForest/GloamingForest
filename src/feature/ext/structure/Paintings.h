// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/entities/EntityUtil.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java (placePainting)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded into the contract by A8
// in W2b). Paintings placed by structures, as upstream's EntityUtil and LichBossRoom place them (the
// structure kit's painting marker handler and the old lich tower use the same code): the lich tower
// revamp (U21, LichBossRoom / LichTowerWingRoom painting markers) and the druid hut (U19) need it.
// A painting placed during generation is spawned when its chunk is loaded.

#include <optional>
#include <string>
#include <vector>

#include "feature/structure/Structure.h"

namespace gf::structure::ext {

/// A painting variant (width x height in blocks).
struct PaintingVariant {
    std::string id; ///< "minecraft:kebab"
    int         width = 1, height = 1;
};
/// The painting_variant registry of 1.21.1 in registry order (sorted by id).
[[nodiscard]] std::vector<PaintingVariant> paintingRegistry();
/// EntityUtil.getPaintingOfSize(level, rand, width, height, exactMeasurements).
[[nodiscard]] std::optional<PaintingVariant> getPaintingOfSize(RandomSource& random, int width, int height, bool exactMeasurements);
/// EntityUtil.getPaintingsOfSizeOrSmaller(level, tag, width, height) (tag order: "minecraft:placeable",
/// "gloamingforest:tower_paintings", "gloamingforest:tower_boss_paintings", ...).
[[nodiscard]] std::vector<PaintingVariant> getPaintingsOfSizeOrSmaller(std::string const& tag, int width, int height);
/// EntityUtil.tryHangPainting(level, pos, direction, painting): hangs it when its box collides with
/// no block and no other hanging entity (and, in a loaded world, when the game sees a wall behind it).
bool tryHangPainting(Region& region, BlockPos const& pos, Orientation direction, std::optional<PaintingVariant> const& painting);
/// LichBossRoom.placePainting(pos, random, limitTries, paintings, level, width, height, dir).
bool placePainting(BlockPos const& pos, RandomSource& random, int limitTries, std::string const& tag, Region& region, int width, int height,
                   Orientation direction);
/// LichBossRoom.placePainting(label "dir:width:height", pos, level, random, chunkBounds, rotation,
/// limitTries, rarityFactor, paintings).
bool placePainting(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBounds, Rotation rotation,
                   int limitTries, int rarityFactor, std::string const& tag);

} // namespace gf::structure::ext
