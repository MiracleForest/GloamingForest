// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/BinaryRotatedBlock.java
//   src/main/java/twilightforest/init/TFBlocks.java (TERRORCOTTA_*)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): placement states of the terrorcotta blocks that F11's families do not cover.
// terrorcotta_arcs is a RotatedPillarBlock (F11 "pillar" family). terrorcotta_curves is a
// GlazedTerracottaBlock: it faces opposite the player's horizontal direction. terrorcotta_lines is a
// BinaryRotatedBlock: `rotated` when the player looks along the z axis.

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/lichdecor/Lichdecor.h"
#include "game/Blocks.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

namespace {

namespace ia = gf::interactions;

class TerrorcottaModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        ia::onPlace({.block = "twilightforest:terrorcotta_curves"}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            setStates(*region, game::blockPos(c.pos), c.block, Json{{"facing", faceName(oppositeFace(lookFace(c.player)))}});
        });
        ia::onPlace({.block = "twilightforest:terrorcotta_lines"}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            int look = lookFace(c.player);
            setStates(*region, game::blockPos(c.pos), c.block, Json{{"rotated", look == 2 || look == 3}});
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::TerrorcottaModule);
