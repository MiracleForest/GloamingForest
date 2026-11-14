#pragma once

// Contract extension of "rules+fx" for U3 (rule R18: new file; A8 folds it into
// src/feature/fx/Fx.h). gf::fx::particle sends to every player within 32 blocks; upstream's mining core
// (OreMagnetItem#doMagnet with sourceIsMineCore) sends one ParticlePacket per ServerPlayer within 64
// blocks, each with its own 16 random offsets drawn from the level's random. This sends one
// particle to one player only, the way gf::fx::particle sends it to each player it reaches: the
// companion's native particle ("fx.particle") or, for a vanilla client, the resource pack effect
// (SpawnParticleEffectPacket with the molang variables gf::fx would set).
//
//     gf::fx::ext::particleTo(player, "gloamingforest:log_core_particle", pos, {0.8f, 0.9f, 0.2f});

#include <string>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class Player;

namespace gf::fx::ext {

/// Sends `count` particles of `particleId` at `pos` (velocity `velocity`, `params` as for
/// gf::fx::particle) to `player` alone. Unknown ids are skipped (gf::fx warns once).
void particleTo(Player& player, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity = {}, int count = 1, nlohmann::json const& params = {});

} // namespace gf::fx::ext
