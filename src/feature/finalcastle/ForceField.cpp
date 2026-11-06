// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/ForceFieldBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the five force fields. Placement decides the six connections
// (getStateForPlacement), a neighbour change may only add the connection towards it (updateShape);
// the block is unbreakable (strength -1, BP: not destructible by mining or explosions). The shape,
// collision and model (arms per connection, corner panels where two connected arms meet) are the BP
// permutations of assets/pack/bp/blocks/finalcastle; a corner panel is drawn when both of its arms
// are connected (TF also asks cornerConnects: a sturdy neighbour or a force field connected the
// other way, which no block state can hold).
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::blocks {

namespace ia = gf::interactions;

bool fieldConnects(BlockSource& region, BlockPos const& pos, Block const* state, int dir) {
    // the block the field is (its id: placement asks before the block is there in Java; here the
    // block at `pos` is already the placed one)
    auto self = state ? game::blockId(*state) : idAt(region, pos);
    BlockPos     rel      = offset(pos, dir);
    Block const& relative = region.getBlock(rel);
    if (game::blockId(relative) == self) return true;
    int better = 0;
    if (!state) {
        for (int f = 0; f < 6; ++f)
            if (idAt(region, offset(pos, f)) == self) ++better;
    } else {
        for (int f = 0; f < 6; ++f)
            if (boolState(*state, sideState(f).c_str())) ++better;
    }
    if (better >= 3) return false;
    return !exceptionForConnection(relative) && sturdy(region, rel, opposite(dir));
}

std::array<bool, 6> fieldPlacement(BlockSource& region, BlockPos const& pos, int clicked, bool sneaking) {
    std::array<bool, 6> out{};
    for (int d = 0; d < 6; ++d) {
        out[static_cast<std::size_t>(d)] = opposite(clicked) == d || (clicked != d ? fieldConnects(region, pos, nullptr, d) : !sneaking);
    }
    return out;
}

void installForceFields() {
    for (char const* field : kFields) {
        // getStateForPlacement (a player's placement; worldgen and commands set the states they give)
        ia::onPlace({.block = field}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region || c.face < 0 || c.face > 5) return;
            auto bp = game::blockPos(c.pos);
            auto on = fieldPlacement(*region, bp, c.face, c.sneaking);
            Json s  = Json::object();
            for (int d = 0; d < 6; ++d) s[sideState(d)] = on[static_cast<std::size_t>(d)];
            setStates(*region, bp, s);
        }, ia::priority::Low);
        // updateShape: only ever adds the connection towards the neighbour that changed
        blockevents::onNeighborChanged(field, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
            for (int d = 0; d < 6; ++d) {
                BlockPos n = offset(pos, d);
                if (n.x != from.x || n.y != from.y || n.z != from.z) continue;
                Block const& state = region.getBlock(pos);
                if (boolState(state, sideState(d).c_str())) return;
                if (fieldConnects(region, pos, &state, d)) setStates(region, pos, {{sideState(d), true}});
                return;
            }
        });
    }
}

} // namespace gf::feature::finalcastle::blocks
