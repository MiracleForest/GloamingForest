// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/HostileWolf.java (isValidLightLevel)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Also translated (vanilla Minecraft 1.21.1, the goals TF's hostiles use from it):
// net.minecraft.world.entity.ai.goal.RangedAttackGoal and AvoidEntityGoal, Mob's equipment
// enchantment and drop rules, DifficultyInstance#getSpecialMultiplier.

#include "feature/hostiles/Common.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "core/landmark/Landmark.h"
#include "feature/dimension/World.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Items.h"
#include "game/WorldDb.h"

namespace gf::feature::hostiles {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

// ---- actors ------------------------------------------------------------------------------------------

std::int64_t uidOf(Actor const& actor) { return fe::uidOf(actor); }
Actor*       actorById(std::int64_t uid) { return fe::actorById(uid); }
Mob*         asMob(Actor& actor) { return fe::asMob(actor); }
std::string  typeOf(Actor const& actor) { return actor.getTypeName(); }
bool         isType(Actor const* actor, char const* id) { return actor && actor->getTypeName() == id; }

bool isLiving(Actor const& actor) { return actor.isPlayer() || actor.hasCategory(ActorCategory::Mob); }

Actor* targetOf(Mob& mob) {
    auto* t = mobs::target(mob);
    return t && t->isAlive() ? t : nullptr;
}

gf::core::jrand::RandomSource& randomOf(Actor& actor) { return fe::randomOf(actor); }

gf::core::jrand::RandomSource& levelRandom() {
    static gf::core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 20 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

BlockPos blockPosOf(Actor const& actor) {
    auto p = actor.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}

int dimOf(Actor const& actor) { return static_cast<int>(actor.getDimensionId()); }

double distSqr(Actor const& a, Vec3 const& p) { return mobs::distanceSqr(a, p); }

double distance(Actor const& a, Actor const& b) { return std::sqrt(mobs::distanceSqr(a, b)); }

bool nativeGoalsPresent(Actor& actor) {
    auto const* s = fe::findState(uidOf(actor));
    return s && s->goals && s->goals->size() > 0;
}

// ---- world ----------------------------------------------------------------------------------------------

namespace {
bool gameRule(GameRules::GameRulesIndex index, bool fallback) {
    auto level = ll::service::getLevel();
    if (!level) return fallback;
    GameRuleId rule;
    rule.mValue = static_cast<int>(index);
    return level->getGameRules().getBool(rule, fallback);
}
} // namespace

bool mobGriefing() { return gameRule(GameRules::GameRulesIndex::MobGriefing, true); }
bool doEntityDrops() { return gameRule(GameRules::GameRulesIndex::DoEntityDrops, true); }

int difficulty() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 0;
}

int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return 0;
    auto pair = region.getBrightnessPair(pos);
    int  sky  = static_cast<int>(pair.sky->mValue) - gf::world::ext::skyDarken(region);
    return std::max(sky, static_cast<int>(pair.block->mValue));
}

float brightnessOf(int light) {
    float f  = static_cast<float>(std::clamp(light, 0, 15)) / 15.0F;
    float f1 = f / (4.0F - 3.0F * f);
    return f1 + 0.01F * (1.0F - f1); // Mth.lerp(ambientLight 0.01, f1, 1.0F)
}

float lightMagicValue(Actor& actor) {
    auto eye = fe::eyeOf(actor);
    BlockPos at{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    return brightnessOf(maxLocalRawBrightness(actor.getDimensionBlockSource(), at));
}

float specialMultiplier() {
    auto level = ll::service::getLevel();
    if (!level) return 0.0F;
    auto diff = level->getDifficulty();
    if (diff == SharedTypes::Legacy::Difficulty::Peaceful) return 0.0F;
    // DifficultyInstance#calculateDifficulty(difficulty, dayTime, inhabitedTime = 0, moonBrightness)
    float f  = 0.75F;
    float f1 = std::clamp((static_cast<float>(level->getTime()) + -72000.0F) / 1440000.0F, 0.0F, 1.0F) * 0.25F;
    f += f1;
    static constexpr float kMoon[] = {1.0F, 0.75F, 0.5F, 0.25F, 0.0F, 0.25F, 0.5F, 0.75F};
    float moon = kMoon[(static_cast<long long>(level->getTime()) / 24000LL % 8 + 8) % 8];
    float f2   = std::clamp(moon * 0.25F, 0.0F, f1);
    if (diff == SharedTypes::Legacy::Difficulty::Easy) f2 *= 0.5F;
    f += f2;
    float effective = static_cast<float>(static_cast<int>(diff)) * f;
    if (effective < 2.0F) return 0.0F;
    return effective > 4.0F ? 1.0F : (effective - 2.0F) / 2.0F;
}

bool hedgeMazeChunk(BlockSource& region, BlockPos const& pos) {
    if (!gf::world::isTwilight(region.getDimension())) return false;
    auto key = gf::core::landmark::pickForChunk(gf::worldgen::seed(), pos.x >> 4, pos.z >> 4, [](int x, int z) { return gf::worldgen::biomeAt(BlockPos{x, 0, z}); });
    return key == "twilightforest:hedge_maze";
}

bool darkEnough(BlockSource& region, BlockPos const& pos, gf::core::jrand::RandomSource& random) {
    return gf::world::ext::isDarkEnoughToSpawn(region, pos, random);
}

bool checkMonsterSpawnRules(BlockSource& region, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random) {
    if (difficulty() == 0) return false;
    if (reason == gf::spawn::SpawnReason::Spawner || reason == gf::spawn::SpawnReason::TrialSpawner) return true; // MobSpawnType#ignoresLightLevel
    return darkEnough(region, pos, random);
}

// ---- effects and sounds ----------------------------------------------------------------------------------

void addEffect(Actor& actor, unsigned effect, int ticks, int amplifier) {
    MobEffectInstance e(effect, EffectDuration{ticks < 0 ? -1 : ticks}); // Bedrock: -1 is infinite
    e.mAmplifier = amplifier;
    actor.addEffect(e);
}

void playSound(Actor& actor, std::string const& event, float volume, float pitch) {
    gf::fx::sound(actor.getDimension(), event, actor.getFeetPos(), volume, pitch);
}

float voicePitch(Actor& actor, float base) {
    auto& r = randomOf(actor);
    return (r.nextFloat() - r.nextFloat()) * 0.2F + base;
}

// ---- equipment --------------------------------------------------------------------------------------------

using Slot = SharedTypes::Legacy::EquipmentSlot;

void setMainHand(Mob& mob, ItemStack const& stack) {
    mob.setItemSlot(Slot::Mainhand, stack);
    mob.sendInventory(false);
}

void setFeet(Mob& mob, ItemStack const& stack) {
    mob.setItemSlot(Slot::Feet, stack);
    mob.sendArmor(std::bitset<5>{0b1111});
}

std::string mainHandId(Mob& mob) { return gf::game::items::idOf(mob.getItemSlot(Slot::Mainhand)); }

void enchantSpawnEquipment(Mob& mob) {
    auto level = ll::service::getLevel();
    if (!level) return;
    float special = specialMultiplier();
    auto& r       = levelRandom();
    for (auto slot : {Slot::Mainhand, Slot::Feet}) {
        float     chance = slot == Slot::Mainhand ? 0.25F : 0.5F;
        ItemStack stack(mob.getItemSlot(slot));
        if (gf::game::items::isEmpty(stack) || !(r.nextFloat() < chance * special)) continue;
        int maxCost = 5 + static_cast<int>(special * 17.0F);
        int cost    = 5 + r.nextInt(maxCost - 5 + 1);
        EnchantUtils::randomlyEnchant(level->getIRandom(), stack, cost, 0, false);
        mob.setItemSlot(slot, stack);
    }
    mob.sendInventory(false);
    mob.sendArmor(std::bitset<5>{0b1111});
}

void dropEquipment(Mob& mob, std::vector<ItemStack>& drops, float handChance, float feetChance, bool byPlayer, Actor* killer) {
    int looting = 0;
    if (killer)
        if (auto* k = fe::asMob(*killer)) looting = EnchantUtils::getEnchantLevel(Enchant::Type::Looting, k->getCarriedItem());
    auto& r = levelRandom();
    for (auto [slot, chance] : {std::pair{Slot::Mainhand, handChance}, std::pair{Slot::Feet, feetChance}}) {
        ItemStack stack(mob.getItemSlot(slot));
        if (gf::game::items::isEmpty(stack)) continue;
        // Mob#dropCustomDeathLoot: drop chance + 0.01 per looting level, only when a player hit it
        if (byPlayer && r.nextFloat() < chance + 0.01F * static_cast<float>(looting)) {
            if (stack.isDamageableItem()) {
                auto* item      = stack.mItem.get();
                int   maxDamage = item ? item->getMaxDamage() : 0;
                if (maxDamage > 0) stack.setDamageValue(static_cast<short>(maxDamage - r.nextInt(1 + r.nextInt(std::max(maxDamage - 3, 1)))));
            }
            drops.push_back(stack);
        }
        mob.setItemSlot(slot, gf::game::items::empty());
    }
}

// ---- the BP's component groups -----------------------------------------------------------------------------

namespace {
std::unordered_map<std::int64_t, std::unordered_map<std::string, bool>>& groupStates() {
    static std::unordered_map<std::int64_t, std::unordered_map<std::string, bool>> m;
    return m;
}
std::unordered_map<std::int64_t, std::unordered_map<std::string, ge::PropertyValue>>& propertyCache() {
    static std::unordered_map<std::int64_t, std::unordered_map<std::string, ge::PropertyValue>> m;
    return m;
}
std::unordered_map<std::int64_t, int>& moveLocks() {
    static std::unordered_map<std::int64_t, int> m;
    return m;
}
[[maybe_unused]] bool sCleared = [] {
    gf::game::db::onWorldClosed([] {
        groupStates().clear();
        propertyCache().clear();
        moveLocks().clear();
    });
    return true;
}();
} // namespace

bool fireEvent(Actor& actor, std::string const& event) {
    VariantParameterList params{};
    params.mSelf = &actor;
    return actor.executeEvent(event, params);
}

void setGroup(Actor& actor, std::string const& group, bool on) {
    auto& states = groupStates()[uidOf(actor)];
    auto  it     = states.find(group);
    if (it != states.end() && it->second == on) return;
    states[group] = on;
    fireEvent(actor, "gf:" + group + (on ? "_on" : "_off"));
}

std::optional<bool> groupState(Actor& actor, std::string const& group) {
    auto it = groupStates().find(uidOf(actor));
    if (it == groupStates().end()) return std::nullopt;
    auto g = it->second.find(group);
    if (g == it->second.end()) return std::nullopt;
    return g->second;
}

void setPropertyCached(Actor& actor, std::string const& name, ge::PropertyValue const& value) {
    auto& cache = propertyCache()[uidOf(actor)];
    auto  it    = cache.find(name);
    if (it != cache.end() && it->second == value) return;
    cache[name] = value;
    ge::setProperty(actor, name, value);
}

// ---- spawning -----------------------------------------------------------------------------------------------

Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, gf::spawn::SpawnReason reason, float yaw) {
    Actor* a = gf::spawn::spawnMob(region, id, pos, reason, [yaw](Actor& actor) {
        if (yaw != 0.0f) const_cast<Vec2&>(actor.getRotation()) = Vec2{0.0f, yaw};
    });
    return a ? fe::asMob(*a) : nullptr;
}

Actor* spawnActor(BlockSource& region, std::string const& id, Vec3 const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, pos, Vec2{0.0F, 0.0F});
    return owned ? level->addEntity(region, std::move(owned)) : nullptr;
}

bool firstSpawn(Actor& actor) {
    auto& done = ge::data(actor, "hostiles:spawned");
    if (done.is_boolean()) return false;
    done = true;
    return true;
}

// ---- vanilla goals -------------------------------------------------------------------------------------------

void MoveLock::hold(bool on) {
    if (on == mHeld) return;
    mHeld = on;
    auto& n = moveLocks()[mUid];
    n += on ? 1 : -1;
    if (n <= 0) moveLocks().erase(mUid);
}

bool MoveLock::held(std::int64_t uid) {
    auto it = moveLocks().find(uid);
    return it != moveLocks().end() && it->second > 0;
}

void MoveLock::forget(std::int64_t uid) {
    moveLocks().erase(uid);
    groupStates().erase(uid);
    propertyCache().erase(uid);
}

LockedGoal::LockedGoal(Mob& mob, ge::GoalPtr inner) : mInner(std::move(inner)), mLock(uidOf(mob)) { flags = mInner->flags; }

void LockedGoal::start() {
    mLock.hold(true);
    mInner->start();
}

void LockedGoal::stop() {
    mInner->stop();
    mLock.hold(false);
}

void syncVanillaMove(Mob& mob) { setGroup(mob, "vanilla_move", !MoveLock::held(uidOf(mob))); }

namespace {
Mob* mobOf(std::int64_t id) {
    auto* a = actorById(id);
    return a && a->isAlive() ? fe::asMob(*a) : nullptr;
}
} // namespace

RangedAttackGoal::RangedAttackGoal(Mob& mob, double speed, int intervalMin, int intervalMax, float radius, Attack attack, std::function<bool(Mob&)> usable)
: mId(uidOf(mob)),
  mSpeed(speed),
  mMin(intervalMin),
  mMax(intervalMax),
  mRadius(radius),
  mAttack(std::move(attack)),
  mUsable(std::move(usable)),
  mLock(uidOf(mob)) {
    flags = Move | Look;
}

bool RangedAttackGoal::canUse() {
    auto* m = mobOf(mId);
    if (!m || (mUsable && !mUsable(*m))) return false;
    auto* t = targetOf(*m);
    if (!t) return false;
    mTarget = uidOf(*t);
    return true;
}

bool RangedAttackGoal::canContinueToUse() {
    if (canUse()) return true;
    auto* m = mobOf(mId);
    auto* t = actorById(mTarget);
    return m && t && t->isAlive() && !mobs::navigationDone(*m) && (!mUsable || mUsable(*m));
}

void RangedAttackGoal::start() { mLock.hold(true); }

void RangedAttackGoal::stop() {
    mTarget     = 0;
    mSeeTime    = 0;
    mAttackTime = -1;
    mLock.hold(false);
}

void RangedAttackGoal::tick() {
    auto* m = mobOf(mId);
    auto* t = actorById(mTarget);
    if (!m || !t) return;
    auto   tp    = t->getFeetPos();
    double d0    = distSqr(*m, tp);
    bool   sight = mobs::hasLineOfSight(*m, *t);
    if (sight) ++mSeeTime;
    else mSeeTime = 0;
    if (d0 <= static_cast<double>(mRadius * mRadius) && mSeeTime >= 5) mobs::stopNavigation(*m);
    else mobs::moveTo(*m, tp, mSpeed);
    mobs::lookAt(*m, *t, 30.0F, 30.0F);
    if (--mAttackTime == 0) {
        if (!sight) return;
        float f  = static_cast<float>(std::sqrt(d0)) / mRadius;
        float f1 = std::clamp(f, 0.1F, 1.0F);
        mAttack(*m, *t, f1);
        mAttackTime = static_cast<int>(std::floor(f * static_cast<float>(mMax - mMin) + static_cast<float>(mMin)));
    } else if (mAttackTime < 0) {
        // Mth.lerp(sqrt(d0) / radius, min, max)
        float f     = static_cast<float>(std::sqrt(d0)) / mRadius;
        mAttackTime = static_cast<int>(std::floor(static_cast<float>(mMin) + f * static_cast<float>(mMax - mMin)));
    }
}

AvoidEntityGoal::AvoidEntityGoal(Mob& mob, std::function<bool(Actor&)> avoid, float distance, double walk, double sprint)
: mId(uidOf(mob)),
  mAvoid(std::move(avoid)),
  mDistance(distance),
  mWalk(walk),
  mSprint(sprint),
  mLock(uidOf(mob)) {
    flags = Move;
}

bool AvoidEntityGoal::canUse() {
    auto* m = mobOf(mId);
    if (!m) return false;
    // Level#getNearestEntity(class, predicate, mob, x, y, z, box inflated (distance, 3, distance))
    auto   bb = m->getAABB();
    AABB   box{Vec3{bb.min.x - mDistance, bb.min.y - 3.0f, bb.min.z - mDistance}, Vec3{bb.max.x + mDistance, bb.max.y + 3.0f, bb.max.z + mDistance}};
    Actor* nearest = nullptr;
    double best    = -1.0;
    for (auto* e : fe::actorsIn(m->getDimensionBlockSource(), dimOf(*m), box)) {
        if (!e || e == m || !mAvoid(*e) || !mobs::attackable(*e)) continue;
        // TargetingConditions.forCombat().range(distance): within the distance, seen
        if (distance(*m, *e) > mDistance || !mobs::hasLineOfSight(*m, *e)) continue;
        double d = mobs::distanceSqr(*m, *e);
        if (!nearest || d < best) nearest = e, best = d;
    }
    if (!nearest) return false;
    auto pos = mobs::randomPosAway(*m, 16, 7, nearest->getFeetPos());
    if (!pos) return false;
    // the spot must be farther from the threat than the mob is
    if (mobs::distanceSqr(*nearest, *pos) < mobs::distanceSqr(*nearest, *m)) return false;
    if (!mobs::canCreatePath(*m, *pos)) return false;
    mToAvoid = uidOf(*nearest);
    mDest    = *pos;
    return true;
}

bool AvoidEntityGoal::canContinueToUse() {
    auto* m = mobOf(mId);
    return m && !mobs::navigationDone(*m);
}

void AvoidEntityGoal::start() {
    mLock.hold(true);
    if (auto* m = mobOf(mId)) mobs::moveTo(*m, mDest, mWalk);
}

void AvoidEntityGoal::stop() {
    mToAvoid = 0;
    mLock.hold(false);
}

void AvoidEntityGoal::tick() {
    auto* m = mobOf(mId);
    auto* t = actorById(mToAvoid);
    if (!m || !t) return;
    mobs::setSpeedModifier(*m, mobs::distanceSqr(*m, *t) < 49.0 ? mSprint : mWalk);
}

} // namespace gf::feature::hostiles
