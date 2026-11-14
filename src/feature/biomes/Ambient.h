#pragma once

// U1 `biomes`, internal: the biomes' ambient particles (ClientLevel#animateTick's ambient particle
// part), shared by the server (vanilla players: SpawnParticleEffectPackets to that player) and the
// client mod (drawn locally). See Ambient.cpp.

#include <functional>
#include <random>
#include <string>

#include "mc/deps/core/math/Vec3.h"

class Biome;
class BlockPos;
class BlockSource;

namespace gf::feature::biomes {

/// The effect of upstream's ambient particle `particleId` in the resource pack (the particle's own id).
[[nodiscard]] char const* ambientEffectOf(std::string const& particleId);

/// One tick of ClientLevel#animateTick's ambient particles around the block `at` (the player's):
/// 667 samples at radius 16 and 667 at radius 32, each spawning its biome's particle with the
/// biome's probability at a random point of a block without a full collision cube; `spawn(particle,
/// pos)` is called for each.
void ambientParticles(BlockSource& region, BlockPos const& at, std::mt19937& random, std::function<void(std::string const& particleId, Vec3 const& pos)> const& spawn);

} // namespace gf::feature::biomes
