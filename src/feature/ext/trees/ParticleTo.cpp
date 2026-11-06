// U3 trees: gf::fx::ext::particleTo (see ParticleTo.h). Sends a particle to one player the way
// gf::fx::particle (src/feature/fx/Fx.cpp) sends it to each player in range.
#include "feature/ext/trees/ParticleTo.h"

#include <algorithm>
#include <optional>
#include <random>

#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/Player.h"

#include "feature/fx/FxInternal.h"
#include "feature/net/Net.h"

namespace gf::fx::ext {

namespace {
nlohmann::json vec(Vec3 const& v) { return nlohmann::json::array({v.x, v.y, v.z}); }
} // namespace

void particleTo(Player& player, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params) {
    auto r = resolveParticle(particleId);
    if (r.native && net::isCompanion(player)) {
        net::send(player, "fx.particle", {{"id", particleId}, {"pos", vec(pos)}, {"vel", vec(velocity)}, {"count", count}, {"params", params}, {"effect", r.effect}});
        return;
    }
    if (r.effect.empty()) return; // legacy-only particles are not used by U3
    bool const moving    = velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f;
    bool const needsVars = moving || (r.vars & (kVarVelocity | kVarDirection | kVarColorRgb | kVarColorRgba | kVarNoteColor | kVarDustLook)) != 0;
    auto const dimId     = static_cast<uchar>(static_cast<int>(player.getDimensionId()));
    Vec3 const at{pos.x + r.offset.x, pos.y + r.offset.y, pos.z + r.offset.z};
    static thread_local std::mt19937 rng{std::random_device{}()};
    Rgba const                       color = colorOf(params, r.color);
    float const                      scale = dustScale(params);
    for (int i = 0, n = std::clamp(count, 1, 16); i < n; ++i) {
        std::optional<MolangVariableMap> molang;
        MolangMap                        map;
        if (needsVars) {
            // the motion and dust look gf::fx::particle gives each particle (Fx.cpp)
            Vec3 direction = velocity;
            if (r.vars & kVarSplashDirection) direction = splashDirection(rng, velocity);
            else if (r.vars & kVarCritDirection) direction = critDirection(rng, velocity);
            else if (r.vars & kVarDustDirection) direction = dustDirection(rng, velocity);
            DustLook const dust = (r.vars & kVarDustLook) ? dustLook(rng, scale) : DustLook{};
            fillEffectVariables(map, r.vars, velocity, direction, color, dust);
            molang = map.get();
        }
        SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(at, r.effect, dimId, std::move(molang)));
        pkt.sendTo(player);
    }
}

} // namespace gf::fx::ext
