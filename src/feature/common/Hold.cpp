// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/common/ValueBox.cpp, click and hold), LGPL-3.0-or-later.
#include "feature/common/Hold.h"

#include <cstdint>
#include <unordered_map>

#include "mc/world/actor/player/Player.h"

#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::hold {

namespace {
struct Pending {
    std::uint64_t   token = 0;
    core::GlobalPos pos;
    Action          onShort;
    Action          onHold;
};
std::unordered_map<std::int64_t, Pending>& holds() {
    static std::unordered_map<std::int64_t, Pending> m;
    return m;
}

nlohmann::json posJson(core::GlobalPos const& p) { return nlohmann::json::array({p.dim, p.x, p.y, p.z}); }

/// The client's answer to `hold.watch`: {token, state: "hold" | "short" | "cancel"}.
void onRelease(Player& player, nlohmann::json const& msg) {
    auto it = holds().find(player.getOrCreateUniqueID().rawID);
    if (it == holds().end() || it->second.token != msg.value("token", std::uint64_t{0})) return;
    auto hold = std::move(it->second);
    holds().erase(it);
    std::string what = msg.value("state", std::string{"cancel"});
    log().info("hold at {} by {}: {}", hold.pos.key(), player.getRealName(), what);
    if (what == "hold" && hold.onHold) hold.onHold(player);
    else if (what == "short" && hold.onShort) hold.onShort(player);
}
} // namespace

bool awaitHold(Player& player, core::GlobalPos const& pos, Action onShort, Action onHold) {
    if (!net::isCompanion(player)) return false;
    static std::uint64_t nextToken = 0;
    auto                 key       = player.getOrCreateUniqueID().rawID;
    auto                 token     = ++nextToken;
    holds()[key]                   = Pending{token, pos, std::move(onShort), std::move(onHold)};
    net::send(player, "hold.watch", {{"token", token}, {"pos", posJson(pos)}});
    return true;
}

bool holdPending(Player const& player, core::GlobalPos const& pos) {
    auto it = holds().find(const_cast<Player&>(player).getOrCreateUniqueID().rawID);
    return it != holds().end() && it->second.pos == pos;
}

class HoldModule {
public:
    void install() {
        net::on("hold.release", onRelease);
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { holds().clear(); });
        }
    }
    void uninstall() { net::off("hold.release"); }
};

} // namespace gf::hold

GF_REGISTER_MODULE_ORDERED(gf::hold::HoldModule, gf::ModuleOrder::Services);
