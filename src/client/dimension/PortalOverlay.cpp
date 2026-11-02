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
// above everything with alpha timer / 60 (a HUD layer of gf::client::hud; the sprite is drawn by
// the contract extension feature/ext/dimension/HudSprite.h), and the screen closing while inside a
// portal. The client tells the server that it draws the overlay itself ("dimension.client"), so the
// server sends it none of the camera-fade stand-in vanilla clients get.

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
#include "feature/ext/dimension/HudSprite.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::dimension {

namespace {

constexpr int  kMaxTicks      = 60; // TFPortalAttachment.MAX_TICKS
constexpr char kPortalBlock[] = "twilightforest:twilight_portal";
/// The twilight_portal block's particle texture (minecraft:block/nether_portal), as the resource
/// pack has it: the portal block's own texture (atlas tile "portal").
constexpr char kPortalSprite[] = "textures/blocks/portal";

struct ClientState {
    int  portalTimer = 0;
    int  ticks       = 0; ///< client ticks, for the sprite's animation
    bool inside      = false;
    bool drawing     = false; ///< the module is installed: F12's HUD layer draws the overlay
    std::string popped;       ///< the screen a pop was scheduled for (until the HUD is on top again)
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
    auto& s = state();
    ++s.ticks;
    auto* player = currentPlayer();
    if (!player) {
        s = ClientState{.drawing = s.drawing};
        return;
    }
    auto&    region = player->getDimensionBlockSource();
    BlockPos feet   = game::blockPosOf(player->getFeetPos());
    bool     inside = openPortalAt(region, feet) || openPortalAt(region, BlockPos{feet.x, feet.y - 1, feet.z});
    bool     before = s.portalTimer > 0;
    if (inside) {
        s.portalTimer = std::min(s.portalTimer + 1, kMaxTicks);
        // close any open screen but the pause and death screens. A pop takes effect a few frames
        // later and a screen may close by itself meanwhile (the chat after Enter): the pop names
        // the screen it expects, so it never takes the HUD below, and is scheduled once per screen
        if (auto* c = currentClient()) {
            if (ISceneStack* stack = c->getClientSceneStack().get()) {
                auto name = stack->getScreenName();
                bool base = name.empty() || name.starts_with("hud") || name == "in_game_play_screen";
                if (base) {
                    s.popped.clear();
                } else if (name != "pause_screen" && name != "death_screen" && name != s.popped && stack->getScheduledPopCount() == 0) {
                    stack->schedulePopScreenWithExpectedNames({name});
                    s.popped = name;
                }
            }
        }
    } else if (s.portalTimer > 0) {
        s.portalTimer = std::max(s.portalTimer - 2, 0);
    }
    if (!inside) s.popped.clear();
    if (!before && s.portalTimer > 0) log().info("client portal overlay: {}", s.drawing ? "native" : "camera fade");
    s.inside = inside;
}

/// PortalOverlay#render: the portal block's particle sprite (the animated nether portal texture,
/// one frame a tick as the block atlas animates it) blitted over the whole GUI with the colour
/// (1, 1, 1, timer / MAX_TICKS).
void draw(screen::Canvas& canvas, float) {
    auto const& s = state();
    if (s.portalTimer <= 0) return;
    float alpha = static_cast<float>(s.portalTimer) / static_cast<float>(kMaxTicks);
    auto [w, h] = screen::guiSize();
    core::Rect whole{-canvas.originX(), -canvas.originY(), w, h};
    if (hud::ext::blitSprite(canvas, kPortalSprite, whole, alpha, s.ticks)) return;
    // the texture is still loading: the sprite's average colour (its own alpha is about 3/4)
    auto a = static_cast<unsigned>(std::clamp(alpha * 0.75f, 0.0f, 1.0f) * 255.0f);
    canvas.fill(whole, (a << 24) | 0x540ABA);
}

} // namespace

class PortalOverlayModule {
public:
    void install() {
        // F12's HUD layers draw it (the layer stays for the process: the contract has no removal,
        // and the layer draws nothing while the timer is 0)
        static bool const layered = [] {
            hud::addLayer("dimension.portal_overlay", 10000, draw);
            return true;
        }();
        (void)layered;
        state().drawing = true;
        feature::net::client::onConnected([] { feature::net::client::send("dimension.client", {{"overlay", state().drawing}}); });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            state() = ClientState{.drawing = state().drawing};
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        state() = ClientState{}; // the layer draws nothing (and reports no native overlay) from now on
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace gf::client::dimension

GF_REGISTER_MODULE(gf::client::dimension::PortalOverlayModule);

#endif // GF_CLIENT
