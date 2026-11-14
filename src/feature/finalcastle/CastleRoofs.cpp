// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13ConicalComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13CrenellatedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof13PeakedComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleRoof9CrenellatedComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the tower roofs.
#include "feature/finalcastle/Castle.h"

#include "feature/ext/finalcastle/W0Internals.h"

namespace gf::feature::finalcastle::castle {

namespace {

constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

/// The roof box of every castle roof: the tower widened by 2, from its top layer up `height` blocks.
BlockBox roofBox(ComponentOld const& sideTower, int height) {
    auto const& t = sideTower.bbox;
    return BlockBox::of(t.minX - 2, t.maxY - 1, t.minZ - 2, t.maxX + 2, t.maxY + height - 1, t.maxZ + 2);
}

} // namespace

// ==== FinalCastleRoof13ConicalComponent ==========================================================================

Roof13Conical::Roof13Conical(RandomSource& rand, int i, ComponentOld const& sideTower, int x, int y, int z) : ComponentOld(type::Roof13Conical, i, x, y, z) {
    slope      = 2 + rand.nextInt(3) + rand.nextInt(3);
    int height = slope * 4;
    setOrientation(sideTower.getOrientation());
    bbox = roofBox(sideTower, height);
}

Roof13Conical::Roof13Conical() : ComponentOld(type::Roof13Conical, 0, 0, 0, 0) {}

Json Roof13Conical::save() const {
    Json j     = ComponentOld::save();
    j["slope"] = slope;
    return j;
}

void Roof13Conical::load(Json const& data) {
    ComponentOld::load(data);
    slope = data.value("slope", 0);
}

void Roof13Conical::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Roof13Conical::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    for (auto rotation : kRotations) {
        fillBlocksRotated(region, sbb, 0, -1, 0, 3, 2, 3, *deco->blockState, rotation);
        setBlockStateRotated(region, *deco->blockState, 1, -2, 2, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 1, -2, 1, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 2, -2, 1, rotation, sbb);
        fillBlocksRotated(region, sbb, 4, 0, 1, 12, 1, 1, *deco->blockState, rotation);
        // more teeny crenellations
        for (int i = 3; i < 13; i += 2) fillBlocksRotated(region, sbb, i, -1, 1, i, 2, 1, *deco->blockState, rotation);
        // cone roof
        for (int i = 2; i < 9; i++) {
            int base = 2 - slope;
            if (i < 7) fillBlocksRotated(region, sbb, i - 1, ((i - 1) * slope) + base, i - 1, i, (i * slope) + base - 1, i, *deco->blockState, rotation);
            else fillBlocksRotated(region, sbb, 16 - i, ((i - 1) * slope) + base, i, 16 - i, (i * slope) + base - 1, i, *deco->roofState, rotation);
            fillBlocksRotated(region, sbb, i + 1, ((i - 1) * slope) + base, i, 15 - i, (i * slope) + base - 1, i, *deco->roofState, rotation);
        }
        // point!
        fillBlocksRotated(region, sbb, 8, (slope * 6) + 2, 8, 8, (slope * 7) + 2, 8, *deco->roofState, rotation);
    }
}

// ==== the fixed-height roofs =====================================================================================

SimpleRoof::SimpleRoof(std::string typeId, int i, ComponentOld const& sideTower, int height) : ComponentOld(std::move(typeId), i, sideTower.getLocatorPosition().x,
                                                                                                         sideTower.getLocatorPosition().y,
                                                                                                         sideTower.getLocatorPosition().z) {
    setOrientation(sideTower.getOrientation());
    bbox = roofBox(sideTower, height);
}

SimpleRoof::SimpleRoof(std::string typeId) : ComponentOld(std::move(typeId), 0, 0, 0, 0) {}

void SimpleRoof::addChildren(Piece& parent, PieceList&, RandomSource&) {
    if (auto const* d = decoOf(parent)) deco = copyDecorator(d);
}

void Roof13Cren::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    // assume square
    int size = bbox.maxX - bbox.minX;
    for (auto rotation : kRotations) {
        // corner
        fillBlocksRotated(region, sbb, 0, -1, 0, 3, 3, 3, *deco->blockState, rotation);
        setBlockStateRotated(region, *deco->blockState, 1, -2, 2, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 1, -2, 1, rotation, sbb);
        setBlockStateRotated(region, *deco->blockState, 2, -2, 1, rotation, sbb);
        // walls
        fillBlocksRotated(region, sbb, 4, 0, 1, size - 4, 1, 1, *deco->blockState, rotation);
        // smaller crenellations
        for (int x = 5; x < size - 5; x += 4) {
            fillBlocksRotated(region, sbb, x, 0, 0, x + 2, 3, 2, *deco->blockState, rotation);
            setBlockStateRotated(region, *deco->blockState, x + 1, -1, 1, rotation, sbb);
            setBlockStateRotated(region, *deco->blockState, x + 1, -2, 1, rotation, sbb);
        }
    }
}

void Roof13Peaked::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& roof  = *deco->roofState;
    Block const& block = *deco->blockState;
    // peaky roof, loop unrolled as it was getting dumb
    for (int i = 0; i < 3; i++) {
        fill(region, sbb, 1, i, i, 15, i, i, roof, roof, false);
        fill(region, sbb, 1, i, 16 - i, 15, i, 16 - i, roof, roof, false);
    }
    for (int i = 0; i < 3; i++) {
        int dz = 3 + i;
        fill(region, sbb, 2, 5 + ((i - 1) * 2), dz, 14, 4 + (i * 2), dz, roof, roof, false);
        fill(region, sbb, 1, 1, dz, 1, 5 + ((i - 1) * 2), dz, roof, roof, false);
        fill(region, sbb, 1, 5 + ((i - 1) * 2), dz - 1, 1, 4 + (i * 2), dz, block, block, false);
        fill(region, sbb, 15, 1, dz, 15, 5 + ((i - 1) * 2), dz, roof, roof, false);
        fill(region, sbb, 15, 5 + ((i - 1) * 2), dz - 1, 15, 4 + (i * 2), dz, block, block, false);
        dz = 13 - i;
        fill(region, sbb, 2, 5 + ((i - 1) * 2), dz, 14, 4 + (i * 2), dz, roof, roof, false);
        fill(region, sbb, 1, 1, dz, 1, 5 + ((i - 1) * 2), dz, roof, roof, false);
        fill(region, sbb, 1, 5 + ((i - 1) * 2), dz, 1, 4 + (i * 2), dz + 1, block, block, false);
        fill(region, sbb, 15, 1, dz, 15, 5 + ((i - 1) * 2), dz, roof, roof, false);
        fill(region, sbb, 15, 5 + ((i - 1) * 2), dz, 15, 4 + (i * 2), dz + 1, block, block, false);
    }
    for (int i = 0; i < 3; i++) {
        int dz = 6 + i;
        fill(region, sbb, 2, 12 + ((i - 1) * 3), dz, 14, 11 + (i * 3), dz, roof, roof, false);
        fill(region, sbb, 1, 1, dz, 1, 12 + ((i - 1) * 3), dz, roof, roof, false);
        fill(region, sbb, 1, 12 + ((i - 1) * 3), dz - 1, 1, 11 + (i * 3), dz, block, block, false);
        fill(region, sbb, 15, 1, dz, 15, 12 + ((i - 1) * 3), dz, roof, roof, false);
        fill(region, sbb, 15, 12 + ((i - 1) * 3), dz - 1, 15, 11 + (i * 3), dz, block, block, false);
        dz = 10 - i;
        fill(region, sbb, 2, 12 + ((i - 1) * 3), dz, 14, 11 + (i * 3), dz, roof, roof, false);
        fill(region, sbb, 1, 1, dz, 1, 12 + ((i - 1) * 3), dz, roof, roof, false);
        fill(region, sbb, 1, 12 + ((i - 1) * 3), dz, 1, 11 + (i * 3), dz + 1, block, block, false);
        fill(region, sbb, 15, 1, dz, 15, 12 + ((i - 1) * 3), dz, roof, roof, false);
        fill(region, sbb, 15, 12 + ((i - 1) * 3), dz, 15, 11 + (i * 3), dz + 1, block, block, false);
    }
    // top roof bobbles
    fill(region, sbb, 1, 18, 8, 5, 18, 8, roof, roof, false);
    fill(region, sbb, 11, 18, 8, 14, 18, 8, roof, roof, false);
    fill(region, sbb, 0, 17, 8, 1, 19, 8, roof, roof, false);
    fill(region, sbb, 15, 17, 8, 16, 19, 8, roof, roof, false);
    for (auto rotation : {Rotation::Clockwise90, Rotation::Counterclockwise90}) {
        // this might be one of my more confusing instances of code recycling
        fillBlocksRotated(region, sbb, 4, 0, 1, 12, 1, 1, block, rotation);
        // more teeny crenellations
        for (int i = 3; i < 13; i += 2) fillBlocksRotated(region, sbb, i, -1, 1, i, 2, 1, block, rotation);
    }
    // corners
    for (auto rotation : kRotations) {
        fillBlocksRotated(region, sbb, 0, -1, 0, 3, 2, 3, block, rotation);
        setBlockStateRotated(region, block, 1, -2, 2, rotation, sbb);
        setBlockStateRotated(region, block, 1, -2, 1, rotation, sbb);
        setBlockStateRotated(region, block, 2, -2, 1, rotation, sbb);
    }
}

void Roof9Cren::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& block = *deco->blockState;
    for (auto rotation : kRotations) {
        fillBlocksRotated(region, sbb, 0, -1, 0, 2, 3, 2, block, rotation);
        setBlockStateRotated(region, block, 1, -2, 2, rotation, sbb);
        setBlockStateRotated(region, block, 1, -2, 1, rotation, sbb);
        setBlockStateRotated(region, block, 2, -2, 1, rotation, sbb);
        setBlockStateRotated(region, block, 3, 0, 1, rotation, sbb);
        setBlockStateRotated(region, block, 3, 1, 1, rotation, sbb);
        fillBlocksRotated(region, sbb, 4, 0, 0, 5, 3, 2, block, rotation);
        setBlockStateRotated(region, block, 6, 0, 1, rotation, sbb);
        setBlockStateRotated(region, block, 6, 1, 1, rotation, sbb);
        fillBlocksRotated(region, sbb, 7, 0, 0, 8, 3, 2, block, rotation);
        setBlockStateRotated(region, block, 9, 0, 1, rotation, sbb);
        setBlockStateRotated(region, block, 9, 1, 1, rotation, sbb);
    }
}

} // namespace gf::feature::finalcastle::castle
