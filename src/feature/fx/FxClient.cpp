// Client half of gf::fx (client build): the "fx.particle" handler (see Fx.cpp and
// feature/ext/combat/FxNative.h).

#ifdef GF_CLIENT

#include <mutex>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "feature/ext/combat/FxNative.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::fx::client {

namespace {
std::mutex& sinkMutex() {
    static std::mutex m;
    return m;
}
NativeSink& sinkSlot() {
    static NativeSink s;
    return s;
}

Vec3 vecOf(nlohmann::json const& j) {
    if (!j.is_array() || j.size() != 3) return {};
    return Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

void onParticle(nlohmann::json const& msg) {
    auto id     = msg.value("id", std::string{});
    auto pos    = vecOf(msg.value("pos", nlohmann::json{}));
    auto vel    = vecOf(msg.value("vel", nlohmann::json{}));
    int  count  = msg.value("count", 1);
    auto params = msg.value("params", nlohmann::json{});
    NativeSink sink;
    {
        std::lock_guard lock(sinkMutex());
        sink = sinkSlot();
    }
    if (sink) {
        try {
            if (sink(id, pos, vel, count, params)) return;
        } catch (std::exception const& ex) {
            log().error("fx: native particle {} failed: {}", id, ex.what());
        }
    }
    auto effect = msg.value("effect", std::string{});
    if (effect.empty()) return;
    auto* player = gf::client::currentPlayer();
    if (!player) return;
    auto& dim = player->getDimension();
    for (int i = 0; i < std::max(1, std::min(count, 64)); ++i) player->getLevel().spawnParticleEffect(effect, pos, &dim);
}

class FxClientModule {
public:
    void install() { net::client::on("fx.particle", onParticle); }
    void uninstall() {}
};
} // namespace

void setNativeSink(NativeSink sink) {
    std::lock_guard lock(sinkMutex());
    sinkSlot() = std::move(sink);
}

} // namespace gf::fx::client

GF_REGISTER_MODULE_ORDERED(gf::fx::client::FxClientModule, gf::ModuleOrder::Contracts);

#endif
