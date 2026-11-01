// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/overlay/PortalOverlay.java
//   src/main/java/twilightforest/client/event/OverlayHandler.java (portal_overlay layer)
//   src/main/java/twilightforest/components/entity/TFPortalAttachment.java (client side)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only. The local player's portal timer (TFPortalAttachment on the client: up 1 a
// tick inside a Twilight portal, down 2 outside, at most 60), the full-screen portal overlay drawn
// above everything with alpha timer / 60 (a HUD layer of gf::client::hud), and the screen closing
// while inside a portal. When the HUD layer framework is not available yet (contract stub) the
// client asks the server for the camera-fade stand-in vanilla clients get ("dimension.client").

#ifdef GF_CLIENT

#include <algorithm>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "core/Stub.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::dimension {

namespace {

constexpr int  kMaxTicks      = 60; // TFPortalAttachment.MAX_TICKS
constexpr char kPortalBlock[] = "twilightforest:twilight_portal";

struct ClientState {
    int  portalTimer = 0;
    bool inside      = false;
    bool native      = false; ///< the HUD layer framework draws our layer
};
ClientState& state() {
    static ClientState s;
    return s;
}

bool portalAt(BlockSource& region, BlockPos const& p) {
    if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) return false;
    return region.getBlock(p).getTypeName() == kPortalBlock;
}

bool openPortalAt(BlockSource& region, BlockPos const& p) {
    return portalAt(region, p) && game::intState(region.getBlock(p), "twilightforest:is_one_way").value_or(0) == 0;
}

/// TFPortalAttachment#tick on the client (entityInside sets "inside" for the tick).
void tick() {
    auto& s      = state();
    auto* player = currentPlayer();
    if (!player) {
        s = ClientState{.native = s.native};
        return;
    }
    auto&    region = player->getDimensionBlockSource();
    BlockPos feet   = game::blockPosOf(player->getFeetPos());
    bool     inside = openPortalAt(region, feet) || openPortalAt(region, BlockPos{feet.x, feet.y - 1, feet.z});
    bool     before = s.portalTimer > 0;
    if (inside) {
        s.portalTimer = std::min(s.portalTimer + 1, kMaxTicks);
        // close any open screen but the pause and death screens
        if (auto* c = currentClient()) {
            if (ISceneStack* stack = c->getClientSceneStack().get()) {
                auto name = stack->getScreenName();
                if (name != "hud_screen" && name != "pause_screen" && name != "death_screen" && !name.empty()) stack->schedulePopScreen(1);
            }
        }
    } else if (s.portalTimer > 0) {
        s.portalTimer = std::max(s.portalTimer - 2, 0);
    }
    if (!before && s.portalTimer > 0) log().info("client portal overlay: {}", s.native ? "native" : "camera fade");
    s.inside = inside;
}

/// PortalOverlay#render: the portal sprite over the whole screen, alpha timer / MAX_TICKS.
void draw(screen::Canvas& canvas, float) {
    auto const& s = state();
    if (s.portalTimer <= 0) return;
    float alpha = static_cast<float>(s.portalTimer) / static_cast<float>(kMaxTicks);
    // the nether portal sprite TF draws, averaged (its own alpha is about 3/4)
    auto a = static_cast<unsigned>(std::clamp(alpha * 0.75f, 0.0f, 1.0f) * 255.0f);
    canvas.fill(core::Rect{-canvas.originX(), -canvas.originY(), 4096.0f, 4096.0f}, (a << 24) | 0x540ABA);
}

} // namespace

class PortalOverlayModule {
public:
    void install() {
        hud::addLayer("dimension.portal_overlay", 10000, draw);
        auto calls       = core::stubCalls();
        state().native   = std::find(calls.begin(), calls.end(), "client.hud.addLayer") == calls.end();
        feature::net::client::onConnected([] { feature::net::client::send("dimension.client", {{"overlay", state().native}}); });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            state() = ClientState{.native = state().native};
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace gf::client::dimension

GF_REGISTER_MODULE(gf::client::dimension::PortalOverlayModule);

#endif // GF_CLIENT
