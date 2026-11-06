// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/finalcastle/StructureTFDecoratorCastle.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the castle's decorator and small helpers shared by the piece files.
#include "feature/finalcastle/Castle.h"

#include "feature/ext/finalcastle/W0Internals.h"

namespace gf::feature::finalcastle::castle {

namespace {

struct CastleDecorator final : Decorator {
    [[nodiscard]] std::string decoString() const override { return "DecoCastle"; }
};

} // namespace

Block const& J(char const* javaId, Json const& props) { return ::gf::structure::blocks::java(javaId, props); }

std::unique_ptr<Decorator> castleDecorator() {
    auto d         = std::make_unique<CastleDecorator>();
    auto def       = ::gf::structure::bases::defaultDecorator();
    d->platformState = def->platformState; // TFStructureDecorator's defaults the castle keeps
    d->floorState    = def->floorState;
    d->blockState    = &J("twilightforest:castle_brick");
    d->accentState   = &J("minecraft:chiseled_quartz_block");
    d->roofState     = &J("twilightforest:castle_roof_tile");
    d->pillarState   = &J("twilightforest:bold_castle_brick_pillar");
    d->fenceState    = &J("minecraft:oak_fence");
    d->stairState    = &J("minecraft:quartz_stairs");
    d->randomBlocks  = ::gf::structure::selectors::get("twilightforest:castle");
    return d;
}

std::unique_ptr<Decorator> copyDecorator(Decorator const* deco) {
    if (!deco) return nullptr;
    std::unique_ptr<Decorator> d;
    if (deco->decoString() == "DecoCastle") d = std::make_unique<CastleDecorator>();
    else d = ::gf::structure::bases::decoratorFor(deco->decoString());
    d->blockState    = deco->blockState;
    d->accentState   = deco->accentState;
    d->stairState    = deco->stairState;
    d->fenceState    = deco->fenceState;
    d->pillarState   = deco->pillarState;
    d->platformState = deco->platformState;
    d->floorState    = deco->floorState;
    d->roofState     = deco->roofState;
    d->randomBlocks  = deco->randomBlocks;
    return d;
}

Decorator const* decoOf(Piece const& piece) {
    // Java: parent instanceof TFStructureComponentOld (the castle's own piece types)
    if (auto const* c = dynamic_cast<ComponentOld const*>(&piece)) return c->deco.get();
    return nullptr;
}

BlockBox componentBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir) {
    switch (dir) {
    case Orientation::West: return BlockBox::of(x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX);
    case Orientation::North: return BlockBox::of(x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ);
    case Orientation::East: return BlockBox::of(x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX);
    default: return BlockBox::of(x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ);
    }
}

std::int64_t decoSeed(Region& region, BlockBox const& box) { return core::finalcastle::decoSeed(region.seed(), box.minX, box.minZ); }

} // namespace gf::feature::finalcastle::castle
