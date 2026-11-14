// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U29 giants: gf::fx::giants_ext::vanillaParticleTo (see ParticleTo.h).
#include "feature/ext/giants/ParticleTo.h"

#include <optional>

#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/Player.h"

#include "feature/fx/FxInternal.h"
#include "feature/net/Net.h"

namespace gf::fx::giants_ext {

void vanillaParticleTo(Player& player, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity) {
    if (net::isCompanion(player)) return;
    auto r = resolveParticle(particleId);
    if (r.effect.empty()) return;
    bool const moving = velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f;
    std::optional<MolangVariableMap> molang;
    MolangMap                        map;
    if (moving || (r.vars & kVarVelocity) != 0) {
        fillEffectVariables(map, r.vars, velocity, velocity, colorOf({}, r.color));
        molang = map.get();
    }
    auto const dimId = static_cast<uchar>(static_cast<int>(player.getDimensionId()));
    Vec3 const at{pos.x + r.offset.x, pos.y + r.offset.y, pos.z + r.offset.z};
    SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(at, r.effect, dimId, std::move(molang)));
    pkt.sendTo(player);
}

} // namespace gf::fx::giants_ext
