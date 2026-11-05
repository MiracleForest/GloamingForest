// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/UrGhast.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastFlightGoal.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastAttackGoal.java
//   src/main/java/twilightforest/entity/ai/goal/UrGhastLookGoal.java
//   src/main/java/twilightforest/entity/projectile/UrGhastFireball.java
//   src/main/java/twilightforest/entity/monster/CarminiteGhastguard.java
//   src/main/java/twilightforest/entity/monster/CarminiteGhastling.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Bedrock: the Ur-Ghast is a native flyer (F8's NoClip move control and wanted position) with its
// goals translated below; the carminite ghastguard and ghastling keep Bedrock's vanilla ghast
// runtime (float wander, ranged fireball attack: their BP JSON), as TF's classes extend vanilla's
// Ghast, plus the parts TF adds (ghastling stare rule, minions, redstone dust, attack timer).

#include "feature/darktower/UrGhast.h"

#include <algorithm>
#include <cmath>
#include <numbers>
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
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/darktower/Devices.h"
#include "feature/darktower/Util.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "game/Blocks.h"
#include "game/Items.h"
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
    std::int64_t lastHurtTick = -100;
};

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
    std::erase_if(found, [&](BlockPos const& p) { return !region.canSeeSky(BlockPos{p.x, p.y + 1, p.z}); });
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

void flightGoal(Mob& m, UrState& s) {
    if (s.inTrapCounter > 0) return; // the trap's pull owns the motion
    // canUse (Ghast.RandomFloatAroundGoal copy): the path is rebuilt every time it is asked
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

/// UrGhastFireball: a vanilla large fireball (Bedrock's minecraft:fireball) with power 1.
void fireball(Mob& m, Vec3 const& at, Vec3 const& dir) {
    auto* l = level();
    if (!l) return;
    double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-9) return;
    // UrGhastFireball#shoot: normalise, gaussian 0.0075 * dist... applied by the projectile's own
    // uncertainty below
    auto owned = l->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:fireball"), &m, at, Vec2{0.0f, 0.0f});
    if (!owned) return;
    auto* fb = l->addEntity(m.getDimensionBlockSource(), std::move(owned));
    if (!fb) return;
    if (auto* pc = fb->getEntityContext().tryGetComponent<ProjectileComponent>())
        pc->shoot(*fb, Vec3{float(dir.x / len), float(dir.y / len), float(dir.z / len)}, 1.0f, 0.0f, Vec3{0, 0, 0}, nullptr);
    ge::data(*fb, "darktower:urghast_fireball") = true;
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

/// NearestAttackableTargetGoal<Player>(mustSee): every tick 1 in 10 (reducedTickDelay(10)).
void targetGoal(Mob& m, UrState& s) {
    if (auto* t = targetOf(m)) {
        if (!targetable(*t) || distSqr(*t, m) > 128.0 * 128.0) m.setTarget(nullptr);
        return;
    }
    if (s.phase.tantrum || s.inTrapCounter > 0) return;
    if (random().nextInt(10) != 0) return;
    auto& region = m.getDimensionBlockSource();
    auto  c      = m.getFeetPos();
    for (auto* p : playersNear(dimOf(region), c, 128.0)) {
        if (!targetable(*p) || !lineOfSight(m, *p)) continue;
        m.setTarget(p);
        return;
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

void urTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    auto& s      = stateOf(self);
    auto& region = self.getDimensionBlockSource();
    bool  first  = s.tickCount == 0;
    s.tickCount++;
    // goals (target selector first, then the goal selector)
    targetGoal(*m, s);
    flightGoal(*m, s);
    lookGoal(*m);
    attackGoal(*m, s);
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
        std::erase_if(s.traps, [&](BlockPos const& p) { return idAt(region, p) != id::GhastTrap || !region.canSeeSky(BlockPos{p.x, p.y + 1, p.z}); });
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
        // the client's boss tears (companions draw them through fx, vanilla clients too)
        Vec3 p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        particle(region, "twilightforest:boss_tear", Vec3{float(p.x + (r.nextDouble() - 0.5) * w * 0.75), float(p.y + r.nextDouble() * h * 0.5), float(p.z + (r.nextDouble() - 0.5) * w * 0.75)});
    }
    if (random().nextBoolean()) {
        Vec3  p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        redstoneDust(region, Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * h - 0.25), float(p.z + (r.nextDouble() - 0.5) * w)});
    }
    syncVisuals(*m, s);
    ge::data(self, kData) = {{"inTantrum", s.phase.tantrum}};
}

void startTantrum(Mob& m, UrState& s) {
    s.phase.setTantrum(true);
    auto& region = m.getDimensionBlockSource();
    double ang = random().nextInt(360) * std::numbers::pi / 180.0;
    auto   p   = m.getFeetPos();
    // Vec3(18, 0, 0).yRot(angle)
    visualLightning(region, Vec3{float(p.x + 18.0 * std::cos(ang)), p.y, float(p.z - 18.0 * std::sin(ang))});
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
    visualLightning(region, Vec3{float(x), float(y + 4), float(z)});
    auto& r = random();
    int   spawns = 0;
    for (int i = 0; i < 24; i++) {
        double sx = x + (r.nextDouble() - r.nextDouble()) * 4;
        double sy = y + r.nextDouble() * 8;
        double sz = z + (r.nextDouble() - r.nextDouble()) * 4;
        // checkSpawnRules(MOB_SUMMONED): the ghastling's rules are not peaceful and a free spot
        BlockPos at{int(std::floor(sx)), int(std::floor(sy)), int(std::floor(sz))};
        if (isAir(region, at)) {
            if (auto* g = spawnMob(region, id::Ghastling, Vec3{float(sx), float(sy), float(sz)}, r.nextFloat() * 360.0f)) {
                makeBossMinion(*g);
                particle(region, "minecraft:poof", g->getFeetPos(), {}, 20);
            }
        }
        if (++spawns >= 6) break;
    }
}

} // namespace

bool isUrGhast(Actor const& a) { return typeOf(a) == id::UrGhast; }

bool isTrappable(Actor const& a) {
    auto t = typeOf(a);
    return t == id::UrGhast || t == id::Ghastguard || t == id::Ghastling || t == "minecraft:ghast";
}

void trapUrGhast(Mob& g) {
    auto& s = stateOf(g);
    if (s.phase.tantrum) gf::log().info("darktower: the ghast trap ends the Ur-Ghast's tantrum");
    s.phase.setTantrum(false);
    s.inTrapCounter = 20;
}

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
    gf::boss::registerSpawner({.blockId = "twilightforest:ur_ghast_boss_spawner", .entityId = id::UrGhast, .range = 9, .particle = "minecraft:dust"});
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
                    int    third = kDeathTicks / 3;
                    int    d2    = deathTime - third + 1;
                    double f     = double(d2) / double(third * 2);
                    Vec3   at{float(p.x + std::sin(d2 * std::numbers::pi * 0.1)), float(p.y + h * 0.5 * (1.0 - f) + std::sin(d2 * std::numbers::pi * 0.05)),
                             float(p.z + std::cos(d2 * std::numbers::pi * 0.1125))};
                    redstoneDust(region, at);
                }
                if (deathTime == 1) visualLightning(region, Vec3{p.x, p.y + h * 0.5f, p.z});
                ge::setProperty(boss, "twilightforest:death_time", std::min(deathTime, 90));
            },
    });
    ge::registerType(id::UrGhast, {
        .onSpawn =
            [](Actor& self) {
                if (auto* m = asMob(self)) ge::setMoveControl(*m, ge::MoveKind::NoClip);
                auto& d = ge::data(self, kData);
                if (d.is_object() && d.value("inTantrum", false)) stateOf(self).phase.setTantrum(true);
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
                visualLightning(self.getDimensionBlockSource(), Vec3{p.x, p.y + (b.max.y - b.min.y) * 0.5f, p.z});
                gf::log().info("darktower: the Ur-Ghast died");
            },
    });
    // isInvulnerableTo: in-wall and fire damage, unless a player-reflected large fireball
    combat::onIncoming(combat::priority::High, [](combat::DamageInfo& d) {
        if (!isUrGhast(d.victim)) {
            // UrGhastFireball#onHitEntity: 16 fireball damage (vanilla 6 + 10)
            if (d.direct && typeOf(*d.direct) == "minecraft:fireball" && ge::data(*d.direct, "darktower:urghast_fireball").is_boolean())
                d.amount += 10.0f;
            return combat::Verdict::Pass;
        }
        bool reflected = d.direct && typeOf(*d.direct) == "minecraft:fireball" && d.attacker && d.attacker->isPlayer();
        if (reflected) return combat::Verdict::Pass;
        static const std::vector<std::string> kFire = {"minecraft:in_fire", "minecraft:on_fire", "minecraft:lava", "minecraft:hot_floor", "minecraft:fireball",
                                                        "minecraft:unattributed_fireball", "minecraft:campfire", "minecraft:lightning_bolt"};
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

    // ---- carminite ghastguard / ghastling -------------------------------------------------------
    auto ghastTick = [](Actor& self) {
        auto& region = self.getDimensionBlockSource();
        if (random().nextBoolean()) {
            auto b = self.getAABB();
            auto p = self.getFeetPos();
            float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
            auto& r = random();
            redstoneDust(region, Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * h - 0.25), float(p.z + (r.nextDouble() - 0.5) * w)});
        }
    };
    ge::registerType(id::Ghastguard, {.tick = ghastTick});
    ge::registerType(id::Ghastling, {
        .onSpawn =
            [](Actor& self) {
                if (ge::data(self, "darktower:minion").is_boolean()) gf::boss::setMinion(self, true);
            },
        .tick = ghastTick,
    });
    // CarminiteGhastling#shouldAttack: an enderman-like stare (never through a pumpkin)
    ge::addTargetFilter([](Mob& attacker, Actor& candidate) {
        if (typeOf(attacker) != id::Ghastling || !candidate.isPlayer()) return true;
        if (game::items::idOf(candidate.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "minecraft:carved_pumpkin") return false;
        double d = std::sqrt(distSqr(attacker, candidate));
        if (!static_cast<Mob&>(candidate).canSee(attacker.getEyePos(), ShapeType::Collision)) return false;
        if (d < 3.5) return true;
        Vec3   look = candidate.getViewVector(1.0f);
        auto   e    = candidate.getEyePos();
        auto   ae   = attacker.getEyePos();
        Vec3   to{ae.x - e.x, ae.y - e.y, ae.z - e.z};
        double len = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
        if (len < 1e-9) return true;
        double dot = (look.x * to.x + look.y * to.y + look.z * to.z) / len;
        return dot > 1.0 - 0.025 / len;
    });
}

} // namespace gf::feature::darktower::ghasts
