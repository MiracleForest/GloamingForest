// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ColorHandler.java (aurora block colours)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's aurora colours (U28): ColorHandler's per-position aurora block colour and the
// pale colour of the aurora pillar, slab and auroralized glass, through F12's tint registry (the
// blocks' materials carry a tint method so the chunk builder asks for it). Vanilla clients see the
// tint method's biome colour instead. The block items' colours (ColorHandler#registerItemColors:
// the colour without a level, hue 0.45 and its pale form) need no client code: the blocks'
// minecraft:item_visual draws them with pre-tinted textures, the same on every client.
#ifdef GF_CLIENT

#include "mc/world/level/BlockPos.h"

#include "client/render/Render.h"
#include "core/aurora/AuroraColor.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::aurora {

namespace {

class AuroraClientModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        namespace ca = core::aurora;
        tint::registerBlock("twilightforest:aurora_block", [](BlockPos const& p) { return ca::auroraBlockColor(p.x, p.y, p.z); });
        for (char const* id : {"twilightforest:aurora_pillar", "twilightforest:aurora_slab", "twilightforest:auroralized_glass"})
            tint::registerBlock(id, [](BlockPos const& p) { return ca::auroraPaleColor(ca::auroraBlockColor(p.x, p.y, p.z)); });
        gf::log().info("aurora: aurora block colours registered");
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::aurora

GF_REGISTER_MODULE(gf::client::aurora::AuroraClientModule);

#endif // GF_CLIENT
