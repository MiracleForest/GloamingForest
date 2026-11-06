// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/ProgressionEvents.java (performProtectionAndPortalChecks)
//   src/main/java/twilightforest/util/Enforcement.java
//   src/main/java/twilightforest/init/custom/Enforcements.java
//   src/main/java/twilightforest/world/components/structures/util/StructureHints.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Biome progression: on every server tick each player whose own tick counter is a multiple of 20
// (ProgressionEvents), while tfEnforcedProgression is true and the player is neither creative nor
// spectator, gets the enforcement of the restriction of the biome at its feet (unless it has the
// restriction's advancements) and a hint for the restriction's structure
// (StructureHints.tryHintForStructure: landmark::trySpawnHint, rate limited by F6). Each
// enforcement checks the tick counter again (60, acid rain 5), so the effective periods are 60
// and 20 ticks.

#include "feature/progression_content/Restrictions.h"

#include <mutex>
#include <optional>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ToastRequestPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"

#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/fx/Fx.h"
#include "feature/gamerules/GameRules.h"
#include "feature/landmark/Landmark.h"
#include "feature/net/Net.h"
#include "feature/progression_content/Content.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::progression_content {

namespace cp = core::progression;

namespace {

struct EnforceState {
    std::mutex                                                   mutex;
    std::unordered_map<std::string, std::string>                 biomeOverride; ///< uuid -> biome (selftests)
    std::unordered_map<std::string, std::string>                 lockedIn;      ///< uuid -> restricted biome (log once per visit)
    std::unordered_map<std::string, std::map<cp::Enforcement, int>> counts;
    std::unordered_map<std::string, cp::LockedToastCountdown>       vanillaToast; ///< uuid -> countdown
    std::unordered_map<std::string, int>                            vanillaToasts; ///< uuid -> toasts sent
    /// uuid -> (the level tick the override ends at, the game type to put back then)
    std::unordered_map<std::string, std::pair<std::uint64_t, std::optional<GameType>>> overrideEnd;
    std::uint64_t now = 0;
};

EnforceState& es() {
    static EnforceState s;
    return s;
}

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

void enforce(Player& player, cp::Restriction const& r, int tickCount) {
    if (!cp::innerGate(r.enforcement, tickCount)) return;
    switch (r.enforcement) {
    case cp::Enforcement::Darkness:
        // new MobEffectInstance(DARKNESS, 200, (int) multiplier, false, true)
        effects::apply(player, "minecraft:darkness", 200, static_cast<int>(r.multiplier));
        break;
    case cp::Enforcement::Hunger:
        effects::apply(player, "minecraft:hunger", 100, cp::hungerAmplifier(effects::amplifier(player, "minecraft:hunger"), r.multiplier));
        break;
    case cp::Enforcement::Fire:
        // igniteForSeconds((int) multiplier)
        player.setOnFire(static_cast<int>(r.multiplier), true);
        break;
    case cp::Enforcement::Frost:
        effects::apply(player, "twilightforest:frosted", 100, static_cast<int>(r.multiplier));
        break;
    case cp::Enforcement::AcidRain:
        if (combat::hurt(player, r.multiplier, "twilightforest:acid_rain")) {
            auto pos = player.getPosition();
            fx::sound(player.getDimension(), "environment.twilightforest.acid_rain", pos, 1.0f, 1.0f);
        }
        break;
    default: return;
    }
    std::lock_guard lock(es().mutex);
    es().counts[uuidOf(player)][r.enforcement]++;
}

/// The vanilla fallback of TF's client-side LockedBiomeToastHandler: the same countdown, kept per
/// player on the server, and the game's toast (text only: ToastRequestPacket has no icon).
void tickVanillaToast(Player& player, int tickCount) {
    if (tickCount % 5 != 0 || net::isCompanion(player)) return;
    if (!gamerules::getBool("tfEnforcedProgression") || player.isCreative() || player.isSpectator()) return;
    auto uuid  = uuidOf(player);
    auto biome = std::string{};
    {
        std::lock_guard lock(es().mutex);
        if (auto it = es().biomeOverride.find(uuid); it != es().biomeOverride.end()) biome = it->second;
    }
    if (biome.empty()) biome = biomeAt(player);
    auto const* r = restrictionFor(player, biome);
    bool        show;
    {
        std::lock_guard lock(es().mutex);
        show = es().vanillaToast[uuid].step(r && !r->toastIcon.empty());
        if (show) es().vanillaToasts[uuid]++;
    }
    if (!show) return;
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::ToastRequest);
    if (!packet) return;
    auto& pkt    = static_cast<ToastRequestPacket&>(*packet);
    pkt.mTitle   = "§e" + trFor(player, "misc.twilightforest.biome_locked", "Biome Locked!");
    pkt.mContent = trFor(player, "misc.twilightforest.biome_locked_2", "Check your advancements");
    pkt.sendTo(player);
    log().info("progression: locked biome toast for {} (vanilla)", player.getRealName());
}

void expireOverride(Player& player) {
    std::optional<GameType> restore;
    {
        std::lock_guard lock(es().mutex);
        auto            it = es().overrideEnd.find(uuidOf(player));
        if (it == es().overrideEnd.end() || es().now < it->second.first) return;
        restore = it->second.second;
        es().biomeOverride.erase(it->first);
        es().overrideEnd.erase(it);
    }
    if (restore) player.setPlayerGameType(*restore);
}

void tickPlayer(Player& player) {
    expireOverride(player);
    int tickCount = player.mTickCount;
    tickVanillaToast(player, tickCount);
    if (!cp::outerGate(tickCount)) return;
    if (!gamerules::getBool("tfEnforcedProgression") || player.isCreative() || player.isSpectator()) return;
    auto uuid  = uuidOf(player);
    auto biome = std::string{};
    {
        std::lock_guard lock(es().mutex);
        if (auto it = es().biomeOverride.find(uuid); it != es().biomeOverride.end()) biome = it->second;
    }
    if (biome.empty()) biome = biomeAt(player);
    auto const* r = restrictionFor(player, biome);
    {
        std::lock_guard lock(es().mutex);
        auto&           was = es().lockedIn[uuid];
        auto            now = r ? r->biome : std::string{};
        if (was != now) {
            if (r) log().info("progression: {} is in the locked biome {} ({})", player.getRealName(), r->biome, r->enforcementId);
            was = now;
        }
    }
    if (!r || r->enforcement == cp::Enforcement::Unknown) return;
    enforce(player, *r, tickCount);
    if (!r->structureKey.empty()) {
        landmark::Start hint;
        hint.key       = r->structureKey;
        hint.dimension = static_cast<int>(player.getDimensionId());
        landmark::trySpawnHint(player, hint, blockPositionOf(player)); // StructureHints: player.blockPosition()
    }
}

class RestrictionsModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            auto level = ll::service::getLevel();
            if (!level) return;
            {
                std::lock_guard lock(es().mutex);
                ++es().now;
            }
            level->forEachPlayer([](Player& p) {
                if (!p.isClientSide() && p.isAlive()) tickPlayer(p);
                return true;
            });
        });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

void setBiomeOverride(Player const& player, std::string const& biome, int forTicks, std::optional<GameType> restoreType) {
    std::lock_guard lock(es().mutex);
    auto            uuid = uuidOf(player);
    es().overrideEnd.erase(uuid);
    if (biome.empty()) {
        es().biomeOverride.erase(uuid);
        return;
    }
    es().biomeOverride[uuid] = biome;
    if (forTicks > 0) es().overrideEnd[uuid] = {es().now + static_cast<std::uint64_t>(forTicks), restoreType};
}

int enforcementCount(Player const& player, cp::Enforcement e) {
    std::lock_guard lock(es().mutex);
    auto            it = es().counts.find(uuidOf(player));
    if (it == es().counts.end()) return 0;
    auto c = it->second.find(e);
    return c == it->second.end() ? 0 : c->second;
}

void resetEnforcementCounts(Player const& player) {
    std::lock_guard lock(es().mutex);
    es().counts.erase(uuidOf(player));
    es().vanillaToast.erase(uuidOf(player));
}

int vanillaLockedToasts(Player const& player) {
    std::lock_guard lock(es().mutex);
    auto            it = es().vanillaToasts.find(uuidOf(player));
    return it == es().vanillaToasts.end() ? 0 : it->second;
}

} // namespace gf::feature::progression_content

GF_REGISTER_MODULE(gf::feature::progression_content::RestrictionsModule);
