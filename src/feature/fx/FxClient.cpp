// Client half of gf::fx (client build): the "fx.particle" handler (see Fx.cpp and
// feature/ext/combat/FxNative.h). An upstream particle goes to the native sink first (F12's particle
// factories, gf::client::particles::spawnFromFx); without a native version the resource pack's
// effect spawns `count` times (at most kMaxClientEffects: Java's count, where vanilla clients get
// at most kMaxVanillaParticles packets), each offset by params "spread" as gf::fx offsets the
// particles of vanilla clients (a gaussian times the distance per axis, ServerLevel#sendParticles),
// with the message's velocity as the molang variable `variable.velocity` (FxMolang.cpp), as
// vanilla clients get it. The effect is added to the client level (Level::addParticleEffect, what
// the game's client code spawns effects with); Level::spawnParticleEffect only broadcasts from a
// server level, so on the client it showed nothing.

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/level/Level.h"

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

std::mutex          gVelocityMutex;
std::optional<Vec3> gLastVelocity;

/// One line per particle id, route and probe (the e2e scenarios wait for it; no line per
/// particle). params "probe" names the caller (a selftest), so each one logs its own line.
void logRoute(std::string const& id, std::string const& route, nlohmann::json const& params) {
    static std::mutex            m;
    static std::set<std::string> seen;
    std::string                  probe = params.is_object() ? params.value("probe", std::string{}) : std::string{};
    std::lock_guard              lock(m);
    if (!seen.insert(id + "|" + route + "|" + probe).second) return;
    if (probe.empty()) log().info("fx: client particle {} {}", id, route);
    else log().info("fx: client particle {} {} (probe {})", id, route, probe);
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
                logRoute(id, "drawn by its native factory", params);
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
        logRoute(id, "is silent (no native factory, no resource pack effect)", params);
        return;
    }
    int  n      = std::clamp(count, 1, kMaxClientEffects);
    auto spread = n > 1 ? spreadOf(params) : std::nullopt;
    thread_local std::mt19937 rng{std::random_device{}()};
    // a moving particle gets its own map; the others share one empty map (never destroyed: the
    // game's destructor must not run at process exit)
    static MolangMap*        sEmpty = new MolangMap();
    std::optional<MolangMap> vars;
    if (vel.x != 0.f || vel.y != 0.f || vel.z != 0.f) fillVariables(vars.emplace(), vel);
    MolangVariableMap const& molang = vars ? vars->get() : sEmpty->get();
    HashedString const       name(effect);
    for (int i = 0; i < n; ++i) player->getLevel().addParticleEffect(name, spread ? spreadAt(pos, *spread, rng) : pos, molang);
    ++gEffects;
    {
        std::lock_guard lock(gVelocityMutex);
        gLastVelocity = vel.x != 0.f || vel.y != 0.f || vel.z != 0.f ? std::optional<Vec3>(vel) : std::nullopt;
    }
    logRoute(id, "spawned as the resource pack effect " + effect, params);
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

std::optional<Vec3> lastEffectVelocity() {
    std::lock_guard lock(gVelocityMutex);
    return gLastVelocity;
}

} // namespace gf::fx::client

GF_REGISTER_MODULE_ORDERED(gf::fx::client::FxClientModule, gf::ModuleOrder::Contracts);

#endif
