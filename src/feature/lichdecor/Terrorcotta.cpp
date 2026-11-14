// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/BinaryRotatedBlock.java
//   src/main/java/gloamingforest/init/TFBlocks.java (TERRORCOTTA_*)
// Copyright (c) the upstream authors (see NOTICE).
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
        ia::onPlace({.block = "gloamingforest:terrorcotta_curves"}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            setStates(*region, game::blockPos(c.pos), c.block, Json{{"facing", faceName(oppositeFace(lookFace(c.player)))}});
        });
        ia::onPlace({.block = "gloamingforest:terrorcotta_lines"}, [](ia::PlaceContext const& c) {
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
