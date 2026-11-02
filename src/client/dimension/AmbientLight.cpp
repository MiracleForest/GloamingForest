// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/generated/resources/data/twilightforest/dimension_type/twilight_forest_type.json (ambient_light)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only. TF's ambient_light 0.01 in the client mod's light texture: the client level's
// Twilight Forest gets the brightness ramp of DimensionType#brightness, as the server's has
// (feature/dimension/Dimension.cpp). The client creates its dimension when the player enters it, so
// the ramp is checked while the player is there (one comparison a tick). On each visit the client's
// own build range of the dimension (from the server's DimensionDataPacket) is logged once: the e2e
// checks that the client builds y -32..256.

#ifdef GF_CLIENT

#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/World.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::dimension {

class AmbientLightModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            static bool logged = false; // this visit's build range was logged
            auto*       player = currentPlayer();
            if (!player) {
                logged = false;
                return;
            }
            auto& dim = player->getDimension();
            if (!feature::dimension::isTwilightDimension(dim)) {
                logged = false;
                return;
            }
            feature::dimension::applyAmbientLight(dim);
            if (!logged) {
                logged = true;
                log().info("client dimension {}: y {}..{}", gf::world::kDimensionId, dim.mHeightRange->mMin, dim.mHeightRange->mMax);
            }
        });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace gf::client::dimension

GF_REGISTER_MODULE(gf::client::dimension::AmbientLightModule);

#endif // GF_CLIENT
