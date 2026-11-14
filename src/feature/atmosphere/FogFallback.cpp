// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/GloamingForestRenderInfo.java
//   src/main/java/gloamingforest/config/GloamingClientConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), server side: the config section "atmosphere" and the vanilla fallback of upstream's
// fog. A vanilla Bedrock client has no upstream fog rendering; it gets the resource pack fog definitions
// of this unit (assets/pack/rp/fogs/atmosphere) pushed with PlayerFogPacket: twice a second the
// server works out each vanilla player's fog stack in the Gloaming Forest (the biome's fog, the
// near fog of the dark forests below y 20; GloamingForestRenderInfo#isFoggyAt) and sends it when it
// changed (an empty stack when the player leaves). Companions (the client mod) draw upstream's own fog and
// are left alone.

#include "feature/atmosphere/Atmosphere.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/PlayerFogPacket.h"
#include "mc/network/packet/PlayerFogPacketPayload.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/atmosphere/Atmosphere.h"
#include "feature/atmosphere/Internal.h"
#include "feature/dimension/World.h"
#include "feature/net/Net.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::atmosphere {

GF_REGISTER_CONFIG("atmosphere", AtmosphereConfig);

namespace ca = gf::core::atmosphere;

std::vector<std::string> fogStackFor(std::string_view biomeId, double y) {
    if (!ca::biomeLook(biomeId)) return {};
    std::string id(biomeId);
    // the spooky forest's own fog is its near fog; the dark forests get theirs below y 20
    if (!ca::isSpooky(biomeId) && ca::isFoggyAt(biomeId, y)) id += "_near";
    return {id};
}

std::vector<std::string> fogDefinitionIds() {
    std::vector<std::string> ids;
    for (auto const& l : ca::biomeLooks()) ids.emplace_back(l.id);
    ids.emplace_back("gloamingforest:dark_forest_near");
    ids.emplace_back("gloamingforest:dark_forest_center_near");
    return ids;
}

namespace {

struct FogState {
    std::mutex                                                      mutex;
    std::unordered_map<std::int64_t, std::vector<std::string>>      sent;   ///< by unique id
    std::unordered_map<std::int64_t, std::string>                   pinned; ///< pinFogBiome
    std::unordered_map<std::int64_t, int>                           dims;   ///< the dimension of the last update
};
FogState& fogState() {
    static FogState s;
    return s;
}
std::atomic<std::uint64_t> gPackets{0};
/// A sent stack no real stack equals: the next update sends whatever it computes.
constexpr char kStale[] = "\x01stale";

std::int64_t keyOf(Player const& player) { return player.getOrCreateUniqueID().rawID; }

/// The biome at the player's feet (upstream reads player.blockPosition(); Bedrock's getPosition is the eyes).
std::string biomeAt(Player& player) {
    BlockPos bp     = player.getFeetBlockPos();
    auto&    region = player.getDimensionBlockSource();
    if (!region.hasChunksAt(bp, 0, false)) return {};
    return region.getBiome(bp).mHash->getString();
}

void sendStack(Player& player, std::vector<std::string> const& stack) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlayerFogSetting);
    if (!packet) return;
    static_cast<PlayerFogPacket&>(*packet).mFogStack = stack;
    packet->sendTo(player);
    gPackets.fetch_add(1, std::memory_order_relaxed);
}

std::string join(std::vector<std::string> const& v) {
    std::string s;
    for (auto const& e : v) s += (s.empty() ? "" : ",") + e;
    return s.empty() ? "(none)" : s;
}

} // namespace

bool updateFog(Player& player, std::optional<std::string> const& biomeOverride) {
    std::vector<std::string> stack;
    if (::gf::world::isGloaming(player.getDimension())) {
        std::optional<std::string> pinned;
        if (!biomeOverride) {
            std::lock_guard lock(fogState().mutex);
            if (auto it = fogState().pinned.find(keyOf(player)); it != fogState().pinned.end()) pinned = it->second;
        }
        std::string biome = biomeOverride ? *biomeOverride : pinned ? *pinned : biomeAt(player);
        stack             = fogStackFor(biome, player.getFeetPos().y); // isFoggyAt: player.position(), the feet
    }
    {
        std::lock_guard lock(fogState().mutex);
        // a vanilla client may reset its fog stack on a dimension change: send the new stack (an
        // empty one too, which clears upstream's fog) whatever was sent before
        int const dim = static_cast<int>(player.getDimensionId());
        if (auto d = fogState().dims.find(keyOf(player)); d != fogState().dims.end() && d->second != dim) fogState().sent[keyOf(player)] = {kStale};
        fogState().dims[keyOf(player)] = dim;
        auto it                        = fogState().sent.find(keyOf(player));
        if (it == fogState().sent.end() ? stack.empty() : it->second == stack) return false;
        fogState().sent[keyOf(player)] = stack;
    }
    sendStack(player, stack);
    log().info("atmosphere: fog {} sent to {}", join(stack), player.getRealName());
    return true;
}

std::optional<std::vector<std::string>> sentFogStack(Player const& player) {
    std::lock_guard lock(fogState().mutex);
    auto            it = fogState().sent.find(keyOf(player));
    if (it == fogState().sent.end()) return std::nullopt;
    return it->second;
}

void forgetFog(Player const& player) {
    std::lock_guard lock(fogState().mutex);
    fogState().sent.erase(keyOf(player));
    fogState().dims.erase(keyOf(player));
}

void pinFogBiome(Player const& player, std::optional<std::string> biome) {
    std::lock_guard lock(fogState().mutex);
    if (biome) fogState().pinned[keyOf(player)] = std::move(*biome);
    else fogState().pinned.erase(keyOf(player));
}

std::uint64_t fogPacketsSent() { return gPackets.load(std::memory_order_relaxed); }

namespace {

class FogFallbackModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks % 10 != 0) return;
            auto level = ll::service::getLevel();
            if (!level) return;
            level->forEachPlayer([](Player& p) {
                if (p.isSimulatedPlayer() || net::isCompanion(p)) return true; // the client mod draws upstream's fog
                updateFog(p);
                return true;
            });
        });
        mLeave = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](auto& ev) {
            forgetFog(ev.self());
            pinFogBiome(ev.self(), std::nullopt);
        });
        // a respawned vanilla client starts from its default fog stack: the next update sends again
        mRespawn = bus.emplaceListener<ll::event::PlayerRespawnEvent>([](auto& ev) {
            {
                std::lock_guard lock(fogState().mutex);
                fogState().sent[keyOf(ev.self())] = {kStale};
            }
            log().info("atmosphere: fog of {} resent after the respawn", ev.self().getRealName());
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mLeave);
        bus.removeListener(mRespawn);
        std::lock_guard lock(fogState().mutex);
        fogState().sent.clear();
    }

private:
    ll::event::ListenerPtr mTick, mLeave, mRespawn;
    std::uint64_t          mTicks = 0;
};

} // namespace

} // namespace gf::feature::atmosphere

GF_REGISTER_MODULE(gf::feature::atmosphere::FogFallbackModule);
