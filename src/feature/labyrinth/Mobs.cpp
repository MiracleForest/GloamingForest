// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/Minotaur.java
//   src/main/java/gloamingforest/entity/boss/Minoshroom.java
//   src/main/java/gloamingforest/entity/monster/MazeSlime.java
//   src/main/java/gloamingforest/entity/ai/goal/GroundAttackGoal.java
//   src/main/java/gloamingforest/block/entity/spawner/MinoshroomSpawnerBlockEntity.java
//   src/main/java/gloamingforest/util/EntityUtil.java (properlyApplyCustomDamageSource)
//   net/minecraft/world/entity/ai/goal/MeleeAttackGoal.java, RandomStrollGoal.java,
//   WaterAvoidingRandomStrollGoal.java, LookAtPlayerGoal.java, RandomLookAroundGoal.java (vanilla
//   goals of the minotaurs), net/minecraft/world/entity/monster/Slime.java (setSize, remove,
//   finalizeSpawn, tick) and Mob.java (populateDefaultEquipmentSlots)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U23's mobs. The minotaurs' goals run natively on F8's goal selector (Java's priorities and
// flags): float and the targets (hurt-by, nearest player) are the BP's; the charge is F8's
// ChargeAttackGoal (goals::charge), the ground slam, the melee attack (axing damage through
// EntityUtil#properlyApplyCustomDamageSource), the stroll and the looks are here. The ground
// slam's charge and the charge's windup are entity properties the client animations read
// (gloamingforest:ground_attack, ground_charge, charging).
//
// The Maze Slime: a slime of size 1, 2 or 4 (the BP's size groups: collision box, health 3 x
// size^2, attack = size, the vanilla slime jump behaviours); its split on death, sounds and its
// mazestone landing particles are native.
#include "feature/labyrinth/Mobs.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <numbers>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/util/VariantParameterList.h"

#include "core/jrand/JRand.h"
#include "core/labyrinth/Terraformer.h"
#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/labyrinth/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::labyrinth::mobs {

namespace fe = gf::feature::entity;
namespace fm = gf::feature::entity::mobs;
using Slot   = SharedTypes::Legacy::EquipmentSlot;

namespace {

constexpr char kAxing[]        = "gloamingforest:axing";
constexpr char kSlam[]         = "gloamingforest:slam";
constexpr char kDiamondAxe[]   = "gloamingforest:diamond_minotaur_axe";
constexpr char kLabyrinth[]    = "gloamingforest:labyrinth";
constexpr char kSpawnerBlock[] = "gloamingforest:minoshroom_boss_spawner";

gf::core::jrand::RandomSource& rng() {
    static gf::core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 20 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

Vec3 feet(Actor const& a) { return a.getFeetPos(); }

bool isType(Actor const& a, char const* id) { return a.getTypeName() == id; }
bool isMinotaurKind(Actor const& a) { return isType(a, kMinotaur) || isType(a, kMinoshroom); }

Mob* mobOf(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

/// Whether F8 holds native goals for the mob (they are added once per session).
bool goalsPresent(Mob& mob) {
    auto& s = fe::stateOf(mob);
    return s.goals && s.goals->size() > 0;
}

// ---- DifficultyInstance -------------------------------------------------------------------------------------

struct Difficulty {
    float effective = 0.0F;
    float special   = 0.0F;
};

/// DifficultyInstance(level difficulty, dayTime, inhabitedTime 0, moonBrightness): Bedrock keeps
/// no chunk inhabited time.
Difficulty difficulty() {
    auto level = ll::service::getLevel();
    if (!level) return {};
    auto diff = level->getDifficulty();
    if (diff == SharedTypes::Legacy::Difficulty::Peaceful) return {};
    float f  = 0.75F;
    float f1 = std::clamp((static_cast<float>(level->getTime()) + -72000.0F) / 1440000.0F, 0.0F, 1.0F) * 0.25F;
    f += f1;
    static constexpr float kMoon[] = {1.0F, 0.75F, 0.5F, 0.25F, 0.0F, 0.25F, 0.5F, 0.75F};
    float                  moon    = kMoon[(static_cast<long long>(level->getTime()) / 24000LL % 8 + 8) % 8];
    float                  f2      = std::clamp(moon * 0.25F, 0.0F, f1);
    if (diff == SharedTypes::Legacy::Difficulty::Easy) f2 *= 0.5F;
    f += f2;
    Difficulty d;
    d.effective = static_cast<float>(static_cast<int>(diff)) * f;
    d.special   = d.effective < 2.0F ? 0.0F : (d.effective > 4.0F ? 1.0F : (d.effective - 2.0F) / 2.0F);
    return d;
}

bool hardDifficulty() {
    auto level = ll::service::getLevel();
    return level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Hard;
}

// ---- equipment ----------------------------------------------------------------------------------------------

void syncEquipment(Mob& mob) {
    mob.sendInventory(false);
    mob.sendArmor(std::bitset<5>{0b1111});
}

void equip(Mob& mob, Slot slot, char const* id) {
    auto s = gf::game::items::make(id, 1, 0);
    if (!s) return;
    if (slot == Slot::Torso || slot == Slot::Legs || slot == Slot::Feet || slot == Slot::Head) {
        static constexpr std::pair<Slot, SharedTypes::Legacy::ArmorSlot> kArmor[] = {
            {Slot::Head, SharedTypes::Legacy::ArmorSlot::Head},
            {Slot::Torso, SharedTypes::Legacy::ArmorSlot::Torso},
            {Slot::Legs, SharedTypes::Legacy::ArmorSlot::Legs},
            {Slot::Feet, SharedTypes::Legacy::ArmorSlot::Feet},
        };
        if (!mob.setItemSlot(slot, *s) || gf::game::items::isEmpty(mob.getItemSlot(slot)))
            for (auto [sl, armor] : kArmor)
                if (sl == slot) mob.setArmor(armor, *s);
        return;
    }
    mob.setItemSlot(slot, *s);
}

/// Mob#getEquipmentForSlot(slot, tier): leather, golden, chainmail, iron, diamond.
char const* equipmentForSlot(Slot slot, int tier) {
    static char const* const kTiers[] = {"leather", "golden", "chainmail", "iron", "diamond"};
    if (tier < 0 || tier > 4) return nullptr;
    static thread_local std::string id;
    char const*                     piece = slot == Slot::Head ? "helmet" : slot == Slot::Torso ? "chestplate" : slot == Slot::Legs ? "leggings" : "boots";
    id = std::string("minecraft:") + kTiers[tier] + "_" + piece;
    return id.c_str();
}

/// Mob#populateDefaultEquipmentSlots (the Minoshroom's super call): random armour.
void populateMobArmor(Mob& mob, gf::core::jrand::RandomSource& random, Difficulty const& d) {
    if (!(random.nextFloat() < 0.15F * d.special)) return;
    int   i = random.nextInt(2);
    float f = hardDifficulty() ? 0.1F : 0.25F;
    if (random.nextFloat() < 0.095F) ++i;
    if (random.nextFloat() < 0.095F) ++i;
    if (random.nextFloat() < 0.095F) ++i;
    bool flag = true;
    // EquipmentSlot.values() order of the humanoid armour: FEET, LEGS, CHEST, HEAD
    for (Slot slot : {Slot::Feet, Slot::Legs, Slot::Torso, Slot::Head}) {
        if (!flag && random.nextFloat() < f) break;
        flag = false;
        if (gf::game::items::isEmpty(mob.getItemSlot(slot)))
            if (char const* id = equipmentForSlot(slot, i)) equip(mob, slot, id);
    }
}

/// Mob#populateDefaultEquipmentEnchantments: the weapon (0.25) and each armour piece (0.5) times
/// the special multiplier, enchanted as MOB_SPAWN_EQUIPMENT (cost 5 .. 5 + special x 17).
void enchantSpawnedEquipment(Mob& mob, gf::core::jrand::RandomSource& random, Difficulty const& d) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (Slot slot : {Slot::Mainhand, Slot::Feet, Slot::Legs, Slot::Torso, Slot::Head}) {
        float     chance = slot == Slot::Mainhand ? 0.25F : 0.5F;
        ItemStack stack(mob.getItemSlot(slot));
        if (gf::game::items::isEmpty(stack) || !(random.nextFloat() < chance * d.special)) continue;
        int maxCost = 5 + static_cast<int>(d.special * 17.0F);
        int cost    = 5 + random.nextInt(maxCost - 5 + 1);
        EnchantUtils::randomlyEnchant(level->getIRandom(), stack, cost, 0, false);
        mob.setItemSlot(slot, stack);
    }
}

// ---- EntityUtil#properlyApplyCustomDamageSource --------------------------------------------------------------

thread_local int tCustomDamage = 0;

/// The minotaurs' doHurtTarget: their attack damage (+ the weapon's enchantments) as axing, the
/// knockback of a knockback weapon with the attack sound.
bool properlyApplyCustomDamageSource(Mob& self, Actor& victim, char const* flingSound) {
    float f = attackDamage(self) + EnchantUtils::getMeleeDamageBonus(victim, self);
    ++tCustomDamage;
    bool flag = gf::combat::hurt(victim, f, kAxing, &self, &self);
    --tCustomDamage;
    if (flag) {
        float f1 = static_cast<float>(EnchantUtils::getEnchantLevel(Enchant::Type::Knockback, self.getCarriedItem()));
        if (f1 > 0.0F) {
            gf::fx::sound(self.getDimension(), flingSound, feet(self), 1.0F, 1.0F);
            float yaw = self.getRotation().y * std::numbers::pi_v<float> / 180.0F;
            // LivingEntity#knockback(f1 * 0.5, sin(yaw), -cos(yaw)): scaled by 1 - knockback resistance
            float strength = f1 * 0.5F;
            if (auto* vm = fe::asMob(victim)) {
                auto        ref  = vm->getAttribute(SharedAttributes::KNOCKBACK_RESISTANCE());
                auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
                if (inst) strength *= 1.0F - std::clamp(inst->mCurrentValue, 0.0F, 1.0F);
            }
            float dx = std::sin(yaw), dz = -std::cos(yaw);
            float len = std::sqrt(dx * dx + dz * dz);
            auto  v   = victim.getPosDelta();
            Vec3  n{v.x / 2.0F - dx / len * strength, victim.isOnGround() ? std::min(0.4F, v.y / 2.0F + strength) : v.y, v.z / 2.0F - dz / len * strength};
            if (strength > 0.0F) fe::setMotion(victim, n);
            auto s = self.getPosDelta();
            fe::setMotion(self, Vec3{s.x * 0.6F, s.y, s.z * 0.6F});
        }
        // EnchantmentHelper#doPostAttackEffects of the weapon (the engine's hit, which would run them,
        // was replaced): fire aspect sets the victim on fire for 4 seconds a level
        int fire = EnchantUtils::getEnchantLevel(Enchant::Type::FireAspect, self.getCarriedItem());
        if (fire > 0) victim.setOnFire(fire * 4, true);
    }
    return flag;
}

char const* attackSoundOf(Actor const& a) { return isType(a, kMinoshroom) ? "entity.gloamingforest.minoshroom.attack" : "entity.gloamingforest.minotaur.attack"; }

// ---- goals ---------------------------------------------------------------------------------------------------

/// Java's MeleeAttackGoal(mob, 1.0, followingTargetEvenIfNotSeen = false).
class MeleeAttackGoal final : public gf::entity::Goal {
public:
    MeleeAttackGoal(Mob& mob, double speed) : mUid(fe::uidOf(mob)), mSpeed(speed) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        long long now = static_cast<long long>(fe::currentTick());
        if (now - mLastCanUseCheck < 20) return false;
        mLastCanUseCheck = now;
        auto* t          = fm::target(*m);
        if (!t || !t->isAlive()) return false;
        if (fm::canCreatePath(*m, feet(*t))) return true;
        return fm::isWithinMeleeAttackRange(*m, *t);
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* t = m ? fm::target(*m) : nullptr;
        if (!t || !t->isAlive()) return false;
        return !fm::navigationDone(*m);
    }
    void start() override {
        if (auto* m = mobOf(mUid))
            if (auto* t = fm::target(*m)) fm::moveTo(*m, feet(*t), mSpeed);
        mRecalc     = 0;
        mNextAttack = 0;
    }
    void stop() override {
        if (auto* m = mobOf(mUid)) fm::stopNavigation(*m);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* t = m ? fm::target(*m) : nullptr;
        if (!t) return;
        fm::lookAt(*m, *t, 30.0F, 30.0F);
        mRecalc = std::max(mRecalc - 1, 0);
        auto tp = feet(*t);
        double moved = (tp.x - mPathed.x) * (tp.x - mPathed.x) + (tp.y - mPathed.y) * (tp.y - mPathed.y) + (tp.z - mPathed.z) * (tp.z - mPathed.z);
        if (fm::hasLineOfSight(*m, *t) && mRecalc <= 0 && (!mHasPathed || moved >= 1.0 || fe::randomOf(*m).nextFloat() < 0.05F)) {
            mPathed    = tp;
            mHasPathed = true;
            mRecalc    = 4 + fe::randomOf(*m).nextInt(7);
            double d   = fm::distanceSqr(*m, *t);
            if (d > 1024.0) mRecalc += 10;
            else if (d > 256.0) mRecalc += 5;
            if (!fm::moveTo(*m, tp, mSpeed)) mRecalc += 15;
        }
        mNextAttack = std::max(mNextAttack - 1, 0);
        if (mNextAttack <= 0 && fm::isWithinMeleeAttackRange(*m, *t) && fm::hasLineOfSight(*m, *t)) {
            mNextAttack = 20;
            m->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            properlyApplyCustomDamageSource(*m, *t, attackSoundOf(*m));
        }
    }

private:
    std::int64_t mUid;
    double       mSpeed;
    long long    mLastCanUseCheck = -100;
    int          mRecalc = 0, mNextAttack = 0;
    Vec3         mPathed{};
    bool         mHasPathed = false;
};

/// WaterAvoidingRandomStrollGoal(mob, 1.0) (interval 120: reducedTickDelay 60).
class StrollGoal final : public gf::entity::Goal {
public:
    StrollGoal(Mob& mob, double speed) : mUid(fe::uidOf(mob)), mSpeed(speed) { flags = Move; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        auto& r = fe::randomOf(*m);
        if (r.nextInt(60) != 0) return false;
        auto pos = m->isInWater() ? fm::randomPos(*m, 15, 7) : fm::randomPos(*m, 10, 7);
        if (!pos) return false;
        mWanted = *pos;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        return m && !fm::navigationDone(*m);
    }
    void start() override {
        if (auto* m = mobOf(mUid)) fm::moveTo(*m, mWanted, mSpeed);
    }
    void stop() override {
        if (auto* m = mobOf(mUid)) fm::stopNavigation(*m);
    }

private:
    std::int64_t mUid;
    double       mSpeed;
    Vec3         mWanted{};
};

/// LookAtPlayerGoal(mob, Player.class, 8.0F).
class LookAtPlayerGoal final : public gf::entity::Goal {
public:
    explicit LookAtPlayerGoal(Mob& mob) : mUid(fe::uidOf(mob)) { flags = Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m || fe::randomOf(*m).nextFloat() >= 0.02F) return false;
        // LookAtPlayerGoal for Player.class: always the nearest player within 8 (not the target)
        Actor* look = nullptr;
        {
            double best = 65.0;
            for (auto* p : fe::playersNear(static_cast<int>(m->getDimensionId()), feet(*m), 8.0)) {
                if (p->isSpectator()) continue;
                double d = fm::distanceSqr(*m, *p);
                if (d < best) best = d, look = p;
            }
        }
        if (!look) return false;
        mLookAt = fe::uidOf(*look);
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mobOf(mUid);
        auto* a = fe::actorById(mLookAt);
        return m && a && a->isAlive() && fm::distanceSqr(*m, *a) <= 64.0 && mLookTime > 0;
    }
    void start() override { mLookTime = 40 + rng().nextInt(40); }
    void stop() override { mLookAt = 0; }
    void tick() override {
        auto* m = mobOf(mUid);
        auto* a = fe::actorById(mLookAt);
        if (m && a && a->isAlive()) fm::lookAt(*m, a->getEyePos(), 10.0F, 40.0F);
        --mLookTime;
    }

private:
    std::int64_t mUid, mLookAt = 0;
    int          mLookTime = 0;
};

/// RandomLookAroundGoal.
class RandomLookAroundGoal final : public gf::entity::Goal {
public:
    explicit RandomLookAroundGoal(Mob& mob) : mUid(fe::uidOf(mob)) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        return m && fe::randomOf(*m).nextFloat() < 0.02F;
    }
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
            fm::lookAt(*m, Vec3{static_cast<float>(e.x + mRelX), e.y, static_cast<float>(e.z + mRelZ)}, 10.0F, 40.0F);
        }
    }

private:
    std::int64_t mUid;
    double       mRelX = 0, mRelZ = 0;
    int          mLookTime = 0;
};

// ---- Minoshroom: GroundAttackGoal -------------------------------------------------------------------------------

void setGroundAttackCharge(Mob& m, bool flag) {
    gf::entity::data(m, "labyrinth:ground_attack") = flag;
    gf::entity::setProperty(m, "gloamingforest:ground_attack", flag);
}
void setMaxCharge(Mob& m, int charge) {
    gf::entity::data(m, "labyrinth:ground_charge") = charge;
    gf::entity::setProperty(m, "gloamingforest:ground_charge", charge);
}

/// The slam itself: sound, game event, the burst of block particles the client draws when the
/// charge ends, and every player on the ground within 7.5 (y to +3) pushed up and hurt.
int slam(Mob& m) {
    auto& r = fe::randomOf(m);
    setGroundAttackCharge(m, false);
    auto p = feet(m);
    gf::fx::sound(m.getDimension(), "entity.gloamingforest.minoshroom.slam", p, 2.0F, 1.0F + r.nextFloat() * 0.1F);
    BlockPos bp{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    // Minoshroom#tick (client): 80 block particles of the block below within +-5
    auto&       region = m.getDimensionBlockSource();
    std::string below  = gf::game::blockId(region.getBlock(BlockPos{bp.x, bp.y - 1, bp.z}));
    if (below != "minecraft:air") {
        float minY = m.getAABB().min.y;
        for (int i = 0; i < 80; i++) {
            Vec3 at{bp.x + rng().nextFloat() * 10.0F - 5.0F, minY + 0.1F + rng().nextFloat() * 0.3F, bp.z + rng().nextFloat() * 10.0F - 5.0F};
            gf::fx::particle(m.getDimension(), "minecraft:block", at, Vec3{}, 1, gf::entity::Json{{"block", below}});
        }
    }
    AABB box{Vec3{bp.x - 7.5F, static_cast<float>(bp.y), bp.z - 7.5F}, Vec3{bp.x + 7.5F, bp.y + 3.0F, bp.z + 7.5F}};
    int  hit    = 0;
    float damage = gf::core::labyrinth::slamDamage(attackDamage(m));
    for (auto* a : fe::actorsIn(region, static_cast<int>(m.getDimensionId()), box)) {
        if (!a->isPlayer() || !a->isOnGround()) continue;
        fe::push(*a, Vec3{0.0F, 0.23F, 0.0F});
        gf::combat::hurt(*a, damage, kSlam, &m, &m);
        hit++;
    }
    return hit;
}

class GroundAttackGoal final : public gf::entity::Goal {
public:
    explicit GroundAttackGoal(Mob& mob) : mUid(fe::uidOf(mob)) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        auto* t = fm::target(*m);
        mTarget = t ? fe::uidOf(*t) : 0;
        if (mCooldown-- > 0 || !t) return false;
        double distance = fm::distanceSqr(*m, *t);
        if (distance < 2.0 || distance > 9.0) return false;
        if (!m->isOnGround()) return false;
        auto& r = fe::randomOf(*m);
        if (fm::hasLineOfSight(*m, *t)) return r.nextInt(24) == 0;
        return r.nextInt(24 - 4) == 0;
    }
    void start() override {
        auto* m = mobOf(mUid);
        if (!m) return;
        auto& r     = fe::randomOf(*m);
        mCooldown   = (20 * 10) + r.nextInt(20 * 10); // 10 - 20 second cooldown
        mAttackTick = 30 + r.nextInt(30);
        setMaxCharge(*m, mAttackTick);
        setGroundAttackCharge(*m, true);
    }
    bool canContinueToUse() override { return mAttackTick >= 0; }
    void stop() override {
        mAttackTick = 0;
        mTarget     = 0;
    }
    void tick() override {
        auto* m = mobOf(mUid);
        if (!m) return;
        // look where we're going
        if (auto* t = fe::actorById(mTarget)) fm::lookAt(*m, *t, 30.0F, 30.0F);
        // MoveControl.Operation.WAIT
        fm::stopNavigation(*m);
        if (mAttackTick-- <= 0) slam(*m);
    }

private:
    std::int64_t mUid;
    std::int64_t mTarget     = 0;
    int          mCooldown   = 0;
    int          mAttackTick = 0;
};

// ---- the minotaurs' behaviours --------------------------------------------------------------------------------------

void addMinotaurGoals(Mob& m, bool boss) {
    if (goalsPresent(m)) return;
    if (boss) gf::entity::addGoal(m, 1, std::make_shared<GroundAttackGoal>(m));
    gf::entity::addGoal(m, 2, gf::entity::goals::charge(m, {.speed = 1.5, .canBreak = boss}));
    gf::entity::addGoal(m, 3, std::make_shared<MeleeAttackGoal>(m, 1.0));
    if (boss) gf::entity::addGoal(m, 5, gf::entity::goals::goHome(m, 1.25)); // addRestrictionGoals
    gf::entity::addGoal(m, 6, std::make_shared<StrollGoal>(m, 1.0));
    gf::entity::addGoal(m, 7, std::make_shared<LookAtPlayerGoal>(m));
    gf::entity::addGoal(m, 7, std::make_shared<RandomLookAroundGoal>(m));
}

/// finalizeSpawn's equipment, once per mob (persisted flag).
void finalizeSpawn(Mob& m, bool boss) {
    auto& done = gf::entity::data(m, "labyrinth:equipped");
    if (done.is_boolean() && done.get<bool>()) return;
    done        = true;
    auto  d     = difficulty();
    auto& r     = fe::randomOf(m);
    if (boss) {
        populateMobArmor(m, r, d);
        equip(m, Slot::Mainhand, kDiamondAxe);
    } else {
        int roll = r.nextInt(10);
        equip(m, Slot::Mainhand, minotaurAxe(roll, d.effective));
    }
    enchantSpawnedEquipment(m, r, d);
    syncEquipment(m);
}

void minotaurSpawn(Actor& a, bool boss) {
    auto* m = fe::asMob(a);
    if (!m) return;
    finalizeSpawn(*m, boss);
    addMinotaurGoals(*m, boss);
    if (boss) {
        auto const& c = gf::entity::data(*m, "labyrinth:ground_charge");
        setMaxCharge(*m, c.is_number_integer() ? c.get<int>() : 0);
        setGroundAttackCharge(*m, false);
    }
}

void minotaurTick(Actor& a, bool boss) {
    auto* m = fe::asMob(a);
    if (!m) return;
    if (!goalsPresent(*m)) minotaurSpawn(a, boss);
    // ITFCharger#isCharging: the windup's flag drives the walk animation (+0.8 a tick while winding
    // up, +0.6 while charging: Minotaur#aiStep)
    bool  charging = gf::entity::ext::isCharging(*m);
    auto& last     = gf::entity::data(*m, "labyrinth:charging_prop");
    if (!last.is_boolean() || last.get<bool>() != charging) {
        last = charging;
        gf::entity::setProperty(*m, "gloamingforest:charging", charging);
    }
}

// ---- Maze slime ---------------------------------------------------------------------------------------------

struct SlimeState {
    bool wasOnGround = true;
};
std::unordered_map<std::int64_t, SlimeState>& slimes() {
    static std::unordered_map<std::int64_t, SlimeState> m;
    return m;
}

float slimeVolume(Actor& a) { return 0.1F * static_cast<float>(slimeSize(a)); } // OH MY GOD, SHUT UP

char const* slimeSound(Actor& a, char const* kind) {
    static thread_local std::string s;
    s = std::string("entity.gloamingforest.maze_slime") + (slimeSize(a) <= 1 ? "_small." : ".") + kind;
    return s.c_str();
}

/// Slime#finalizeSpawn: size 1 << nextInt(3), raised once with 0.5 x the special multiplier.
int rollSlimeSize(gf::core::jrand::RandomSource& r) {
    int  i       = r.nextInt(3);
    bool boosted = i < 2 && r.nextFloat() < 0.5F * difficulty().special;
    return gf::core::labyrinth::mazeSlimeSize(i, boosted);
}

void slimeSpawn(Actor& a) {
    auto* m = fe::asMob(a);
    if (!m) return;
    auto const& s = gf::entity::data(*m, "labyrinth:size");
    if (!s.is_number_integer()) setSlimeSize(*m, rollSlimeSize(fe::randomOf(*m)));
}

void slimeTick(Actor& a) {
    auto* m = fe::asMob(a);
    if (!m || !m->isAlive()) return;
    if (!gf::entity::data(*m, "labyrinth:size").is_number_integer()) slimeSpawn(a);
    if (slimes().size() > 1024) std::erase_if(slimes(), [](auto const& e) { return fe::actorById(e.first) == nullptr; });
    auto& st     = slimes()[fe::uidOf(*m)];
    bool  ground = m->isOnGround();
    auto& r      = fe::randomOf(*m);
    int   size   = slimeSize(*m);
    if (ground && !st.wasOnGround) {
        // MazeSlime#spawnCustomParticles: mazestone brick block particles instead of slime balls
        auto p = feet(*m);
        for (int j = 0; j < size * 8; ++j) {
            float f  = r.nextFloat() * (std::numbers::pi_v<float> * 2.0F);
            float f1 = r.nextFloat() * 0.5F + 0.5F;
            float f2 = std::sin(f) * static_cast<float>(size) * 0.5F * f1;
            float f3 = std::cos(f) * static_cast<float>(size) * 0.5F * f1;
            gf::fx::particle(m->getDimension(), "minecraft:block", Vec3{p.x + f2, m->getAABB().min.y, p.z + f3}, Vec3{}, 1,
                             gf::entity::Json{{"block", "gloamingforest:mazestone_brick"}});
        }
        gf::fx::sound(m->getDimension(), slimeSound(*m, "squish"), p, slimeVolume(*m), ((r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F) / 0.8F);
    } else if (!ground && st.wasOnGround && m->getPosDelta().y > 0.0F) {
        // the jump (Slime.SlimeMoveControl): getJumpSound at getSoundPitch
        float pitch = ((r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F) * (size <= 1 ? 1.4F : 0.8F);
        gf::fx::sound(m->getDimension(), slimeSound(*m, "squish"), feet(*m), slimeVolume(*m), pitch);
    }
    st.wasOnGround = ground;
}

/// Slime#remove(KILLED): 2-4 slimes of half the size.
void slimeDie(Actor& a, std::string const&, Actor*) {
    slimes().erase(fe::uidOf(a));
    auto* m = fe::asMob(a);
    if (!m) return;
    int size = slimeSize(*m);
    gf::fx::sound(m->getDimension(), slimeSound(*m, "death"), feet(*m), slimeVolume(*m), (rng().nextFloat() - rng().nextFloat()) * 0.2F + 1.0F);
    if (size <= 1) return;
    int   j = size / 2;
    int   k = 2 + rng().nextInt(3);
    // Slime#remove: the children half the parent's width apart, with its name (a named mob is persistent on Bedrock)
    float half    = 0.52F * static_cast<float>(size) / 2.0F;
    auto  name    = m->getNameTag();
    auto  p = feet(*m);
    auto& region = m->getDimensionBlockSource();
    for (int l = 0; l < k; ++l) {
        float f1 = (static_cast<float>(l % 2) - 0.5F) * half;
        float f2 = (static_cast<float>(l / 2) - 0.5F) * half;
        gf::spawn::spawnMob(region, kMazeSlime, Vec3{p.x + f1, p.y + 0.5F, p.z + f2}, gf::spawn::SpawnReason::Triggered, [j, name](Actor& child) {
            if (auto* cm = fe::asMob(child)) {
                setSlimeSize(*cm, j);
                if (!name.empty()) cm->setNameTag(name);
                float yaw = rng().nextFloat() * 360.0F;
                cm->setYBodyRotation(yaw);
            }
        });
    }
}

// ---- spawn rules ----------------------------------------------------------------------------------------------

bool notPeaceful() {
    auto level = ll::service::getLevel();
    return level && level->getDifficulty() != SharedTypes::Legacy::Difficulty::Peaceful;
}

/// Monster#checkMonsterSpawnRules.
bool spawnerReason(gf::spawn::SpawnReason reason) { return reason == gf::spawn::SpawnReason::Spawner || reason == gf::spawn::SpawnReason::TrialSpawner; }

/// Mob#checkMobSpawnRules: a spawner places anywhere, else the block below must be valid to spawn on
/// (BlockState#isValidSpawn: not air, leaves, glass, barriers or magma).
bool checkMobSpawnRules(BlockSource& region, BlockPos const& pos, gf::spawn::SpawnReason reason) {
    if (spawnerReason(reason)) return true;
    std::string below = gf::game::blockIdAt(region, BlockPos{pos.x, pos.y - 1, pos.z});
    return below != "minecraft:air" && below.find("leaves") == std::string::npos && below.find("glass") == std::string::npos && below != "minecraft:barrier" &&
           below != "minecraft:magma";
}

bool checkMonsterSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random) {
    if (!notPeaceful()) return false;
    // Monster#checkMonsterSpawnRules: isDarkEnoughToSpawn (spawners skip it) and checkMobSpawnRules
    if (spawnerReason(reason)) return true;
    return gf::world::ext::isDarkEnoughToSpawn(region, pos, random) && checkMobSpawnRules(region, pos, reason);
}

/// MazeSlime#getCanSpawnHere: not peaceful, Mob#checkMobSpawnRules and Monster#isDarkEnoughToSpawn.
bool mazeSlimeSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random) {
    if (!notPeaceful()) return false;
    return checkMobSpawnRules(region, pos, reason) && gf::world::ext::isDarkEnoughToSpawn(region, pos, random);
}

} // namespace

// ---- public helpers ----------------------------------------------------------------------------------------------

float weaponModifier(std::string const& id) { return gf::core::labyrinth::weaponModifier(id.c_str()); }

float attackDamage(Mob& mob) { return 2.0F + weaponModifier(gf::game::items::idOf(mob.getCarriedItem())); }

char const* minotaurAxe(int roll, float effectiveDifficulty) { return gf::core::labyrinth::minotaurAxe(roll, effectiveDifficulty); }

int slimeSize(Actor& slime) {
    auto const& s = gf::entity::data(slime, "labyrinth:size");
    return s.is_number_integer() ? std::max(1, s.get<int>()) : 1;
}

void setSlimeSize(Mob& slime, int size) {
    size = size >= 4 ? 4 : (size >= 2 ? 2 : 1);
    gf::entity::data(slime, "labyrinth:size") = size;
    // the BP's size groups: variant = size, collision 0.52 x size, health 3 x size^2, attack = size,
    // movement 0.2 + 0.1 x size; experience size + 3
    VariantParameterList params{};
    params.mSelf = &slime;
    slime.executeEvent("gf:size_" + std::to_string(size), params);
}

int groundSlamForTest(Mob& minoshroom) { return slam(minoshroom); }

void install() {
    gf::entity::registerType(kMinotaur, {
                                            .onSpawn = [](Actor& a) { minotaurSpawn(a, false); },
                                            .tick    = [](Actor& a) { minotaurTick(a, false); },
                                        });
    gf::entity::registerType(kMinoshroom, {
                                              .onSpawn = [](Actor& a) { minotaurSpawn(a, true); },
                                              .tick    = [](Actor& a) { minotaurTick(a, true); },
                                          });
    gf::entity::registerType(kMazeSlime, {
                                             .onSpawn = slimeSpawn,
                                             .tick    = slimeTick,
                                             .die     = slimeDie,
                                         });

    // Minotaur#doHurtTarget for every melee hit of a minotaur (F8's ChargeAttackGoal calls the
    // engine's attack): axing damage from the minotaur's own attack value
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (tCustomDamage > 0 || !d.attacker || d.direct != d.attacker || !isMinotaurKind(*d.attacker)) return gf::combat::Verdict::Pass;
        if (d.type == kAxing || d.type == kSlam) return gf::combat::Verdict::Pass;
        if (d.type != "minecraft:mob_attack" && d.type != "minecraft:generic") return gf::combat::Verdict::Pass;
        auto* m = fe::asMob(*d.attacker);
        if (!m) return gf::combat::Verdict::Pass;
        properlyApplyCustomDamageSource(*m, d.victim, attackSoundOf(*m));
        return gf::combat::Verdict::Cancel;
    });

    // the maze slime's hurt sound: MAZE_SLIME_HURT(_SMALL) at 0.1 x size
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (!isType(d.victim, kMazeSlime) || !d.victim.isAlive()) return;
        gf::fx::sound(d.victim.getDimension(), slimeSound(d.victim, "hurt"), feet(d.victim), slimeVolume(d.victim), (rng().nextFloat() - rng().nextFloat()) * 0.2F + 1.0F);
    });

    // the Minoshroom as an upstream boss (BaseTFBoss): bar 0xFF0000, conquers the labyrinth, its loot into a
    // mangrove chest; home radius 20 at its spawner
    gf::boss::registerBoss({
        .entityId        = kMinoshroom,
        .homeStructure   = kLabyrinth,
        .lootTable       = "gloamingforest:entities/minoshroom",
        .deathChestBlock = "gloamingforest:mangrove_chest",
        .bar             = {.rgb = 0xFF0000},
    });
    gf::boss::ext::setHomeRadius(kMinoshroom, 20);
    // MinoshroomSpawnerBlockEntity: range 9, crimson spores, the nearest player below spawner y + 4
    // (F8's spawner kind of this block id)
    gf::boss::registerSpawner({.blockId = kSpawnerBlock, .entityId = kMinoshroom, .range = 9, .particle = "minecraft:crimson_spore"});

    gf::spawn::registerPlacement(kMinotaur, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    gf::spawn::registerPlacement(kMinoshroom, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    gf::spawn::registerPlacement(kMazeSlime, gf::spawn::PlacementRule::OnGround, mazeSlimeSpawnRules);

    gf::game::db::onWorldClosed([] {
        slimes().clear();
    });
}

} // namespace gf::feature::labyrinth::mobs
