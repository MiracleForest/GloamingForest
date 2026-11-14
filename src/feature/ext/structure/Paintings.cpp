// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/entities/EntityUtil.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java (placePainting)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The contract extension forwards to the structure kit's painting code (feature/structure/Paintings.cpp).
#include "feature/ext/structure/Paintings.h"

#include "feature/structure/Internal.h"

namespace gf::structure::ext {

namespace {

PaintingVariant toExt(paintings::Variant const& v) { return PaintingVariant{v.id, v.width, v.height}; }

std::vector<PaintingVariant> toExt(std::vector<paintings::Variant> const& list) {
    std::vector<PaintingVariant> out;
    out.reserve(list.size());
    for (auto const& v : list) out.push_back(toExt(v));
    return out;
}

} // namespace

std::vector<PaintingVariant> paintingRegistry() { return toExt(paintings::registry()); }

std::optional<PaintingVariant> getPaintingOfSize(RandomSource& random, int width, int height, bool exactMeasurements) {
    auto v = paintings::ofSize(random, width, height, exactMeasurements);
    return v ? std::optional<PaintingVariant>(toExt(*v)) : std::nullopt;
}

std::vector<PaintingVariant> getPaintingsOfSizeOrSmaller(std::string const& tag, int width, int height) {
    return toExt(paintings::ofSizeOrSmaller(tag, width, height));
}

bool tryHangPainting(Region& region, BlockPos const& pos, Orientation direction, std::optional<PaintingVariant> const& painting) {
    if (!painting) return false;
    return paintings::tryHang(region, pos, direction, paintings::Variant{painting->id, painting->width, painting->height});
}

bool placePainting(BlockPos const& pos, RandomSource& random, int limitTries, std::string const& tag, Region& region, int width, int height,
                   Orientation direction) {
    return paintings::place(pos, random, limitTries, tag, region, width, height, direction);
}

bool placePainting(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBounds, Rotation rotation,
                   int limitTries, int rarityFactor, std::string const& tag) {
    return paintings::placeLabelled(label, pos, region, random, chunkBounds, rotation, limitTries, rarityFactor, tag);
}

} // namespace gf::structure::ext
