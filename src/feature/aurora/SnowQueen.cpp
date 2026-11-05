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
#include <array>
#include <cmath>
#include <map>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/aurora/Mobs.h"
#include "feature/aurora/Structures.h"
#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
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

/// SnowQueen#doHurtTarget: squish during DROP, else the mob attack.
bool doHurtTarget(Mob& self, Actor& target) {
    if (stateOf(self).phase == Phase::Drop) return gf::combat::hurt(target, 7.0f, "twilightforest:squish", &self, &self);
    return mobs::doHurtTarget(self, target);
}

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
    static bool isPositionOccupied(Mob& m, double hx, double hy, double hz) {
        float radius = kWidth / 2.0F;
        auto& region = m.getDimensionBlockSource();
        for (int x = static_cast<int>(std::floor(hx - radius)); x <= static_cast<int>(std::floor(hx + radius)); x++)
            for (int y = static_cast<int>(std::floor(hy)); y <= static_cast<int>(std::floor(hy + kHeight)); y++)
                for (int z = static_cast<int>(std::floor(hz - radius)); z <= static_cast<int>(std::floor(hz + radius)); z++)
                    if (!region.getBlock(BlockPos{x, y, z}).isAir()) return true;
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
            mobs::lookAt(*m, *t, tracking, tracking);
        }
    }

private:
    void makeNewHoverSpot(Actor& target) override {
        HoverBase::makeNewHoverSpot(target);
        mBeamY     = feet(target).y;
        mSeekTimer = 0;
    }
    static void doRayAttack(Mob& m) {
        double range = 20.0, offset = 10.0;
        auto   p     = feet(m);
        Vec3   src{p.x, p.y + 0.25f, p.z};
        Vec3   look = m.getViewVector(1.0f);
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

void queenTick(Actor& actor) {
    auto* self = fe::asMob(actor);
    if (!self || !self->isAlive()) return;
    auto& q = stateOf(*self);
    // the hover goals push up; she sinks 0.05 a tick
    auto v = self->getPosDelta();
    fe::setMotion(*self, Vec3{v.x, v.y - 0.05f, v.z});

    ensureShields(*self);
    applyShieldCollisions(*self);

    // switch phases
    if (q.phase == Phase::Summon && q.summonsRemaining == 0 && countMyMinions(*self) <= 0) setCurrentPhase(*self, Phase::Drop);
    if (q.phase == Phase::Drop && q.successfulDrops >= q.maxDrops) setCurrentPhase(*self, Phase::Beam);
    if (q.phase == Phase::Beam && q.damageWhileBeaming >= MAX_DAMAGE_WHILE_BEAMING) setCurrentPhase(*self, Phase::Summon);

    // the client effects (SnowQueen#spawnParticles), sent through fx
    auto& dim = self->getDimension();
    snowParticles(*self, 2.125f, 3);
    if (q.phase == Phase::Drop)
        for (auto const& h : gf::entity::ext::parts(*self))
            if (auto* part = gf::entity::partActor(h)) gf::fx::particle(dim, "twilightforest:snow_warning", feet(*part), Vec3{}, 1, nlohmann::json{{"spread", {0.2, 0.2, 0.2}}});
    if (q.breathing) {
        Vec3   look = self->getViewVector(1.0f);
        auto   p    = feet(*self);
        Vec3   at{p.x + look.x * 0.5f, p.y + 1.7f + look.y * 0.5f, p.z + look.z * 0.5f};
        for (int i = 0; i < 10; i++) {
            double spread   = 2.0 + rng().nextDouble() * 2.5;
            double velocity = 2.0 + rng().nextDouble() * 0.15;
            double dx       = (look.x + rng().nextGaussian() * 0.0075 * spread) * velocity;
            double dy       = (rng().nextGaussian() * 0.0075 * spread) * velocity;
            double dz       = (look.z + rng().nextGaussian() * 0.0075 * spread) * velocity;
            gf::fx::particle(dim, "twilightforest:ice_beam", at, Vec3{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)});
        }
    }
}

void queenSpawn(Actor& actor) {
    auto* self = fe::asMob(actor);
    if (!self) return;
    (void)stateOf(*self);
    gf::entity::setMoveControl(*self, gf::entity::MoveKind::Hovering);
    gf::entity::addGoal(*self, 1, std::make_shared<HoverSummonGoal>(*self));
    gf::entity::addGoal(*self, 2, std::make_shared<HoverThenDropGoal>(*self, 80, 20));
    gf::entity::addGoal(*self, 3, std::make_shared<HoverBeamGoal>(*self, 80, 100));
    gf::entity::addGoal(*self, 5, gf::entity::goals::goHome(*self, 1.25));
    gf::entity::addGoal(*self, 6, gf::entity::goals::simplifiedAttack(*self));
    ensureShields(*self);
}

bool queenHurt(Actor& actor, float& amount, std::string const&, Actor*) {
    auto const& part = gf::entity::ext::hurtPart();
    if (part.starts_with("shield_")) {
        // SnowQueenIceShield#hurt: blocked, with the break sound (a piercing arrow passes, harmless)
        gf::fx::sound(actor.getDimension(), "entity.twilightforest.snow_queen.break", feet(actor), 1.0F, (rr() * 0.7F + 1.0F) * 2.0F);
        return false;
    }
    auto& q = stateOf(actor);
    if (q.phase == Phase::Beam) q.damageWhileBeaming += static_cast<int>(amount);
    return true;
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
        .die     = [](Actor& self, std::string const&, Actor*) { queens().erase(uidOf(self)); },
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
int queenBeamDamageForTest(Actor& queen) { return stateOf(queen).damageWhileBeaming; }

} // namespace gf::feature::aurora
