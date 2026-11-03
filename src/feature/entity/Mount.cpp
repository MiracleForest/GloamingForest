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
// the thrower; ThrowRiderGoal#stop sets that state for a Player rider only, any other rider is
// just pushed, so non-player riders keep plain fall damage as in TF). A mob is a hostile mount
// when its type is marked (ext::markHostileMount, Java's IHostileMount marker) or when it grabbed
// its rider (mount::grab).
//   - preventTeleportingOffHostileMounts (EntityTeleportEvent of any living rider: ender pearl,
//     chorus fruit, commands, enderman / shulker teleports): Player::teleportTo and Mob::teleportTo
//     are cancelled for a rider of a hostile mount (every cause but the engine's internal
//     "unknown" moves), Actor::tryTeleportTo (the random teleports) fails for one.
//   - preventMountDismount: Bedrock has no dismount event to cancel, so a rider that left the
//     seat is put back on the next tick, unless the mob let go (hostileDismount) or the player is
//     invulnerable (creative / spectator, as Java's `!player.getAbilities().invulnerable`).
//   - preventHostilMountCrouching: every passenger of a hostile mount stops sneaking each tick.

#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::entity {

namespace {

constexpr int kThrowCooldown = 200; ///< YetiThrowAttachment.THROW_COOLDOWN
constexpr int kRemountTries  = 20;  ///< ticks a held rider that left the seat is tried to be put back

struct ThrowState {
    bool         thrown   = false;
    std::int64_t thrower  = 0;
    int          cooldown = 0;
    Vec3         vector{};
};

struct MountState {
    std::unordered_map<std::int64_t, ThrowState> throws; ///< player -> YetiThrowAttachment
    bool                                         allowDismount = false;
    int                                          blockedTeleports = 0; ///< selftests
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
        // the throw has to lift the player first: the two pushes land on the first ticks
        bool launching = t.cooldown >= kThrowCooldown - 2;
        if (t.thrown && !launching && (player->isOnGround() || player->isInWater())) {
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

/// The passengers of `vehicle` (Bedrock keeps them in a component: the actors around it whose
/// vehicle it is).
std::vector<Actor*> passengersOf(Actor& vehicle) {
    std::vector<Actor*> out;
    if (!vehicle.hasPassenger()) return out;
    auto const& bb = vehicle.getAABB();
    AABB        box{Vec3{bb.min.x - 4.0f, bb.min.y - 4.0f, bb.min.z - 4.0f}, Vec3{bb.max.x + 4.0f, bb.max.y + 4.0f, bb.max.z + 4.0f}};
    for (auto* a : actorsIn(vehicle.getDimensionBlockSource(), static_cast<int>(vehicle.getDimensionId()), box))
        if (a && a != &vehicle && a->getVehicle() == &vehicle) out.push_back(a);
    return out;
}

bool hostileMount(Actor const& vehicle) {
    if (hostileMountType(vehicle.getTypeName())) return true;
    auto* s = findState(uidOf(vehicle));
    return s && s->hostileMount;
}

} // namespace

bool ridingUnfriendly(Actor const& actor) {
    auto* vehicle = actor.getVehicle();
    return vehicle && hostileMount(*vehicle);
}

/// The mount itself is being teleported: its passengers go with it (Java moves riders with their
/// vehicle; only the vehicle's own teleport is an event).
int gMountTeleports = 0;

/// HostileMountEvents#preventTeleportingOffHostileMounts (EntityTeleportEvent of any living rider:
/// commands, ender pearls, chorus fruit, enderman / shulker teleports) for Actor::teleportTo,
/// Player::teleportTo and Actor::tryTeleportTo.
bool blocksTeleport(Actor& actor, int cause, bool anyCause) {
    if (actor.isClientSide() || (cause == 0 && !anyCause) || S().allowDismount || gMountTeleports > 0 || !ridingUnfriendly(actor)) return false;
    if (!actor.isPlayer() && !actor.hasCategory(ActorCategory::Mob)) return false; // LivingEntity only
    ++S().blockedTeleports;
    auto name = actor.isPlayer() ? static_cast<Player&>(actor).getRealName() : actor.getTypeName();
    log().info("entity: the teleport of {} was cancelled (riding {})", name, actor.getVehicle()->getTypeName());
    return true;
}

int blockedTeleportsForTest() { return S().blockedTeleports; }

void tickMount(Mob& mob, ActorState& s) {
    // HostileMountEvents#preventHostilMountCrouching: every passenger
    auto riders = passengersOf(mob);
    for (auto* rider : riders) rider->setSneaking(false);
    // a player that got on by itself rides unfriendly too (IHostileMount): it is held from now on
    if (!s.grabbed)
        for (auto* rider : riders)
            if (rider->isPlayer()) {
                s.grabbed = uidOf(*rider);
                setRidingUnfriendly(static_cast<Player&>(*rider), uidOf(mob), true);
                break;
            }
    if (!s.grabbed) return;
    auto* player = playerById(s.grabbed);
    if (!player) {
        s.grabbed = 0;
        return;
    }
    if (player->getVehicle() == &mob) {
        s.remountTries = 0;
        return;
    }
    // the player left the seat: HostileMountEvents#preventMountDismount puts it back
    bool invulnerable = player->isCreative() || player->isSpectator();
    if (player->isAlive() && mob.isAlive() && !invulnerable && !S().allowDismount && player->getDimensionId() == mob.getDimensionId()) {
        player->startRiding(mob, true);
        if (player->getVehicle() == &mob) {
            s.remountTries = 0;
            return;
        }
        // Java cancels the dismount itself; here the engine may still be taking the rider out of
        // the seat (the passenger list is updated after the mob's tick), so startRiding can fail on
        // the first ticks after the dismount: keep holding the player and try again next tick
        if (++s.remountTries < kRemountTries) return;
        log().warn("entity: could not put {} back on {} after {} ticks: letting go", player->getRealName(), mob.getTypeName(), kRemountTries);
    }
    s.remountTries = 0;
    s.grabbed      = 0;
    setRidingUnfriendly(*player, uidOf(mob), false);
}

} // namespace gf::feature::entity

LL_TYPE_INSTANCE_HOOK(
    EntityRiderTeleportHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$teleportTo,
    void,
    Vec3 const& pos,
    bool        shouldStopRiding,
    int         cause,
    int         sourceEntityType,
    bool        keepVelocity
) {
    if (gf::feature::entity::blocksTeleport(*this, cause, false)) return;
    origin(pos, shouldStopRiding, cause, sourceEntityType, keepVelocity);
}

// the same for every other living rider (Mob#teleportTo; Player overrides it, its hook is the one above)
LL_TYPE_INSTANCE_HOOK(
    EntityActorRiderTeleportHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$teleportTo,
    void,
    Vec3 const& pos,
    bool        shouldStopRiding,
    int         cause,
    int         sourceEntityType,
    bool        keepVelocity
) {
    if (!isPlayer() && gf::feature::entity::blocksTeleport(*this, cause, false)) return;
    origin(pos, shouldStopRiding, cause, sourceEntityType, keepVelocity);
}

// the random teleports (chorus fruit, endermen, shulkers: EntityTeleportEvent.ChorusFruit /
// EnderEntity) of a rider of a hostile mount fail
LL_TYPE_INSTANCE_HOOK(
    EntityRiderTryTeleportHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::tryTeleportTo,
    bool,
    Vec3 const& pos,
    bool        landOnBlock,
    bool        avoidLiquid,
    int         cause,
    int         entityType
) {
    if (gf::feature::entity::blocksTeleport(*this, cause, true)) return false;
    return origin(pos, landOnBlock, avoidLiquid, cause, entityType);
}

LL_TYPE_INSTANCE_HOOK(
    EntityMountTeleportHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::teleportPassengersTo,
    void,
    Vec3 const& pos,
    int         cause,
    int         entityType
) {
    ++gf::feature::entity::gMountTeleports;
    origin(pos, cause, entityType);
    --gf::feature::entity::gMountTeleports;
}

namespace gf::feature::entity {

void installMounts() {
    EntityRiderTeleportHook::hook();
    EntityActorRiderTeleportHook::hook();
    EntityRiderTryTeleportHook::hook();
    EntityMountTeleportHook::hook();
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

void uninstallMounts() {
    EntityMountTeleportHook::unhook();
    EntityRiderTryTeleportHook::unhook();
    EntityActorRiderTeleportHook::unhook();
    EntityRiderTeleportHook::unhook();
}

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
    s.grabbed      = fe::uidOf(player);
    s.remountTries = 0;
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
