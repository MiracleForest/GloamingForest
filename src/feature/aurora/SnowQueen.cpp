// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/SnowQueen.java
//   src/main/java/twilightforest/entity/boss/SnowQueenIceShield.java
//   src/main/java/twilightforest/entity/ai/goal/HoverBaseGoal.java
//   src/main/java/twilightforest/entity/ai/goal/HoverSummonGoal.java
//   src/main/java/twilightforest/entity/ai/goal/HoverThenDropGoal.java
//   src/main/java/twilightforest/entity/ai/goal/HoverBeamGoal.java
//   src/main/java/twilightforest/block/entity/spawner/SnowQueenSpawnerBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Snow Queen on F8's boss kit: the SUMMON / DROP / BEAM phases (not saved, as TF: a reloaded
// queen starts over in SUMMON), the three hover goals, the seven ice shields (F8 parts), the ice
// crystal minions, the ice beam and the client effects (sent through gf::fx). Her phase and beam
// flag are entity properties, which the resource pack's animations read on every client.
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/enchanting/EnchantUtils.h"

#include "feature/aurora/Mobs.h"
#include "feature/aurora/Structures.h"
#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/aurora/W0Internals.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/projectile/Projectile.h"
#include "game/WorldDb.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::aurora {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

namespace {

enum class Phase { Summon, Drop, Beam };

constexpr int   MAX_SUMMONS              = 6;
constexpr int   MAX_DAMAGE_WHILE_BEAMING = 25;
constexpr float BREATH_DAMAGE            = 4.0F;
constexpr int   MAX_MINIONS_AT_ONCE      = 4;
constexpr float kWidth = 0.7f, kHeight = 2.5f;

/// What SnowQueen keeps in fields (not saved).
struct Queen {
    Phase phase              = Phase::Summon;
    int   summonsRemaining   = MAX_SUMMONS;
    int   successfulDrops    = 0;
    int   maxDrops           = 0;
    int   damageWhileBeaming = 0;
    bool  breathing          = false;
};

std::map<std::int64_t, Queen>& queens() {
    static std::map<std::int64_t, Queen> q;
    return q;
}

std::int64_t uidOf(Actor const& a) { return a.getOrCreateUniqueID().rawID; }

/// The queens whose native goals are added (in this process).
std::set<std::int64_t>& queenGoals() {
    static std::set<std::int64_t> s;
    return s;
}

Queen& stateOf(Actor& self) {
    auto [it, fresh] = queens().try_emplace(uidOf(self));
    if (fresh) {
        // the constructor calls setCurrentPhase(SUMMON)
        gf::entity::setProperty(self, "twilightforest:phase", 0);
        gf::entity::setProperty(self, "twilightforest:beam", false);
    }
    return it->second;
}

void setBreathing(Actor& self, bool flag) {
    auto& q = stateOf(self);
    if (q.breathing == flag) return;
    q.breathing = flag;
    gf::entity::setProperty(self, "twilightforest:beam", flag);
}

void setCurrentPhase(Actor& self, Phase phase) {
    auto& q = stateOf(self);
    q.phase = phase;
    gf::entity::setProperty(self, "twilightforest:phase", static_cast<int>(phase));
    if (phase == Phase::Summon) q.summonsRemaining = MAX_SUMMONS;
    if (phase == Phase::Drop) {
        q.successfulDrops = 0;
        q.maxDrops        = 2 + rng().nextInt(3);
    }
    if (phase == Phase::Beam) q.damageWhileBeaming = 0;
    gf::log().info("aurora: snow queen phase {}", phase == Phase::Summon ? "summon" : phase == Phase::Drop ? "drop" : "beam");
}

Mob* mobOf(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

AABB boxOf(Actor const& a) { return a.getAABB(); }

/// Mth#wrapDegrees.
float wrapDegrees(float d) {
    d = std::fmod(d, 360.0F);
    if (d >= 180.0F) d -= 360.0F;
    if (d < -180.0F) d += 360.0F;
    return d;
}

/// Mob#rotlerp / LookControl#rotateTowards: turn a towards b by at most max degrees.
float rotlerp(float a, float b, float max) { return a + std::clamp(wrapDegrees(b - a), -max, max); }

/// The yaw and pitch from the mob's eyes to the target's eyes (Mob#lookAt, LookControl#getYRotD / getXRotD).
std::pair<float, float> anglesTo(Mob& m, Actor& t) {
    auto   me = m.getEyePos();
    auto   te = t.getEyePos();
    double dx = te.x - me.x, dy = te.y - me.y, dz = te.z - me.z;
    float  yaw   = static_cast<float>(std::atan2(dz, dx) * 180.0 / std::numbers::pi) - 90.0F;
    float  pitch = static_cast<float>(-(std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * 180.0 / std::numbers::pi));
    return {yaw, pitch};
}

/// Mob#lookAt(target, maxYRot, maxXRot): turns only the yaw and pitch themselves (rotlerp). The head
/// (yHeadRot, which her view vector reads) is turned by the LookControl alone (mobs::lookAt), as in
/// Java. Writes the rotation component directly: Actor::setRotationWrapped is client-only.
void mobLookAt(Mob& m, Actor& t, float maxY, float maxX) {
    auto [yaw, pitch] = anglesTo(m, t);
    auto& rc          = *m.mBuiltInComponents->mActorRotationComponent;
    Vec2  rot         = rc.mRot; // (pitch, yaw)
    rc.mRot           = Vec2{wrapDegrees(rotlerp(rot.x, pitch, maxX)), wrapDegrees(rotlerp(rot.y, yaw, maxY))};
}

/// SnowQueen#countMyMinions: ice crystals in the queen's 1 x 1 x 1 box inflated by 32 / 16 / 32.
int countMyMinions(Actor& self) {
    auto p   = feet(self);
    AABB box{Vec3{p.x - 32, p.y - 16, p.z - 32}, Vec3{p.x + 1 + 32, p.y + 1 + 16, p.z + 1 + 32}};
    int  n   = 0;
    for (auto* a : fe::actorsIn(self.getDimensionBlockSource(), static_cast<int>(self.getDimensionId()), box))
        if (a->isAlive() && a->getTypeName() == kIceCrystal) n++;
    return n;
}

/// LivingEntity#randomTeleport(x, y, z, true) (no liquid / collision checks of entities).
bool randomTeleport(Actor& actor, double x, double y, double z) {
    auto&    region = actor.getDimensionBlockSource();
    BlockPos pos{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
    if (!region.hasChunksAt(pos, 0, false)) return false;
    bool ground = false;
    while (pos.y > region.getMinHeight()) {
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (!region.getBlock(below).isAir()) {
            ground = true;
            break;
        }
        --pos.y;
    }
    if (!ground) return false;
    if (!region.getBlock(pos).isAir() || !region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z}).isAir()) return false;
    actor.teleportTo(Vec3{static_cast<float>(x), static_cast<float>(pos.y), static_cast<float>(z)}, true, 0, 1, false);
    return true;
}

/// SnowQueen#summonMinionAt.
void summonMinionAt(Mob& self, Actor& target) {
    auto& q     = stateOf(self);
    auto  p     = feet(self);
    auto* actor = gf::spawn::spawnMob(self.getDimensionBlockSource(), kIceCrystal, p, gf::spawn::SpawnReason::MobSummoned,
                                      [](Actor& crystal) { markSummonedCrystal(crystal); });
    if (actor) {
        auto home = gf::entity::home(self);
        for (int i = 0; i < 100; i++) {
            double ax, ay, az;
            if (home && home->pos.dim == static_cast<int>(self.getDimensionId())) {
                ax = home->pos.x + rng().nextGaussian() * 3.0;
                ay = home->pos.y + rng().nextGaussian() * 2.0;
                az = home->pos.z + rng().nextGaussian() * 3.0;
            } else {
                auto t = feet(target);
                ax     = t.x + rng().nextGaussian() * 6.0;
                ay     = t.y + rng().nextGaussian() * 8.0;
                az     = t.z + rng().nextGaussian() * 6.0;
            }
            if (randomTeleport(*actor, ax, ay, az)) break;
        }
        if (auto* m = fe::asMob(*actor)) m->setTarget(&target);
        markSummonedCrystal(*actor); // setToDieIn30Seconds
    }
    q.summonsRemaining--;
}

/// SnowQueen#destroyBlocksInAABB: ice blocks (no drops), with mob griefing.
void destroyBlocksInAABB(Actor& self, AABB const& box) {
    if (!mobGriefing()) return;
    auto& region = self.getDimensionBlockSource();
    for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x)); x++)
        for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y)); y++)
            for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z)); z++) {
                BlockPos pos{x, y, z};
                auto     id = gf::game::blockId(region.getBlock(pos));
                if (gf::tags::has(gf::tags::Registry::Block, "minecraft:ice", id) || id == "minecraft:ice" || id == "minecraft:packed_ice" ||
                    id == "minecraft:blue_ice" || id == "minecraft:frosted_ice")
                    gf::game::clearBlock(region, pos);
            }
}

/// SnowQueen#doHurtTarget -> EntityUtil#properlyApplyCustomDamageSource: the ATTACK_DAMAGE attribute
/// plus the weapon's enchantment bonus, as squish during DROP, else as a mob attack.
bool doHurtTarget(Mob& self, Actor& target) {
    float damage = 7.0F;
    auto  ref    = self.getAttribute(SharedAttributes::ATTACK_DAMAGE());
    if (auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr)) damage = inst->mCurrentValue;
    damage += EnchantUtils::getMeleeDamageBonus(target, self);
    char const* type = stateOf(self).phase == Phase::Drop ? "twilightforest:squish" : "minecraft:mob_attack";
    return gf::combat::hurt(target, damage, type, &self, &self);
}

/// Java's MeleeAttackGoal(mob, 1.0, followingTargetEvenIfNotSeen = true) (priority 6).
class MeleeAttackGoal final : public gf::entity::Goal {
public:
    MeleeAttackGoal(Mob& mob, double speed) : mUid(uidOf(mob)), mSpeed(speed) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        long long now = m->mTickCount;
        if (now - mLastCanUseCheck < 20) return false;
        mLastCanUseCheck = now;
        auto* t          = mobs::target(*m);
        // (Java also asks for a path; the hovering queen's navigation always has one: no probe that
        // would start moving her while the hover goals run)
        return t && t->isAlive();
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (!t || !t->isAlive()) return false;
        if (auto home = gf::entity::home(*m)) {
            auto   p = feet(*t);
            double dx = p.x - home->pos.x, dy = p.y - home->pos.y, dz = p.z - home->pos.z;
            if (home->radius > 0 && dx * dx + dy * dy + dz * dz >= static_cast<double>(home->radius) * home->radius) return false;
        }
        if (t->isPlayer()) {
            auto& pl = static_cast<Player&>(*t);
            if (pl.isCreative() || pl.isSpectator()) return false;
        }
        return true;
    }
    void start() override {
        if (auto* m = mobOf(mUid))
            if (auto* t = mobs::target(*m)) mobs::moveTo(*m, feet(*t), mSpeed);
        mRecalc     = 0;
        mNextAttack = 0;
    }
    void stop() override {
        if (auto* m = mobOf(mUid)) mobs::stopNavigation(*m);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* t = m ? mobs::target(*m) : nullptr;
        if (!t) return;
        mobs::lookAt(*m, *t, 30.0F, 30.0F);
        mRecalc = std::max(mRecalc - 1, 0);
        if (mRecalc <= 0) {
            mRecalc  = 4 + rng().nextInt(7);
            double d = mobs::distanceSqr(*m, *t);
            if (d > 1024.0) mRecalc += 10;
            else if (d > 256.0) mRecalc += 5;
            if (!mobs::moveTo(*m, feet(*t), mSpeed)) mRecalc += 15;
        }
        mNextAttack = std::max(mNextAttack - 1, 0);
        if (mNextAttack <= 0 && withinReach(*m, *t) && mobs::hasLineOfSight(*m, *t)) {
            mNextAttack = 20;
            m->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            doHurtTarget(*m, *t);
        }
    }

private:
    /// Mob#isWithinMeleeAttackRange: the box inflated by DEFAULT_ATTACK_REACH (sqrt(2.04) - 0.6) across.
    static bool withinReach(Mob& m, Actor& t) {
        AABB        a     = boxOf(m);
        AABB        b     = boxOf(t);
        float const reach = static_cast<float>(std::sqrt(2.04) - 0.6);
        return a.min.x - reach < b.max.x && a.max.x + reach > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z - reach < b.max.z &&
               a.max.z + reach > b.min.z;
    }
    std::int64_t mUid;
    double       mSpeed;
    long long    mLastCanUseCheck = -100;
    int          mRecalc = 0, mNextAttack = 0;
};

/// Java's LookAtPlayerGoal(mob, Player.class, 8.0F) (priority 8, LOOK). As F8 goals (not the BP's
/// look behaviours) so that the hover goals' LOOK flag keeps them off her head while she beams.
class LookAtPlayerGoal final : public gf::entity::Goal {
public:
    explicit LookAtPlayerGoal(Mob& mob) : mUid(uidOf(mob)) { flags = Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m || rng().nextFloat() >= kProbability) return false;
        Actor* look = mobs::target(*m);
        if (!look) {
            auto   p    = feet(*m);
            double best = kDistance * kDistance + 1;
            AABB   box{Vec3{p.x - kDistance, p.y - 3.0f, p.z - kDistance}, Vec3{p.x + kWidth + kDistance, p.y + kHeight + 3.0f, p.z + kWidth + kDistance}};
            for (auto* a : fe::actorsIn(m->getDimensionBlockSource(), static_cast<int>(m->getDimensionId()), box)) {
                if (!a->isPlayer() || !a->isAlive() || static_cast<Player*>(a)->isSpectator()) continue;
                double d = mobs::distanceSqr(*m, *a);
                if (d <= kDistance * kDistance && d < best) best = d, look = a;
            }
        }
        if (!look) return false;
        mLookAt = uidOf(*look);
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* a = fe::actorById(mLookAt);
        return m && a && a->isAlive() && mobs::distanceSqr(*m, *a) <= kDistance * kDistance && mLookTime > 0;
    }
    void start() override { mLookTime = 40 + rng().nextInt(40); }
    void stop() override { mLookAt = 0; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* a = fe::actorById(mLookAt);
        if (m && a && a->isAlive()) mobs::lookAt(*m, a->getEyePos(), 10.0F, 40.0F);
        --mLookTime;
    }

private:
    static constexpr float kDistance = 8.0F, kProbability = 0.02F;
    std::int64_t           mUid, mLookAt = 0;
    int                    mLookTime = 0;
};

/// Java's RandomLookAroundGoal (priority 8, MOVE + LOOK).
class RandomLookAroundGoal final : public gf::entity::Goal {
public:
    explicit RandomLookAroundGoal(Mob& mob) : mUid(uidOf(mob)) { flags = Move | Look; }
    bool canUse() override { return rng().nextFloat() < 0.02F; }
    bool canContinueToUse() override { return mLookTime >= 0; }
    bool requiresUpdateEveryTick() override { return true; }
    void start() override {
        double d0 = 2.0 * std::numbers::pi * rng().nextDouble();
        mRelX     = std::cos(d0);
        mRelZ     = std::sin(d0);
        mLookTime = 20 + rng().nextInt(20);
    }
    void tick() override {
        --mLookTime;
        if (auto* m = mobOf(mUid)) {
            auto e = m->getEyePos();
            mobs::lookAt(*m, Vec3{static_cast<float>(e.x + mRelX), e.y, static_cast<float>(e.z + mRelZ)}, 10.0F, 40.0F);
        }
    }

private:
    std::int64_t mUid;
    double       mRelX = 0, mRelZ = 0;
    int          mLookTime = 0;
};

// ---- hover goals ---------------------------------------------------------------------------------

class HoverBase : public gf::entity::Goal {
public:
    HoverBase(Mob& mob, float height, float radius) : mUid(uidOf(mob)), mHoverHeight(height), mHoverRadius(radius) { flags = Move | Look; }
    void start() override {
        auto* m = mob();
        if (!m) return;
        if (auto* t = mobs::target(*m)) makeNewHoverSpot(*t);
    }

protected:
    Mob*   mob() const { return mobOf(mUid); }
    Actor* target() const {
        auto* m = mob();
        return m ? mobs::target(*m) : nullptr;
    }
    virtual void makeNewHoverSpot(Actor& target) {
        auto* m = mob();
        if (!m) return;
        double hx = 0, hy = 0, hz = 0;
        bool   found = false;
        auto   t     = feet(target);
        for (int i = 0; i < 100; i++) {
            hx = t.x + rr() * mHoverRadius;
            hy = t.y + mHoverHeight;
            hz = t.z + rr() * mHoverRadius;
            if (!isPositionOccupied(*m, hx, hy, hz) && mobs::hasLineOfSight(*m, target)) {
                found = true;
                break;
            }
        }
        if (!found) gf::log().debug("Found no spots, giving up");
        mHoverPosX = hx;
        mHoverPosY = hy;
        mHoverPosZ = hz;
    }
    /// HoverBaseGoal#isPositionOccupied: !isUnobstructed (entities that block) || !noCollision (block
    /// collision shapes) for the queen's box at the spot.
    static bool isPositionOccupied(Mob& m, double hx, double hy, double hz) {
        float radius = kWidth / 2.0F;
        AABB  box{Vec3{static_cast<float>(hx - radius), static_cast<float>(hy), static_cast<float>(hz - radius)},
                 Vec3{static_cast<float>(hx + radius), static_cast<float>(hy + kHeight), static_cast<float>(hz + radius)}};
        auto& region = m.getDimensionBlockSource();
        for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x)); x++)
            for (int y = static_cast<int>(std::floor(box.min.y)) - 1; y <= static_cast<int>(std::floor(box.max.y)); y++)
                for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z)); z++) {
                    BlockPos     pos{x, y, z};
                    Block const& block = region.getBlock(pos);
                    if (block.isAir()) continue;
                    AABB s = block.getBlockType().getCollisionShape(block, region, pos, nullptr);
                    if (s.max.x - s.min.x <= 0.0F || s.max.y - s.min.y <= 0.0F || s.max.z - s.min.z <= 0.0F) continue;
                    if (s.min.x < box.max.x && s.max.x > box.min.x && s.min.y < box.max.y && s.max.y > box.min.y && s.min.z < box.max.z && s.max.z > box.min.z)
                        return true;
                }
        // Entity#blocksBuilding: living things (not the queen, her shields or the dead)
        for (auto* e : fe::actorsIn(region, static_cast<int>(m.getDimensionId()), box)) {
            if (!e || e == &m || !e->isAlive() || gf::entity::ext::parentOf(*e) == &m) continue;
            if (e->hasCategory(ActorCategory::Mob)) return true;
        }
        return false;
    }
    double distanceToHoverSqr(Mob& m) const { return mobs::distanceSqr(m, Vec3{static_cast<float>(mHoverPosX), static_cast<float>(mHoverPosY), static_cast<float>(mHoverPosZ)}); }
    /// The push towards the hover spot (offset / dist x (0.05, 0.1, 0.05), y + gravity offset).
    void pushTowardsSpot(Mob& m, double gravityOffset) const {
        auto   p        = feet(m);
        double offsetX  = mHoverPosX - p.x;
        double offsetY  = mHoverPosY - p.y;
        double offsetZ  = mHoverPosZ - p.z;
        double distance = offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ;
        distance        = static_cast<float>(std::sqrt(static_cast<float>(distance)));
        if (distance <= 0) return;
        double velX = offsetX / distance * 0.05;
        double velY = offsetY / distance * 0.1 + gravityOffset;
        double velZ = offsetZ / distance * 0.05;
        fe::push(m, Vec3{static_cast<float>(velX), static_cast<float>(velY), static_cast<float>(velZ)});
    }

    std::int64_t mUid;
    float        mHoverHeight, mHoverRadius;
    double       mHoverPosX = 0, mHoverPosY = 0, mHoverPosZ = 0;
};

class HoverSummonGoal final : public HoverBase {
public:
    explicit HoverSummonGoal(Mob& m) : HoverBase(m, 6.0F, 6.0F) {}
    bool canUse() override {
        auto* m = mob();
        auto* t = target();
        if (!m || !t || !t->isAlive()) return false;
        if (stateOf(*m).phase != Phase::Summon) return false;
        return mobs::hasLineOfSight(*m, *t);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = target();
        if (!m || !t || !t->isAlive()) return false;
        if (stateOf(*m).phase != Phase::Summon) return false;
        if (mSeekTimer > kMaxSeekTime) return false;
        return mobs::hasLineOfSight(*m, *t);
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        mSeekTimer++;
        auto* t = target();
        // are we there yet?
        if (t && distanceToHoverSqr(*m) <= 3.0F) {
            auto& q = stateOf(*m);
            if (q.summonsRemaining > 0 && countMyMinions(*m) < MAX_MINIONS_AT_ONCE) summonMinionAt(*m, *t);
            makeNewHoverSpot(*t);
        }
        pushTowardsSpot(*m, 0.05);
        if (t) mobs::lookAt(*m, *t, 30.0F, 30.0F);
    }

private:
    void makeNewHoverSpot(Actor& target) override {
        HoverBase::makeNewHoverSpot(target);
        mSeekTimer = 0;
    }
    static constexpr int kMaxSeekTime = 80;
    int                  mSeekTimer   = 0;
};

class HoverThenDropGoal final : public HoverBase {
public:
    HoverThenDropGoal(Mob& m, int hoverTime, int dropTime) : HoverBase(m, 6.0F, 0.0F), mMaxHoverTime(hoverTime), mMaxDropTime(dropTime), mMaxSeekTime(hoverTime) {}
    bool canUse() override {
        auto* m = mob();
        auto* t = target();
        return m && t && t->isAlive() && stateOf(*m).phase == Phase::Drop;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = target();
        if (!m || !t || !t->isAlive()) return false;
        if (stateOf(*m).phase != Phase::Drop) return false;
        if (mSeekTimer > mMaxSeekTime) return false;
        if (distanceToHoverSqr(*m) <= 1.0F) {
            // are we there yet?
            mHoverTimer++;
            return true;
        }
        if (mDropTimer < mMaxDropTime) return true;
        // max drop time!
        stateOf(*m).successfulDrops++;
        return false;
    }
    void stop() override {
        mHoverTimer = 0;
        mDropTimer  = 0;
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        if (mHoverTimer > 0) mHoverTimer++;
        else mSeekTimer++;
        if (mHoverTimer < mMaxHoverTime) {
            pushTowardsSpot(*m, 0.05);
            if (auto* t = target()) mobs::lookAt(*m, *t, 30.0F, 30.0F);
        } else {
            // drop!
            mDropTimer++;
            if (feet(*m).y > mDropY) {
                auto b = boxOf(*m);
                destroyBlocksInAABB(*m, AABB{Vec3{b.min.x - 1, b.min.y - 0.5f, b.min.z - 1}, Vec3{b.max.x + 1, b.max.y + 0.5f, b.max.z + 1}});
            }
        }
    }

private:
    void makeNewHoverSpot(Actor& target) override {
        HoverBase::makeNewHoverSpot(target);
        mDropY     = feet(target).y - 1.0F;
        mSeekTimer = 0;
    }
    int    mMaxHoverTime, mMaxDropTime, mMaxSeekTime;
    int    mHoverTimer = 0, mDropTimer = 0, mSeekTimer = 0;
    double mDropY      = 0;
};

/// Entity#calculateViewVector(xRot, yHeadRot) (LivingEntity#getViewYRot is the head's yaw).
Vec3 viewVector(Mob const& m) {
    double pitch = m.getRotation().x * std::numbers::pi / 180.0;
    double yaw   = -m.getYHeadRot() * std::numbers::pi / 180.0;
    double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    return Vec3{static_cast<float>(sy * cp), static_cast<float>(-sp), static_cast<float>(cy * cp)};
}

/// AABB#clip (the slab test): the entry point of the segment from -> to, nullopt when it misses.
std::optional<Vec3> clip(AABB const& b, Vec3 const& from, Vec3 const& to) {
    double tmin = 0.0, tmax = 1.0;
    double f[3] = {from.x, from.y, from.z}, t[3] = {to.x, to.y, to.z}, lo[3] = {b.min.x, b.min.y, b.min.z}, hi[3] = {b.max.x, b.max.y, b.max.z};
    for (int i = 0; i < 3; i++) {
        double d = t[i] - f[i];
        if (std::abs(d) < 1e-9) {
            if (f[i] < lo[i] || f[i] > hi[i]) return std::nullopt;
            continue;
        }
        double t1 = (lo[i] - f[i]) / d, t2 = (hi[i] - f[i]) / d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return Vec3{static_cast<float>(f[0] + (t[0] - f[0]) * tmin), static_cast<float>(f[1] + (t[1] - f[1]) * tmin), static_cast<float>(f[2] + (t[2] - f[2]) * tmin)};
}

class HoverBeamGoal final : public HoverBase {
public:
    HoverBeamGoal(Mob& m, int hoverTime, int beamTime) : HoverBase(m, 3.0F, 4.0F), mMaxHoverTime(hoverTime), mMaxBeamTime(beamTime), mMaxSeekTime(hoverTime) {}
    bool canUse() override {
        auto* m = mob();
        auto* t = target();
        return m && t && t->isAlive() && stateOf(*m).phase == Phase::Beam;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = target();
        if (!m || !t || !t->isAlive()) return false;
        if (stateOf(*m).phase != Phase::Beam) return false;
        if (mSeekTimer >= mMaxSeekTime) return false;
        return mBeamTimer < mMaxBeamTime;
    }
    void stop() override {
        mSeekTimer    = 0;
        mHoverTimer   = 0;
        mBeamTimer    = 0;
        mIsInPosition = false;
        if (auto* m = mob()) setBreathing(*m, false);
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        if (distanceToHoverSqr(*m) <= 1.0F) mIsInPosition = true;
        if (mIsInPosition) mHoverTimer++;
        else mSeekTimer++;
        if (mHoverTimer >= mMaxHoverTime) {
            mBeamTimer++;
            setBreathing(*m, true);
            doRayAttack(*m);
            // descend
            mHoverPosY -= 0.05F;
            if (mHoverPosY < mBeamY) mHoverPosY = mBeamY;
        }
        auto   p   = feet(*m);
        double ox  = mHoverPosX - p.x, oy = mHoverPosY - p.y, oz = mHoverPosZ - p.z;
        double dis = static_cast<float>(std::sqrt(static_cast<float>(ox * ox + oy * oy + oz * oz)));
        if (dis > 0.5) pushTowardsSpot(*m, 0.045F);
        if (auto* t = target()) {
            float tracking = mIsInPosition ? 1.0F : 20.0F;
            mobLookAt(*m, *t, tracking, tracking);
            mobs::lookAt(*m, *t, tracking, tracking);
        }
    }

private:
    void makeNewHoverSpot(Actor& target) override {
        HoverBase::makeNewHoverSpot(target);
        mBeamY     = feet(target).y;
        mSeekTimer = 0;
    }
public:
    static void doRayAttack(Mob& m) {
        double range = 20.0, offset = 10.0;
        auto   p     = feet(m);
        Vec3   src{p.x, p.y + 0.25f, p.z};
        Vec3   look = viewVector(m);
        Vec3   dest{src.x + look.x * static_cast<float>(range), src.y + look.y * static_cast<float>(range), src.z + look.z * static_cast<float>(range)};
        auto   b = boxOf(m);
        AABB   search{Vec3{b.min.x + look.x * 10 - 20, b.min.y + look.y * 10 - 20, b.min.z + look.z * 10 - 20},
                    Vec3{b.max.x + look.x * 10 + 20, b.max.y + look.y * 10 + 20, b.max.z + look.z * 10 + 20}};
        (void)offset;
        double hitDist = 0;
        for (auto* e : fe::actorsIn(m.getDimensionBlockSource(), static_cast<int>(m.getDimensionId()), search)) {
            if (e == &m || !e->isAlive() || !e->hasCategory(ActorCategory::Mob)) continue;
            if (gf::entity::ext::parentOf(*e) == &m) continue; // the shields
            AABB cb = boxOf(*e);
            if (cb.contains(src)) {
                if (0.0 < hitDist || hitDist == 0.0) {
                    gf::combat::hurt(*e, BREATH_DAMAGE, "twilightforest:chilling_breath", &m, &m);
                    hitDist = 0.0;
                }
            } else if (auto hit = clip(cb, src, dest)) {
                double d = std::sqrt((hit->x - src.x) * (hit->x - src.x) + (hit->y - src.y) * (hit->y - src.y) + (hit->z - src.z) * (hit->z - src.z));
                if (d < hitDist || hitDist == 0.0) {
                    gf::combat::hurt(*e, BREATH_DAMAGE, "twilightforest:chilling_breath", &m, &m);
                    hitDist = d;
                }
            }
        }
    }
    int    mMaxHoverTime, mMaxBeamTime, mMaxSeekTime;
    int    mHoverTimer = 0, mBeamTimer = 0, mSeekTimer = 0;
    double mBeamY        = 0;
    bool   mIsInPosition = false;
};

// ---- the shields ----------------------------------------------------------------------------------

float shieldAngle(Actor const& self, int idx) { return 60.0F * static_cast<float>(idx) + (static_cast<float>(self.mTickCount) * 5.0F); }

Vec3 shieldOffset(Mob& self, int idx) {
    if (idx == 6) return Vec3{0, -1.0f, 0}; // the last block beneath
    double a = shieldAngle(self, idx) * std::numbers::pi / 180.0;
    return Vec3{static_cast<float>(std::cos(a)), 0.1f, static_cast<float>(std::sin(a))};
}

void ensureShields(Mob& self) {
    if (!gf::entity::ext::parts(self).empty()) return;
    for (int i = 0; i < 7; i++)
        gf::entity::addPart(self, "shield_" + std::to_string(i), {0.75f, 0.75f}, [i](Mob& parent) { return shieldOffset(parent, i); });
}

/// SnowQueen#applyShieldCollisions: whatever touches a shield is pushed, and hurt and lifted.
void applyShieldCollisions(Mob& self) {
    for (auto const& h : gf::entity::ext::parts(self)) {
        auto* part = gf::entity::partActor(h);
        if (!part) continue;
        AABB b = boxOf(*part);
        AABB box{Vec3{b.min.x + 0.2f, b.min.y + 0.2f, b.min.z + 0.2f}, Vec3{b.max.x - 0.2f, b.max.y - 0.2f, b.max.z - 0.2f}};
        auto pp = feet(*part);
        for (auto* collided : fe::actorsIn(self.getDimensionBlockSource(), static_cast<int>(self.getDimensionId()), box)) {
            if (collided == &self || collided == part || gf::entity::ext::parentOf(*collided) == &self) continue;
            if (!collided->hasCategory(ActorCategory::Mob)) continue; // isPushable: living things
            // Entity#push(Entity)
            auto   cp = feet(*collided);
            double d0 = cp.x - pp.x, d1 = cp.z - pp.z;
            double d2 = std::max(std::abs(d0), std::abs(d1));
            if (d2 >= 0.01) {
                d2 = std::sqrt(d2);
                d0 /= d2;
                d1 /= d2;
                double d3 = std::min(1.0, 1.0 / d2);
                fe::push(*collided, Vec3{static_cast<float>(d0 * d3 * 0.05), 0, static_cast<float>(d1 * d3 * 0.05)});
            }
            if (doHurtTarget(self, *collided)) {
                fe::push(*collided, Vec3{0, 0.4f, 0});
                gf::fx::sound(self.getDimension(), "entity.twilightforest.snow_queen.attack", feet(self), 1.0F, 1.0F);
            }
        }
    }
}

// ---- SnowQueen#tick / aiStep / customServerAiStep --------------------------------------------------------

void queenSpawn(Actor& actor);
void recordArrows(Mob& self);

void queenTick(Actor& actor) {
    auto* self = fe::asMob(actor);
    if (!self || !self->isAlive()) return;
    if (!queenGoals().contains(uidOf(*self))) queenSpawn(*self); // a queen loaded from disk: her goals (and a fresh SUMMON)
    auto& q = stateOf(*self);
    // the hover goals push up; she sinks 0.05 a tick
    auto v = self->getPosDelta();
    fe::setMotion(*self, Vec3{v.x, v.y - 0.05f, v.z});

    ensureShields(*self);
    applyShieldCollisions(*self);
    recordArrows(*self);
    // the shields' angle is 60 x i + tick x 5 (shieldAngle): the client entity turns its shield
    // geometry from this anchor and its own clock (snow_queen.entity.json, v.shield_t)
    if (self->mTickCount % 20 == 0) gf::entity::setProperty(*self, "twilightforest:shield_tick", static_cast<int>(self->mTickCount % 72));

    // switch phases
    if (q.phase == Phase::Summon && q.summonsRemaining == 0 && countMyMinions(*self) <= 0) setCurrentPhase(*self, Phase::Drop);
    if (q.phase == Phase::Drop && q.successfulDrops >= q.maxDrops) setCurrentPhase(*self, Phase::Beam);
    if (q.phase == Phase::Beam && q.damageWhileBeaming >= MAX_DAMAGE_WHILE_BEAMING) setCurrentPhase(*self, Phase::Summon);

    // the client effects (SnowQueen#spawnParticles) are the client entity's own emitters, driven by
    // the synced phase and beam properties (snow_queen.entity.json, aurora.animation.json): the snow
    // around her, the ice beam along her look (10 a tick) and the drop warning at each shield. The
    // server sends no particle packets for them.
}

void queenSpawn(Actor& actor) {
    auto* self = fe::asMob(actor);
    if (!self || !queenGoals().insert(uidOf(*self)).second) return;
    (void)stateOf(*self);
    gf::entity::setMoveControl(*self, gf::entity::MoveKind::Hovering);
    gf::entity::addGoal(*self, 1, std::make_shared<HoverSummonGoal>(*self));
    gf::entity::addGoal(*self, 2, std::make_shared<HoverThenDropGoal>(*self, 80, 20));
    gf::entity::addGoal(*self, 3, std::make_shared<HoverBeamGoal>(*self, 80, 100));
    gf::entity::addGoal(*self, 5, gf::entity::goals::goHome(*self, 1.25));
    gf::entity::addGoal(*self, 6, std::make_shared<MeleeAttackGoal>(*self, 1.0));
    gf::entity::addGoal(*self, 8, std::make_shared<LookAtPlayerGoal>(*self));
    gf::entity::addGoal(*self, 8, std::make_shared<RandomLookAroundGoal>(*self));
    ensureShields(*self);
}

bool queenHurt(Actor& actor, float&, std::string const&, Actor*) {
    auto const& part = gf::entity::ext::hurtPart();
    if (part.starts_with("shield_")) {
        // SnowQueenIceShield#hurt: blocked, with the break sound (a piercing arrow never gets here:
        // it passes the shield, see installSnowQueen)
        gf::fx::sound(actor.getDimension(), "entity.twilightforest.snow_queen.break", feet(actor), 1.0F, (rr() * 0.7F + 1.0F) * 2.0F);
        return false;
    }
    // BEAM's damage counts once the hurt went through (SnowQueen#hurt: result && phase == BEAM):
    // the combat::onDealt handler of installSnowQueen
    return true;
}

/// An arrow's motion (F8's projectiles keep their own velocity).
Vec3 currentVelocity(Actor& e) { return gf::projectile::ext::isProjectile(e) ? gf::projectile::ext::velocity(e) : e.getPosDelta(); }

/// The arrows near a queen as they were at her last tick (position and motion): the engine may
/// stop an arrow on contact before the hurt reaches the shield, so the pierce pass-through reads
/// the flight from here.
struct ArrowSnap {
    Vec3          pos;
    Vec3          vel;
    std::uint64_t stamp;
};
std::unordered_map<std::int64_t, ArrowSnap>& arrowSnaps() {
    static std::unordered_map<std::int64_t, ArrowSnap> m;
    return m;
}

void recordArrows(Mob& self) {
    // the level's tick (every queen records into one map: a counter of their own would age it faster)
    std::uint64_t now = self.getLevel().getCurrentServerTick().tickID;
    AABB                 b     = boxOf(self);
    AABB                 around{Vec3{b.min.x - 6.0f, b.min.y - 6.0f, b.min.z - 6.0f}, Vec3{b.max.x + 6.0f, b.max.y + 6.0f, b.max.z + 6.0f}};
    for (auto* e : fe::actorsIn(self.getDimensionBlockSource(), static_cast<int>(self.getDimensionId()), around)) {
        if (!e || !e->isAlive() || !e->getEntityContext().tryGetComponent<ProjectileComponent>()) continue;
        auto v = currentVelocity(*e);
        if (std::abs(v.x) + std::abs(v.y) + std::abs(v.z) < 0.05f) continue; // stopped: keep its last flight
        arrowSnaps()[uidOf(*e)] = ArrowSnap{feet(*e), v, now};
    }
    if (now % 40 == 0) std::erase_if(arrowSnaps(), [now](auto const& kv) { return now - kv.second.stamp > 200; });
}

/// The piercing level of an arrow (ProjectileComponent; 0 for anything else).
int pierceLevel(Actor& projectile) {
    auto pc = projectile.getEntityContext().tryGetComponent<ProjectileComponent>();
    return pc ? static_cast<int>(pc->mPiercingEnchantLevel) : 0;
}

} // namespace

void installSnowQueen() {
    gf::entity::ext::setFlyingSpeed(kSnowQueen, 0.23);
    gf::entity::ext::onBreathAttack(kSnowQueen, [](Mob& breather, Actor& target) {
        gf::combat::hurt(target, BREATH_DAMAGE, "twilightforest:chilling_breath", &breather, &breather);
    });
    gf::entity::registerType(kSnowQueen, {
        .onSpawn = queenSpawn,
        .tick    = queenTick,
        .hurt    = queenHurt,
        .die     = [](Actor& self, std::string const&, Actor*) {
            queens().erase(uidOf(self));
            queenGoals().erase(uidOf(self));
        },
    });
    // a client opens many worlds in one process and unique ids repeat: no state outlives its world
    gf::game::db::onWorldClosed([] {
        queens().clear();
        queenGoals().clear();
        arrowSnaps().clear();
    });
    // SnowQueen#hurt: damageWhileBeaming += damage when the hurt succeeded (not the shields' hits)
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (d.victim.getTypeName() != kSnowQueen || d.amount <= 0.0F) return;
        auto it = queens().find(uidOf(d.victim));
        if (it != queens().end() && it->second.phase == Phase::Beam) it->second.damageWhileBeaming += static_cast<int>(d.amount);
    });
    // SnowQueenIceShield#hurt: `source.getDirectEntity() instanceof AbstractArrow arrow && arrow.getPierceLevel() > 0`
    // returns true without any effect: the piercing arrow flies on through the shield
    gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
        auto* parent = gf::entity::ext::parentOf(d.victim);
        if (!parent || parent->getTypeName() != kSnowQueen) return gf::combat::Verdict::Pass;
        // the arrow: the damage's direct entity, or (F10 reports an arrow's hit with the shooter as
        // the direct entity, type minecraft:thrown) the arrow at the shield that made this hit
        Actor* projectile = d.direct && pierceLevel(*d.direct) > 0 ? d.direct : nullptr;
        if (!projectile && (d.type == "minecraft:thrown" || d.type == "minecraft:arrow" || d.type == "minecraft:projectile")) {
            // of every arrow of this shooter near the shield (piercing or not): the one whose flight
            // (its motion now, or the one recorded at the queen's last tick, before the engine
            // stopped it) crosses the shield's box first, else the nearest one. A plain arrow there
            // means a plain hit (blocked as TF), even with a piercing one nearby.
            AABB         b = d.victim.getAABB();
            AABB         near{Vec3{b.min.x - 3.0f, b.min.y - 3.0f, b.min.z - 3.0f}, Vec3{b.max.x + 3.0f, b.max.y + 3.0f, b.max.z + 3.0f}};
            AABB         hitBox{Vec3{b.min.x - 0.3f, b.min.y - 0.3f, b.min.z - 0.3f}, Vec3{b.max.x + 0.3f, b.max.y + 0.3f, b.max.z + 0.3f}};
            Vec3         centre{(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2};
            std::int64_t shooter = d.attacker ? uidOf(*d.attacker) : 0;
            double       best    = 1e9;
            Actor*       hitting = nullptr;
            for (auto* e : fe::actorsIn(d.victim.getDimensionBlockSource(), static_cast<int>(d.victim.getDimensionId()), near)) {
                if (!e || !e->isAlive() || !e->getEntityContext().tryGetComponent<ProjectileComponent>()) continue;
                std::int64_t owner = e->getOwnerId().rawID;
                if (shooter != 0 && owner != 0 && owner != -1 && owner != shooter) continue; // another shooter's arrow
                auto   p = feet(*e);
                double score;
                if (hitBox.contains(p)) score = 0.0;
                else {
                    score        = 1e9;
                    auto segment = [&](Vec3 from, Vec3 to) {
                        if (auto hit = clip(hitBox, from, to)) {
                            double hx = hit->x - from.x, hy = hit->y - from.y, hz = hit->z - from.z;
                            score     = std::min(score, std::sqrt(hx * hx + hy * hy + hz * hz));
                        }
                    };
                    auto v = currentVelocity(*e);
                    segment(Vec3{p.x - v.x, p.y - v.y, p.z - v.z}, Vec3{p.x + v.x, p.y + v.y, p.z + v.z});
                    if (auto it = arrowSnaps().find(uidOf(*e)); it != arrowSnaps().end()) {
                        auto const& r = it->second;
                        segment(r.pos, Vec3{r.pos.x + r.vel.x * 2.0f, r.pos.y + r.vel.y * 2.0f, r.pos.z + r.vel.z * 2.0f});
                    }
                    // none crosses: the nearest arrow, after any that does
                    if (score >= 1e9) score = 100.0 + std::sqrt((p.x - centre.x) * (p.x - centre.x) + (p.y - centre.y) * (p.y - centre.y) + (p.z - centre.z) * (p.z - centre.z));
                }
                if (score < best) {
                    best    = score;
                    hitting = e;
                }
            }
            if (hitting && pierceLevel(*hitting) > 0) projectile = hitting;
        }
        if (!projectile) return gf::combat::Verdict::Pass;
        auto arrow = projectile->getOrCreateUniqueID().rawID;
        auto v     = currentVelocity(*projectile);
        auto at    = feet(*projectile);
        // stopped on contact: the motion it had at the queen's last tick
        if (std::abs(v.x) + std::abs(v.y) + std::abs(v.z) < 0.05f)
            if (auto it = arrowSnaps().find(arrow); it != arrowSnaps().end()) v = it->second.vel;
        gf::interactions::nextTick([arrow, v, at] {
            auto* a = fe::actorById(arrow);
            if (!a || !a->isAlive()) return;
            // past the shield (0.75 wide) along its flight, with the speed it had
            double len = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
            if (len > 1e-6) {
                float k = static_cast<float>(1.0 / len);
                a->teleportTo(Vec3{at.x + v.x * k, at.y + v.y * k, at.z + v.z * k}, false, 0, 1, false);
            }
            if (gf::projectile::ext::isProjectile(*a)) gf::projectile::ext::setVelocity(*a, v);
            else fe::setMotion(*a, v); // a vanilla arrow: its motion
        });
        gf::log().info("aurora: a piercing arrow passes a snow queen shield");
        return gf::combat::Verdict::Cancel;
    });
    gf::boss::registerBoss({
        .entityId        = kSnowQueen,
        .homeStructure   = kAuroraPalace,
        .lootTable       = "twilightforest:entities/snow_queen",
        .deathChestBlock = "twilightforest:twilight_oak_chest",
        .bar             = {.rgb = 0x8CF0F0},
    });
    gf::boss::ext::setHomeRadius(kSnowQueen, 20);
    gf::boss::ext::setLifecycle(kSnowQueen, {
        .tickDeath = [](Mob& boss, int) {
            // the death animation: explosion and poof particles around her
            auto p = feet(boss);
            for (int k = 0; k < 5; k++) {
                Vec3 v{static_cast<float>(rng().nextGaussian() * 0.02), static_cast<float>(rng().nextGaussian() * 0.02), static_cast<float>(rng().nextGaussian() * 0.02)};
                Vec3 at{p.x + rng().nextFloat() * kWidth * 2.0f - kWidth, p.y + rng().nextFloat() * kHeight, p.z + rng().nextFloat() * kWidth * 2.0f - kWidth};
                gf::fx::particle(boss.getDimension(), rng().nextBoolean() ? "minecraft:explosion" : "minecraft:poof", at, v);
            }
        },
    });
    gf::boss::registerSpawner({.blockId = "twilightforest:snow_queen_boss_spawner", .entityId = kSnowQueen, .range = 9, .particle = "twilightforest:snow"});
}

/// The current phase (selftests): 0 summon, 1 drop, 2 beam; -1 when the queen has no state yet.
int queenPhaseForTest(Actor& queen) {
    auto it = queens().find(uidOf(queen));
    return it == queens().end() ? -1 : static_cast<int>(it->second.phase);
}
void setQueenPhaseForTest(Actor& queen, int phase) { setCurrentPhase(queen, static_cast<Phase>(phase)); }
int  queenSummonsForTest(Actor& queen) { return stateOf(queen).summonsRemaining; }
int  queenDropsForTest(Actor& queen, int* maxDrops) {
    auto& q = stateOf(queen);
    if (maxDrops) *maxDrops = q.maxDrops;
    return q.successfulDrops;
}
int  queenBeamDamageForTest(Actor& queen) { return stateOf(queen).damageWhileBeaming; }
bool queenBreathingForTest(Actor& queen) { return stateOf(queen).breathing; }
void rayAttackForTest(Mob& queen) { HoverBeamGoal::doRayAttack(queen); }
int  pierceLevelForTest(Actor& projectile) { return pierceLevel(projectile); }

} // namespace gf::feature::aurora
