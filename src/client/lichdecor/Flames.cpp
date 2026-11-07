// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/LightableBlock.java (addParticlesAndSound)
//   src/main/java/twilightforest/block/CandelabraBlock.java (animateTick)
//   src/main/java/twilightforest/block/AbstractSkullCandleBlock.java (animateTick)
//   src/main/java/twilightforest/block/WallSkullCandleBlock.java (animateTick)
//   src/main/java/twilightforest/block/OminousCandleBlock.java (animateTick, addParticlesAndSound)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 on the client (client mod): the candle flames of the lit candelabras, skull candles and
// ominous candles. Java draws them in Block#animateTick, which ClientLevel#animateTick runs every
// tick for 667 samples at radius 16 and 667 at radius 32 around the local player (each axis
// nextInt(r) - nextInt(r)); every sampled lit block puts a flame (small / dim / ominous flame) on
// each candle and, 30 % of the time, a smoke puff (NORMAL lighting and ominous candles). Custom
// Bedrock blocks have no display tick, so vanilla clients see the candles without flames.
// The candle ambient sound (17 %) is not played.
#ifdef GF_CLIENT

#include <atomic>
#include <cmath>
#include <cstdint>
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
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "feature/ext/ui/Client.h"
#include "feature/fx/FxInternal.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::lichdecor {

namespace {

namespace light = gf::feature::lichdecor::light;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int   nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
float nextFloat() { return std::uniform_real_distribution<float>(0.0F, 1.0F)(rng()); }

std::atomic<std::uint64_t> gFlames{0};

/// The cheap pre-check before the block's states are read.
bool maybeLightable(std::string const& id) {
    return id == light::kCandelabra || id.ends_with("skull_candle") || id.find("ominous_candle") != std::string::npos;
}

/// A particle as a companion's client draws it: the native factory, else the resource pack effect.
void spawn(LocalPlayer& player, std::string const& particle, Vec3 const& pos) {
    if (gf::client::particles::spawn(particle, pos, Vec3{0, 0, 0})) return;
    static auto* sMap = new gf::fx::MolangMap(); // never destroyed (the game's destructor at exit)
    auto         r    = gf::fx::resolveParticle(particle);
    if (r.effect.empty()) return;
    if (r.vars & gf::fx::kVarVelocity) sMap->setVelocity(Vec3{0, 0, 0});
    player.getLevel().addParticleEffect(HashedString(r.effect), Vec3{pos.x + r.offset.x, pos.y + r.offset.y, pos.z + r.offset.z}, sMap->get());
}

void animate(LocalPlayer& player, Block const& block, BlockPos const& pos, std::int64_t time) {
    for (auto const& f : light::animateFlames(block, pos, time)) {
        Vec3 at{pos.x + f.offset.x, pos.y + f.offset.y, pos.z + f.offset.z};
        // LightableBlock#addParticlesAndSound: smoke 30 % of the time, then the flame
        if (nextFloat() < 0.3F && f.smoke) spawn(player, "minecraft:smoke", at);
        spawn(player, f.particle, at);
        if (gFlames.fetch_add(1, std::memory_order_relaxed) == 0)
            log().info("lichdecor: the client draws the candle flames ({} at {} {} {})", f.particle, pos.x, pos.y, pos.z);
    }
}

void animateTick() {
    auto* player = gf::client::currentPlayer();
    if (!player) return;
    auto const& region = player->getDimensionBlockSourceConst();
    auto const  p      = player->getPosition();
    BlockPos    at{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    short       minY = region.getMinHeight(), maxY = region.getMaxHeight();
    auto        time = static_cast<std::int64_t>(player->getLevel().getCurrentTick().tickID);
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            BlockPos q{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (q.y < minY || q.y >= maxY) continue;
            auto const& b = region.getBlock(q);
            if (b.isAir() || !maybeLightable(b.getTypeName())) continue;
            animate(*player, b, q, time);
        }
    }
}

class FlamesModule {
public:
    void install() { mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { animateTick(); }); }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace

} // namespace gf::client::lichdecor

GF_REGISTER_MODULE(gf::client::lichdecor::FlamesModule);

#endif
