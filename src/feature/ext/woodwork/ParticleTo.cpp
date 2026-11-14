// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/DryingRackBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract extension of U4 woodwork (ParticleTo.h): a particle for one player, and the sampling
// rate of Minecraft's ClientLevel#animateTick that drives DryingRackBlock#animateTick.

#include "feature/ext/woodwork/ParticleTo.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <optional>

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/Player.h"

namespace gf::woodwork_ext::particles {

namespace {
std::atomic<std::uint64_t> gSent{0};

/// P(nextInt(r) - nextInt(r) == d).
double axis(int d, int r) {
    int a = std::abs(d);
    if (a >= r) return 0.0;
    return static_cast<double>(r - a) / (static_cast<double>(r) * r);
}
} // namespace

void particleTo(Player& player, std::string const& effectId, Vec3 const& pos) {
    auto dimId = static_cast<uchar>(static_cast<int>(player.getDimensionId()));
    SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(pos, effectId, dimId, std::nullopt));
    pkt.sendTo(player);
    ++gSent;
}

double animateTickChance(int dx, int dy, int dz) {
    double e = 667.0 * (axis(dx, 16) * axis(dy, 16) * axis(dz, 16) + axis(dx, 32) * axis(dy, 32) * axis(dz, 32));
    return std::clamp(e, 0.0, 1.0);
}

std::uint64_t sentCount() { return gSent.load(); }

} // namespace gf::woodwork_ext::particles
