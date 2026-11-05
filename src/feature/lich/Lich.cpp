// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/Lich.java
//   src/main/java/twilightforest/entity/ai/goal/LichShadowsGoal.java
//   src/main/java/twilightforest/entity/ai/goal/LichMinionsGoal.java
//   src/main/java/twilightforest/entity/ai/goal/LichPopMobsGoal.java
//   src/main/java/twilightforest/entity/ai/goal/LichAbsorbMinionsGoal.java
//   src/main/java/twilightforest/block/entity/spawner/LichSpawnerBlockEntity.java
//   src/main/java/twilightforest/item/LifedrainScepterItem.java
//   src/main/java/twilightforest/components/entity/FortificationShieldAttachment.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Twilight Lich: three phases (shield and shadow clones, minions, melee), teleports, bolts and
// bombs, popping nearby mobs, the candle spawn sequence and its 295-tick death animation. The boss
// lifecycle (bar, loot buffer, death chest, conquest, spawner) is F8's boss kit; this file plugs the
// Lich's overrides into it (BaseTFBoss#shouldSpawnLoot / placeSpawner / postRemoval / tickDeath ...).

#include "feature/lich/Lich.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/lich/Common.h"
#include "feature/lich/Edits.h"
#include "feature/lich/Pending.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::lich {

namespace ge = gf::entity;
namespace gb = gf::boss;

namespace {

constexpr int kBurstCooldown = 23;                 // PARTICLE_BURST_COOLDOWN
constexpr int kPointA        = kBurstCooldown * 5; // DEATH_ANIMATION_POINT_A (115)
constexpr int kPointB        = kPointA + 16;       // 131
constexpr int kPointC        = kPointB + 32;       // 163
constexpr int kDeathDuration = kPointC + 132;      // 295
constexpr int kMaxActiveMinions = 3;
constexpr int kHomeRadius    = 30;

constexpr char const* kFortificationScepter = "twilightforest:fortification_scepter";
constexpr char const* kTwilightScepter      = "twilightforest:twilight_scepter";
constexpr char const* kLifedrainScepter     = "twilightforest:lifedrain_scepter";
constexpr char const* kZombieScepter        = "twilightforest:zombie_scepter";
constexpr char const* kGoldenSword          = "minecraft:golden_sword";

std::unordered_map<std::int64_t, LichState>& states() {
    static std::unordered_map<std::int64_t, LichState> m;
    return m;
}

Mob* self(Actor& a) { return asMob(a); }

// ---- home (EnforcedHomePoint) ------------------------------------------------------------------------

std::optional<BlockPos> homePos(Mob& lich) {
    auto h = ge::home(lich);
    if (!h || h->pos.dim != dimOf(lich)) return std::nullopt;
    return BlockPos{h->pos.x, h->pos.y, h->pos.z};
}

BlockPos homeOrElseCurrent(Mob& lich) {
    if (auto h = homePos(lich)) return *h;
    return game::blockPosOf(lich.getFeetPos());
}

/// Lich#isOutsideHomeRange.
bool isOutsideHomeRange(Mob& lich, Vec3 const& pos) {
    auto h = homePos(lich);
    if (!h) return false;
    auto y = lich.getFeetPos().y;
    if (!targetOf(lich) && y < h->y - 2) return true;
    Vec3 c{h->x + 0.5f, h->y + 0.5f, h->z + 0.5f};
    return distSqr(c, pos) > double(kHomeRadius) * kHomeRadius || (phaseOf(lich) == 3 && y < h->y - 3);
}

// ---- particles ---------------------------------------------------------------------------------------

/// Entity#getX(scale): x + width * scale (Entity#getRandomX uses the same).
double getX(Actor& a, double scale) { return a.getFeetPos().x + bbWidth(a) * scale; }
double getZ(Actor& a, double scale) { return a.getFeetPos().z + bbWidth(a) * scale; }
double getY(Actor& a, double scale) { return a.getFeetPos().y + bbHeight(a) * scale; }

void lichTeleportParticles(Mob& lich, bool appear) {
    auto& r     = randomOf(lich);
    bool  clone = isShadowClone(lich);
    if (appear) {
        auto pos = lich.getFeetPos();
        Vec3 mid{pos.x, pos.y + bbHeight(lich) * 0.5f, pos.z};
        for (int j = 0; j < 64; ++j) {
            double x = getX(lich, r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0));
            double y = getY(lich, r.nextDouble());
            double z = getZ(lich, r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0));
            particle(lich, clone ? "minecraft:smoke" : "twilightforest:ominous_flame", Vec3{float(x), float(y), float(z)},
                     Vec3{float((x - mid.x) * 0.0625), float((y - mid.y) * 0.0625), float((z - mid.z) * 0.0625)});
        }
    } else {
        int n = clone ? 64 : 128;
        for (int j = 0; j < n; ++j) {
            double x = getX(lich, (r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0)) * 2.0);
            double y = getY(lich, r.nextDouble() * 1.125);
            double z = getZ(lich, (r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0)) * 2.0);
            particle(lich, "minecraft:smoke", Vec3{float(x), float(y), float(z)});
        }
    }
}

/// LifedrainScepterItem#animateTargetShatter.
void animateTargetShatter(Mob& lich, Actor& target) {
    auto&  r     = randomOf(lich);
    bool   big   = r.nextInt(100) == 0;
    double power = big ? 1.0 : 0.3;
    double gf    = 5.0;
    float  w = bbWidth(target), h = bbHeight(target);
    auto   pos = target.getFeetPos();
    Json   params{{"item", "minecraft:rotten_flesh"}};
    int    n = 50 + static_cast<int>(w) * (big ? 75 : 25);
    for (int i = 0; i < n; ++i) {
        double gx = r.nextGaussian() * 0.01, gy = r.nextGaussian() * 0.01, gz = r.nextGaussian() * 0.01;
        double speed = r.nextFloat() * power;
        double x     = r.nextFloat() * w * 1.5F - w - gx * gf + (r.nextGaussian() * gx);
        double y     = r.nextFloat() * h - gy * gf + (r.nextGaussian() * gy);
        double z     = r.nextFloat() * w * 1.5F - w - gz * gf + (r.nextGaussian() * gz);
        particle(lich, "minecraft:item", Vec3{float(pos.x + x), float(pos.y + y), float(pos.z + z)}, Vec3{float(x * speed), float(y * speed), float(z * speed)}, params);
    }
}

/// EntityUtil#getDeathSound for the mobs the Lich pops.
std::string deathSoundOf(Actor& mob) {
    auto const& id = mob.getTypeName();
    if (id == kMinion) return "entity.twilightforest.minion.death";
    // the vanilla poppables: Bedrock's own sound names (gf::fx passes a bare name through)
    if (id == "minecraft:zombie" || id == "minecraft:husk" || id == "minecraft:drowned" || id == "minecraft:zombie_villager") return "mob.zombie.death";
    if (id == "minecraft:skeleton" || id == "minecraft:stray" || id == "minecraft:wither_skeleton" || id == "minecraft:bogged") return "mob.skeleton.death";
    if (id == "minecraft:enderman") return "mob.endermen.death";
    if (id == "minecraft:spider" || id == "minecraft:cave_spider") return "mob.spider.death";
    if (id == "minecraft:creeper") return "mob.creeper.death";
    if (id.rfind("twilightforest:", 0) == 0) return "entity.twilightforest." + id.substr(15) + ".death";
    return {};
}

/// FortificationShieldAttachment#addShieldBreakParticles(src, lich).
void addShieldBreakParticles(Mob& lich, Actor* direct, Actor* attacker) {
    auto&               r = randomOf(lich);
    std::optional<Vec3> src;
    if (direct) src = direct->getPosition();
    if (direct && (direct->isPlayer() || direct->hasCategory(ActorCategory::Mob))) src = eyeOf(*direct);
    if (attacker && direct && attacker != direct && (attacker->isPlayer() || attacker->hasCategory(ActorCategory::Mob))) src = eyeOf(*attacker);
    auto pos = lich.getFeetPos();
    Vec3 base{pos.x, pos.y + 0.65f * bbHeight(lich), pos.z};
    if (src) {
        double ox = src->x - base.x, oz = src->z - base.z;
        double l  = std::sqrt(ox * ox + oz * oz);
        if (l > 1e-9) ox /= l, oz /= l;
        Vec3 centre{float(base.x + 0.55 * ox), base.y, float(base.z + 0.55 * oz)};
        for (int i = 0; i < 16; ++i) {
            double h  = r.nextDouble() - 0.5;
            double dx = 0.85 * oz * h, dy = 0.85 * (r.nextDouble() - 0.5), dz = -0.85 * ox * h;
            particle(lich, "twilightforest:shield_break", Vec3{float(centre.x + dx), float(centre.y + dy), float(centre.z + dz)},
                     Vec3{float(0.5 * dx), float(0.5 * dy), float(0.5 * dz)});
        }
    } else {
        for (int i = 0; i < 16; ++i) {
            double dx = r.nextDouble() - 0.5, dy = 0.25 * (r.nextDouble() - 0.5), dz = r.nextDouble() - 0.5;
            particle(lich, "twilightforest:shield_break", Vec3{float(base.x + dx), float(base.y + dy), float(base.z + dz)},
                     Vec3{float(0.33 * dx), float(0.33 * dy), float(0.33 * dz)});
        }
    }
}

// ---- teleporting -------------------------------------------------------------------------------------

void teleportToNoChecks(Mob& lich, Vec3 const& dest) {
    lichTeleportParticles(lich, false);
    teleportTo(lich, dest);
    playSound(lich, "entity.twilightforest.lich.teleport", 0.75F, 0.75F);
    setTeleportInvisibility(lich, 20);
    lich.stopFire();
}

void setScepterTime(Mob& lich) {
    auto* s            = lichState(lich);
    s->heldScepterTime = 20 + randomOf(lich).nextInt(20);
    setMainHand(lich, kLifedrainScepter);
}

/// The popping / absorbing shared by LichPopMobsGoal and LichAbsorbMinionsGoal.
void popMob(Mob& lich, Mob& mob, bool absorb) {
    float minionHealth = health(mob);
    auto  eye          = eyeOf(mob);
    auto  pos          = mob.getFeetPos();
    auto  deathSound   = deathSoundOf(mob);
    float mobPitch     = voicePitch(mob);
    animateTargetShatter(lich, mob);
    discard(mob);
    forgetMinionCount(lich);
    if (!deathSound.empty()) playSoundAt(lich, pos, deathSound, 1.0F, mobPitch);
    auto& r = randomOf(lich);
    playSound(lich, "entity.twilightforest.lich.pop_mob", 3.0F, 0.4F + r.nextFloat() * 0.2F);
    playSoundAt(lich, pos, "entity.twilightforest.lich.pop_mob", 3.0F, 0.4F + r.nextFloat() * 0.2F);
    makeMagicTrail(lich, eye, eyeOf(lich), 1.0F, 0.5F, 0.5F);
    heal(lich, absorb ? minionHealth : 2.0F);
    swing(lich);
    lichState(lich)->popCooldown = 40;
}

AABB inflated(Actor& a, double h, double v) {
    auto const& b = a.getAABB();
    return AABB{Vec3{float(b.min.x - h), float(b.min.y - v), float(b.min.z - h)}, Vec3{float(b.max.x + h), float(b.max.y + v), float(b.max.z + h)}};
}

/// new AABB(x, y, z, x + 1, y + 1, z + 1).inflate(32, 16, 32).
AABB unitBoxInflated(Actor& a) {
    auto p = a.getFeetPos();
    return AABB{Vec3{p.x - 32.0f, p.y - 16.0f, p.z - 32.0f}, Vec3{p.x + 33.0f, p.y + 17.0f, p.z + 33.0f}};
}

// ---- goals ------------------------------------------------------------------------------------------

using Goal = ge::Goal;

/// AttemptToGoHomeGoal(1.25) as Lich overrides it.
struct GoHomeGoal final : Goal {
    Mob& m;
    bool pathOk = false;
    explicit GoHomeGoal(Mob& mob) : m(mob) { flags = Move; }
    bool canUse() override { return homePos(m) && isOutsideHomeRange(m, m.getFeetPos()); }
    bool requiresUpdateEveryTick() override { return true; }
    void start() override {
        auto h = homePos(m);
        pathOk = h && moveTo(m, Vec3{h->x + 0.5f, float(h->y), h->z + 0.5f}, 1.25);
    }
    void tick() override {
        auto* s = lichState(m);
        if (s->tpInvisibility > 0) return;
        if (!pathOk || navigationDone(m)) {
            if (!teleportToNewTarget(m, targetOf(m), 20.0F, false)) teleportHome(m);
        }
    }
};

struct PopMobsGoal final : Goal {
    Mob& m;
    explicit PopMobsGoal(Mob& mob) : m(mob) { flags = Look | Move; }
    bool requiresUpdateEveryTick() override { return true; }
    std::vector<Mob*> poppables(bool needSight) {
        std::vector<Mob*> out;
        for (auto* a : actorsIn(m.getDimensionBlockSource(), dimOf(m), inflated(m, 32.0, 16.0))) {
            if (a == &m || a->isPlayer() || !a->isAlive()) continue;
            auto* mob = asMob(*a);
            if (!mob || !entityTypeIn("twilightforest:lich_poppables", a->getTypeName())) continue;
            if (needSight && !hasLineOfSight(m, *mob)) continue;
            out.push_back(mob);
        }
        return out;
    }
    bool canUse() override {
        auto* s = lichState(m);
        return !isShadowClone(m) && health(m) < maxHealth(m) && s->popCooldown == 0 && !poppables(true).empty();
    }
    void start() override { setScepterTime(m); }
    void stop() override { lichState(m)->heldScepterTime = 0; }
    void tick() override {
        auto* s = lichState(m);
        if (s->tpInvisibility > 0 || s->heldScepterTime > 0) return;
        for (auto* mob : poppables(false)) {
            if (!hasLineOfSight(m, *mob)) continue;
            popMob(m, *mob, false);
            break;
        }
    }
};

struct AbsorbMinionsGoal final : Goal {
    Mob& m;
    explicit AbsorbMinionsGoal(Mob& mob) : m(mob) { flags = Look | Move; }
    bool canUse() override { return health(m) < maxHealth(m) / 2 && countMyMinions(m) > 0; }
    void start() override { setScepterTime(m); }
    void stop() override { lichState(m)->heldScepterTime = 0; }
    void tick() override {
        auto* s = lichState(m);
        if (s->tpInvisibility > 0 || s->heldScepterTime > 0) return;
        for (auto* a : actorsIn(m.getDimensionBlockSource(), dimOf(m), inflated(m, 32.0, 16.0))) {
            if (!isType(*a, kMinion) || minionMaster(*a) != &m) continue;
            if (auto* mob = asMob(*a)) {
                popMob(m, *mob, true);
                break;
            }
        }
    }
};

void attackWith(Mob& lich) {
    auto* s = lichState(lich);
    launchProjectile(lich, s->attackType);
    swing(lich);
    s->attackType     = randomOf(lich).nextInt(3) > 0 ? 0 : 1;
    s->attackCooldown = 100;
}

/// LichShadowsGoal (phase 1).
struct ShadowsGoal final : Goal {
    Mob&  m;
    float range;
    ShadowsGoal(Mob& mob, float attackRange) : m(mob), range(attackRange) { flags = Look | Move; }
    bool canUse() override { return phaseOf(m) == 1 && lichState(m)->tickCount > 20 && targetOf(m) != nullptr; }
    void start() override { setMainHand(m, kTwilightScepter); }
    void stop() override { despawnClones(m); }
    void tick() override {
        auto* s = lichState(m);
        if (!isShadowClone(m)) {
            for (auto* clone : clonesOf(m)) {
                auto* cs           = lichState(*clone);
                cs->attackCooldown = s->attackCooldown;
                setTeleportInvisibility(*clone, s->tpInvisibility);
            }
        }
        if (s->tpInvisibility > 0) return;
        if (!isShadowClone(m)) {
            Actor* target = targetOf(m);
            if (s->attackCooldown == 60) {
                if (!teleportToNewTarget(m, target, range, true)) teleportHome(m);
            } else if (target && s->attackCooldown == 0) {
                if (distance(m, *target) < range) {
                    attackWith(m);
                    for (auto* clone : clonesOf(m)) {
                        setTarget(*clone, targetOf(m));
                        attackWith(*clone);
                    }
                }
            }
        } else checkForMaster();
    }
    void checkForMaster() {
        if (!masterOf(m)) findNewMaster();
        Mob* master = masterOf(m);
        if (!master || !master->isAlive() || phaseOf(*master) != 1) discardLater(m);
    }
    void findNewMaster() {
        for (auto* a : actorsIn(m.getDimensionBlockSource(), dimOf(m), unitBoxInflated(m))) {
            if (!isLich(*a) || a == &m) continue;
            auto* other = asMob(*a);
            if (!other || isShadowClone(*other)) continue;
            // Lich#wantsNewClone
            if (countMyClones(*other) < lichState(*other)->cloneCount) {
                lichState(m)->master = uidOf(*other);
                lichState(*other)->clones.push_back(uidOf(m));
                setTarget(m, targetOf(*other));
                break;
            }
        }
    }
};

/// LichMinionsGoal (phase 2).
struct MinionsGoal final : Goal {
    static constexpr float kRange = 20.0F;
    Mob& m;
    int  seeTime = 0;
    bool clockwise = false, backwards = false;
    int  strafingTime = -1;
    explicit MinionsGoal(Mob& mob) : m(mob) { flags = Move | Look; }
    bool canUse() override { return phaseOf(m) == 2 && !isShadowClone(m); }
    void start() override { setMainHand(m, kZombieScepter); }
    void stop() override { seeTime = 0; }
    void checkAndSpawnMinions() { lich::checkAndSpawnMinions(m); }
    void tick() override {
        auto* s = lichState(m);
        if (s->tpInvisibility > 0) return;
        Actor* target = targetOf(m);
        if (!target) return;
        float dist = static_cast<float>(distance(m, *target));
        if (s->attackCooldown % 15 == 0) checkAndSpawnMinions();
        bool sight = hasLineOfSight(m, *target);
        if (sight != (seeTime > 0)) seeTime = 0;
        if (sight) ++seeTime;
        else --seeTime;
        if (dist < kRange && seeTime >= 20) {
            stopNavigation(m);
            ++strafingTime;
        } else strafingTime = -1;
        auto& r = randomOf(m);
        if (strafingTime >= 20) {
            if (r.nextFloat() < 0.3) clockwise = !clockwise;
            if (r.nextFloat() < 0.3) backwards = !backwards;
            strafingTime = 0;
        }
        if (strafingTime > -1) {
            if (dist > kRange * 0.75F) backwards = false;
            else if (dist < kRange * 0.25F) backwards = true;
            // MoveControl#strafe(forward, sideways): a short step relative to the facing
            auto   p   = m.getFeetPos();
            auto   tp  = target->getFeetPos();
            double dx  = tp.x - p.x, dz = tp.z - p.z;
            double len = std::sqrt(dx * dx + dz * dz);
            if (len > 1e-6) {
                dx /= len, dz /= len;
                double f = backwards ? -0.5 : 0.5, side = clockwise ? 0.5 : -0.5;
                Vec3   step{float(p.x + (dx * f - dz * side) * 2.0), p.y, float(p.z + (dz * f + dx * side) * 2.0)};
                moveTo(m, step, 1.0);
            }
            lookAt(m, *target, 30.0F, 30.0F);
        } else lookAt(m, *target, 30.0F, 30.0F);
        if (s->attackCooldown == 0) {
            if (dist < 2.0F) {
                doHurtTarget(m, *target);
                swing(m);
                s->attackCooldown = 20;
            } else if (dist < kRange && hasLineOfSight(m, *target)) {
                launchProjectile(m, s->attackType);
                swing(m);
                s->attackType     = r.nextBoolean() ? 0 : 1;
                s->attackCooldown = 60;
            } else {
                teleportToNewTarget(m, target, kRange, false);
                s->attackCooldown = 20;
            }
        }
    }
};

/// MeleeAttackGoal(0.75, true) as Lich overrides it (phase 3).
struct MeleeGoal final : Goal {
    Mob& m;
    int  ticksUntilNextAttack = 0;
    int  ticksUntilRepath     = 0;
    explicit MeleeGoal(Mob& mob) : m(mob) { flags = Move | Look; }
    bool canUse() override {
        Actor* t = targetOf(m);
        return phaseOf(m) == 3 && t && t->isAlive();
    }
    bool canContinueToUse() override {
        Actor* t = targetOf(m);
        if (!t || !t->isAlive()) return false;
        if (t->isPlayer() && (static_cast<Player*>(t)->isCreative() || t->isSpectator())) return false;
        return true; // followingTargetEvenIfNotSeen
    }
    void start() override {
        if (Actor* t = targetOf(m)) moveTo(m, t->getFeetPos(), 0.75);
        ticksUntilRepath     = 0;
        ticksUntilNextAttack = 0;
        setMainHand(m, kGoldenSword);
    }
    void stop() override { stopNavigation(m); }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        if (lichState(m)->tpInvisibility > 0) return;
        Actor* t = targetOf(m);
        if (!t) return;
        lookAt(m, *t, 30.0F, 30.0F);
        ticksUntilRepath = std::max(ticksUntilRepath - 1, 0);
        if (ticksUntilRepath <= 0) {
            ticksUntilRepath = 4 + randomOf(m).nextInt(7);
            double d         = distSqr(m.getFeetPos(), t->getFeetPos());
            if (d > 1024.0) ticksUntilRepath += 10;
            else if (d > 256.0) ticksUntilRepath += 5;
            if (!moveTo(m, t->getFeetPos(), 0.75)) ticksUntilRepath += 15;
        }
        ticksUntilNextAttack = std::max(ticksUntilNextAttack - 1, 0);
        if (withinMeleeRange(m, *t) && ticksUntilNextAttack <= 0 && hasLineOfSight(m, *t)) {
            ticksUntilNextAttack = 20;
            swing(m);
            doHurtTarget(m, *t);
        }
        // the Lich's addition: out of reach with the path done and no new path -> teleport
        if (!withinMeleeRange(m, *t) && navigationDone(m)) {
            if (!moveTo(m, t->getFeetPos(), 0.75)) teleportToSightOfEntity(m, t);
        }
    }
};

/// HurtByTargetGoal: target the last attacker (not a Lich of the same master).
struct HurtByTargetGoal final : Goal {
    Mob& m;
    int  timestamp = 0;
    explicit HurtByTargetGoal(Mob& mob) : m(mob) { flags = Target; }
    bool canUse() override {
        int    ts = m.mLastHurtByMobTimestamp;
        Actor* by = lastHurtBy(m);
        if (!by || ts == timestamp) return false;
        if (isLich(*by)) {
            auto* other = asMob(*by);
            if (other && masterOf(*other) == masterOf(m)) return false;
        }
        if (by->isPlayer() && (static_cast<Player*>(by)->isCreative() || by->isSpectator())) return false;
        return true;
    }
    bool canContinueToUse() override {
        Actor* t = targetOf(m);
        return t && t->isAlive();
    }
    void start() override {
        setTarget(m, lastHurtBy(m));
        timestamp = m.mLastHurtByMobTimestamp;
    }
};

/// NearestAttackableTargetGoal(Player, mustSee false).
struct NearestPlayerTargetGoal final : Goal {
    Mob&   m;
    double followRange;
    explicit NearestPlayerTargetGoal(Mob& mob, double range) : m(mob), followRange(range) { flags = Target; }
    Player* find() {
        Player* best = nullptr;
        double  bd   = 1e300;
        for (auto* p : playersNear(dimOf(m), m.getFeetPos(), followRange)) {
            if (p->isCreative() || p->isSpectator()) continue;
            double d = distSqr(p->getFeetPos(), m.getFeetPos());
            if (d < bd) bd = d, best = p;
        }
        return best;
    }
    bool canUse() override {
        if (randomOf(m).nextInt(10) != 0) return false; // reducedTickDelay(10)
        return find() != nullptr;
    }
    bool canContinueToUse() override {
        Actor* t = targetOf(m);
        if (!t || !t->isAlive()) return false;
        if (t->isPlayer() && (static_cast<Player*>(t)->isCreative() || t->isSpectator())) return false;
        return distSqr(t->getFeetPos(), m.getFeetPos()) <= followRange * followRange;
    }
    void start() override {
        if (auto* p = find()) setTarget(m, p);
    }
    void stop() override { setTarget(m, nullptr); }
};

void addGoals(Mob& m) {
    ge::addGoal(m, 0, std::make_shared<GoHomeGoal>(m));
    ge::addGoal(m, 1, ge::goals::randomLookIfBored(m));
    ge::addGoal(m, 1, ge::goals::alwaysWatchTarget(m));
    ge::addGoal(m, 1, std::make_shared<PopMobsGoal>(m));
    ge::addGoal(m, 1, std::make_shared<AbsorbMinionsGoal>(m));
    ge::addGoal(m, 2, std::make_shared<ShadowsGoal>(m, 30.0F));
    ge::addGoal(m, 3, std::make_shared<MinionsGoal>(m));
    ge::addGoal(m, 4, std::make_shared<MeleeGoal>(m));
    ge::addTargetGoal(m, 1, std::make_shared<HurtByTargetGoal>(m));
    ge::addTargetGoal(m, 2, std::make_shared<NearestPlayerTargetGoal>(m, 35.0));
}

// ---- synced state to the clients (entity properties) -------------------------------------------------

void syncProperties(Mob& m, LichState& s) {
    int  ph    = phaseOf(m);
    int  sh    = shieldOf(m);
    int  clone = s.master != 0 ? 1 : 0;
    if (ph != s.lastPropPhase) ge::setProperty(m, "twilightforest:phase", ph), s.lastPropPhase = ph;
    if (sh != s.lastPropShield) ge::setProperty(m, "twilightforest:shield", sh), s.lastPropShield = sh;
    if (clone != s.lastPropClone) ge::setProperty(m, "twilightforest:clone", clone != 0), s.lastPropClone = clone;
    // Lich#isInvisible: teleport invisibility, or a shadow clone in its first 10 ticks
    bool invisible = s.tpInvisibility > 0 || (clone && s.tickCount <= 10);
    if (invisible != s.lastInvisible) {
        m.setInvisible(invisible);
        s.lastInvisible = invisible;
    }
}

// ---- the behaviour ----------------------------------------------------------------------------------

void onSpawn(Actor& a) {
    auto* m = self(a);
    if (!m) return;
    auto* s = lichState(a);
    if (!s->goalsAdded) {
        addGoals(*m);
        s->goalsAdded = true;
    }
    auto& init = ge::data(a, "lich:init");
    if (!init.is_boolean()) {
        init = true;
        // finalizeSpawn (not a clone): the fortification scepter, its sound, a swing
        if (!isShadowClone(*m)) {
            setMainHand(*m, kFortificationScepter);
            playSound(*m, "entity.twilightforest.fortification_shield.add", 1.5F, voicePitch(*m));
            swing(*m);
        }
        // LichSpawnerBlockEntity#spawnMyBoss: attackCooldown 40, setExtinguishTimer
        auto const& sp = ge::data(a, "boss:spawner");
        if (sp.is_string() && sp.get<std::string>() == kSpawner) {
            s->attackCooldown = 40;
            setExtinguishTimer(*m);
        }
    }
    if (isShadowClone(*m)) gb::bar(*m).setVisible(false);
    s->lastPropPhase = s->lastPropShield = s->lastPropClone = -1;
    syncProperties(*m, *s);
}

/// Lich#deflection for projectiles whose hit the unit does not run itself: U9's wand bolt (a member
/// of #twilightforest:lich_deflects_phase_2 like the lich bolt and bomb, whose own hits call
/// deflect). Java deflects in Projectile#hitTargetOrDeflectSelf before the hit; here a wand bolt
/// whose path of the next two ticks crosses the Lich's box is deflected now, before its hit.
void deflectIncoming(Mob& m) {
    auto  p   = m.getFeetPos();
    AABB  box{Vec3{p.x - 6.0f, p.y - 6.0f, p.z - 6.0f}, Vec3{p.x + 6.0f, p.y + 8.0f, p.z + 6.0f}};
    auto  aabb = m.getAABB();
    for (auto* a : actorsIn(m.getDimensionBlockSource(), dimOf(m), box)) {
        if (!a || !isType(*a, kWandBolt) || !a->isAlive()) continue;
        auto pos = a->getPosition();
        auto v   = gf::projectile::ext::velocity(*a);
        bool hits = false;
        for (int i = 1; i <= 16 && !hits; ++i) {
            float k = static_cast<float>(i) / 8.0F; // two ticks ahead
            Vec3  q{pos.x + v.x * k, pos.y + v.y * k, pos.z + v.z * k};
            hits = q.x >= aabb.min.x - 0.3f && q.x <= aabb.max.x + 0.3f && q.y >= aabb.min.y - 0.3f && q.y <= aabb.max.y + 0.3f && q.z >= aabb.min.z - 0.3f
                && q.z <= aabb.max.z + 0.3f;
        }
        if (hits && deflect(m, *a, projectileOwner(*a))) log().info("lich: deflected {}", a->getTypeName());
    }
}

/// Lich#customServerAiStep.
void serverStep(Actor& a) {
    auto* m = self(a);
    if (!m || !m->isAlive()) return;
    auto* s = lichState(a);
    s->tickCount++;
    if (isOutsideHomeRange(*m, m->getFeetPos()) && s->tpInvisibility <= 0) teleportHome(*m);
    if (s->attackCooldown > 0 && s->spawnTime <= 0) s->attackCooldown--;
    if (s->popCooldown > 0 && health(*m) < maxHealth(*m) && s->heldScepterTime <= 0) s->popCooldown--;
    if (s->heldScepterTime == 0 && s->popCooldown < 30 && mainHandId(*m) == kLifedrainScepter)
        setMainHand(*m, phaseOf(*m) == 2 ? kZombieScepter : kGoldenSword);
    if (s->heldScepterTime > 0) s->heldScepterTime--;
    if (targetOf(*m)) {
        if (s->spawnTime > 0 && s->spawnTime-- < 40) turnNearbyCandles(*m, 40 - s->spawnTime, !(s->spawnTime - 1 > 0 && s->spawnTime - 1 < 40));
    }
    // tickBossBar: hidden for clones, the style per phase
    int phase = phaseOf(*m);
    gb::bar(*m).setVisible(!isShadowClone(*m));
    if (phase != s->previousPhase) {
        unsigned rgb = shieldOf(*m) > 0 ? 0xFFD800u : (phase == 2 ? 0xBE23FFu : 0xFF0000u);
        auto     ov  = shieldOf(*m) > 0 ? gb::ext::Overlay::Notched6 : gb::ext::Overlay::Progress;
        gb::ext::setBarStyle(*m, rgb, ov, s->previousPhase != 1);
    }
    s->previousPhase = phase;
    syncProperties(*m, *s);
    if (phase > 1) deflectIncoming(*m);
}

/// Lich#aiStep.
void aiStep(Actor& a) {
    auto* m = self(a);
    if (!m) return;
    auto* s = lichState(a);
    if (s->tpInvisibility > 0) {
        if (Actor* t = targetOf(*m)) lookAt(*m, *t, 10.0F, 40.0F);
        setTeleportInvisibility(*m, s->tpInvisibility - 1);
        if (s->tpInvisibility <= 0) {
            lichTeleportParticles(*m, true);
            playSound(*m, "entity.twilightforest.lich.teleport", 1.125F, 1.125F);
        }
        syncProperties(*m, *s);
        return;
    }
    if (!m->isAlive()) return;
    auto& r = randomOf(*m);
    if (phaseOf(*m) == 3) {
        auto p = m->getFeetPos();
        Vec3 at{float(p.x + (r.nextDouble() * 2.0 - 1.0) * bbWidth(*m) * 0.65), float(eyeOf(*m).y + 0.25 + r.nextFloat() * 0.5),
                float(p.z + (r.nextDouble() * 2.0 - 1.0) * bbWidth(*m) * 0.65)};
        particle(*m, "twilightforest:angry_lich", at, Vec3{float(r.nextGaussian() * 0.02), float(r.nextGaussian() * 0.02), float(r.nextGaussian() * 0.02)});
        return;
    }
    // the hand particles
    float  angle = m->getYBodyRotation() * std::numbers::pi_v<float> / 180.0F;
    auto   p     = m->getFeetPos();
    double dx    = p.x + std::cos(angle) * 0.65;
    double dy    = p.y + bbHeight(*m) * 0.94;
    double dz    = p.z + std::sin(angle) * 0.65;
    int    factor    = (80 - s->attackCooldown) / 10;
    int    particles = factor > 0 ? r.nextInt(factor) : 1;
    for (int j = 0; j < particles; ++j) {
        float sparkle = 1.0F - (s->attackCooldown + 1.0F) / 60.0F;
        sparkle *= sparkle;
        float red = 0.37F * sparkle, grn = 0.99F * sparkle, blu = 0.89F * sparkle;
        if (s->attackType != 0) red = 0.99F * sparkle, grn = 0.47F * sparkle, blu = 0.0F;
        particle(*m, "twilightforest:magic_effect",
                 Vec3{float(dx + r.nextGaussian() * 0.025), float(dy + r.nextGaussian() * 0.025), float(dz + r.nextGaussian() * 0.025)}, {},
                 Json{{"color", {red, grn, blu}}});
    }
}

/// Lich#hurt (the damage pipeline's view). Returns false to cancel.
bool hurt(Mob& m, gf::combat::DamageInfo& d) {
    auto* s      = lichState(m);
    bool  bypass = bypassesInvulnerability(d.type);
    if (s->tpInvisibility > 0 && !bypass) return false;
    if (d.type == "minecraft:in_wall" && targetOf(m)) teleportToNewTarget(m, targetOf(m), 20.0F, false);
    if (isShadowClone(m) && !bypass) {
        playSound(m, "entity.twilightforest.lich_clone.hurt", 1.0F, voicePitch(m) * 2.0F);
        return false;
    }
    if (d.attacker && isLich(*d.attacker)) return false;
    if (!bypass && shieldOf(m) > 0) {
        if (damageTypeIn("twilightforest:breaks_lich_shields", d.type) && d.amount > 2) {
            int ns = shieldOf(m) - 1;
            setShield(m, ns);
            float volume = 1.5F;
            addShieldBreakParticles(m, d.direct, d.attacker);
            if (ns < 6) volume += 0.25F * (6 - ns);
            if (ns == 0) volume += 0.5F;
            playSound(m, "entity.twilightforest.fortification_shield.break", volume, voicePitch(m) * 1.25F);
            log().info("lich: shield broken by {} ({} left)", d.type, ns);
        } else {
            playSound(m, "entity.twilightforest.fortification_shield.block", 0.75F, voicePitch(m) * 1.75F);
            if (d.attacker && (d.attacker->isPlayer() || d.attacker->hasCategory(ActorCategory::Mob))) setLastHurtBy(m, d.attacker);
        }
        return false;
    }
    return true;
}

/// The rest of Lich#hurt once the damage applied.
void dealt(Mob& m) {
    auto* s     = lichState(m);
    int   bound = phaseOf(m) == 3 ? 6 : 3;
    if (randomOf(m).nextInt(bound) <= s->hitsWithoutTeleport++ && m.isAlive() && health(m) > 0) {
        s->hitsWithoutTeleport = 0;
        teleportToNewTarget(m, targetOf(m), 20.0F, false);
    }
}

void die(Actor& a, std::string const&, Actor*) {
    auto* m = self(a);
    if (!m || isShadowClone(*m)) return;
    despawnClones(*m);
    if (shieldOf(*m) > 0) {
        setShield(*m, 0);
        playSound(*m, "entity.twilightforest.fortification_shield.break", 1.2F, voicePitch(*m) * 2.0F);
    }
}

void save(Actor& a, Json& out) {
    auto* s = lichState(a);
    if (s->master) out["MasterLich"] = s->master;
    if (!s->clones.empty()) out["SummonedClones"] = s->clones;
    out["ShieldStrength"]      = s->shield;
    out["MinionsToSummon"]     = s->minionsLeft;
    out["BabyMinionsSummoned"] = s->babies;
    out["HitsWithoutTeleport"] = s->hitsWithoutTeleport;
    out["clone_count"]         = s->cloneCount;
    out["minion_count"]        = s->minionCount;
}

void load(Actor& a, Json const& in) {
    auto* s                = lichState(a);
    s->master              = in.value("MasterLich", std::int64_t{0});
    s->clones              = in.value("SummonedClones", std::vector<std::int64_t>{});
    s->shield              = in.value("ShieldStrength", 0);
    s->minionsLeft         = in.value("MinionsToSummon", 0);
    s->babies              = in.value("BabyMinionsSummoned", 0);
    s->hitsWithoutTeleport = in.value("HitsWithoutTeleport", 0);
    s->cloneCount          = in.value("clone_count", 2);
    s->minionCount         = in.value("minion_count", 9);
}

// ---- death animation (Lich#tickDeathAnimation / #tickDeath) ------------------------------------------

Actor* lookAtUponDeath(Mob& m) {
    if (Actor* t = targetOf(m)) return t;
    if (auto* p = actorById(m.mLastHurtByPlayerId->rawID)) return p;
    return nearestPlayer(dimOf(m), m.getFeetPos(), 20.0);
}

float wrapDegrees(float v) {
    v = std::fmod(v, 360.0F);
    if (v >= 180.0F) v -= 360.0F;
    if (v < -180.0F) v += 360.0F;
    return v;
}

void tickDeath(Mob& m, int deathTime) {
    ge::setProperty(m, "twilightforest:death_time", std::min(deathTime, kDeathDuration));
    if (deathTime >= kPointA) setMainHand(m, "");
    else if (Actor* t = lookAtUponDeath(m)) {
        auto  p = m.getFeetPos(), q = t->getFeetPos();
        float f = static_cast<float>(std::atan2(q.z - p.z, q.x - p.x) * 180.0 / 3.1415927410125732) - 90.0F;
        float h = m.getYHeadRot();
        float d = std::clamp(wrapDegrees(f - h), -30.0F, 30.0F);
        m.setYHeadRot(h + d);
    }
    if (isShadowClone(m)) return;
    auto& r   = randomOf(m);
    auto  pos = m.getFeetPos();
    float h   = bbHeight(m);
    Json  bone{{"item", "minecraft:bone"}};
    auto  emit = [&](std::string const& id, double x, double y, double z, Vec3 v = {}, Json const& params = {}) {
        particle(m, id, Vec3{float(x), float(y), float(z)}, v, params);
    };
    if (deathTime <= kPointA) {
        bool done = deathTime == kPointA;
        bool hit  = deathTime % kBurstCooldown == 0;
        if (done) playSound(m, "entity.twilightforest.lich.death", 1.0F, voicePitch(m));
        else if (hit) playSound(m, "entity.twilightforest.lich.hurt", 1.0F, voicePitch(m));
        for (int i = 0; i < (hit ? 12 : 3); ++i) {
            double x = (r.nextDouble() - 0.5) * 0.7, y = r.nextDouble() * h, z = (r.nextDouble() - 0.5) * 0.7;
            if (r.nextBoolean() || hit) emit("minecraft:item", pos.x + x, pos.y + y, pos.z + z, {}, bone);
            else emit("minecraft:smoke", pos.x + x, pos.y + y, pos.z + z);
        }
        if (hit) {
            double x = (r.nextDouble() - 0.5) * 0.7, y = r.nextDouble() * h, z = (r.nextDouble() - 0.5) * 0.7;
            for (int i = 0; i < 7; ++i) {
                double x1 = x + (r.nextDouble() - 0.5) * 0.1, y1 = y + (r.nextDouble() - 0.5) * 0.1, z1 = z + (r.nextDouble() - 0.5) * 0.1;
                if (r.nextBoolean()) emit("minecraft:item", pos.x + x1, pos.y + y1, pos.z + z1, {}, bone);
                else emit("minecraft:cloud", pos.x + x1, pos.y + y1, pos.z + z1);
            }
            Vec3 added{pos.x, pos.y + h * 0.5f, pos.z};
            for (int i = 0; i < (done ? 18 : 6); ++i) {
                double x1 = getX(m, r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0));
                double y1 = getY(m, r.nextDouble());
                double z1 = getZ(m, r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0));
                emit("minecraft:smoke", x1, y1, z1, Vec3{float((x1 - added.x) * 0.0125), float((y1 - added.y) * 0.0125), float((z1 - added.z) * 0.0125)});
            }
        }
        if (done) {
            for (int i = 0; i < 32; ++i) {
                double x = (r.nextDouble() - 0.5) * 0.7, y = r.nextDouble() * h, z = (r.nextDouble() - 0.5) * 0.7;
                if (r.nextBoolean()) emit("minecraft:item", pos.x + x, pos.y + y, pos.z + z, {}, bone);
                else emit("minecraft:cloud", pos.x + x, pos.y + y, pos.z + z);
            }
        }
    } else if (deathTime == kPointB) {
        for (int i = 0; i < 3; ++i) {
            double x = (r.nextDouble() - 0.5) * 0.75, z = (r.nextDouble() - 0.5) * 0.75;
            emit("minecraft:cloud", pos.x + x, pos.y, pos.z + z);
        }
    } else if (deathTime > kPointC) {
        Vec3     start{pos.x, pos.y + 0.45f, pos.z};
        BlockPos chest = homePos(m) ? BlockPos{homePos(m)->x, homePos(m)->y - 1, homePos(m)->z} : game::blockPosOf(pos);
        Vec3     end{chest.x + 0.5f, chest.y + 0.5f, chest.z + 0.5f};
        double   factor       = double(deathTime - kPointC) / double(kDeathDuration - kPointC);
        double   powFactor    = factor * factor * 2.0;
        double   expandFactor = (std::cos((factor + 0.5) * std::numbers::pi * 2) + 1.0) * 0.5;
        double   k            = std::min(factor * 2.0, 1.0);
        Vec3     pp{float(start.x + (end.x - start.x) * k), float(start.y + (end.y - start.y) * k), float(start.z + (end.z - start.z) * k)};
        for (double i = 0.0; i < 1.0; i += 0.2) {
            double x = std::sin((powFactor + i) * std::numbers::pi * 2.0) * expandFactor * 1.75;
            double z = std::cos((powFactor + i) * std::numbers::pi * 2.0) * expandFactor * 1.75;
            emit("twilightforest:ominous_flame", pp.x + x, pp.y - 0.25, pp.z + z);
        }
    }
    if (deathTime > kPointB && r.nextFloat() <= 0.33F) {
        double x = (r.nextDouble() - 0.5) * 0.25, y = r.nextDouble() * h * 0.1, z = (r.nextDouble() - 0.5) * 0.25;
        emit("minecraft:smoke", pos.x + x, pos.y + 0.15 + y, pos.z + z);
    }
}

bool gameRuleDoMobLoot() {
    auto level = ll::service::getLevel();
    if (!level) return true;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::DoMobLoot);
    return level->getGameRules().getBool(id, true);
}

// ---- candles -----------------------------------------------------------------------------------------

constexpr char const* kColors[] = {"",     "white_", "orange_", "magenta_", "light_blue_", "yellow_", "lime_", "pink_", "gray_",
                                   "light_gray_", "cyan_", "purple_", "blue_", "brown_", "green_", "red_", "black_"};

/// OminousCandleBlock.CANDLE_MAP: the vanilla candle -> the ominous one ("" when none).
std::string ominousOf(std::string const& id) {
    for (auto const* c : kColors)
        if (id == std::string("minecraft:") + c + "candle") return std::string("twilightforest:ominous_") + c + "candle";
    return {};
}

std::string vanillaOf(std::string const& id) {
    for (auto const* c : kColors)
        if (id == std::string("twilightforest:ominous_") + c + "candle") return std::string("minecraft:") + c + "candle";
    return {};
}

Json statesJson(Block const& block) {
    Json j = Json::object();
    for (auto const& [k, v] : game::statesOf(block)) {
        if (v == "true" || v == "false") j[k] = v == "true";
        else if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-')) j[k] = std::stoi(v);
        else j[k] = v;
    }
    return j;
}

} // namespace

// ---- the public operations ------------------------------------------------------------------------------

LichState* lichState(Actor const& actor) {
    if (!isType(actor, kLich)) return nullptr;
    auto [it, created] = states().try_emplace(uidOf(actor));
    if (created) {
        // the map is a cache: a Lich whose record was dropped (unloaded, pruned) gets its saved
        // fields back from its document (Behaviour::save's last write)
        auto const& saved = ge::data(const_cast<Actor&>(actor), "entity:save");
        if (saved.is_object() && !saved.empty()) load(const_cast<Actor&>(actor), saved);
    }
    return &it->second;
}

bool isLich(Actor const& actor) { return isType(actor, kLich); }

bool isShadowClone(Mob& lich) {
    auto* s = lichState(lich);
    return s && s->master != 0;
}

int shieldOf(Mob& lich) {
    auto* s = lichState(lich);
    return !s || s->master != 0 ? 0 : s->shield;
}

int phaseOf(Mob& lich) {
    auto* s = lichState(lich);
    if (!s) return 1;
    if (s->master != 0 || s->shield > 0) return 1;
    if (s->minionsLeft > 0 || countMyMinions(lich) > 0) return 2;
    return 3;
}

Mob* masterOf(Mob& lich) {
    auto* s = lichState(lich);
    if (!s || !s->master) return nullptr;
    auto* m = mobById(s->master);
    return m && isLich(*m) ? m : nullptr;
}

std::vector<Mob*> clonesOf(Mob& lich) {
    std::vector<Mob*> out;
    if (isShadowClone(lich)) return out;
    for (auto id : lichState(lich)->clones)
        if (auto* c = mobById(id); c && isLich(*c) && masterOf(*c) == &lich) out.push_back(c);
    return out;
}

int countMyClones(Mob& lich) {
    int n = 0;
    for (auto id : lichState(lich)->clones)
        if (auto* c = mobById(id); c && isLich(*c) && masterOf(*c) == &lich) ++n;
    return n;
}

int countMyMinions(Mob& lich) {
    // asked many times a tick (getPhase): counted once per tick and Lich
    auto* s     = lichState(lich);
    auto  level = ll::service::getLevel();
    std::uint64_t tick = level ? level->getCurrentServerTick().tickID : 0;
    if (s && s->minionCountTick == tick && tick != 0) return s->minionCountCache;
    int  n = 0;
    auto p = lich.getFeetPos();
    AABB box{Vec3{p.x - 32.0f, p.y - 16.0f, p.z - 32.0f}, Vec3{p.x + 33.0f, p.y + 17.0f, p.z + 33.0f}};
    for (auto* a : actorsIn(lich.getDimensionBlockSource(), dimOf(lich), box))
        if (isType(*a, kMinion) && a->isAlive() && minionMaster(*a) == &lich) ++n;
    if (s) s->minionCountTick = tick, s->minionCountCache = n;
    return n;
}

void forgetMinionCount(Mob& lich) {
    if (auto* s = lichState(lich)) s->minionCountTick = 0;
}

bool wantsNewMinion(Mob& lich) { return countMyMinions(lich) < kMaxActiveMinions; }

void checkAndSpawnMinions(Mob& m) {
    auto* s = lichState(m);
    if (!s || s->minionsLeft <= 0) return;
    if (countMyMinions(m) >= kMaxActiveMinions) return;
    // spawnMinionAt (nothing when findVecInLOSOf finds no spot; the summon is used up anyway)
    Actor* target = targetOf(m);
    if (auto spot = findVecInLOSOf(m, target)) {
        if (Mob* minion = spawnMinion(m, *spot, target)) {
            auto& r = randomOf(m);
            playSound(*minion, "entity.twilightforest.minion.summon", 1.0F, ((r.nextFloat() - r.nextFloat()) * 0.7F + 1.0F) * 0.75F);
            swing(m);
            float cr = r.nextFloat() * 0.0625F + 0.125F, cg = r.nextFloat() * 0.0625F + 0.125F, cb = r.nextFloat() * 0.0625F + 0.125F;
            makeMagicTrail(m, eyeOf(m), eyeOf(*minion), cr, cg, cb);
        }
    }
    setMinionsLeft(m, s->minionsLeft - 1);
}

void makeMagicTrail(Mob& lich, Vec3 const& from, Vec3 const& to, float r, float g, float b) {
    constexpr int n = 60;
    if (playersNear(dimOf(lich), from, 64.0).empty()) return; // distanceToSqr(source) < 4096
    auto& rnd = randomOf(lich);
    Json  params{{"color", {r, g, b}}};
    for (int i = 0; i < n; ++i) {
        double f  = i / (n - 1.0);
        double tx = from.x + (to.x - from.x) * f + rnd.nextGaussian() * 0.005;
        double ty = from.y + 0.2 + (to.y - from.y) * f + rnd.nextGaussian() * 0.005;
        double tz = from.z + (to.z - from.z) * f + rnd.nextGaussian() * 0.005;
        particle(lich, "twilightforest:magic_effect", Vec3{float(tx), float(ty), float(tz)}, {}, params);
    }
}

std::optional<Vec3> findVecInLOSOf(Mob& lich, Actor* target) {
    if (!target) return std::nullopt;
    auto& region = lich.getDimensionBlockSource();
    auto& r      = randomOf(lich);
    auto  tp     = target->getFeetPos();
    float eye    = eyeOf(lich).y - lich.getFeetPos().y;
    for (int i = 0; i < 100; ++i) {
        double tx = tp.x + r.nextGaussian() * 16.0;
        double ty = tp.y + 2;
        double tz = tp.z + r.nextGaussian() * 16.0;
        auto   spot  = randomTeleportSpot(region, Vec3{float(tx), float(ty), float(tz)}, bbWidth(lich), bbHeight(lich));
        bool   clear = spot.has_value();
        bool   sees  = false;
        if (clear) {
            tx = spot->x, ty = spot->y, tz = spot->z;
            Vec3 from{spot->x, spot->y + eye, spot->z};
            sees = distSqr(from, target->getEyePos()) <= 128.0 * 128.0 && clearPath(region, from, target->getEyePos());
        }
        Vec3 tpPos{float(tx), float(ty), float(tz)};
        if (i < 85 && ty < tp.y) continue;
        if (clear && sees && !isOutsideHomeRange(lich, tpPos) && distSqr(tpPos, tp) >= 25.0) return tpPos;
    }
    return std::nullopt;
}

bool teleportToSightOfEntity(Mob& lich, Actor* entity) {
    if (!entity) return false;
    auto dest = findVecInLOSOf(lich, entity);
    if (!dest) return false;
    teleportToNoChecks(lich, *dest);
    lookAt(lich, *entity, 100.0F, 100.0F);
    // yBodyRot = getYRot(): face the entity at once
    auto  p   = lich.getFeetPos(), q = entity->getFeetPos();
    float yaw = static_cast<float>(std::atan2(q.z - p.z, q.x - p.x) * 180.0 / std::numbers::pi) - 90.0F;
    lich.setYBodyRotation(yaw);
    const_cast<Vec2&>(lich.getRotation()) = Vec2{lich.getRotation().x, yaw};
    return true;
}

bool teleportToNewTarget(Mob& lich, Actor* target, float range, bool fromShadows) {
    std::vector<Player*> possible;
    for (auto* p : playersIn(dimOf(lich)))
        if (distance(*p, lich) < range && p->isAlive()) possible.push_back(p);
    if (!possible.empty()) target = possible[randomOf(lich).nextInt(static_cast<int>(possible.size()))];
    if (!target) return false;
    setTarget(lich, target);
    if (!teleportToSightOfEntity(lich, target)) return false;
    for (auto* clone : clonesOf(lich)) {
        setTarget(*clone, target);
        teleportToSightOfEntity(*clone, target);
    }
    if (fromShadows) checkAndSpawnClones(lich, *target);
    return true;
}

void teleportHome(Mob& lich) {
    auto h = homePos(lich);
    if (!h) return;
    auto&    region = lich.getDimensionBlockSource();
    BlockPos pos    = *h;
    BlockPos below2{pos.x, pos.y - 2, pos.z};
    if (region.getBlock(below2).isAir())
        if (auto const* pillar = game::blockWith("twilightforest:bold_stone_pillar")) game::setBlock(region, below2, *pillar);
    if (region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z}).isAir()) pos.y -= 1;
    teleportToNoChecks(lich, Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f});
}

void despawnClones(Mob& lich) {
    auto& r = randomOf(lich);
    for (auto* clone : clonesOf(lich)) {
        for (int j = 0; j < 128; ++j) {
            double x = getX(*clone, (r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0)) * 1.5);
            double y = getY(*clone, r.nextDouble() * r.nextDouble() * 1.25);
            double z = getZ(*clone, (r.nextDouble() * r.nextDouble() * (r.nextBoolean() ? 1.0 : -1.0)) * 1.5);
            particle(lich, "minecraft:smoke", Vec3{float(x), float(y), float(z)});
        }
        discard(*clone);
    }
}

Mob* spawnClone(Mob& master, Vec3 const& pos) {
    Mob* clone = spawnMob(master.getDimensionBlockSource(), kLich, pos, master.getRotation().y);
    if (!clone) return nullptr;
    auto* cs   = lichState(*clone);
    cs->master = uidOf(master);
    ge::data(*clone, "lich:init") = true; // no finalizeSpawn scepter for clones
    if (auto h = ge::home(master)) ge::setHome(*clone, h->pos, h->radius);
    gb::bar(*clone).setVisible(false);
    lichState(master)->clones.push_back(uidOf(*clone));
    return clone;
}

void checkAndSpawnClones(Mob& lich, Actor& target) {
    if (countMyClones(lich) >= lichState(lich)->cloneCount) return;
    auto spot = findVecInLOSOf(lich, &target);
    if (!spot) return;
    Mob* clone = spawnClone(lich, *spot);
    if (!clone) return;
    auto& r = randomOf(lich);
    setTarget(*clone, &target);
    lichState(*clone)->attackCooldown = 60 + r.nextInt(3) - r.nextInt(3);
    setMainHand(*clone, kTwilightScepter);
    setTeleportInvisibility(*clone, lichState(lich)->tpInvisibility);
    log().info("lich: shadow clone {} of {}", uidOf(*clone), uidOf(lich));
}

void setExtinguishTimer(Mob& lich) { lichState(lich)->spawnTime = 40; }

void turnNearbyCandles(Mob& lich, int tick, bool done) {
    auto&    region = lich.getDimensionBlockSource();
    auto&    r      = randomOf(lich);
    BlockPos home   = homeOrElseCurrent(lich);
    int      range  = static_cast<int>(static_cast<float>(tick) / 2.5F + 2);
    int      yRange = static_cast<int>(static_cast<float>(tick) / 2.0F + 2);
    for (int x = home.x - range; x <= home.x + range; ++x)
        for (int y = home.y - 3; y <= home.y + yRange; ++y)
            for (int z = home.z - range; z <= home.z + range; ++z) {
                if (!(r.nextInt(std::max(40 - tick, 2)) == 1 || done)) continue;
                BlockPos     pos{x, y, z};
                Block const& b  = region.getBlock(pos);
                if (b.isAir()) continue;
                std::string  id = game::blockId(b);
                if (auto om = ominousOf(id); !om.empty()) {
                    auto st = game::statesOf(b);
                    if (st["lit"] != "true") continue;
                    int count = (st.contains("candles") ? std::stoi(st["candles"]) : 0) + 1;
                    if (auto const* nb = game::blockWithStates(om, Json{{"twilightforest:candles", count}})) {
                        game::setBlock(region, pos, *nb);
                        playSoundAt(lich, Vec3{x + 0.5f, y + 0.5f, z + 0.5f}, "block.twilightforest.ominous_fire.light", 0.5F, (r.nextFloat() - r.nextFloat()) * 0.2F + 0.75F);
                    }
                } else if (id.rfind("twilightforest:", 0) == 0) {
                    auto st = game::statesOf(b);
                    auto it = st.find("twilightforest:lighting");
                    if (it == st.end() || it->second != "normal") continue;
                    Json j                        = statesJson(b);
                    j["twilightforest:lighting"] = "ominous";
                    if (auto const* nb = game::blockWithStates(id, j)) {
                        game::setBlock(region, pos, *nb);
                        playSoundAt(lich, Vec3{x + 0.5f, y + 0.5f, z + 0.5f}, "block.twilightforest.candelabra.ominous", 0.5F, (r.nextFloat() - r.nextFloat()) * 0.2F + 0.75F);
                    }
                }
            }
}

void lightNearbyCandles(Mob& lich) {
    auto&    region = lich.getDimensionBlockSource();
    BlockPos home   = homeOrElseCurrent(lich);
    for (int x = home.x - 40; x <= home.x + 40; ++x)
        for (int z = home.z - 40; z <= home.z + 40; ++z)
            for (int y = home.y - 3; y <= home.y + 40; ++y) {
                BlockPos     pos{x, y, z};
                Block const& b = region.getBlock(pos);
                if (b.isAir()) continue;
                std::string id = game::blockId(b);
                if (id.rfind("twilightforest:", 0) != 0) continue;
                if (auto van = vanillaOf(id); !van.empty()) {
                    int count = game::intState(b, "twilightforest:candles").value_or(1);
                    if (auto const* nb = game::blockWithStates(van, Json{{"candles", std::max(count - 1, 0)}, {"lit", true}})) {
                        game::setBlock(region, pos, *nb);
                        playSoundAt(lich, Vec3{x + 0.5f, y + 0.5f, z + 0.5f}, "minecraft:block.fire.ambient", 0.25F, 1.5F);
                    }
                } else {
                    auto st = game::statesOf(b);
                    auto it = st.find("twilightforest:lighting");
                    if (it == st.end() || it->second != "ominous") continue;
                    Json j                        = statesJson(b);
                    j["twilightforest:lighting"] = "normal";
                    if (auto const* nb = game::blockWithStates(id, j)) {
                        game::setBlock(region, pos, *nb);
                        playSoundAt(lich, Vec3{x + 0.5f, y + 0.5f, z + 0.5f}, "minecraft:block.fire.ambient", 0.25F, 1.15F);
                    }
                }
            }
}

Actor* launchProjectile(Mob& lich, int type) {
    Actor* target = targetOf(lich);
    if (!target) return nullptr;
    float  angle = lich.getYBodyRotation() * std::numbers::pi_v<float> / 180.0F;
    auto   p     = lich.getFeetPos();
    double sx    = p.x + std::cos(angle) * 0.65;
    double sy    = p.y + bbHeight(lich) * 0.82;
    double sz    = p.z + std::sin(angle) * 0.65;
    auto   tp    = target->getFeetPos();
    double tx    = tp.x - sx;
    double ty    = (target->getAABB().min.y + bbHeight(*target) / 2.0F) - (p.y + bbHeight(lich) / 2.0F);
    double tz    = tp.z - sz;
    auto&  r     = randomOf(lich);
    float  pitch = (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F;
    if (type != 0) pitch *= 0.85F;
    playSound(lich, "entity.twilightforest.lich.shoot", 1.0F, pitch);
    char const* id = type == 0 ? kLichBolt : kLichBomb;
    Actor*      pr = gf::projectile::shoot(lich, id, Vec3{float(tx), float(ty), float(tz)}, 0.5F, 1.0F);
    if (pr) pr->moveTo(Vec3{float(sx), float(sy), float(sz)}, lich.getRotation());
    return pr;
}

void setShield(Mob& lich, int shield) {
    lichState(lich)->shield = shield;
    syncProperties(lich, *lichState(lich));
}

void setMinionsLeft(Mob& lich, int left) { lichState(lich)->minionsLeft = left; }

void setTeleportInvisibility(Mob& lich, int ticks) { lichState(lich)->tpInvisibility = ticks; }

bool deflect(Mob& lich, Actor& projectile, Actor* owner) {
    if (!entityTypeIn("twilightforest:lich_deflects_phase_2", projectile.getTypeName())) return false;
    if (!(owner == nullptr || owner->isPlayer() || isLich(*owner)) || phaseOf(lich) <= 1) return false;
    auto& r = randomOf(lich);
    auto  v = lich.getPosDelta();
    Vec3  nv{float((v.x + (0.5 - r.nextDouble())) * 0.75), float((v.y + 0.75) * 1.5), float((v.z + (0.5 - r.nextDouble())) * 0.75)};
    gf::projectile::ext::setVelocity(projectile, nv);
    setProjectileOwner(projectile, &lich);
    playSound(lich, "entity.twilightforest.fortification_shield.block", 0.5F, voicePitch(lich) * 1.5F);
    swing(lich);
    return true;
}

void levelTick() {
    static int sweep = 0, queued = 0, leashes = 0;
    if (++queued >= 20) {
        queued = 0;
        pending::tick();
    }
    if (++leashes >= 100) {
        leashes = 0;
        if (auto level = ll::service::getLevel())
            level->forEachPlayer(std::function<bool(Player&)>([](Player& p) {
                auto f = p.getFeetPos();
                edits::tickTethers(p.getDimensionBlockSource(), dimOf(p), AABB{Vec3{f.x - 48.0f, f.y - 24.0f, f.z - 48.0f}, Vec3{f.x + 48.0f, f.y + 24.0f, f.z + 48.0f}});
                return true;
            }));
    }
    edits::tickLeashes();
    if (++sweep >= 200) {
        sweep = 0;
        pruneRandoms();
        pruneMobs();
        pruneProjectiles();
    }
    // ServerTFBossBar of phase 1 shows the shield (the boss kit sets the health fraction every tick)
    for (auto it = states().begin(); it != states().end();) {
        auto* m = mobById(it->first);
        if (!m || !isLich(*m)) {
            forgetRandom(it->first);
            it = states().erase(it);
            continue;
        }
        if (m->isAlive() && it->second.master == 0 && phaseOf(*m) == 1)
            gb::bar(*m).setProgress(static_cast<float>(it->second.shield) / static_cast<float>(std::max(it->second.maxShield, 1)));
        ++it;
    }
}

void registerLich() {
    ge::registerType(kLich, {
        .onSpawn = onSpawn,
        .tick    = serverStep,
        .aiStep  = aiStep,
        .die     = die,
        .save    = save,
        .load    = load,
    });
    gb::registerBoss({
        .entityId        = kLich,
        .homeStructure   = kTowerKey,
        .lootTable       = "twilightforest:entities/lich",
        .deathChestBlock = "twilightforest:twilight_oak_chest",
        .bar             = {.rgb = 0xFFD800},
        .deathTicks      = kDeathDuration,
    });
    gb::registerSpawner({.blockId = kSpawner, .entityId = kLich, .range = 9, .particle = "twilightforest:ominous_flame"});
    gb::ext::setHomeRadius(kLich, kHomeRadius);
    gb::ext::setLifecycle(kLich, {
        .shouldSpawnLoot     = [](Mob& m) { return !isShadowClone(m) && gameRuleDoMobLoot(); },
        .shouldCreateSpawner = [](Mob& m) { return !isShadowClone(m); },
        .tickDeath           = [](Mob& m, int t) { tickDeath(m, t); },
        .deathAnimationFinished = [](Mob&, int t) { return t >= kDeathDuration; }, // Lich#isDeathAnimationFinished, clones too
        .deathContainer =
            [](Mob& m, gf::core::jrand::RandomSource&) {
                return randomOf(m).nextBoolean() ? std::string("twilightforest:canopy_chest") : std::string("twilightforest:twilight_oak_chest");
            },
        .onPostRemoval = [](Mob& m) {
            if (!isShadowClone(m)) lightNearbyCandles(m);
        },
        .placeSpawner =
            [](Mob& m, BlockPos const& pos) {
                gb::ext::placeBossSpawner(m, pos);
                lightNearbyCandles(m);
            },
    });
    // Lich#hurt in the damage pipeline (before F8's Behaviour::hurt at Normal)
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (!isLich(d.victim)) return gf::combat::Verdict::Pass;
        auto* m = asMob(d.victim);
        if (!m || !isServerActor(*m)) return gf::combat::Verdict::Pass;
        return hurt(*m, d) ? gf::combat::Verdict::Pass : gf::combat::Verdict::Cancel;
    });
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (!isLich(d.victim)) return;
        if (auto* m = asMob(d.victim); m && isServerActor(*m)) dealt(*m);
    });
}

} // namespace gf::feature::lich
