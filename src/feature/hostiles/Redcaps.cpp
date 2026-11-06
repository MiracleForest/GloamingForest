// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/Redcap.java
//   src/main/java/twilightforest/entity/monster/RedcapSapper.java
//   src/main/java/twilightforest/entity/ai/goal/RedcapBaseGoal.java
//   src/main/java/twilightforest/entity/ai/goal/RedcapShyGoal.java
//   src/main/java/twilightforest/entity/ai/goal/RedcapLightTNTGoal.java
//   src/main/java/twilightforest/entity/ai/goal/RedcapPlantTNTGoal.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The redcap and the redcap sapper. Float, look and the targets are the BP's behaviours; TF's goals
// run natively: 1 AvoidAnyEntity primed TNT, 2 RedcapShy, 3 RedcapLightTNT, (4 RedcapPlantTNT); the
// BP's melee (5) and stroll (6) yield while one of them holds MOVE (group gf:vanilla_move). The held
// stacks (pick, flint and steel, TNT) are swapped into the main hand as in TF; TNTLeft is saved.

#include <cmath>
#include <memory>
#include <optional>
#include <random>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::hostiles::redcaps {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

namespace {

using Slot = SharedTypes::Legacy::EquipmentSlot;

Mob* redcapById(std::int64_t uid) { return liveMob(uid); }

bool isSapper(Actor const& a) { return typeOf(a) == id::RedcapSapper; }

// ---- held stacks ------------------------------------------------------------------------------------------

int tntCount(Actor& redcap) {
    auto& v = ge::data(redcap, "hostiles:tnt_left");
    if (!v.is_number_integer()) v = isSapper(redcap) ? 3 : 1; // the constructor's heldTNT
    return v.get<int>();
}

void setTntCount(Actor& redcap, int n) { ge::data(redcap, "hostiles:tnt_left") = std::max(n, 0); }

/// Redcap#heldPick (iron pickaxe; the sapper's ironwood pickaxe), with the enchantments it spawned with.
ItemStack heldPick(Actor& redcap) {
    auto const& saved = ge::data(redcap, "hostiles:pick");
    if (saved.is_object()) return gf::game::items::fromJson(saved);
    auto s = gf::game::items::make(isSapper(redcap) ? "twilightforest:ironwood_pickaxe" : "minecraft:iron_pickaxe");
    return s ? *s : gf::game::items::empty();
}

void holdPick(Mob& m) { setMainHand(m, heldPick(m)); }
void holdFlint(Mob& m) {
    if (auto s = gf::game::items::make("minecraft:flint_and_steel")) setMainHand(m, *s);
}
void holdTnt(Mob& m) {
    int n = tntCount(m);
    if (n <= 0) {
        setMainHand(m, gf::game::items::empty());
        return;
    }
    if (auto s = gf::game::items::make("minecraft:tnt", n)) setMainHand(m, *s);
}

/// Mob#playAmbientSound.
void playAmbient(Mob& m) { playSound(m, "entity.twilightforest.redcap.ambient", 1.0F, voicePitch(m)); }

/// Redcap#finalizeSpawn: populateDefaultEquipmentSlots + enchantments, drop chances 0.2.
void finalizeSpawn(Mob& m) {
    bool sapper = isSapper(m);
    if (auto s = gf::game::items::make(sapper ? "twilightforest:ironwood_pickaxe" : "minecraft:iron_pickaxe")) setMainHand(m, *s);
    if (auto s = gf::game::items::make(sapper ? "twilightforest:ironwood_boots" : "minecraft:iron_boots")) setFeet(m, *s);
    enchantSpawnEquipment(m);
    ge::data(m, "hostiles:pick") = gf::game::items::toJson(m.getItemSlot(Slot::Mainhand));
    (void)tntCount(m);
}

// ---- RedcapBaseGoal ----------------------------------------------------------------------------------------

/// Returns true in a 120 degree arc in front of the target's view.
bool isTargetLookingAtMe(Actor& redcap, Actor& target) {
    auto   r  = redcap.getFeetPos();
    auto   t  = target.getFeetPos();
    double dx = r.x - t.x;
    double dz = r.z - t.z;
    float  angle      = static_cast<float>((std::atan2(dz, dx) * 180.0) / 3.141592653589793) - 90.0F;
    float  difference = std::abs(std::fmod(target.getRotation().y - angle, 360.0F));
    return difference < 60.0F || difference > 300.0F;
}

std::optional<BlockPos> findBlockTNTNearby(Actor& redcap, int range) {
    static Block const* tnt = gf::game::blockWith("minecraft:tnt");
    if (!tnt) return std::nullopt;
    auto const& tntType = tnt->getBlockType();
    auto&       region  = redcap.getDimensionBlockSource();
    auto        p       = blockPosOf(redcap);
    for (int x = -range; x <= range; x++)
        for (int y = -range; y <= range; y++)
            for (int z = -range; z <= range; z++) {
                BlockPos at{p.x + x, p.y + y, p.z + z};
                if (&region.getBlock(at).getBlockType() == &tntType) return at;
            }
    return std::nullopt;
}

bool isLitTNTNearby(Actor& redcap, int range) {
    auto bb = redcap.getAABB();
    AABB box{Vec3{bb.min.x - range, bb.min.y - range, bb.min.z - range}, Vec3{bb.max.x + range, bb.max.y + range, bb.max.z + range}};
    for (auto* e : fe::actorsIn(redcap.getDimensionBlockSource(), dimOf(redcap), box))
        if (e && e->getTypeName() == "minecraft:tnt") return true;
    return false;
}

/// Redcap#isShy: not hurt by a player in the last 100 ticks.
bool isShy(Mob& m) { return m.mLastHurtByPlayerTime <= 0; }

// ---- goals ---------------------------------------------------------------------------------------------------

class ShyGoal final : public ge::Goal {
public:
    ShyGoal(Mob& m, float speed) : mId(uidOf(m)), mSpeed(speed), mLock(uidOf(m)) {
        flags = Move | Look;
        static std::mt19937 rng{std::random_device{}()};
        mLefty = std::uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5; // Math.random() < 0.5
    }
    bool canUse() override {
        auto* m = redcapById(mId);
        if (!m) return false;
        auto* t = targetOf(*m);
        if (!t || !isShy(*m)) return false;
        double d = distance(*t, *m);
        if (d > kMax || d < kMin || !isTargetLookingAtMe(*m, *t)) return false;
        mTarget = uidOf(*t);
        mDest   = findCirclePoint(*m, *t, 5.0, mLefty ? 1.0 : -1.0);
        return true;
    }
    void start() override {
        mLock.hold(true);
        if (auto* m = redcapById(mId)) mobs::moveTo(*m, mDest, mSpeed);
    }
    bool canContinueToUse() override {
        auto* m = redcapById(mId);
        if (!m) return false;
        auto* t      = targetOf(*m);
        auto* chosen = actorById(mTarget);
        if (!t || !chosen || !chosen->isAlive() || mobs::navigationDone(*m)) return false;
        double d = distance(*t, *m);
        return isShy(*m) && d < kMax && d > kMin && isTargetLookingAtMe(*m, *t);
    }
    void tick() override {
        auto* m = redcapById(mId);
        auto* t = actorById(mTarget);
        if (m && t) mobs::lookAt(*m, *t, 30.0F, 30.0F);
    }
    void stop() override {
        mTarget = 0;
        if (auto* m = redcapById(mId)) mobs::stopNavigation(*m);
        mLock.hold(false);
    }

private:
    static constexpr double kMin = 3.0;
    static constexpr double kMax = 6.0;
    static Vec3 findCirclePoint(Actor& circler, Actor& toCircle, double radius, double rotation) {
        auto   c      = circler.getFeetPos();
        auto   t      = toCircle.getFeetPos();
        double vecx   = c.x - t.x;
        double vecz   = c.z - t.z;
        float  rangle = static_cast<float>(std::atan2(vecz, vecx));
        rangle += static_cast<float>(rotation); // add a little, so he circles
        double dx = std::cos(rangle) * radius;
        double dz = std::sin(rangle) * radius;
        return Vec3{float(t.x + dx), circler.getAABB().min.y, float(t.z + dz)};
    }
    std::int64_t mId;
    float        mSpeed;
    bool         mLefty  = false;
    std::int64_t mTarget = 0;
    Vec3         mDest{};
    MoveLock     mLock;
};

class LightTNTGoal final : public ge::Goal {
public:
    LightTNTGoal(Mob& m, float speed) : mId(uidOf(m)), mSpeed(speed), mLock(uidOf(m)) { flags = Move | Look; }
    bool canUse() override {
        auto* m = redcapById(mId);
        if (!m || !mobGriefing()) return false;
        if (mDelay > 0) {
            --mDelay;
            return false;
        }
        auto tnt = findBlockTNTNearby(*m, 8);
        if (!tnt) return false;
        mTnt = *tnt;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = redcapById(mId);
        return m && mTnt && gf::game::blockIdAt(m->getDimensionBlockSource(), *mTnt) == "minecraft:tnt";
    }
    void start() override {
        mLock.hold(true);
        if (auto* m = redcapById(mId)) holdFlint(*m);
    }
    void stop() override {
        if (auto* m = redcapById(mId)) {
            mobs::stopNavigation(*m);
            holdPick(*m);
        }
        mDelay = 20;
        mTnt.reset();
        mLock.hold(false);
    }
    void tick() override {
        auto* m = redcapById(mId);
        if (!m || !mTnt) return;
        auto  p = *mTnt;
        Vec3  corner{float(p.x), float(p.y), float(p.z)};
        mobs::lookAt(*m, corner, 30.0F, 90.0F); // getMaxHeadXRot
        if (distSqr(*m, corner) < 2.4 * 2.4) {
            playAmbient(*m);
            // TntBlock#onCaughtFire -> explode: a primed TNT at the block's centre, by the redcap
            auto& region = m->getDimensionBlockSource();
            spawnActor(region, "minecraft:tnt", Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f}, m);
            gf::fx::sound(m->getDimension(), "random.fuse", Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f}, 1.0F, 1.0F);
            m->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            gf::game::clearBlock(region, p);
            mobs::stopNavigation(*m);
            log().info("hostiles: {} lit TNT at {} {} {}", typeOf(*m), p.x, p.y, p.z);
        } else {
            mobs::moveTo(*m, corner, mSpeed);
        }
    }

private:
    std::int64_t            mId;
    float                   mSpeed;
    int                     mDelay = 0;
    std::optional<BlockPos> mTnt;
    MoveLock                mLock;
};

class PlantTNTGoal final : public ge::Goal {
public:
    explicit PlantTNTGoal(Mob& m) : mId(uidOf(m)) {}
    bool canUse() override {
        auto* m = redcapById(mId);
        if (!m) return false;
        auto* t = targetOf(*m);
        return t && tntCount(*m) > 0 && mobs::distanceSqr(*m, *t) < 25.0 && !isTargetLookingAtMe(*m, *t) && mobGriefing()
            && !isLitTNTNearby(*m, 8) && !findBlockTNTNearby(*m, 5);
    }
    void start() override {
        auto* m = redcapById(mId);
        if (!m) return;
        auto pos = blockPosOf(*m);
        holdTnt(*m);
        auto& region = m->getDimensionBlockSource();
        if (region.getBlock(pos).isAir()) {
            setTntCount(*m, tntCount(*m) - 1);
            playAmbient(*m);
            if (auto const* tnt = gf::game::blockWith("minecraft:tnt")) gf::game::setBlock(region, pos, *tnt);
            log().info("hostiles: {} planted TNT at {} {} {}", typeOf(*m), pos.x, pos.y, pos.z);
        }
    }
    void stop() override {
        if (auto* m = redcapById(mId)) holdPick(*m);
    }

private:
    std::int64_t mId;
};

void redcapTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (firstSpawn(self)) finalizeSpawn(*m);
    if (!nativeGoalsPresent(self)) {
        ge::addGoal(*m, 1, std::make_shared<LockedGoal>(*m, ge::goals::avoidAny(*m, [](Actor& a) { return a.getTypeName() == "minecraft:tnt"; }, 2.0F, 1.0, 2.0)));
        ge::addGoal(*m, 2, std::make_shared<ShyGoal>(*m, 1.0F));
        ge::addGoal(*m, 3, std::make_shared<LightTNTGoal>(*m, 1.0F));
        if (isSapper(self)) ge::addGoal(*m, 4, std::make_shared<PlantTNTGoal>(*m));
    }
    syncVanillaMove(*m);
}

} // namespace

int tntLeft(Actor& redcap) { return tntCount(redcap); }

void install() {
    for (char const* type : {id::Redcap, id::RedcapSapper})
        ge::registerType(type, {.tick = redcapTick, .die = forgetActor});
    gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& ctx) {
        auto type = typeOf(dead);
        if (type != id::Redcap && type != id::RedcapSapper) return;
        // Redcap#finalizeSpawn: setDropChance(MAINHAND, 0.2), setDropChance(FEET, 0.2)
        if (auto* m = asMob(dead)) dropEquipment(*m, drops, 0.2F, 0.2F, ctx.player != nullptr, ctx.attacker);
    });
}

} // namespace gf::feature::hostiles::redcaps
