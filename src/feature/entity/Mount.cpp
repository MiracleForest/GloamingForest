// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/IHostileMount.java
//   src/main/java/twilightforest/events/HostileMountEvents.java
//   src/main/java/twilightforest/components/entity/YetiThrowAttachment.java
//   src/main/java/twilightforest/entity/ai/goal/ThrowRiderGoal.java
//   src/main/java/twilightforest/network/MovePlayerPacket.java
//   src/main/java/twilightforest/network/UpdateThrownPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// IHostileMount: a mob grabs a player as its rider (alpha yeti, pinch beetle, yeti); the player
// can't dismount, crouch off or teleport away, takes no suffocation damage while held, and is
// thrown with YetiThrowAttachment's state (fall damage while thrown becomes "yeeted" damage by
// the thrower). Bedrock has no dismount event to cancel: a held player that left the seat is put
// back on the next tick (teleports within 32 blocks included), unless the mob let go
// (hostileDismount) or the player is invulnerable (creative / spectator, as Java's
// `!player.getAbilities().invulnerable`).

#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/combat/Combat.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::entity {

namespace {

constexpr int    kThrowCooldown = 200; ///< YetiThrowAttachment.THROW_COOLDOWN
constexpr double kRegrabRange   = 32.0;

struct ThrowState {
    bool         thrown   = false;
    std::int64_t thrower  = 0;
    int          cooldown = 0;
    Vec3         vector{};
};

struct MountState {
    std::unordered_map<std::int64_t, ThrowState> throws; ///< player -> YetiThrowAttachment
    bool                                         allowDismount = false;
};
MountState& S() {
    static MountState s;
    return s;
}

/// Java's player.push + MovePlayerPacket: the server's motion and the client's. A companion's
/// client pushes itself (entity.move_player), a vanilla client gets the motion packet.
void pushPlayer(Player& player, Vec3 const& v) {
    auto d = player.getPosDelta();
    Vec3 n{d.x + v.x, d.y + v.y, d.z + v.z};
    if (gf::net::isCompanion(player)) {
        const_cast<Vec3&>(player.getPosDelta()) = n;
        gf::net::send(player, "entity.move_player", {{"x", v.x}, {"y", v.y}, {"z", v.z}});
    } else {
        setMotion(player, n);
    }
}

/// UpdateThrownPacket to the player itself and the companions that see it.
void sendThrown(Player& player, ThrowState const& t) {
    nlohmann::json msg{
        {"id",       uidOf(player)},
        {"thrown",   t.thrown     },
        {"thrower",  t.thrower    },
        {"cooldown", t.cooldown   },
    };
    for (auto* p : playersNear(static_cast<int>(player.getDimensionId()), player.getFeetPos(), 160.0))
        if (gf::net::isCompanion(*p)) gf::net::send(*p, "projectile.thrown", msg);
}

void setRidingUnfriendly(Player& player, std::int64_t vehicle, bool riding) {
    if (gf::net::isCompanion(player)) gf::net::send(player, "entity.hostile_mount", {{"vehicle", vehicle}, {"riding", riding}});
}

/// YetiThrowAttachment#tick for every player with throw state.
void tickThrows() {
    auto& throws = S().throws;
    for (auto it = throws.begin(); it != throws.end();) {
        auto* player = playerById(it->first);
        if (!player) {
            it = throws.erase(it);
            continue;
        }
        auto& t = it->second;
        if (t.thrown && (player->isOnGround() || player->isInWater())) {
            t.thrown  = false;
            t.thrower = 0;
            sendThrown(*player, t);
        }
        if (t.cooldown > 0) {
            if (t.cooldown == kThrowCooldown - 1) { // actually throw the victim
                pushPlayer(*player, t.vector);
                t.vector = Vec3{0, 0, 0};
            }
            t.cooldown--;
        }
        if (!t.thrown && t.cooldown <= 0) it = throws.erase(it);
        else ++it;
    }
}

} // namespace

bool ridingUnfriendly(Actor const& actor) {
    auto* vehicle = actor.getVehicle();
    if (!vehicle) return false;
    auto* s = findState(uidOf(*vehicle));
    return s && s->hostileMount;
}

void tickMount(Mob& mob, ActorState& s) {
    // HostileMountEvents#preventHostilMountCrouching
    for (auto* rider = mob.getFirstPassenger(); rider; rider = nullptr) rider->setSneaking(false);
    if (!s.grabbed) return;
    auto* player = playerById(s.grabbed);
    if (!player) {
        s.grabbed = 0;
        return;
    }
    if (player->getVehicle() == &mob) return;
    // the player left the seat (sneak, teleport): preventMountDismount / preventTeleporting
    bool invulnerable = player->isCreative() || player->isSpectator();
    if (player->isAlive() && mob.isAlive() && !invulnerable && !S().allowDismount
        && mobs::distanceSqr(*player, mob) < kRegrabRange * kRegrabRange) {
        player->startRiding(mob, true);
        if (player->getVehicle() == &mob) return;
    }
    s.grabbed = 0;
    setRidingUnfriendly(*player, uidOf(mob), false);
}

void installMounts() {
    static bool once = false;
    if (std::exchange(once, true)) return;
    onLevelTick(tickThrows);
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        // lets not make the player take suffocation damage if riding something
        if (d.victim.isPlayer() && ridingUnfriendly(d.victim) && d.type == "minecraft:in_wall") return gf::combat::Verdict::Cancel;
        if (d.type == "minecraft:fall" && d.victim.isPlayer()) {
            auto it = S().throws.find(uidOf(d.victim));
            if (it != S().throws.end() && it->second.thrown) {
                float amount = d.amount;
                gf::combat::hurt(d.victim, amount, "twilightforest:yeeted", actorById(it->second.thrower));
                return gf::combat::Verdict::Cancel;
            }
        }
        return gf::combat::Verdict::Pass;
    });
    game::db::onWorldClosed([] { S().throws.clear(); });
}

void uninstallMounts() {}

} // namespace gf::feature::entity

namespace gf::entity {

namespace fe = gf::feature::entity;

namespace mount {

bool grab(Mob& mob, Player& player) {
    if (!mob.isAlive() || !player.isAlive()) return false;
    auto& s        = fe::stateOf(mob);
    s.hostileMount = true;
    if (player.getVehicle() == &mob) {
        s.grabbed = fe::uidOf(player);
        return true;
    }
    // pluck them from the boat, minecart, donkey, or whatever
    if (player.getVehicle()) {
        fe::S().allowDismount = true;
        player.stopRiding(true, false, false, false);
        fe::S().allowDismount = false;
    }
    if (!player.startRiding(mob, true) || player.getVehicle() != &mob) return false;
    s.grabbed = fe::uidOf(player);
    fe::setRidingUnfriendly(player, fe::uidOf(mob), true);
    log().info("entity: {} grabbed {}", mob.getTypeName(), player.getRealName());
    return true;
}

// ThrowRiderGoal#stop with the caller's throw vector.
void throwRider(Mob& mob, Vec3 const& velocity) {
    Actor* rider = mob.getFirstPassenger();
    if (!rider) return;
    ext::hostileDismount(*rider);
    if (rider->isPlayer()) {
        auto& player = static_cast<Player&>(*rider);
        auto& t      = fe::S().throws[fe::uidOf(player)];
        t.thrown     = true;
        t.thrower    = fe::uidOf(mob);
        // make it so other yetis won't try to pick us up for a bit, 10 seconds seems fair
        t.vector   = velocity;
        t.cooldown = fe::kThrowCooldown;
        fe::sendThrown(player, t);
        fe::pushPlayer(player, velocity);
        log().info("entity: {} threw {}", mob.getTypeName(), player.getRealName());
    } else {
        fe::push(*rider, velocity);
    }
}

} // namespace mount

namespace ext {

bool isRidingUnfriendly(Actor const& actor) { return fe::ridingUnfriendly(actor); }

void hostileDismount(Actor& rider) {
    auto* vehicle = rider.getVehicle();
    fe::S().allowDismount = true;
    rider.stopRiding(true, false, false, false);
    fe::S().allowDismount = false;
    if (!vehicle) return;
    if (auto* s = fe::findState(fe::uidOf(*vehicle)); s && s->grabbed == fe::uidOf(rider)) s->grabbed = 0;
    if (rider.isPlayer()) fe::setRidingUnfriendly(static_cast<Player&>(rider), fe::uidOf(*vehicle), false);
}

bool thrown(Player const& player) {
    auto it = fe::S().throws.find(fe::uidOf(player));
    return it != fe::S().throws.end() && it->second.thrown;
}

int throwCooldown(Player const& player) {
    auto it = fe::S().throws.find(fe::uidOf(player));
    return it != fe::S().throws.end() ? it->second.cooldown : 0;
}

Actor* thrower(Player const& player) {
    auto it = fe::S().throws.find(fe::uidOf(player));
    return it != fe::S().throws.end() ? fe::actorById(it->second.thrower) : nullptr;
}

} // namespace ext

} // namespace gf::entity
