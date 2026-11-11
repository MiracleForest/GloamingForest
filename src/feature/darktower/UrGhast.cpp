// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/UrGhast.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastFlightGoal.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastAttackGoal.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastLookGoal.java
//   src/main/java/twilightforest/entity/projectile/UrGhastFireball.java
//   src/main/java/twilightforest/entity/monster/CarminiteGhastguard.java
//   src/main/java/twilightforest/entity/monster/CarminiteGhastling.java
//   src/main/java/twilightforest/loot/conditions/IsMinionCondition.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Bedrock: the Ur-Ghast is a native flyer (F8's NoClip move control and wanted position until it
// reaches its first flight point, then Java's noPhysics is cleared by the BP event
// "solid") with its goals translated below. The carminite ghastguard and ghastling run
// TF's goals natively too (GhastguardHomedFlightGoal, GhastguardRandomFlyGoal, Ghast.GhastLookGoal,
// GhastguardAttackGoal, NearestAttackableTargetGoal) with vanilla's GhastMoveControl, as their BP
// has no navigation or vanilla AI. The Ur-Ghast's fireballs are vanilla minecraft:fireball actors
// whose impact this unit runs one step ahead (UrGhastFireball#onHit: 16 fireball damage to the
// entity hit, then an explosion of power 1 that never breaks blocks).

#include "feature/darktower/UrGhast.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <unordered_map>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/darktower/Devices.h"
#include "feature/darktower/Util.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/loot/Loot.h"
#include "feature/darktower/Runtime.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::darktower::ghasts {

namespace dt = core::darktower;
namespace ge = gf::entity;

namespace {

constexpr int  kHoverAltitude = 20;
constexpr int  kHomeRadius    = 64;
constexpr int  kDeathTicks    = 90;
constexpr char kData[]        = "darktower:urghast";

struct UrState {
    dt::UrGhastPhase      phase;
    int                   inTrapCounter  = 0;
    int                   nextTantrumCry = 0;
    int                   tickCount      = 0;
    std::vector<BlockPos> traps;
    // flight goal
    std::vector<BlockPos> points;
    int                   currentPoint = 0;
    // attack goal
    int  attackTimer = 0, prevAttackTimer = 0;
    bool attackRunning = false;
    bool charging      = false;
    bool solid         = false; ///< Java's noPhysics cleared
    bool syncedTantrum = false; ///< the tantrum flag the companions were told last
    std::int64_t lastHurtTick = -100;
    int          unseenTicks  = 0; ///< TargetGoal#unseenTicks
};

/// DATA_TANTRUM for the companions' client effects (UrGhast#aiStep's dust and boss tears,
/// TFWeatherRenderer.urGhastAlive): "darktower.urghast" {uid, on}, a heartbeat every 10 ticks for each
/// Ur-Ghast to the companions tracking it (one that comes near later learns it; the client forgets an
/// Ur-Ghast not repeated for 40 ticks), and at once when the flag changes. Java tracks the Ur-Ghast
/// for TFEntities#make's 80 chunks, capped by the view distance (ChunkMap.TrackedEntity
/// #getEffectiveRange): here the companion's own chunk radius, capped at 80 chunks.
constexpr char kTantrumChannel[] = "darktower.urghast";
constexpr double kTantrumTrackingRange = 80.0 * 16.0;

/// U15 (atmosphere) draws the Twilight Forest weather (F12's sky provider) and raises its Ur-Ghast
/// rain splashes from "atmosphere.ur_ghast" {"active": bool}, which it wants on change (its unit
/// header). Per companion: the Ur-Ghasts in a tantrum it tracks; true when the first one starts, false
/// when the last one ends, dies or goes out of its view. Strings only (rule R2); a client without U15
/// ignores the channel.
constexpr char kAtmosphereChannel[] = "atmosphere.ur_ghast";

std::unordered_map<std::int64_t, std::vector<std::int64_t>>& atmosphereActive() {
    static std::unordered_map<std::int64_t, std::vector<std::int64_t>> m; // player uid -> Ur-Ghast uids
    return m;
}

void tellAtmosphere(std::int64_t playerUid, bool active) {
    auto* a = actorById(playerUid);
    if (a && a->isPlayer()) net::send(static_cast<Player&>(*a), kAtmosphereChannel, Json{{"active", active}});
}

/// seen: the companions that track this Ur-Ghast now (with its flag on).
void syncAtmosphere(std::int64_t ghast, bool on, std::vector<std::int64_t> const& seen) {
    auto& all = atmosphereActive();
    for (auto it = all.begin(); it != all.end();) {
        auto& ghasts = it->second;
        bool  had    = std::find(ghasts.begin(), ghasts.end(), ghast) != ghasts.end();
        bool  want   = on && std::find(seen.begin(), seen.end(), it->first) != seen.end();
        if (had && !want) {
            std::erase(ghasts, ghast);
            if (ghasts.empty()) {
                tellAtmosphere(it->first, false);
                it = all.erase(it);
                continue;
            }
        }
        ++it;
    }
    if (!on) return;
    for (auto player : seen) {
        auto& ghasts = all[player];
        if (std::find(ghasts.begin(), ghasts.end(), ghast) != ghasts.end()) continue;
        ghasts.push_back(ghast);
        if (ghasts.size() == 1) tellAtmosphere(player, true);
    }
}

void sendTantrum(Actor& self, bool on) {
    auto& region = self.getDimensionBlockSource();
    Json  msg{{"uid", uidOf(self)}, {"on", on}};
    auto  at = self.getFeetPos();
    std::vector<std::int64_t> seen;
    for (auto* p : playersNear(dimOf(region), at, kTantrumTrackingRange, true)) {
        if (!net::isCompanion(*p)) continue;
        double range = std::min(kTantrumTrackingRange, std::max(1.0, double(static_cast<unsigned>(p->mChunkRadius))) * 16.0);
        auto   pp    = p->getFeetPos();
        double dx = pp.x - at.x, dz = pp.z - at.z;
        if (std::max(std::abs(dx), std::abs(dz)) > range) continue; // the chunk square the player sees
        net::send(*p, kTantrumChannel, msg);
        seen.push_back(uidOf(*p));
    }
    syncAtmosphere(uidOf(self), on, seen);
}

std::unordered_map<std::int64_t, UrState>& states() {
    static std::unordered_map<std::int64_t, UrState> m;
    return m;
}

UrState& stateOf(Actor& a) { return states()[uidOf(a)]; }

void syncVisuals(Mob& m, UrState const& s) {
    ge::setProperty(m, "twilightforest:tantrum", s.phase.tantrum);
    ge::setProperty(m, "twilightforest:attack_timer", std::clamp(s.attackTimer, -40, 20));
    ge::setProperty(m, "twilightforest:charging", s.charging);
    bool target = m.mTargetId->rawID != -1 && actorById(m.mTargetId->rawID) != nullptr;
    ge::setProperty(m, "twilightforest:attack_status", target && !s.phase.tantrum ? 1 : 0);
}

Actor* targetOf(Mob& m) {
    auto* t = actorById(m.mTargetId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

bool targetable(Actor& a) {
    if (!a.isPlayer() || !a.isAlive() || a.isSpectator()) return false;
    auto mode = static_cast<Player&>(a).getPlayerGameType();
    return mode != GameType::Creative && mode != GameType::Spectator;
}

double distSqr(Actor const& a, Actor const& b) {
    auto p = a.getFeetPos(), q = b.getFeetPos();
    double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
    return dx * dx + dy * dy + dz * dz;
}

bool lineOfSight(Mob& m, Actor& t) { return m.canSee(t.getEyePos(), ShapeType::Collision); }

/// The scan point: the home position when it has one, else its position.
BlockPos scanPoint(Mob& m) {
    if (auto h = ge::home(m)) return BlockPos{h->pos.x, h->pos.y, h->pos.z};
    auto p = m.getFeetPos();
    return BlockPos{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
}

std::vector<BlockPos> scanForTraps(Mob& m) {
    auto& region = m.getDimensionBlockSource();
    auto  c      = scanPoint(m);
    auto  found  = devices::ghastTraps(dimOf(region), c, kHomeRadius);
    // (a trap in a chunk that is not loaded stays a candidate: Java loads the chunk to ask)
    std::erase_if(found, [&](BlockPos const& p) { return loaded(region, p) && !region.canSeeSky(BlockPos{p.x, p.y + 1, p.z}); });
    std::sort(found.begin(), found.end(), [&](BlockPos const& a, BlockPos const& b) {
        auto d = [&](BlockPos const& p) {
            double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
            return dx * dx + dy * dy + dz * dz;
        };
        return d(a) < d(b);
    });
    return found;
}

std::vector<BlockPos> createPath(Mob& m, UrState& s) {
    std::vector<BlockPos> pts;
    BlockPos              pos = scanPoint(m);
    if (!s.traps.empty()) pts = s.traps;
    else {
        pts.push_back(BlockPos{pos.x + 20, pos.y, pos.z});
        pts.push_back(BlockPos{pos.x, pos.y, pos.z - 20});
        pts.push_back(BlockPos{pos.x - 20, pos.y, pos.z});
        pts.push_back(BlockPos{pos.x, pos.y, pos.z + 20});
    }
    auto& r = random(); // Collections.shuffle with the level random
    for (int i = static_cast<int>(pts.size()) - 1; i > 0; --i) std::swap(pts[i], pts[r.nextInt(i + 1)]);
    if (s.traps.empty()) pts.push_back(pos);
    return pts;
}

/// UrGhast#checkGhastsAtTraps: at least 4 ghastlings near at least one trap.
bool checkGhastsAtTraps(Mob& m, UrState& s) {
    auto& region = m.getDimensionBlockSource();
    for (auto const& t : s.traps) {
        AABB box{Vec3{t.x + 0.5f - 8.0f, t.y + 0.5f - 16.0f, t.z + 0.5f - 8.0f}, Vec3{t.x + 1.5f + 8.0f, t.y + 1.5f + 16.0f, t.z + 1.5f + 8.0f}};
        int  n = 0;
        for (auto* a : region.getEntities(box, 0.0f))
            if (a && typeOf(*a) == id::Ghastling) ++n;
        if (n >= 4) return true;
    }
    return false;
}

/// Java's noPhysics: false = the BP's "noclip" group (no block collision), true = "solid".
void setSolid(Mob& m, UrState& s, bool solid) {
    if (s.solid == solid) return;
    s.solid = solid;
    VariantParameterList params{};
    params.mSelf = &m;
    gf::game::compat::executeEvent(m, solid ? "solid" : "noclip", params);
    ge::data(m, "darktower:solid") = solid;
}

void flightGoal(Mob& m, UrState& s) {
    // canUse (Ghast.RandomFloatAroundGoal copy): the path is rebuilt every time it is asked; no
    // trap check, as UrGhastFlightGoal (the trap's pull and the move control add up)
    s.points = createPath(m, s);
    if (s.points.empty()) return;
    auto wanted = ge::ext::wantedPosition(m);
    if (wanted) {
        auto   p  = m.getFeetPos();
        double dx = wanted->x - p.x, dy = wanted->y - p.y, dz = wanted->z - p.z, d = dx * dx + dy * dy + dz * dz;
        if (!(d < 1.0 || d > 3600.0)) return;
    }
    // start
    if (s.currentPoint >= static_cast<int>(s.points.size())) {
        s.currentPoint = 0;
        if (!checkGhastsAtTraps(m, s)) spawnGhastsAtTraps(m);
    }
    auto const& pt = s.points[s.currentPoint];
    double      y  = pt.y + (ge::home(m) ? kHoverAltitude : 0);
    ge::ext::setWantedPosition(m, Vec3{float(pt.x), float(y), float(pt.z)}, 1.0);
    s.currentPoint++;
    // we have reached cruising altitude, time to turn noClip off (every start, as Java)
    setSolid(m, s, true);
}

void lookGoal(Mob& m) {
    float yaw;
    if (auto* t = targetOf(m)) {
        if (distSqr(*t, m) >= 4096.0) return;
        auto tp = t->getFeetPos(), p = m.getFeetPos();
        yaw     = -static_cast<float>(std::atan2(tp.x - p.x, tp.z - p.z)) * (180.0f / std::numbers::pi_v<float>);
    } else {
        auto v = m.getPosDelta();
        yaw    = -static_cast<float>(std::atan2(v.x, v.z)) * (180.0f / std::numbers::pi_v<float>);
    }
    const_cast<Vec2&>(m.getRotation()) = Vec2{m.getRotation().x, yaw};
    m.setYBodyRotation(yaw);
}

float voicePitch() { return (random().nextFloat() - random().nextFloat()) * 0.2f + 0.5f; }

/// LookControl#tick's pitch towards the target's eyes (rotateTowards by at most `maxStep`).
void lookAtPitch(Mob& m, Actor& t, float maxStep) {
    auto   e  = m.getEyePos();
    auto   te = t.getEyePos();
    double dx = te.x - e.x, dy = te.y - e.y, dz = te.z - e.z;
    double h  = std::sqrt(dx * dx + dz * dz);
    if (std::abs(dy) <= 1e-5 && h <= 1e-5) return;
    float  want = static_cast<float>(-(std::atan2(dy, h) * 180.0 / std::numbers::pi));
    auto   rot  = m.getRotation();
    float  diff = want - rot.x;
    while (diff < -180.0f) diff += 360.0f;
    while (diff >= 180.0f) diff -= 360.0f;
    diff = std::clamp(diff, -maxStep, maxStep);
    const_cast<Vec2&>(m.getRotation()) = Vec2{rot.x + diff, rot.y};
}

/// The Ur-Ghast fireballs in flight (by unique id): the shooter, and Projectile#leftOwner.
/// AbstractHurtingProjectile: assignDirectionalMovement(movement, accelerationPower = 0.1) launches
/// at 0.1 blocks a tick; Bedrock's minecraft:fireball keeps its speed (inertia 1, no acceleration),
/// so the flight is driven here every tick.
constexpr float kLaunchSpeed = 0.1f;

struct UrFireball {
    std::int64_t shooter = 0;
    bool         leftOwner = false;
    Vec3         velocity{};       ///< the last tick's motion (Bedrock moves projectiles without a pos delta)
    Vec3         last{};
    bool         hasLast = false;
    /// a carminite ghastguard's vanilla LargeFireball: only its player-reflected hit on a darktower
    /// ghast is taken over (Ghast#hurt), everything else is the engine's own impact
    bool vanilla = false;
    int  age     = 0; ///< ticks in flight (ProjectileUtil#computeMargin)
    int  dim     = 0; ///< the dimension (an unloaded fireball's chunk is looked up there)
    int  absent  = 0; ///< ticks its actor has been missing
    int  missing = 0; ///< of those, ticks in a row with its chunk loaded
    /// AbstractHurtingProjectile's flight: the speed of the next move (0.1 at launch, then
    /// (speed + accelerationPower 0.1) * inertia 0.95 per tick, terminal 1.9; 1 after a player's
    /// AIM_DEFLECT) along dir, the direction last set (a turn the engine made is a deflection)
    float speed = kLaunchSpeed;
    Vec3  dir{};
    std::int64_t lastReflector = -1; ///< ProjectileComponent::mLastReflectActor last tick
    int          lastImmunity  = 0;  ///< ProjectileComponent::mReflectImmunityTicks last tick
};

constexpr char kFireballsKey[] = "gf:darktower:fireballs";
constexpr int  kMaxAbsentTicks = 20 * 60 * 60; ///< an unloaded fireball is forgotten after an hour
std::unordered_map<std::int64_t, UrFireball>& fireballs() {
    static std::unordered_map<std::int64_t, UrFireball> m;
    return m;
}

bool boxesIntersect(AABB const& a, AABB const& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z && a.max.z > b.min.z;
}

/// Projectile#getOwner of a fireball now: a player's hit re-aims it and makes the player its owner
/// (Bedrock's ProjectileComponent reflection sets the owner, or at least records the reflecting
/// actor); before that, the shooter.
Actor* currentOwner(Actor& fb, std::int64_t shooter) {
    std::int64_t ownerId = fb.getOwnerId().rawID;
    if (auto pc = fb.getEntityContext().tryGetComponent<ProjectileComponent>()) {
        std::int64_t reflector = pc->mLastReflectActor->rawID;
        if ((ownerId == shooter || ownerId == -1 || ownerId == 0) && reflector != -1 && reflector != 0 && reflector != shooter) ownerId = reflector;
    }
    if (ownerId == -1 || ownerId == 0) ownerId = shooter;
    return actorById(ownerId);
}

/// UrGhastFireball: a vanilla large fireball (Bedrock's minecraft:fireball) with power 1.
Actor* fireball(Mob& m, Vec3 const& at, Vec3 const& dir) {
    auto* l = level();
    if (!l) return nullptr;
    double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-9) return nullptr;
    // UrGhastFireball#shoot: normalise, gaussian 0.0075 * dist... applied by the projectile's own
    // uncertainty below
    auto owned = l->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:fireball"), &m, at, Vec2{0.0f, 0.0f});
    if (!owned) return nullptr;
    auto* fb = l->addEntity(m.getDimensionBlockSource(), std::move(owned));
    if (!fb) return nullptr;
    if (auto pc = fb->getEntityContext().tryGetComponent<ProjectileComponent>())
        pc->shoot(*fb, Vec3{float(dir.x / len), float(dir.y / len), float(dir.z / len)}, kLaunchSpeed, 0.0f, Vec3{0, 0, 0}, nullptr);
    auto& st = fireballs()[uidOf(*fb)] = UrFireball{uidOf(m), false, Vec3{float(dir.x / len), float(dir.y / len), float(dir.z / len)}, at, true};
    st.dim = dimOf(m.getDimensionBlockSource());
    st.dir = st.velocity;
    st.velocity = Vec3{st.dir.x * kLaunchSpeed, st.dir.y * kLaunchSpeed, st.dir.z * kLaunchSpeed};
    return fb;
}

/// A vanilla large fireball (CarminiteGhastguard / GhastguardAttackGoal: LargeFireball, power 1).
Actor* largeFireball(Mob& m, Vec3 const& at, Vec3 const& dir) {
    auto* l = level();
    if (!l) return nullptr;
    double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-9) return nullptr;
    auto owned = l->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:fireball"), &m, at, Vec2{0.0f, 0.0f});
    if (!owned) return nullptr;
    auto* fb = l->addEntity(m.getDimensionBlockSource(), std::move(owned));
    if (!fb) return nullptr;
    if (auto pc = fb->getEntityContext().tryGetComponent<ProjectileComponent>())
        pc->shoot(*fb, Vec3{float(dir.x / len), float(dir.y / len), float(dir.z / len)}, kLaunchSpeed, 0.0f, Vec3{0, 0, 0}, nullptr);
    UrFireball state{uidOf(m), false, Vec3{float(dir.x / len), float(dir.y / len), float(dir.z / len)}, at, true};
    state.vanilla       = true;
    state.dim           = dimOf(m.getDimensionBlockSource());
    state.dir           = state.velocity;
    state.velocity      = Vec3{state.dir.x * kLaunchSpeed, state.dir.y * kLaunchSpeed, state.dir.z * kLaunchSpeed};
    fireballs()[uidOf(*fb)] = state;
    return fb;
}

/// AbstractHurtingProjectile#tick's hit test (ProjectileUtil#getHitResultOnMoveVector), run one step
/// before the engine's own impact (which would break blocks): the ray from the fireball's position
/// along its motion stops at the first block whose shape blocks motion (ClipContext.Block.COLLIDER);
/// an entity is hit when that ray enters its box inflated by computeMargin (min(0.3, (age - 2) / 20)).
/// The hit runs UrGhastFireball#onHit at the fireball's position before the move (Java explodes at
/// this.getX/Y/Z): an entity in the way takes 16 fireball damage (another fireball is passed through,
/// as onHitEntity ignores AbstractHurtingProjectile), then the fireball explodes with power 1, fire
/// when mob griefing allows it, no block damage (Level.ExplosionInteraction.NONE) and no source.
/// The ray is also followed half a tick further (the engine's own box may touch first): a hit there
/// explodes where Java's next tick would, at the end of this tick's motion.
///
/// A ghastguard's vanilla LargeFireball (state.vanilla) is only taken over when a player sent it back
/// and it hits a darktower ghast (on a carminite ghast Ghast#hurt makes it 1000; Bedrock's
/// minecraft:fire_immune would refuse its fire impact): LargeFireball#onHitEntity's 6 damage, then
/// its explosion (power 1, fire and block damage when mob griefing allows it,
/// ExplosionInteraction.MOB).
/// AbstractHurtingProjectile#tick's motion update: the fireball moves by its delta, then the delta
/// becomes (delta + normalize(delta) * 0.1) * inertia (0.95, 0.8 in water). Sets this tick's delta
/// along the current direction; a direction the engine changed (Bedrock's reflect_on_hurt, Java's
/// ProjectileDeflection.AIM_DEFLECT to the player's look) restarts at speed 1 (onDeflection: the
/// acceleration power back to 0.1). Returns the delta.
Vec3 flightStep(Actor& fb, UrFireball& state, Vec3 const& engine) {
    double el = std::sqrt(engine.x * engine.x + engine.y * engine.y + engine.z * engine.z);
    Vec3   dir = state.dir;
    double dl  = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (el > 1e-6) {
        Vec3 now{float(engine.x / el), float(engine.y / el), float(engine.z / el)};
        // a turn (dot < 0.98), or the engine's reflect even along the old path: a new reflecting actor
        // or a fresh reflect immunity (ProjectileComponent#_tryReflectOnHurt)
        bool turned = dl > 1e-6 && (now.x * dir.x + now.y * dir.y + now.z * dir.z) / dl < 0.98;
        if (auto pc = fb.getEntityContext().tryGetComponent<ProjectileComponent>()) {
            std::int64_t reflector = pc->mLastReflectActor->rawID;
            int          immunity  = pc->mReflectImmunityTicks;
            if ((reflector != state.lastReflector && reflector != -1 && reflector != 0) || immunity > state.lastImmunity) turned = true;
            state.lastReflector = reflector;
            state.lastImmunity  = immunity;
        }
        dir         = now;
        if (turned) state.speed = 1.0f;
        else state.speed = (state.speed + 0.1f) * (fb.isInWater() ? 0.8f : 0.95f);
    } else if (dl > 1e-6) {
        dir         = Vec3{float(dir.x / dl), float(dir.y / dl), float(dir.z / dl)};
        state.speed = (state.speed + 0.1f) * (fb.isInWater() ? 0.8f : 0.95f);
    } else {
        return engine;
    }
    state.dir = dir;
    Vec3 v{dir.x * state.speed, dir.y * state.speed, dir.z * state.speed};
    setMotion(fb, v);
    return v;
}

void urFireballTick(Actor& fb, UrFireball& state) {
    if (!fb.isAlive()) return;
    auto& region = fb.getDimensionBlockSource();
    Vec3  p      = fb.getFeetPos();
    // the motion: the engine's delta when it has one, else what the fireball moved since last tick
    Vec3 v = fb.getPosDelta();
    if (state.hasLast && v.x == 0.0f && v.y == 0.0f && v.z == 0.0f) {
        Vec3 moved{p.x - state.last.x, p.y - state.last.y, p.z - state.last.z};
        if (moved.x != 0.0f || moved.y != 0.0f || moved.z != 0.0f) state.velocity = moved;
        v = state.velocity;
    }
    // Java's accelerating flight (the engine's own fireball keeps a constant speed)
    v              = flightStep(fb, state, v);
    state.velocity = v;
    state.last    = p;
    state.hasLast = true;
    int age       = state.age++;
    auto  b       = fb.getAABB();
    Actor* owner  = currentOwner(fb, state.shooter);
    bool   byPlayer = owner && owner->isPlayer();
    if (state.vanilla && !byPlayer) return; // the engine's own LargeFireball
    // Projectile#checkLeftOwner: once the fireball's box (inflated 1) no longer touches its owner,
    // the owner can be hit too
    if (!state.leftOwner) {
        AABB grown{Vec3{b.min.x - 1, b.min.y - 1, b.min.z - 1}, Vec3{b.max.x + 1, b.max.y + 1, b.max.z + 1}};
        state.leftOwner = !owner || !boxesIntersect(grown, owner->getAABB());
    }
    float  margin = std::clamp((age - 2) / 20.0f, 0.0f, 0.3f);
    double len    = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    int    steps  = std::max(2, static_cast<int>(std::ceil(len * 1.5 / 0.125)));
    // the entities the ray can reach: their chunks (getEntities finds an entity by the chunk of its
    // position: the 14-wide Ur-Ghast reaches 7 blocks past its chunk), then the boxes themselves
    Vec3 far{float(p.x + v.x * 1.5), float(p.y + v.y * 1.5), float(p.z + v.z * 1.5)};
    AABB near{Vec3{std::min(p.x, far.x) - 9, std::min(p.y, far.y) - 19, std::min(p.z, far.z) - 9},
              Vec3{std::max(p.x, far.x) + 9, std::max(p.y, far.y) + 2, std::max(p.z, far.z) + 9}};
    std::vector<Actor*> candidates;
    for (auto* a : region.getEntities(near, 0.0f)) {
        if (!a || a == &fb || !a->isAlive() || a->isSpectator() || gf::projectile::ext::isProjectile(*a)) continue;
        // Projectile#canHitEntity: the owner only after the fireball left it
        if (a == owner && !state.leftOwner) continue;
        // AbstractHurtingProjectile#canHitEntity: never an entity with noPhysics (the Ur-Ghast
        // before its first flight point or in a trap)
        if (isUrGhast(*a) && !stateOf(*a).solid) continue;
        auto t = typeOf(*a);
        if (t == "minecraft:fireball" || t == "minecraft:small_fireball" || t == "minecraft:item" || t == "minecraft:xp_orb") continue;
        // only the reflected hit on a darktower ghast is ours (Bedrock's own ghast keeps its own)
        if (state.vanilla && (!isTrappable(*a) || t == "minecraft:ghast")) continue;
        candidates.push_back(a);
    }
    if (state.vanilla && candidates.empty()) return;
    auto inside = [&](AABB const& box, Vec3 const& q) {
        return q.x >= box.min.x - margin && q.x <= box.max.x + margin && q.y >= box.min.y - margin && q.y <= box.max.y + margin &&
               q.z >= box.min.z - margin && q.z <= box.max.z + margin;
    };
    for (int i = 0; i <= steps; i++) {
        double t = 1.5 * i / steps;
        Vec3   q{float(p.x + v.x * t), float(p.y + v.y * t), float(p.z + v.z * t)};
        // the block clip first (Java ends the entity search at the block hit)
        bool blockHit = blocksMotion(region, BlockPos{int(std::floor(q.x)), int(std::floor(q.y)), int(std::floor(q.z))});
        if (blockHit && state.vanilla) return; // the engine's own block impact
        Actor* hit = nullptr;
        if (!blockHit) {
            for (auto* a : candidates)
                if (inside(a->getAABB(), q)) {
                    hit = a;
                    break;
                }
        }
        if (!hit && !blockHit) continue;
        // this tick's motion: Java's explosion point is the position before the move; past it, the
        // position after it (where Java's next tick finds the hit)
        Vec3 at = t <= 1.0 ? p : Vec3{float(p.x + v.x), float(p.y + v.y), float(p.z + v.z)};
        if (state.vanilla) {
            // LargeFireball#onHitEntity: 6 damage (a carminite ghast's Ghast#hurt turns it into 1000)
            combat::hurt(*hit, 6.0f, "minecraft:mob_projectile", owner, &fb);
            if (fb.isAlive()) {
                region.getLevel().explode(region, nullptr, at, 1.0f, mobGriefing(), mobGriefing(), 3.40282347e38f, false);
                fb.remove();
            }
            return;
        }
        // the fire-immune ghasts (Bedrock's minecraft:fire_immune refuses every fire cause, Java's
        // Ghast / UrGhast let a player-reflected fireball through): the hit as a projectile
        bool reflectedOnGhast = hit && byPlayer && isTrappable(*hit);
        if (hit) combat::hurt(*hit, 16.0f, reflectedOnGhast ? "minecraft:mob_projectile" : "minecraft:fireball", owner, &fb);
        if (fb.isAlive()) {
            region.getLevel().explode(region, nullptr, at, 1.0f, mobGriefing(), false, 3.40282347e38f, false);
            fb.remove();
        }
        return;
    }
}

/// Runs every Ur-Ghast fireball one step ahead of the engine (runtime's per-tick hook: the vanilla
/// minecraft:fireball id keeps its own behaviour slot free for others).
void tickFireballs() {
    auto& all = fireballs();
    if (all.empty()) return;
    std::vector<std::int64_t> ids;
    ids.reserve(all.size());
    for (auto const& [id, f] : all) ids.push_back(id);
    for (auto id : ids) {
        auto* fb = actorById(id);
        auto  it = all.find(id);
        if (it == all.end()) continue;
        if (!fb) {
            // gone with its chunk (it comes back when the chunk loads): kept; gone from a loaded chunk: removed
            auto* region = game::regionOf(it->second.dim);
            auto  at     = it->second.last;
            // (a chunk that just loaded may add its actors a few ticks later: 20 ticks of grace)
            ++it->second.absent;
            bool chunkLoaded = region && loaded(*region, BlockPos{int(std::floor(at.x)), int(std::floor(at.y)), int(std::floor(at.z))});
            it->second.missing = chunkLoaded ? it->second.missing + 1 : 0;
            if (it->second.missing > 20 || it->second.absent > kMaxAbsentTicks)
                all.erase(it);
            continue;
        }
        if (!fb->isAlive()) {
            all.erase(it);
            continue;
        }
        it->second.absent  = 0;
        it->second.missing = 0;
        urFireballTick(*fb, it->second);
        if (!fb->isAlive()) all.erase(id);
    }
}

/// The fireballs in flight are saved with the world (their actors keep their unique ids), so one that
/// crosses a save, a reload or a chunk unload still has UrGhastFireball#onHit.
void saveFireballs() {
    Json arr = Json::array();
    for (auto const& [id, f] : fireballs())
        arr.push_back({{"id", id}, {"shooter", f.shooter}, {"left", f.leftOwner}, {"v", {f.velocity.x, f.velocity.y, f.velocity.z}},
                       {"last", {f.last.x, f.last.y, f.last.z}}, {"hasLast", f.hasLast}, {"vanilla", f.vanilla}, {"age", f.age}, {"dim", f.dim},
                       {"speed", f.speed}, {"dir", {f.dir.x, f.dir.y, f.dir.z}}});
    if (arr.empty()) game::db::erase(kFireballsKey);
    else game::db::put(kFireballsKey, arr);
}

void loadFireballs() {
    fireballs().clear();
    auto j = game::db::get(kFireballsKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j) {
        try {
            UrFireball f;
            f.shooter   = e.at("shooter").get<std::int64_t>();
            f.leftOwner = e.at("left").get<bool>();
            auto v = e.at("v"), last = e.at("last");
            f.velocity  = Vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
            f.last      = Vec3{last[0].get<float>(), last[1].get<float>(), last[2].get<float>()};
            f.hasLast   = e.at("hasLast").get<bool>();
            f.vanilla   = e.at("vanilla").get<bool>();
            f.age       = e.at("age").get<int>();
            f.dim       = e.at("dim").get<int>();
            f.speed     = e.value("speed", 1.9f);
            if (auto d = e.find("dir"); d != e.end() && d->is_array() && d->size() == 3)
                f.dir = Vec3{(*d)[0].get<float>(), (*d)[1].get<float>(), (*d)[2].get<float>()};
            fireballs()[e.at("id").get<std::int64_t>()] = f;
        } catch (...) {}
    }
}
void spitFireball(Mob& m) {
    auto* t = targetOf(m);
    if (!t) return;
    auto  p  = m.getFeetPos();
    auto  tp = t->getFeetPos();
    auto  tb = t->getAABB();
    auto  mb = m.getAABB();
    float h  = mb.max.y - mb.min.y;
    double ox = tp.x - p.x;
    double oy = tb.min.y + (tb.max.y - tb.min.y) / 2.0 - (p.y + h / 2.0);
    double oz = tp.z - p.z;
    Vec3   look = m.getViewVector(1.0f);
    double dist = 8.5;
    fireball(m, Vec3{float(p.x + look.x * dist), float(p.y + h / 2.0 + look.y * dist + 2.0), float(p.z + look.z * dist)}, Vec3{float(ox), float(oy), float(oz)});
    auto& r = random();
    for (int i = 0; i < 2; i++) {
        double dx = ox + (r.nextFloat() - r.nextFloat()) * 8;
        double dz = oz + (r.nextFloat() - r.nextFloat()) * 8;
        fireball(m, Vec3{float(p.x + look.x * dist), float(p.y + h / 2.0 + look.y * dist), float(p.z + look.z * dist)}, Vec3{float(dx), float(oy), float(dz)});
    }
}

void attackGoal(Mob& m, UrState& s) {
    auto* t       = targetOf(m);
    bool  canUse  = t && !s.phase.tantrum;
    if (!canUse) {
        if (s.attackRunning) {
            s.attackRunning = false;
            s.charging      = false; // stop
        }
        return;
    }
    if (!s.attackRunning) {
        s.attackRunning = true;
        s.attackTimer = s.prevAttackTimer = 0; // start
    }
    auto& region = m.getDimensionBlockSource();
    if (distSqr(*t, m) < 4096.0 && lineOfSight(m, *t)) {
        s.prevAttackTimer = s.attackTimer;
        ++s.attackTimer;
        lookAtPitch(m, *t, 500.0f); // setLookAt(target, 10, getMaxHeadXRot()): UrGhast returns 500
        if (s.attackTimer == 10) soundAt(region, m.getFeetPos(), "entity.twilightforest.ur_ghast.warn", 10.0f, voicePitch());
        if (s.attackTimer == 20) {
            if (!s.phase.tantrum) {
                soundAt(region, m.getFeetPos(), "entity.twilightforest.ur_ghast.shoot", 10.0f, voicePitch());
                spitFireball(m);
                s.prevAttackTimer = s.attackTimer;
            }
            s.attackTimer = -40;
        }
    } else if (s.attackTimer > 0) {
        s.prevAttackTimer = s.attackTimer;
        --s.attackTimer;
    }
    s.charging = s.attackTimer > 10;
}

/// NearestAttackableTargetGoal<Player>#findTarget: for a Player target type Java asks
/// Level#getNearestPlayer(targetConditions, mob, x, eyeY, z): the nearest player passing the
/// conditions (range = the follow range around (x, eyeY, z), mustSee) from every player of the level;
/// the inflated box (followRange, 4, followRange) is used only for non-player target types.
Player* nearestTargetablePlayer(Mob& m, double range) {
    auto& region = m.getDimensionBlockSource();
    auto  feet   = m.getFeetPos();
    Vec3  c{feet.x, m.getEyePos().y, feet.z};
    for (auto* p : playersNear(dimOf(region), c, range)) // nearest first
        if (targetable(*p) && lineOfSight(m, *p)) return p;
    return nullptr;
}

/// TargetGoal#canContinueToUse (mustSee): the target is dropped when it leaves the follow range, can
/// no longer be attacked, or was out of sight for more than reducedTickDelay(60) = 30 full goal ticks
/// (60 game ticks, unseenMemoryTicks). Run on the goal selector's full ticks.
bool keepTarget(Mob& m, Actor& t, double range, int& unseenTicks) {
    if (!targetable(t) || distSqr(t, m) > range * range) return false;
    if (lineOfSight(m, t)) unseenTicks = 0;
    else if (++unseenTicks > 30) return false;
    return true;
}

/// NearestAttackableTargetGoal<Player>(mustSee): on each full goal tick 1 in reducedTickDelay(10) = 5.
void targetGoal(Mob& m, UrState& s) {
    if (auto* t = targetOf(m)) {
        if (!keepTarget(m, *t, 128.0, s.unseenTicks)) m.setTarget(nullptr);
        return;
    }
    if (s.phase.tantrum || s.inTrapCounter > 0) return;
    if (random().nextInt(5) != 0) return;
    if (auto* p = nearestTargetablePlayer(m, 128.0)) {
        m.setTarget(p);
        s.unseenTicks = 0; // TargetGoal#start
    }
}

void doTantrumDamageEffects(Mob& m) {
    auto& region = m.getDimensionBlockSource();
    auto  b      = m.getAABB();
    AABB  below{Vec3{b.min.x, b.min.y - 32.0f, b.min.z}, Vec3{b.max.x, b.max.y, b.max.z}}; // move(0, -16, 0).inflate(0, 16, 0)
    for (auto* a : actorsIn(region, below)) {
        if (a->isPlayer()) {
            auto fp = a->getFeetPos();
            if (region.canSeeSkyFromBelowWater(BlockPos{int(std::floor(fp.x)), int(std::floor(fp.y)), int(std::floor(fp.z))}))
                combat::hurt(*a, 3.0f, "twilightforest:ghast_tear", &m, &m);
        } else if (typeOf(*a) == id::Ghastling) {
            push(*a, Vec3{0.0f, 1.0f, 0.0f});
        }
    }
}

/// Ghast#isReflectedFireball: the direct entity a large fireball, the causing entity a player.
bool reflectedFireball(combat::DamageInfo const& d) {
    return d.direct && typeOf(*d.direct) == "minecraft:fireball" && d.attacker && d.attacker->isPlayer();
}

void syncTantrum(Actor& self, UrState& s) {
    bool on = s.phase.tantrum && !dying(self);
    if (on == s.syncedTantrum && s.tickCount % 10 != 1) return;
    s.syncedTantrum = on;
    sendTantrum(self, on);
}

void urTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    auto& s      = stateOf(self);
    auto& region = self.getDimensionBlockSource();
    bool  first  = s.tickCount == 0;
    s.tickCount++;
    // goals (target selector first, then the goal selector). Mob#serverAiStep runs both selectors in
    // full only every other tick; in between tickRunningGoals(false) ticks just the goals that
    // override requiresUpdateEveryTick (UrGhastLookGoal), so the attack timer moves every 2 ticks
    bool full = goalTick(s.tickCount);
    if (full) {
        targetGoal(*m, s);
        flightGoal(*m, s);
    }
    lookGoal(*m);
    if (full) attackGoal(*m, s);
    // customServerAiStep
    if (s.inTrapCounter > 0) {
        s.inTrapCounter--;
        m->setTarget(nullptr);
    }
    // despawn mini ghasts in our box
    auto b = self.getAABB();
    AABB grown{Vec3{b.min.x - 1, b.min.y - 1, b.min.z - 1}, Vec3{b.max.x + 1, b.max.y + 1, b.max.z + 1}};
    for (auto* a : region.getEntities(grown, 0.0f)) {
        if (!a || typeOf(*a) != id::Ghastling || !a->isAlive()) continue;
        particle(region, "minecraft:poof", a->getFeetPos(), {}, 20); // spawnAnim
        a->remove();
        self.heal(2);
    }
    if (s.tickCount % 60 == 0 && !s.traps.empty())
        std::erase_if(s.traps, [&](BlockPos const& p) { return loaded(region, p) && (idAt(region, p) != id::GhastTrap || !region.canSeeSky(BlockPos{p.x, p.y + 1, p.z})); });
    if (first || s.tickCount % 100 == 0) {
        for (auto const& p : scanForTraps(*m))
            if (std::find(s.traps.begin(), s.traps.end(), p) == s.traps.end()) s.traps.push_back(p);
    }
    if (s.phase.tantrum) {
        m->setTarget(nullptr);
        if (--s.nextTantrumCry <= 0) {
            soundAt(region, self.getFeetPos(), "entity.twilightforest.ur_ghast.tantrum", 16.0f, voicePitch());
            s.nextTantrumCry = 20 + random().nextInt(30);
        }
        if (s.tickCount % 10 == 0) doTantrumDamageEffects(*m);
    }
    // UrGhast#aiStep's client side: the companions draw the redstone dust, the boss tears (and their
    // shatter sound) and the tantrum rain themselves from the synced tantrum flag
    // (src/client/darktower); only the vanilla clients get them from here
    if (random().nextBoolean()) {
        Vec3  p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        vanillaRedDust(region, Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * h - 0.25), float(p.z + (r.nextDouble() - 0.5) * w)});
    }
    if (s.phase.tantrum && !dying(self)) {
        Vec3  p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        vanillaParticle(region, "twilightforest:boss_tear", Vec3{float(p.x + (r.nextDouble() - 0.5) * w * 0.75), float(p.y + r.nextDouble() * h * 0.5), float(p.z + (r.nextDouble() - 0.5) * w * 0.75)});
    }
    syncTantrum(self, s);
    syncVisuals(*m, s);
    ge::data(self, kData) = {{"inTantrum", s.phase.tantrum}};
}

/// ServerLevel#findLightningTargetAround: the MOTION_BLOCKING surface at (x, z); the closest lightning
/// rod within 128 that is the top block of its column wins (its block above); else a random living
/// entity that sees the sky in the column's box (to the build limit) inflated by 3; else the surface.
BlockPos lightningTargetAround(BlockSource& region, BlockPos const& at) {
    BlockPos surface = region.getHeightmapPos(BlockPos{at.x, 0, at.z});
    // LightningRodBlock POIs (PoiManager#findClosest, radius 128, the rod at WORLD_SURFACE - 1)
    std::optional<BlockPos> rod;
    double                  best    = 0.0;
    static BlockType const* rodType = [] {
        auto b = Block::tryGetFromRegistry("minecraft:lightning_rod");
        return b ? &b->getBlockType() : nullptr;
    }();
    for (int cx = (surface.x - 128) >> 4; rodType && cx <= (surface.x + 128) >> 4; cx++)
        for (int cz = (surface.z - 128) >> 4; cz <= (surface.z + 128) >> 4; cz++) {
            if (!loaded(region, BlockPos{cx * 16 + 8, surface.y, cz * 16 + 8})) continue;
            for (int x = cx * 16; x < cx * 16 + 16; x++)
                for (int z = cz * 16; z < cz * 16 + 16; z++) {
                    double dx = x - surface.x, dz = z - surface.z;
                    if (dx * dx + dz * dz > 128.0 * 128.0) continue;
                    BlockPos top = region.getHeightmapPos(BlockPos{x, 0, z});
                    // Bedrock's heightmap skips blocks that let light through: the rod is the block
                    // at or just under it
                    for (int y : {top.y, top.y - 1}) {
                        BlockPos q{x, y, z};
                        // the block type compared by pointer (no id string per column)
                        if (&region.getBlock(q).getBlockType() != rodType || !isAir(region, BlockPos{x, y + 1, z})) continue;
                        double dy = y - surface.y, d = dx * dx + dy * dy + dz * dz;
                        if (!rod || d < best) {
                            rod  = BlockPos{x, y + 1, z};
                            best = d;
                        }
                    }
                }
        }
    if (rod) return *rod;
    AABB column{Vec3{float(surface.x - 3), float(surface.y - 3), float(surface.z - 3)},
                Vec3{float(surface.x + 4), float(region.getMaxHeight() + 4), float(surface.z + 4)}};
    std::vector<Actor*> living;
    for (auto* a : actorsIn(region, column)) {
        if (!a->isAlive() || !asMob(*a)) continue;
        auto fp = a->getFeetPos();
        if (region.canSeeSky(BlockPos{int(std::floor(fp.x)), int(std::floor(fp.y)), int(std::floor(fp.z))})) living.push_back(a);
    }
    if (!living.empty()) {
        auto fp = living[random().nextInt(static_cast<int>(living.size()))]->getFeetPos();
        return BlockPos{int(std::floor(fp.x)), int(std::floor(fp.y)), int(std::floor(fp.z))};
    }
    if (surface.y == region.getMinHeight() - 1) surface.y += 2;
    return surface;
}

void startTantrum(Mob& m, UrState& s) {
    s.phase.setTantrum(true);
    double ang = random().nextInt(360) * std::numbers::pi / 180.0;
    auto   p   = m.getFeetPos();
    // findLightningTargetAround(containing(position + Vec3(18, 0, 0).yRot(angle))), at its bottom centre
    BlockPos aim{int(std::floor(p.x + 18.0 * std::cos(ang))), int(std::floor(p.y)), int(std::floor(p.z - 18.0 * std::sin(ang)))};
    BlockPos hit = lightningTargetAround(m.getDimensionBlockSource(), aim);
    visualLightning(m, Vec3{hit.x + 0.5f, float(hit.y), hit.z + 0.5f});
    spawnGhastsAtTraps(m);
    gf::log().info("darktower: the Ur-Ghast throws a tantrum");
}

void switchPhase(Mob& m, UrState& s) {
    if (s.phase.tantrum) {
        s.phase.setTantrum(false);
        gf::log().info("darktower: the Ur-Ghast's tantrum ends");
    } else {
        startTantrum(m, s);
    }
    s.phase.reset();
}

void spawnMinionGhastsAt(Mob& boss, int x, int y, int z) {
    auto& region = boss.getDimensionBlockSource();
    visualLightning(boss, Vec3{float(x), float(y + 4), float(z)});
    auto& r = random();
    int   spawns = 0;
    for (int i = 0; i < 24; i++) {
        double sx = x + (r.nextDouble() - r.nextDouble()) * 4;
        double sy = y + r.nextDouble() * 8;
        double sz = z + (r.nextDouble() - r.nextDouble()) * 4;
        // minion.checkSpawnRules(level, MOB_SUMMONED): CarminiteGhastling is a FlyingMob, so this is
        // Mob#checkSpawnRules, which is always true (only PathfinderMob overrides it; canSpawnHere
        // is the spawn placement predicate, not asked here): every try spawns, wherever it lands
        if (auto* g = spawnMob(region, id::Ghastling, Vec3{float(sx), float(sy), float(sz)}, r.nextFloat() * 360.0f)) {
            makeBossMinion(*g);
            particle(region, "minecraft:poof", g->getFeetPos(), {}, 20); // spawnAnim
        }
        if (++spawns >= 6) break;
    }
}

// ---- carminite ghastguard / ghastling AI -------------------------------------------------------------

struct GhastState {
    int  tickCount   = 0;
    int  unseenTicks = 0; ///< TargetGoal#unseenTicks
    int  attackTimer = 0, prevAttackTimer = 0;
    bool attackRunning = false;
    int  floatDuration = 0;
    bool hasWanted     = false;
    Vec3 wanted{};
};

std::unordered_map<std::int64_t, GhastState>& ghastStates() {
    static std::unordered_map<std::int64_t, GhastState> m;
    return m;
}

bool isGhastling(Actor const& a) { return typeOf(a) == id::Ghastling; }

/// CarminiteGhastguard#wanderFactor: 16, the ghastling 4, a boss minion 0.005.
float wanderFactor(Actor& a) {
    if (!isGhastling(a)) return 16.0f;
    return ge::data(a, "darktower:minion").is_boolean() ? 0.005f : 4.0f;
}

/// The FOLLOW_RANGE attribute: 64 for the ghastguard, 16 for the ghastling.
double followRange(Actor const& a) { return isGhastling(a) ? 16.0 : 64.0; }

/// CarminiteGhastling#shouldAttack (an EnderMan#isLookingAtMe copy; never with a pumpkin helmet);
/// the ghastguard always attacks.
bool shouldAttack(Mob& ghast, Actor& living) {
    if (!isGhastling(ghast)) return true;
    if (game::items::idOf(living.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "minecraft:pumpkin") return false;
    auto* livingMob = asMob(living);
    bool  sees      = livingMob && livingMob->canSee(ghast.getEyePos(), ShapeType::Collision);
    if (std::sqrt(distSqr(living, ghast)) <= 3.5) return sees;
    Vec3   look = living.getViewVector(1.0f);
    double ll   = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
    auto   e    = living.getEyePos();
    auto   gb   = ghast.getAABB();
    auto   gp   = ghast.getFeetPos();
    double eyeH = ghast.getEyePos().y - gp.y;
    Vec3   to{float(gp.x - living.getFeetPos().x), float(gb.min.y + eyeH - e.y), float(gp.z - living.getFeetPos().z)};
    double d0 = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
    if (d0 < 1e-9 || ll < 1e-9) return sees;
    double d1 = (look.x * to.x + look.y * to.y + look.z * to.z) / (ll * d0);
    return d1 > 1.0 - 0.025 / d0 && sees;
}

/// GhastMoveControl#canReach: the box moved `length - 1` times along `dir` stays free of blocks.
bool canReach(Mob& m, Vec3 const& dir, int length) {
    auto& region = m.getDimensionBlockSource();
    AABB  box    = m.getAABB();
    for (int i = 1; i < length; i++) {
        box.min = Vec3{box.min.x + dir.x, box.min.y + dir.y, box.min.z + dir.z};
        box.max = Vec3{box.max.x + dir.x, box.max.y + dir.y, box.max.z + dir.z};
        for (int x = int(std::floor(box.min.x)); x <= int(std::floor(box.max.x - 1e-4)); x++)
            for (int y = int(std::floor(box.min.y)); y <= int(std::floor(box.max.y - 1e-4)); y++)
                for (int z = int(std::floor(box.min.z)); z <= int(std::floor(box.max.z - 1e-4)); z++)
                    if (blocksMotion(region, BlockPos{x, y, z})) return false;
    }
    return true;
}

/// GhastMoveControl#tick.
void ghastMove(Mob& m) {
    auto& g = ghastStates()[uidOf(m)];
    if (!g.hasWanted) return;
    if (g.floatDuration-- <= 0) {
        g.floatDuration += random().nextInt(5) + 2;
        auto   p  = m.getFeetPos();
        double dx = g.wanted.x - p.x, dy = g.wanted.y - p.y, dz = g.wanted.z - p.z;
        double d0 = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d0 < 1e-9) return;
        Vec3 dir{float(dx / d0), float(dy / d0), float(dz / d0)};
        if (canReach(m, dir, static_cast<int>(std::ceil(d0)))) push(m, Vec3{dir.x * 0.1f, dir.y * 0.1f, dir.z * 0.1f});
        else g.hasWanted = false; // Operation.WAIT
    }
}

/// GhastguardRandomFlyGoal (the homed flight goal never starts: TF sets no restriction point on
/// these ghasts, so isMobWithinHomeArea is always true).
void randomFlyGoal(Mob& m, GhastState& g) {
    bool canUse;
    if (!g.hasWanted) {
        canUse = targetOf(m) == nullptr;
    } else {
        auto   p  = m.getFeetPos();
        double dx = g.wanted.x - p.x, dy = g.wanted.y - p.y, dz = g.wanted.z - p.z, d3 = dx * dx + dy * dy + dz * dz;
        canUse    = targetOf(m) == nullptr && (d3 < 1.0 || d3 > 3600.0);
    }
    if (!canUse) return;
    auto& r  = random();
    float wf = wanderFactor(m);
    auto  p  = m.getFeetPos();
    double x = p.x + (r.nextFloat() * 2.0f - 1.0f) * wf;
    double y = p.y + (r.nextFloat() * 2.0f - 1.0f) * wf;
    double z = p.z + (r.nextFloat() * 2.0f - 1.0f) * wf;
    g.wanted    = Vec3{float(x), float(y), float(z)};
    g.hasWanted = true;
}

/// NearestAttackableTargetGoal<Player>(mustSee): a player within the follow range in sight (1 in 5 on
/// each full goal tick), kept while TargetGoal#canContinueToUse holds.
void ghastTargetGoal(Mob& m, GhastState& g) {
    if (auto* t = targetOf(m)) {
        if (!keepTarget(m, *t, followRange(m), g.unseenTicks)) m.setTarget(nullptr);
        return;
    }
    if (random().nextInt(5) != 0) return;
    if (auto* p = nearestTargetablePlayer(m, followRange(m))) {
        m.setTarget(p);
        g.unseenTicks = 0;
    }
}

/// GhastguardAttackGoal#spitFireball.
void ghastSpitFireball(Mob& m, Actor& t) {
    Vec3   look = m.getViewVector(1.0f);
    auto   p    = m.getFeetPos();
    auto   mb   = m.getAABB();
    auto   tb   = t.getAABB();
    float  h    = mb.max.y - mb.min.y;
    auto   tp   = t.getFeetPos();
    double d2   = tp.x - (p.x + look.x * 4.0);
    double d3   = tb.min.y + (tb.max.y - tb.min.y) / 2.0 - (0.5 + p.y + h / 2.0);
    double d4   = tp.z - (p.z + look.z * 4.0);
    largeFireball(m, Vec3{float(p.x + look.x * 4.0), float(p.y + h / 2.0 + 0.5), float(p.z + look.z * 4.0)}, Vec3{float(d2), float(d3), float(d4)});
    if (random().nextInt(6) == 0) m.setTarget(nullptr);
}

/// GhastguardAttackGoal.
void ghastAttackGoal(Mob& m, GhastState& g) {
    auto* t      = targetOf(m);
    bool  canUse = t && shouldAttack(m, *t);
    if (!g.attackRunning) {
        if (!canUse) return;
        g.attackRunning = true;
        g.attackTimer = g.prevAttackTimer = 0; // start
    } else if (!canUse) {
        g.attackRunning = false; // stop: setCharging(false)
        ge::setProperty(m, "twilightforest:charging", false);
        return;
    }
    auto&       region = m.getDimensionBlockSource();
    std::string kind   = isGhastling(m) ? "carminite_ghastling" : "carminite_ghastguard";
    auto        pitch  = [] { return (random().nextFloat() - random().nextFloat()) * 0.2f + 1.0f; };
    if (distSqr(*t, m) < 4096.0 && lineOfSight(m, *t)) {
        g.prevAttackTimer = g.attackTimer;
        ++g.attackTimer;
        lookAtPitch(m, *t, 500.0f); // CarminiteGhastguard#getMaxHeadXRot
        if (g.attackTimer == 10) soundAt(region, m.getFeetPos(), ("entity.twilightforest." + kind + ".warn").c_str(), 10.0f, pitch());
        if (g.attackTimer == 20) {
            if (shouldAttack(m, *t)) {
                soundAt(region, m.getFeetPos(), ("entity.twilightforest." + kind + ".shoot").c_str(), 10.0f, pitch());
                ghastSpitFireball(m, *t);
                g.prevAttackTimer = g.attackTimer;
            }
            g.attackTimer = -40;
        }
    } else if (g.attackTimer > 0) {
        g.prevAttackTimer = g.attackTimer;
        --g.attackTimer;
    }
    ge::setProperty(m, "twilightforest:charging", g.attackTimer > 10);
}

void ghastSpawn(Actor& self) {
    if (isGhastling(self) && ge::data(self, "darktower:minion").is_boolean()) gf::boss::setMinion(self, true);
    if (auto* m = asMob(self)) ge::setMoveControl(*m, [](Mob& mob) { ghastMove(mob); });
}

void ghastDie(Actor& self, std::string const&, Actor*) { ghastStates().erase(uidOf(self)); }

void ghastTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    auto& g = ghastStates()[uidOf(self)];
    // the target selector, then the goals (flight, look, attack): GhastguardAttackGoal, the fly goal
    // and the target goal run on the goal selector's full ticks only (every other tick, as the Ur-Ghast)
    bool full = goalTick(++g.tickCount);
    if (full) {
        ghastTargetGoal(*m, g);
        randomFlyGoal(*m, g);
    }
    lookGoal(*m);
    if (full) ghastAttackGoal(*m, g);
    // CarminiteGhastguard#customServerAiStep: the attack timer for the renderer's squash
    ge::setProperty(self, "twilightforest:attack_timer", std::clamp(g.attackTimer, -40, 20));
    // ATTACK_STATUS: 1 while it has a target it would attack (the renderer's open eyes)
    auto* t = targetOf(*m);
    ge::setProperty(self, "twilightforest:attack_status", t && shouldAttack(*m, *t) ? 1 : 0);
    // aiStep: redstone dust half the time
    auto& region = self.getDimensionBlockSource();
    if (random().nextBoolean()) {
        auto  b = self.getAABB();
        auto  p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        redstoneDust(region, Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * h - 0.25), float(p.z + (r.nextDouble() - 0.5) * w)});
    }
    // now and then drop the state of ghasts no longer loaded
    static std::uint64_t lastSweep = 0;
    if (now() - lastSweep > 1200) {
        lastSweep = now();
        std::erase_if(ghastStates(), [](auto const& e) { return actorById(e.first) == nullptr; });
        std::erase_if(states(), [](auto const& e) { return actorById(e.first) == nullptr; });
    }
}

} // namespace

bool isUrGhast(Actor const& a) { return typeOf(a) == id::UrGhast; }

Actor* shootUrGhastFireball(Mob& owner, Vec3 const& at, Vec3 const& dir) { return fireball(owner, at, dir); }

Actor* shootLargeFireball(Mob& owner, Vec3 const& at, Vec3 const& dir) { return largeFireball(owner, at, dir); }

bool isTrappable(Actor const& a) {
    auto t = typeOf(a);
    return t == id::UrGhast || t == id::Ghastguard || t == id::Ghastling || t == "minecraft:ghast";
}

void trapUrGhast(Mob& g) {
    auto& s = stateOf(g);
    if (s.phase.tantrum) gf::log().info("darktower: the ghast trap ends the Ur-Ghast's tantrum");
    s.phase.setTantrum(false);
    s.inTrapCounter = 20;
    setSolid(g, s, false); // ghast.noPhysics = true: "turn this on so we can pull it in close"
}

bool solid(Actor& g) { return stateOf(g).solid; }

void setSolid(Mob& g, bool solid) { setSolid(g, stateOf(g), solid); }

void resetPhaseDamage(Mob& g) { stateOf(g).phase.reset(); }

bool inTantrum(Actor& g) { return stateOf(g).phase.tantrum; }

void setTantrum(Mob& g, bool t) {
    auto& s = stateOf(g);
    if (t && !s.phase.tantrum) startTantrum(g, s);
    else s.phase.setTantrum(t);
}

float damageUntilNextPhase(Actor& g) { return stateOf(g).phase.damageUntilNextPhase; }

int trappedTicks(Actor& g) { return stateOf(g).inTrapCounter; }

std::vector<BlockPos> trapLocations(Actor& g) { return stateOf(g).traps; }

void spawnGhastsAtTraps(Mob& g) {
    auto& s     = stateOf(g);
    auto  spots = s.traps;
    auto& r     = random();
    for (int i = static_cast<int>(spots.size()) - 1; i > 0; --i) std::swap(spots[i], spots[r.nextInt(i + 1)]);
    int n = std::min<int>(2, static_cast<int>(spots.size()));
    for (int i = 0; i < n; i++) spawnMinionGhastsAt(g, spots[i].x, spots[i].y, spots[i].z);
}

void makeBossMinion(Mob& g) {
    gf::boss::setMinion(g, true);
    ge::data(g, "darktower:minion") = true;
    // Attributes.MAX_HEALTH base 6, health 6
    auto attr = g.getMutableAttribute(SharedAttributes::HEALTH());
    if (auto* inst = attr.mInstance->mPtr) {
        inst->mDefaultMaxValue = 6.0f;
        inst->mCurrentMaxValue = 6.0f;
        inst->mCurrentValue    = std::min<float>(inst->mCurrentValue, 6.0f);
    }
}

void install() {
    gf::boss::registerBoss({
        .entityId        = id::UrGhast,
        .homeStructure   = "twilightforest:dark_tower",
        .lootTable       = "twilightforest:entities/ur_ghast",
        .deathChestBlock = "twilightforest:dark_chest",
        .bar             = {.rgb = 0xFF0000},
        .deathTicks      = kDeathTicks,
    });
    gf::boss::ext::setHomeRadius(id::UrGhast, kHomeRadius);
    // UrGhastSpawnerBlockEntity#anyPlayerInRange: the nearest player within 9 must also stand above
    // the spawner's y - 4 (else nothing spawns yet)
    gf::boss::registerSpawner({.blockId  = "twilightforest:ur_ghast_boss_spawner",
                               .entityId = id::UrGhast,
                               .range    = 9,
                               .particle = "minecraft:dust",
                               .spawnFn  = [](BlockSource& region, BlockPos const& pos) -> Mob* {
                                   Vec3    c{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
                                   Player* closest = nearestPlayer(dimOf(region), c, 9.0);
                                   if (!closest || closest->getFeetPos().y <= pos.y - 4) return nullptr;
                                   // BossSpawnerBlockEntity#spawnMyBoss: at the block below when its
                                   // collision shape is empty, bottom centre, random yaw
                                   BlockPos at = pos;
                                   if (!hasCollision(region, BlockPos{pos.x, pos.y - 1, pos.z}))
                                       at = BlockPos{pos.x, pos.y - 1, pos.z};
                                   return spawnMob(region, id::UrGhast, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, random().nextFloat() * 360.0f);
                               }});
    gf::boss::ext::setLifecycle(id::UrGhast, {
        .tickDeath =
            [](Mob& boss, int deathTime) {
                // UrGhast#tickDeathAnimation: explosions for the first third, then a redstone trail
                // (the chest location is below the boss; the trail descends to it)
                auto& region = boss.getDimensionBlockSource();
                auto  b      = boss.getAABB();
                float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
                auto  p      = boss.getFeetPos();
                auto& r      = random();
                const_cast<Vec3&>(boss.getPosDelta()) = Vec3{0.0f, -0.03f, 0.0f}; // DYING_DECENT
                if (deathTime <= kDeathTicks / 3) {
                    for (int k = 0; k < 12; k++) {
                        Vec3 at{p.x + r.nextFloat() * w * 1.8f - w, p.y + r.nextFloat() * h, p.z + r.nextFloat() * w * 1.8f - w};
                        particle(region, r.nextBoolean() ? (r.nextBoolean() ? "minecraft:poof" : "minecraft:explosion") : "minecraft:dust", at);
                    }
                } else {
                    // EntityUtil.bossChestLocation: the home point's block below, else the boss's block
                    Vec3 start{p.x, p.y + h * 0.5f, p.z};
                    Vec3 end{std::floor(p.x) + 0.5f, std::floor(p.y) + 0.5f, std::floor(p.z) + 0.5f};
                    if (auto home = ge::home(boss)) end = Vec3{home->pos.x + 0.5f, home->pos.y - 1 + 0.5f, home->pos.z + 0.5f};
                    int    third = kDeathTicks / 3;
                    int    d2    = deathTime - third + 1;
                    double f     = double(d2) / double(third * 2);
                    Vec3   at{float(start.x + (end.x - start.x) * f + std::sin(d2 * std::numbers::pi * 0.1)),
                              float(start.y + (end.y - start.y) * f + std::sin(d2 * std::numbers::pi * 0.05)),
                              float(start.z + (end.z - start.z) * f + std::cos(d2 * std::numbers::pi * 0.1125))};
                    for (int i = 0; i < 40; i++) {
                        double x = (r.nextDouble() - 0.5) * 0.05 * i;
                        double y = (r.nextDouble() - 0.5) * 0.05 * i;
                        double z = (r.nextDouble() - 0.5) * 0.05 * i;
                        redstoneDust(region, Vec3{float(at.x + x), float(at.y + y), float(at.z + z)});
                    }
                }
                ge::setProperty(boss, "twilightforest:death_time", std::min(deathTime, 90));
            },
    });
    ge::registerType(id::UrGhast, {
        .onSpawn =
            [](Actor& self) {
                if (auto* m = asMob(self)) ge::setMoveControl(*m, ge::MoveKind::NoClip);
                if (auto const& solidTag = ge::data(self, "darktower:solid"); solidTag.is_boolean() && solidTag.get<bool>()) {
                    stateOf(self).solid = true;
                    VariantParameterList params{};
                    params.mSelf = &self;
                    gf::game::compat::executeEvent(self, "solid", params);
                }
                auto& d = ge::data(self, kData);
                // readAdditionalSaveData: setInTantrum(saved) also resets damageUntilNextPhase to 18
                stateOf(self).phase.setTantrum(d.is_object() && d.value("inTantrum", false));
            },
        .tick = urTick,
        .hurt =
            [](Actor& self, float& amount, std::string const& type, Actor*) {
                auto& s = stateOf(self);
                amount  = s.phase.incoming(amount);
                (void)type;
                return true;
            },
        .die =
            [](Actor& self, std::string const&, Actor*) {
                auto p = self.getFeetPos();
                auto b = self.getAABB();
                visualLightning(self, Vec3{p.x, p.y + (b.max.y - b.min.y) * 0.5f, p.z});
                sendTantrum(self, false); // !isDeadOrDying()
                states().erase(uidOf(self));
                gf::log().info("darktower: the Ur-Ghast died");
            },
    });
    // isInvulnerableTo: in-wall and fire damage, unless a player-reflected large fireball
    combat::onIncoming(combat::priority::High, [](combat::DamageInfo& d) {
        if (!isUrGhast(d.victim)) return combat::Verdict::Pass;
        static const std::vector<std::string> kFire = {"minecraft:in_fire", "minecraft:on_fire", "minecraft:lava", "minecraft:hot_floor", "minecraft:fireball",
                                                        "minecraft:unattributed_fireball", "minecraft:campfire"};
        if (reflectedFireball(d)) {
            // let through with its own amount (UrGhast is a BaseTFBoss, not a Ghast: no 1000 rule);
            // Bedrock's minecraft:fire_immune refuses a fire cause outright, so a fire-typed reflected
            // hit goes in again as a projectile hit of the same amount
            if (std::find(kFire.begin(), kFire.end(), d.type) != kFire.end()) {
                combat::hurt(d.victim, d.amount, "minecraft:mob_projectile", d.attacker, d.direct);
                return combat::Verdict::Cancel;
            }
            return combat::Verdict::Pass;
        }
        if (d.type == "minecraft:in_wall" || std::find(kFire.begin(), kFire.end(), d.type) != kFire.end()) return combat::Verdict::Cancel;
        return combat::Verdict::Pass;
    });
    // the phase bookkeeping after the damage applied (hurtTime == hurtDuration: a fresh hurt)
    combat::onDealt(combat::priority::Normal, [](combat::DamageInfo const& d) {
        if (!isUrGhast(d.victim)) return;
        auto* m = asMob(d.victim);
        if (!m) return;
        auto& s     = stateOf(d.victim);
        auto  t     = static_cast<std::int64_t>(now());
        bool  fresh = t - s.lastHurtTick >= 10;
        if (fresh) s.lastHurtTick = t;
        if (s.phase.afterHurt(d.amount, fresh, dying(d.victim) || m->getHealth() <= 0, s.inTrapCounter > 0)) {
            // afterHurt already toggled the flag: run the tantrum's start effects when it began
            bool nowTantrum = s.phase.tantrum;
            s.phase.setTantrum(!nowTantrum);
            switchPhase(*m, s);
        }
    });

    runtime::onEveryTick(tickFireballs);
    // an Ur-Ghast gone with its chunk (or a player who left) no longer keeps the atmosphere's rain up
    runtime::onEveryTick([] {
        if (now() % 40 != 0 || atmosphereActive().empty()) return;
        std::vector<std::int64_t> gone;
        for (auto const& [player, ghasts] : atmosphereActive())
            for (auto g : ghasts)
                if (!actorById(g) && std::find(gone.begin(), gone.end(), g) == gone.end()) gone.push_back(g);
        for (auto g : gone) syncAtmosphere(g, false, {});
        std::erase_if(atmosphereActive(), [](auto const& e) { return actorById(e.first) == nullptr; });
    });

    // ---- carminite ghastguard / ghastling ----------------------------------------------------------
    // Ghast#hurt (CarminiteGhastguard extends Ghast, CarminiteGhastling extends it): a large fireball
    // a player sent back deals 1000 (isReflectedFireball: direct entity a LargeFireball, causing entity
    // a player). Not the Ur-Ghast: UrGhast extends BaseTFBoss (a Monster), so a reflected fireball deals
    // its own damage there (UrGhastFireball 16, LargeFireball 6), divided by 10 in a tantrum.
    combat::onIncoming(combat::priority::High, [](combat::DamageInfo& d) {
        auto t = typeOf(d.victim);
        if (t != id::Ghastguard && t != id::Ghastling) return combat::Verdict::Pass;
        if (!reflectedFireball(d)) return combat::Verdict::Pass;
        // Bedrock's minecraft:fire_immune refuses a fire cause outright: the reflected hit goes in again
        // as a projectile hit (Java: Ghast#isInvulnerableTo lets a reflected fireball through)
        if (d.type == "minecraft:fireball" || d.type == "minecraft:unattributed_fireball") {
            combat::hurt(d.victim, 1000.0f, "minecraft:mob_projectile", d.attacker, d.direct);
            return combat::Verdict::Cancel;
        }
        d.amount = 1000.0f;
        return combat::Verdict::Pass;
    });
    // IsMinionCondition (inverse) of entities/carminite_ghastling: a minion ghastling drops nothing
    // (the condition's owner registers no twilightforest:is_minion yet: the table's pool has it
    // inlined away, tools/import/overrides/darktower.json)
    gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const&) {
        if (typeOf(dead) == id::Ghastling && gf::boss::isMinion(dead)) drops.clear();
    });
    ge::registerType(id::Ghastguard, {.onSpawn = ghastSpawn, .tick = ghastTick, .die = ghastDie});
    ge::registerType(id::Ghastling, {.onSpawn = ghastSpawn, .tick = ghastTick, .die = ghastDie});
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        game::db::onWorldReady(loadFireballs);
        game::db::onWorldSave(saveFireballs);
        game::db::onWorldClosed([] {
            states().clear();
            ghastStates().clear();
            atmosphereActive().clear();
            fireballs().clear();
        });
    }
}

} // namespace gf::feature::darktower::ghasts
