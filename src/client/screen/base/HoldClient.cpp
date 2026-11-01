// Adapted from LiteLDev/Levitate v0.1.0 (src/client/screen/ValueBoxClient.cpp, click and hold),
// LGPL-3.0-or-later.
//
// Client side of click and hold (feature/common/Hold.h): the server asks with `hold.watch
// {token, pos}`; every client tick we answer `hold.release {token, state}`: released -> "short",
// held for more than 3 ticks -> "hold", looked away / sneaking / a screen opened -> "cancel".

#ifdef GF_CLIENT

#include <atomic>
#include <cstdint>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/HitResult.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "core/GlobalPos.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::hold {

namespace {
namespace net = gf::feature::net;
using Json    = nlohmann::json;

/// The use (right) mouse button, from the input thread; whether the mouse ever used it (a
/// gamepad or touch use is not seen here: those clicks count as a hold at once).
std::atomic<bool> gUseDown{false};
std::atomic<bool> gMouseUse{false};

struct Hold {
    bool            active = false;
    std::uint64_t   token  = 0;
    core::GlobalPos pos;
    int             ticks = 0;
};
Hold& hold() {
    static Hold h;
    return h;
}

void answer(char const* state) {
    auto& h = hold();
    net::client::send("hold.release", {{"token", h.token}, {"state", state}});
    log().info("hold client: {} at {}", state, h.pos.key());
    h.active = false;
}

void poll() {
    auto& h = hold();
    if (!h.active) return;
    auto* player = currentPlayer();
    if (!player || !net::client::connected()) {
        h.active = false;
        return;
    }
    auto const& hit   = player->getLevel().getHitResult();
    bool        onPos = hit.mType == HitResultType::Tile && static_cast<int>(player->getDimensionId()) == h.pos.dim && hit.mBlock.x == h.pos.x
              && hit.mBlock.y == h.pos.y && hit.mBlock.z == h.pos.z;
    if (!onPos || player->isSneaking() || screen::current()) return answer("cancel");
    if (!gMouseUse.load()) return answer("hold");
    if (!gUseDown.load()) return answer("short");
    if (h.ticks > 3) return answer("hold");
    ++h.ticks;
}
} // namespace

class HoldClientModule {
public:
    void install() {}
    // Client input listeners are added at mod load, before the game's threads run: hooking them
    // while the client is running (mod enable) can deadlock the game.
    void load() {
        net::client::on("hold.watch", [](Json const& msg) {
            auto  p = msg.value("pos", Json::array());
            auto& h = hold();
            if (h.active) answer("cancel"); // a newer click replaces it
            if (!p.is_array() || p.size() != 4) return;
            h        = Hold{};
            h.active = true;
            h.token  = msg.value("token", std::uint64_t{0});
            h.pos    = core::GlobalPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
            log().info("hold client: watching at {}", h.pos.key());
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
            if (ev.actionButtonId() != 2) return;
            gUseDown  = ev.buttonData() != 0;
            gMouseUse = true;
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { poll(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { hold() = Hold{}; }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace gf::client::hold

GF_REGISTER_MODULE(gf::client::hold::HoldClientModule);

#endif // GF_CLIENT
