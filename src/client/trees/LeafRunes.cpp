// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TransformationLeavesBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees on the client (client mod): the leaves of transformation shed leaf runes. Java's client
// runs ClientLevel#animateTick every tick around the local player (667 samples at radius 16 and 667
// at radius 32, each axis nextInt(r) - nextInt(r)); a sampled TransformationLeavesBlock spawns one
// twilightforest:leaf_rune below its bottom face (ParticleUtils.spawnParticlesOnBlockFace, DOWN,
// spread 0.5). This is that loop, on the local player's tick, drawing the resource pack effect in
// the client level only. The server does it only for players without the client mod
// (src/feature/trees/Leaves.cpp).
#ifdef GF_CLIENT

#include <cmath>
#include <cstdint>
#include <random>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "feature/fx/FxInternal.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::trees {

namespace {

constexpr char kLeaves[] = "twilightforest:transformation_leaves";
constexpr char kRune[]   = "twilightforest:leaf_rune";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

std::uint64_t gRunes = 0;

void animateTick() {
    auto* player = gf::client::currentPlayer();
    if (!player) return;
    auto const& region = player->getDimensionBlockSourceConst();
    auto const  pos    = player->getPosition();
    BlockPos    at{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    short       minY = region.getMinHeight(), maxY = region.getMaxHeight();
    // never destroyed: the game's destructor must not run at process exit
    static auto* sEmpty = new gf::fx::MolangMap();
    static HashedString const name(kRune);
    int                       spawned = 0;
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            BlockPos p{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (p.y < minY || p.y >= maxY) continue;
            if (region.getBlock(p).getTypeName() != kLeaves) continue;
            // spawnParticleOnFace(DOWN): x / z spread 0.5 around the centre, y 0.55 below it
            Vec3 v{static_cast<float>(p.x + 0.5 + (-0.5 + nextDouble())), static_cast<float>(p.y + 0.5 - 0.55), static_cast<float>(p.z + 0.5 + (-0.5 + nextDouble()))};
            player->getLevel().addParticleEffect(name, v, sEmpty->get());
            ++spawned;
        }
    }
    if (spawned > 0 && gRunes == 0) log().info("trees: the client draws the leaf runes of the leaves of transformation");
    gRunes += static_cast<std::uint64_t>(spawned);
}

class LeafRunesModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        gf::client::movement::onLocalTick(0, [](gf::client::movement::MoveState&) { animateTick(); });
    }
    void uninstall() {}
};

} // namespace

std::uint64_t leafRunesDrawn() { return gRunes; }

} // namespace gf::client::trees

GF_REGISTER_MODULE(gf::client::trees::LeafRunesModule);

#endif
