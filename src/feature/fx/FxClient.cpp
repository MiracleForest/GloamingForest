// Client half of gf::fx (client build): the "fx.particle" handler (see Fx.cpp and
// feature/ext/combat/FxNative.h). A TF particle goes to the native sink first (F12's particle
// factories, gf::client::particles::spawnFromFx); without a native version the resource pack's
// effect spawns, `count` times (at most 64), each offset by params "spread" as gf::fx offsets the
// particles of vanilla clients (a gaussian times the distance per axis, ServerLevel#sendParticles).

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "feature/ext/combat/FxNative.h"
#include "feature/fx/FxInternal.h"
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

std::atomic<std::uint64_t> gNative{0}, gEffects{0}, gSilent{0};

/// One line per particle id and route (the e2e scenarios wait for it; no line per particle).
void logRoute(std::string const& id, std::string const& route) {
    static std::mutex            m;
    static std::set<std::string> seen;
    std::lock_guard              lock(m);
    if (seen.insert(id + "|" + route).second) log().info("fx: client particle {} {}", id, route);
}

Vec3 vecOf(nlohmann::json const& j) {
    if (!j.is_array() || j.size() != 3) return {};
    for (auto const& v : j)
        if (!v.is_number()) return {};
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
            if (sink(id, pos, vel, count, params)) {
                ++gNative;
                logRoute(id, "drawn by its native factory");
                return;
            }
        } catch (std::exception const& ex) {
            log().error("fx: native particle {} failed: {}", id, ex.what());
        }
    }
    auto  effect = msg.value("effect", std::string{});
    auto* player = gf::client::currentPlayer();
    if (effect.empty() || !player) {
        ++gSilent;
        logRoute(id, "is silent (no native factory, no resource pack effect)");
        return;
    }
    auto& dim = player->getDimension();
    int   n   = std::clamp(count, 1, 64);
    std::optional<Vec3> spread;
    if (params.is_object()) {
        if (auto s = params.find("spread"); s != params.end() && n > 1) {
            auto v = vecOf(*s);
            if (s->is_array() && s->size() == 3) spread = v;
        }
    }
    thread_local std::mt19937       rng{std::random_device{}()};
    std::normal_distribution<float> gauss(0.f, 1.f);
    for (int i = 0; i < n; ++i) {
        Vec3 at = pos;
        if (spread) at = Vec3{pos.x + gauss(rng) * spread->x, pos.y + gauss(rng) * spread->y, pos.z + gauss(rng) * spread->z};
        player->getLevel().spawnParticleEffect(effect, at, &dim);
    }
    ++gEffects;
    logRoute(id, "spawned as the resource pack effect " + effect);
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

bool hasNativeSink() {
    std::lock_guard lock(sinkMutex());
    return static_cast<bool>(sinkSlot());
}

ClientCounters clientCounters() { return {gNative.load(), gEffects.load(), gSilent.load()}; }

} // namespace gf::fx::client

GF_REGISTER_MODULE_ORDERED(gf::fx::client::FxClientModule, gf::ModuleOrder::Contracts);

#endif
