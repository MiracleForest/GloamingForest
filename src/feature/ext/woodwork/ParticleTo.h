#pragma once

// Contract extension of U4 woodwork (rule R18: new files only; A8 folds it into feature/fx/Fx.h as
// fx::particleTo). gf::fx::particle shows a particle to every player within 32 blocks. Particles
// that Java spawns from a block's client-side animateTick (DryingRackBlock#animateTick) depend on
// each client's own random sampling around its player (ClientLevel#animateTick), so the server
// decides per player and shows the particle to that player only:
//
//     if (roll < gf::woodwork_ext::particles::animateTickChance(dx, dy, dz) / 5.0)
//         gf::woodwork_ext::particles::particleTo(player, "gloamingforest:drying_rack", pos);
//
// particleTo sends the particle's resource-pack effect (the effect identifier is the particle id)
// to that one player as a SpawnParticleEffectPacket, with no molang variables (a still particle).
// Companions get the same packet: the effect is the particle (F12 declares no native factory for
// the ids this is used for). Server thread.

#include <cstdint>
#include <string>

class Player;
class Vec3;

namespace gf::woodwork_ext::particles {

/// Shows the resource-pack particle effect `effectId` at `pos` to `player` only.
void particleTo(Player& player, std::string const& effectId, Vec3 const& pos);

/// ClientLevel#animateTick: in one client tick, the expected number of animateTick calls the block
/// at offset (dx, dy, dz) from the player's block gets (667 samples with radius 16 plus 667 with
/// radius 32; each axis is nextInt(r) - nextInt(r), P(d) = (r - |d|) / r^2), clamped to 0..1.
[[nodiscard]] double animateTickChance(int dx, int dy, int dz);

/// Packets particleTo sent this process (selftests).
[[nodiscard]] std::uint64_t sentCount();

} // namespace gf::woodwork_ext::particles
