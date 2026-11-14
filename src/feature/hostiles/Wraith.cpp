// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/Wraith.java
//   src/main/java/gloamingforest/entity/EnforcedHomePoint.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The wraith: a FlyingMob without physics (the BP: no gravity, no block collision) steered by F8's
// NoClipMoveControl. Its goals are native: 2 MoveTowardsHome 0.85, 4 SimplifiedAttack (F8),
// 5 FlyTowardsTarget, 6 RandomFloatAround, 7 LookAround (the ghast's); the target (nearest player,
// no sight needed) is the BP's. A wraith from a spawner keeps a home of radius 20.

#include <cmath>
#include <memory>
#include <numbers>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/MobSpawnMethod.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "mod/Log.h"

namespace gf::feature::hostiles::wraith {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;
namespace ia   = gf::feature::common::interactions;

namespace {

constexpr int kHomeRadius = 20; // Wraith#getHomeRadius

Mob* wraithById(std::int64_t uid) { return liveMob(uid); }

/// EnforcedHomePoint#isMobWithinHomeArea (no valid home point: everywhere).
bool withinHome(Mob& m) {
    auto h = ge::home(m);
    if (!h || h->pos.dim != dimOf(m)) return true;
    auto   p  = blockPosOf(m);
    double dx = p.x - h->pos.x, dy = p.y - h->pos.y, dz = p.z - h->pos.z;
    return dx * dx + dy * dy + dz * dz < static_cast<double>(h->radius * h->radius);
}

class MoveTowardsHomeGoal final : public ge::Goal {
public:
    MoveTowardsHomeGoal(Mob& m, double speed) : mId(uidOf(m)), mSpeed(speed) { flags = Move; }
    bool canUse() override {
        auto* m = wraithById(mId);
        if (!m || withinHome(*m) || targetOf(*m)) return false;
        auto h = ge::home(*m);
        if (!h) return false;
        auto& r = randomOf(*m);
        // Direction.getRandom: DOWN, UP, NORTH, SOUTH, WEST, EAST
        static constexpr int kDirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        auto const&          d           = kDirs[r.nextInt(6)];
        int                  ox          = r.nextInt(5);
        int                  oy          = r.nextInt(5);
        int                  oz          = r.nextInt(5);
        BlockPos             pos{h->pos.x + d[0] + ox, h->pos.y + d[1] + oy, h->pos.z + d[2] + oz};
        if (!m->getDimensionBlockSource().hasChunksAt(pos, 0, false)) return false;
        mWanted = Vec3{float(pos.x), float(pos.y), float(pos.z)};
        return true;
    }
    bool canContinueToUse() override { return false; }
    void start() override {
        if (auto* m = wraithById(mId)) ge::ext::setWantedPosition(*m, mWanted, mSpeed);
    }

private:
    std::int64_t mId;
    double       mSpeed;
    Vec3         mWanted{};
};

class FlyTowardsTargetGoal final : public ge::Goal {
public:
    explicit FlyTowardsTargetGoal(Mob& m) : mId(uidOf(m)) { flags = Move; }
    bool canUse() override {
        auto* m = wraithById(mId);
        return m && targetOf(*m);
    }
    bool canContinueToUse() override { return false; }
    void start() override {
        auto* m = wraithById(mId);
        if (!m) return;
        if (auto* t = targetOf(*m)) ge::ext::setWantedPosition(*m, t->getFeetPos(), 0.5);
    }

private:
    std::int64_t mId;
};

/// [VanillaCopy] Ghast.RandomFloatAroundGoal.
class RandomFloatAroundGoal final : public ge::Goal {
public:
    explicit RandomFloatAroundGoal(Mob& m) : mId(uidOf(m)) { flags = Move; }
    bool canUse() override {
        auto* m = wraithById(mId);
        if (!m || targetOf(*m) || !withinHome(*m)) return false;
        auto wanted = ge::ext::wantedPosition(*m);
        if (!wanted) return true; // MoveControl's wanted point starts at the mob
        double d3 = distSqr(*m, *wanted);
        return d3 < 1.0 || d3 > 3600.0;
    }
    bool canContinueToUse() override { return false; }
    void start() override {
        auto* m = wraithById(mId);
        if (!m) return;
        auto& r = randomOf(*m);
        auto  p = m->getFeetPos();
        double d0 = p.x + (r.nextFloat() * 2.0F - 1.0F) * 16.0F;
        double d1 = p.y + (r.nextFloat() * 2.0F - 1.0F) * 16.0F;
        double d2 = p.z + (r.nextFloat() * 2.0F - 1.0F) * 16.0F;
        ge::ext::setWantedPosition(*m, Vec3{float(d0), float(d1), float(d2)}, 0.5);
    }

private:
    std::int64_t mId;
};

/// [VanillaCopy] Ghast.GhastLookGoal.
class LookAroundGoal final : public ge::Goal {
public:
    explicit LookAroundGoal(Mob& m) : mId(uidOf(m)) { flags = Look; }
    bool canUse() override { return true; }
    void tick() override {
        auto* m = wraithById(mId);
        if (!m) return;
        auto* t = targetOf(*m);
        float yaw;
        if (!t) {
            auto v = m->getPosDelta();
            if (std::abs(v.x) < 1e-7 && std::abs(v.z) < 1e-7) return;
            yaw = -static_cast<float>(std::atan2(v.x, v.z)) * (180.0F / std::numbers::pi_v<float>);
        } else {
            if (mobs::distanceSqr(*t, *m) >= 4096.0) return;
            auto   tp = t->getFeetPos();
            auto   p  = m->getFeetPos();
            double d1 = tp.x - p.x;
            double d2 = tp.z - p.z;
            yaw       = -static_cast<float>(std::atan2(d1, d2)) * (180.0F / std::numbers::pi_v<float>);
        }
        const_cast<Vec2&>(m->getRotation()) = Vec2{m->getRotation().x, yaw};
        m->setYBodyRotation(yaw);
    }

private:
    std::int64_t mId;
};


void wraithTick(Actor& self) {
    if (despawnInPeaceful(self)) return;
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (firstSpawn(self)) {
        // Wraith#finalizeSpawn: a SPAWNER spawn keeps its spawn position as home (a structure that
        // places a wraith gives it its home with gf::entity::setHome; Bedrock keeps no spawn reason)
        auto method = static_cast<MobSpawnMethod>(m->mSpawnMethod);
        if (method == MobSpawnMethod::Spawner && !ge::home(*m)) {
            auto p = blockPosOf(self);
            ge::setHome(*m, gf::core::GlobalPos{dimOf(self), p.x, p.y, p.z}, kHomeRadius);
        }
    }
    if (!nativeGoalsPresent(self)) {
        ge::setMoveControl(*m, ge::MoveKind::NoClip);
        ge::addGoal(*m, 2, std::make_shared<MoveTowardsHomeGoal>(*m, 0.85));
        ge::addGoal(*m, 4, ge::goals::simplifiedAttack(*m));
        ge::addGoal(*m, 5, std::make_shared<FlyTowardsTargetGoal>(*m));
        ge::addGoal(*m, 6, std::make_shared<RandomFloatAroundGoal>(*m));
        ge::addGoal(*m, 7, std::make_shared<LookAroundGoal>(*m));
    }
}

} // namespace

void install() {
    ge::registerType(id::Wraith, {.tick = wraithTick, .die = forgetActor});
    // Wraith#doHurtTarget: first TFDamageTypes.HAUNT of its attack damage, then the vanilla hit, which
    // the hurt's invulnerable time swallows (same amount): the haunt is the hit
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (!isType(d.attacker, id::Wraith) || (d.direct && d.direct != d.attacker) || d.type != "minecraft:mob_attack") return gf::combat::Verdict::Pass;
        auto victim = uidOf(d.victim);
        auto wraith = uidOf(*d.attacker);
        ia::nextTick([victim, wraith] {
            auto* v = actorById(victim);
            auto* w = wraithById(wraith);
            if (v && w && v->isAlive()) gf::combat::hurt(*v, attackDamage(*w, 5.0F), "gloamingforest:haunt", w);
        });
        return gf::combat::Verdict::Cancel;
    });
    // Wraith#hurt: a living attacker (not a creative player, not its vehicle or passengers) becomes
    // its target
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (typeOf(d.victim) != id::Wraith || !d.attacker || d.attacker == &d.victim || !isLiving(*d.attacker)) return;
        if (d.victim.getVehicle() == d.attacker || d.attacker->getVehicle() == &d.victim) return;
        if (d.attacker->isPlayer() && static_cast<Player*>(d.attacker)->isCreative()) return;
        if (auto* m = asMob(d.victim)) m->setTarget(d.attacker);
    });
}

} // namespace gf::feature::hostiles::wraith
