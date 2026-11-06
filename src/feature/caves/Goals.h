// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/ai/goal/ThrowRiderGoal.java
// and Minecraft 1.21.1's MeleeAttackGoal, RangedAttackGoal and NearestAttackableTargetGoal, which
// TF's troll, yeti and alpha yeti use.
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The goals U27's mobs share, as F8 goals (gf::entity::Goal). They draw from the mob's goal random
// source (F8's JavaRandom of the actor) in Java's order.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/phys/AABB.h"

#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/tags/Tags.h"
#include "feature/ext/caves/W0Internals.h"

namespace gf::feature::caves {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

/// The mob behind a goal's actor id (nullptr: gone).
inline Mob* mobOf(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

/// Mob#isWithinRestriction(pos) (EnforcedHomePoint: no home, or inside its radius).
inline bool withinRestriction(Mob& mob, Vec3 const& pos) {
    auto h = gf::entity::home(mob);
    if (!h || h->radius <= 0 || h->pos.dim != static_cast<int>(mob.getDimensionId())) return true;
    double dx = std::floor(pos.x) - h->pos.x, dy = std::floor(pos.y) - h->pos.y, dz = std::floor(pos.z) - h->pos.z;
    return dx * dx + dy * dy + dz * dz < static_cast<double>(h->radius) * h->radius;
}

/// Java's MeleeAttackGoal (1.21.1).
class MeleeAttackGoal : public gf::entity::Goal {
public:
    MeleeAttackGoal(Mob& mob, double speed, bool followingTargetEvenIfNotSeen)
    : mUid(fe::uidOf(mob)),
      mSpeed(speed),
      mFollow(followingTargetEvenIfNotSeen) {
        flags = Move | Look;
    }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        long long now = static_cast<long long>(fe::currentTick());
        if (now - mLastCanUseCheck < 20) return false;
        mLastCanUseCheck = now;
        auto* t          = mobs::target(*m);
        if (!t || !t->isAlive()) return false;
        if (mobs::canCreatePath(*m, t->getFeetPos())) return true;
        return mobs::isWithinMeleeAttackRange(*m, *t);
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (!t || !t->isAlive()) return false;
        if (!mFollow) return !mobs::navigationDone(*m);
        if (!withinRestriction(*m, t->getFeetPos())) return false;
        return mobs::attackable(*t);
    }
    void start() override {
        if (auto* m = mobOf(mUid))
            if (auto* t = mobs::target(*m)) mobs::moveTo(*m, t->getFeetPos(), mSpeed);
        mRecalc     = 0;
        mNextAttack = 0;
    }
    void stop() override {
        auto* m = mobOf(mUid);
        if (!m) return;
        auto* t = mobs::target(*m);
        if (t && !mobs::attackable(*t)) m->setTarget(nullptr);
        mobs::stopNavigation(*m);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (!t) return;
        auto& rand = fe::randomOf(*m);
        mobs::lookAt(*m, *t, 30.0F, 30.0F);
        mRecalc  = std::max(mRecalc - 1, 0);
        auto tp  = t->getFeetPos();
        bool far = mPathed.x == 0 && mPathed.y == 0 && mPathed.z == 0;
        if (!far) {
            double dx = tp.x - mPathed.x, dy = tp.y - mPathed.y, dz = tp.z - mPathed.z;
            far       = dx * dx + dy * dy + dz * dz >= 1.0;
        }
        if ((mFollow || mobs::hasLineOfSight(*m, *t)) && mRecalc <= 0 && (far || rand.nextFloat() < 0.05F)) {
            mPathed  = tp;
            mRecalc  = 4 + rand.nextInt(7);
            double d = mobs::distanceSqr(*m, *t);
            if (d > 1024.0) mRecalc += 10;
            else if (d > 256.0) mRecalc += 5;
            if (!mobs::moveTo(*m, tp, mSpeed)) mRecalc += 15;
        }
        mNextAttack = std::max(mNextAttack - 1, 0);
        checkAndPerformAttack(*m, *t);
    }

protected:
    /// MeleeAttackGoal#checkAndPerformAttack.
    virtual void checkAndPerformAttack(Mob& m, Actor& victim) {
        if (canPerformAttack(m, victim)) {
            resetAttackCooldown();
            m.swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            mobs::doHurtTarget(m, victim);
        }
    }
    [[nodiscard]] bool canPerformAttack(Mob& m, Actor& victim) const {
        return mNextAttack <= 0 && mobs::isWithinMeleeAttackRange(m, victim) && mobs::hasLineOfSight(m, victim);
    }
    void               resetAttackCooldown() { mNextAttack = 20; }
    [[nodiscard]] int  ticksUntilNextAttack() const { return mNextAttack; }
    [[nodiscard]] Mob* self() const { return mobOf(mUid); }

    std::int64_t mUid;

private:
    double    mSpeed;
    bool      mFollow;
    long long mLastCanUseCheck = -100;
    int       mRecalc = 0, mNextAttack = 0;
    Vec3      mPathed{0, 0, 0};
};

/// Java's RangedAttackGoal (1.21.1) with RangedAttackMob#performRangedAttack as `attack`.
class RangedAttackGoal : public gf::entity::Goal {
public:
    using AttackFn = std::function<void(Mob& self, Actor& target, float distanceFactor)>;
    RangedAttackGoal(Mob& mob, double speed, int intervalMin, int intervalMax, float radius, AttackFn attack)
    : mUid(fe::uidOf(mob)),
      mSpeed(speed),
      mIntervalMin(intervalMin),
      mIntervalMax(intervalMax),
      mRadius(radius),
      mRadiusSqr(radius * radius),
      mAttack(std::move(attack)) {
        flags = Move | Look;
    }
    bool canUse() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (t && t->isAlive()) {
            mTarget = fe::uidOf(*t);
            return true;
        }
        return false;
    }
    bool canContinueToUse() override {
        if (canUse()) return true;
        auto* m = mobOf(mUid);
        auto* t = mTarget ? fe::actorById(mTarget) : nullptr;
        return m && t && t->isAlive() && !mobs::navigationDone(*m);
    }
    void stop() override {
        mTarget     = 0;
        mSeeTime    = 0;
        mAttackTime = -1;
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* t = mTarget ? fe::actorById(mTarget) : nullptr;
        if (!m || !t) return;
        double d0   = mobs::distanceSqr(*m, *t);
        bool   flag = mobs::hasLineOfSight(*m, *t);
        if (flag) ++mSeeTime;
        else mSeeTime = 0;
        if (d0 <= mRadiusSqr && mSeeTime >= 5) mobs::stopNavigation(*m);
        else mobs::moveTo(*m, t->getFeetPos(), mSpeed);
        mobs::lookAt(*m, *t, 30.0F, 30.0F);
        if (--mAttackTime == 0) {
            if (!flag) return;
            float f  = static_cast<float>(std::sqrt(d0)) / mRadius;
            float f1 = std::clamp(f, 0.1F, 1.0F);
            if (mAttack) mAttack(*m, *t, f1);
            mAttackTime = static_cast<int>(std::floor(f * static_cast<float>(mIntervalMax - mIntervalMin) + static_cast<float>(mIntervalMin)));
        } else if (mAttackTime < 0) {
            double delta = std::sqrt(d0) / mRadius;
            mAttackTime  = static_cast<int>(std::floor(mIntervalMin + delta * (mIntervalMax - mIntervalMin)));
        }
    }

protected:
    std::int64_t mUid;

private:
    std::int64_t mTarget = 0;
    double       mSpeed;
    int          mIntervalMin, mIntervalMax;
    float        mRadius, mRadiusSqr;
    AttackFn     mAttack;
    int          mSeeTime    = 0;
    int          mAttackTime = -1;
};

/// TF's ThrowRiderGoal: a melee goal that grabs the target as a rider and throws it.
class ThrowRiderGoal : public MeleeAttackGoal {
public:
    /// `onGrab`: after an attack that took a rider (the yetis' grab sounds); `onThrow`: before the
    /// throw, while the rider is still held (their throw sounds).
    ThrowRiderGoal(Mob& mob, double speed, bool useLongMemory, std::function<void(Mob&)> onGrab, std::function<void(Mob&)> onThrow)
    : MeleeAttackGoal(mob, speed, useLongMemory),
      mOnGrab(std::move(onGrab)),
      mOnThrow(std::move(onThrow)) {}

    bool canUse() override {
        auto* m = self();
        if (!m || m->getFirstPassenger() != nullptr) return false;
        auto* t = mobs::target(*m);
        if (!t) return false;
        // !#c:bosses and the target's yeti_throwing cooldown at 0
        if (gf::tags::has(gf::tags::Registry::EntityType, "c:bosses", t->getTypeName())) return false;
        if (t->isPlayer() && gf::entity::ext::throwCooldown(static_cast<Player&>(*t)) > 0) return false;
        return MeleeAttackGoal::canUse();
    }
    void start() override {
        if (auto* m = self()) {
            auto& rand = fe::randomOf(*m);
            mThrowTimer = 10 + rand.nextInt(30); // wait 0.5 to 2 seconds before we throw the target
            mTimeout    = 80 + rand.nextInt(40); // lets only try to chase for around 4-6 seconds
        }
        MeleeAttackGoal::start();
    }
    void tick() override {
        mTimeout--;
        auto* m = self();
        if (m && m->getFirstPassenger() != nullptr) mThrowTimer--;
        else MeleeAttackGoal::tick();
    }
    void stop() override {
        if (auto* m = self(); m && m->getFirstPassenger() != nullptr) {
            if (mOnThrow) mOnThrow(*m);
            Actor* rider = m->getFirstPassenger();
            // the throw vector: the mob's look angle x 2 horizontally, 0.9 up
            auto look = lookAngle(*m);
            Vec3 throwVec{look.x * 2.0f, 0.9f, look.z * 2.0f};
            if (rider->isPlayer()) {
                gf::entity::mount::throwRider(*m, throwVec);
            } else {
                gf::entity::ext::hostileDismount(*rider);
                fe::push(*rider, throwVec);
            }
        }
        MeleeAttackGoal::stop();
    }
    bool canContinueToUse() override {
        auto* m        = self();
        bool  carrying = m && m->getFirstPassenger() != nullptr;
        return (mThrowTimer > 0 && carrying) || (mTimeout > 0 && MeleeAttackGoal::canContinueToUse() && !carrying);
    }

protected:
    void checkAndPerformAttack(Mob& m, Actor& victim) override {
        if (canPerformAttack(m, victim) && ticksUntilNextAttack() <= 0 && m.getFirstPassenger() == nullptr && mCooldown-- == 0) {
            mCooldown = 3; // a pause so it doesn't pick the target back up immediately after throwing
            resetAttackCooldown();
            m.swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            if (m.getFirstPassenger() == nullptr) {
                Actor* v = victim.getVehicle();
                if (v == nullptr || !obstructsSnatching(*v)) {
                    // pluck them from the boat, minecart, donkey, or whatever
                    if (victim.isPlayer()) gf::entity::mount::grab(m, static_cast<Player&>(victim));
                    else {
                        if (victim.getVehicle()) victim.stopRiding(true, false, false, false);
                        victim.startRiding(m, true);
                    }
                }
            }
        }
        if (m.getFirstPassenger() != nullptr && mOnGrab) mOnGrab(m);
    }

private:
    /// Entity#getLookAngle.
    static Vec3 lookAngle(Mob& m) { return m.getViewVector(1.0f); }
    /// #twilightforest:rides_obstruct_snatching.
    static bool obstructsSnatching(Actor& vehicle) {
        return gf::tags::has(gf::tags::Registry::EntityType, "twilightforest:rides_obstruct_snatching", vehicle.getTypeName());
    }

    std::function<void(Mob&)> mOnGrab, mOnThrow;
    int                       mThrowTimer = 0, mTimeout = 0, mCooldown = 0;
};

/// The ThrowRiderGoal of the yeti (`alpha` false) or the alpha yeti, with their grab / throw sounds (Yeti.cpp).
[[nodiscard]] std::shared_ptr<ThrowRiderGoal> makeThrowRiderGoal(Mob& mob, bool alpha);

/// Java's NearestAttackableTargetGoal(mob, Player.class, mustSee = true) with the mob's follow
/// range (`range` is asked each time: the troll's rock and the yeti's anger raise it by 8).
class NearestPlayerTargetGoal : public gf::entity::Goal {
public:
    NearestPlayerTargetGoal(Mob& mob, std::function<double(Mob&)> range) : mUid(fe::uidOf(mob)), mRange(std::move(range)) { flags = Target; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        // randomInterval 10: reducedTickDelay(10) = 5
        if (fe::randomOf(*m).nextInt(5) != 0) return false;
        double r    = mRange(*m);
        auto   p    = m->getFeetPos();
        Actor* best = nullptr;
        double bd   = 1e30;
        for (auto* pl : fe::playersNear(static_cast<int>(m->getDimensionId()), p, r + 4.0)) {
            if (!mobs::attackable(*pl)) continue;
            auto   q  = pl->getFeetPos();
            double dx = q.x - p.x, dz = q.z - p.z, dy = q.y - p.y;
            if (std::abs(dx) > r || std::abs(dz) > r || std::abs(dy) > 4.0 + (m->getAABB().max.y - m->getAABB().min.y)) continue; // the follow range box inflate(r, 4, r)
            double d = mobs::distanceSqr(*m, *pl);
            if (d > r * r) continue;
            if (!mobs::hasLineOfSight(*m, *pl)) continue;
            if (d < bd) bd = d, best = pl;
        }
        if (!best) return false;
        mTarget = fe::uidOf(*best);
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (!m || !t || !mobs::attackable(*t)) return false;
        double r = mRange(*m);
        if (mobs::distanceSqr(*m, *t) > r * r) return false;
        if (mobs::hasLineOfSight(*m, *t)) mUnseen = 0;
        else if (++mUnseen > 30) return false; // reducedTickDelay(60)
        return true;
    }
    void start() override {
        auto* m = mobOf(mUid);
        auto* t = mTarget ? fe::actorById(mTarget) : nullptr;
        if (m && t) m->setTarget(t);
        mUnseen = 0;
    }
    void stop() override {
        if (auto* m = mobOf(mUid)) m->setTarget(nullptr);
        mTarget = 0;
    }

private:
    std::int64_t                 mUid;
    std::function<double(Mob&)>  mRange;
    std::int64_t                 mTarget = 0;
    int                          mUnseen = 0;
};

} // namespace gf::feature::caves
