// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U1 `biomes`, client build: the biomes' ambient particles drawn by the client mod itself, as Java's
// ClientLevel#animateTick draws them around the local player (feature/biomes/Ambient.cpp's sampler,
// every client tick in an upstream biome). A particle with a native factory (F12's registry) is drawn by
// it, any other as its resource pack effect added to the client level. The server sends these
// samples only to players without the client mod, so nothing is drawn twice.

#ifdef GF_CLIENT

#include <atomic>
#include <memory>
#include <new>
#include <random>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "feature/biomes/Ambient.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::biomes {

namespace {

/// An empty molang variable map for the effects (built in place: the game's map has no exported
/// constructor; never destroyed, so the game's destructor does not run at process exit).
MolangVariableMap const& emptyMolang() {
    static auto* map = [] {
        alignas(MolangVariableMap) static unsigned char storage[sizeof(MolangVariableMap)];
        auto* m = reinterpret_cast<MolangVariableMap*>(storage);
        std::construct_at(std::addressof(m->mMapFromVariableIndexToVariableArrayOffset));
        std::construct_at(std::addressof(m->mVariables));
        m->mHasPublicVariables = false;
        return std::launder(m);
    }();
    return *map;
}

std::atomic<std::uint64_t> gDrawn{0};

void tick() {
    auto* player = gf::client::currentPlayer();
    if (!player) return;
    auto& region = player->getDimensionBlockSource();
    auto  at     = game::blockPosOf(player->getFeetPos());
    if (!feature::biomes::isTfBiome(feature::biomes::biomeIdOf(region.getBiome(at)))) return;
    thread_local std::mt19937 random{std::random_device{}()};
    feature::biomes::ambientParticles(region, at, random, [&](std::string const& particle, Vec3 const& pos) {
        bool native = gf::client::particles::spawn(particle, pos, Vec3{0, 0, 0});
        if (!native) {
            char const* effect = feature::biomes::ambientEffectOf(particle);
            if (!effect) return;
            player->getLevel().addParticleEffect(HashedString(effect), pos, emptyMolang());
        }
        if (gDrawn.fetch_add(1, std::memory_order_relaxed) == 0)
            log().info("biomes: client drew the ambient particle {} at {:.1f} {:.1f} {:.1f} ({})", particle, pos.x, pos.y, pos.z, native ? "native" : "resource pack effect");
    });
}

class BiomeAmbientClientModule {
public:
    void install() { mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); }); }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace

} // namespace gf::client::biomes

GF_REGISTER_MODULE(gf::client::biomes::BiomeAmbientClientModule);

#endif
