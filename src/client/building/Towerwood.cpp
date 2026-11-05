// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ColorHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building), client mod: ColorHandler's towerwood colour (tint index 0 of every face) on
// towerwood, cracked and mossy towerwood (infested towerwood is U26's block, which registers the
// same colour). The BP blocks carry tint_method dry_foliage (tools/import/overrides/building.json)
// so the block tessellator asks the position tint resolver; a vanilla client draws them with the
// biome's dry foliage colour (the nearest vanilla brown-orange), the client mod with TF's colour.
#ifdef GF_CLIENT

#include "client/render/Render.h"
#include "core/building/TowerwoodColor.h"
#include "mc/world/level/BlockPos.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::building {

namespace {

struct TowerwoodTintModule {
    void install() {
        for (char const* id : {"twilightforest:towerwood", "twilightforest:cracked_towerwood", "twilightforest:mossy_towerwood"})
            tint::registerBlock(id, [](BlockPos const& p) { return core::building::towerwoodColor(p.x, p.y, p.z); });
        log().info("building: towerwood tints registered");
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::building

GF_REGISTER_MODULE(gf::client::building::TowerwoodTintModule);

#endif // GF_CLIENT
