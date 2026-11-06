// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/LowerGoblinKnight.java
//   src/main/java/twilightforest/entity/monster/UpperGoblinKnight.java
//   src/main/java/twilightforest/entity/ai/goal/RiderSpearAttackGoal.java
//   src/main/java/twilightforest/entity/ai/goal/HeavySpearAttackGoal.java
//   src/main/java/twilightforest/entity/monster/BlockChainGoblin.java
//   src/main/java/twilightforest/entity/SpikeBlock.java
//   src/main/java/twilightforest/entity/ai/goal/ThrowSpikeBlockGoal.java
//   src/main/java/twilightforest/entity/monster/HelmetCrab.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The stronghold's mobs. Their vanilla goals (float, melee, stroll, look, leap, the hurt-by and
// nearest-player targets, the TNT avoidance) are the BP's behaviours; the upper goblin knight rides
// the lower one from the BP's addrider. Native here: the armour and shield breaking by attack
// angle, the heavy spear, the rider's melee, the block-and-chain swing and throw, the helmet crab's
// blue variant and AlwaysWatchTargetGoal, the armour attributes Bedrock mobs lack, and the sounds
// that depend on state (the lower knight's muffled voice).
#include "feature/stronghold/Goblins.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/spawn/Spawn.h"
#include "feature/stronghold/Util.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::stronghold::goblins {

namespace ge = gf::entity;
namespace fe = gf::feature::entity;
using Json   = nlohmann::json;

namespace {

constexpr int kHeavySpearTimerStart = 60;

/// Set while the unit itself runs a vanilla melee (Mob#doHurtTarget through `super`), so the
/// interceptors of the overridden doHurtTarget leave it alone.
thread_local int tVanillaMelee = 0;
struct VanillaMelee {
    VanillaMelee() { ++tVanillaMelee; }
    ~VanillaMelee() { --tVanillaMelee; }
};

Json& dataOf(Actor& a, char const* key) { return ge::data(a, key); }

bool flag(Actor& a, char const* key, bool fallback) {
    auto const& v = ge::data(a, key);
    return v.is_boolean() ? v.get<bool>() : fallback;
}

void setFlag(Actor& a, char const* key, char const* property, bool value) {
    dataOf(a, key) = value;
    ge::setProperty(a, property, value);
}

int intOf(Actor& a, char const* key) {
    auto const& v = ge::data(a, key);
    return v.is_number_integer() ? v.get<int>() : 0;
}

/// LivingEntity#breakItem(iron chestplate) three times (entity event 5).
void breakEffects(Actor& a) {
    auto& r = levelRandom();
    auto  p = a.getFeetPos();
    for (int i = 0; i < 3; i++) {
        float pitch = 0.8F + r.nextFloat() * 0.4F;
        playSound(a, "minecraft:entity.item.break", 0.8F, pitch);
        for (int j = 0; j < 5; j++)
            gf::fx::particle(a.getDimension(), "minecraft:item", Vec3{p.x, p.y + 1.0f, p.z}, Vec3{(r.nextFloat() - 0.5f) * 0.1f, 0.1f, (r.nextFloat() - 0.5f) * 0.1f}, 1,
                             Json{{"item", "minecraft:iron_chestplate"}});
    }
}

/// The attack angle of LowerGoblinKnight#hurt / UpperGoblinKnight#hurt: |(yBodyRot - angle) % 360|.
float attackDifference(Mob& self, Actor& attacker) {
    auto   sp    = self.getFeetPos();
    auto   ap    = attacker.getFeetPos();
    double dx    = sp.x - ap.x;
    double dz    = sp.z - ap.z;
    float  angle = static_cast<float>((std::atan2(dz, dx) * 180.0) / std::numbers::pi) - 90.0F;
    return std::abs(std::fmod(self.getYBodyRotation() - angle, 360.0F));
}

Mob* riderOf(Mob& lower) {
    auto* p = lower.getFirstPassenger();
    if (!p || p->getTypeName() != id::UpperGoblinKnight) return nullptr;
    return fe::asMob(*p);
}

Mob* mountOf(Actor& upper) {
    auto* v = upper.getVehicle();
    return v ? fe::asMob(*v) : nullptr;
}

/// Mob#ambientSoundTime: a native ambient voice (the lower knight's depends on its armour).
void ambientVoice(Mob& m, std::string const& event) {
    auto& t = dataOf(m, "goblin:ambient_time");
    int   v = t.is_number_integer() ? t.get<int>() : 0;
    if (levelRandom().nextInt(1000) < v++) {
        v = -80;
        playSound(m, event, 1.0F, voicePitch(levelRandom()));
    }
    t = v;
}

// ---- UpperGoblinKnight ----------------------------------------------------------------------------------

bool upperHasArmor(Actor& a) { return flag(a, "goblin:has_armor", true); }
bool upperHasShield(Actor& a) { return flag(a, "goblin:has_shield", true); }
bool shieldDisabled(Actor& a) { return flag(a, "goblin:shield_disabled", false); }

void setSpearTimer(Actor& a, int t) {
    dataOf(a, "goblin:spear_timer") = t;
    ge::setProperty(a, "twilightforest:spear_timer", std::clamp(t, 0, kHeavySpearTimerStart));
}

void breakArmor(Actor& a) {
    breakEffects(a);
    setFlag(a, "goblin:has_armor", "twilightforest:armor", false);
}

void breakShield(Actor& a) {
    breakEffects(a);
    setFlag(a, "goblin:has_shield", "twilightforest:shield", false);
}

void damageShield(Actor& upper) {
    playSound(upper, "minecraft:entity.zombie.attack_iron_door", 0.25F, 0.25F);
    int hits = intOf(upper, "goblin:shield_hits") + 1;
    dataOf(upper, "goblin:shield_hits") = hits;
    if (hits >= 3) breakShield(upper);
}

bool holdsAxe(Actor& actor) {
    auto id = game::items::idOf(actor.getCarriedItem());
    return gf::tags::has(gf::tags::Registry::Item, "minecraft:axes", id) || (id.size() > 4 && id.ends_with("_axe") && id.find("pickaxe") == std::string::npos);
}

} // namespace

bool takeHitOnShield(Mob& upper, Actor* attacker, float amount) {
    if (shieldDisabled(upper)) return false;
    auto& r = levelRandom();
    if (attacker && holdsAxe(*attacker)) {
        setFlag(upper, "goblin:shield_disabled", "twilightforest:shield_disabled", true);
        dataOf(upper, "goblin:shield_disabled_ticks") = 0;
        playSound(upper, "minecraft:item.shield.break", 1.0F, 0.8F + r.nextFloat() * 0.4F);
        return true;
    }
    if (amount > 10.0F) damageShield(upper);
    else playSound(upper, "minecraft:item.shield.block", 1.0F, 0.8F + r.nextFloat() * 0.4F);
    // knockback(0, d0 / 4, d1 / 4): a strength of 0 moves nothing; the attacker is remembered
    if (attacker && attacker->hasCategory(ActorCategory::Mob)) upper.setLastHurtByMob(fe::asMob(*attacker));
    return true;
}

void landHeavySpearAttack(Mob& upper) {
    // find vector in front of us
    float  yaw    = upper.getRotation().y * std::numbers::pi_v<float> / 180.0F;
    float  pitch  = upper.getRotation().x * std::numbers::pi_v<float> / 180.0F;
    double lx     = -std::sin(yaw) * std::cos(pitch);
    double lz     = std::cos(yaw) * std::cos(pitch);
    double dist   = 1.25;
    auto   p      = upper.getFeetPos();
    double px     = p.x + lx * dist;
    double py     = upper.getAABB().min.y - (upper.getVehicle() ? 0.75 : 0.0);
    double pz     = p.z + lz * dist;
    auto&  r      = levelRandom();
    for (int i = 0; i < 50; i++) {
        float  a = r.nextFloat(), b = r.nextFloat();
        double g = r.nextGaussian();
        float  c = r.nextFloat(), d = r.nextFloat();
        double h = r.nextGaussian();
        gf::fx::particle(upper.getDimension(), "minecraft:large_smoke",
                         Vec3{static_cast<float>(px + (a - b) * 0.25F * g), static_cast<float>(py), static_cast<float>(pz + (c - d) * 0.25F * h)});
    }
    // damage things in front that aren't us or our "mount"
    double radius = 1.5;
    auto   inBox  = actorsInBox(static_cast<int>(upper.getDimensionId()), Vec3{static_cast<float>(px - radius), static_cast<float>(py - radius), static_cast<float>(pz - radius)},
                                Vec3{static_cast<float>(px + radius), static_cast<float>(py + radius), static_cast<float>(pz + radius)}, "");
    auto*  mount  = upper.getVehicle();
    int    hit    = 0;
    for (auto* e : inBox) {
        if (e == &upper || e == mount) continue;
        hit++;
        // super.doHurtTarget: the attack damage with the spear's +12 x base modifier (8 x 13)
        VanillaMelee guard;
        gf::combat::hurt(*e, 8.0F * 13.0F, "minecraft:mob_attack", &upper, &upper);
    }
    if (hit > 0) playSound(upper, "minecraft:entity.player.attack.crit", 1.0F, voicePitch(r));
    gf::log().info("stronghold: goblin knight heavy spear landed ({} hit)", hit);
}

int spearTimer(Actor const& upper) {
    auto const& v = ge::data(const_cast<Actor&>(upper), "goblin:spear_timer");
    return v.is_number_integer() ? v.get<int>() : 0;
}

bool hasArmor(Actor const& goblin) { return flag(const_cast<Actor&>(goblin), "goblin:has_armor", true); }
bool hasShield(Actor const& upper) { return upperHasShield(const_cast<Actor&>(upper)); }

namespace {

/// UpperGoblinKnight#doHurtTarget: no hit while the spear runs, half the time the spear starts.
bool upperMelee(Mob& upper) {
    if (spearTimer(upper) > 0) return false;
    if (levelRandom().nextInt(2) == 0) {
        setSpearTimer(upper, kHeavySpearTimerStart); // entity event 4
        return false;
    }
    return true;
}

class GoblinGoal : public ge::Goal {
public:
    explicit GoblinGoal(std::int64_t uid) : mUid(uid) {}

protected:
    Mob* mob() const {
        auto* a = fe::actorById(mUid);
        return a ? fe::asMob(*a) : nullptr;
    }
    std::int64_t mUid;
};

bool validTarget(Mob& m) {
    auto* t = fe::mobs::target(m);
    return t && fe::mobs::attackable(*t);
}

/// HeavySpearAttackGoal.
class HeavySpearAttackGoal final : public GoblinGoal {
public:
    explicit HeavySpearAttackGoal(std::int64_t uid) : GoblinGoal(uid) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        int t = spearTimer(*m);
        return t > 0 && t < kHeavySpearTimerStart && validTarget(*m);
    }
    void tick() override {
        auto* m = mob();
        if (m && spearTimer(*m) == 25) landHeavySpearAttack(*m);
    }
};

/// RiderSpearAttackGoal (MOVE, LOOK: the mount holds still while its rider's spear runs).
class RiderSpearAttackGoal final : public GoblinGoal {
public:
    explicit RiderSpearAttackGoal(std::int64_t uid) : GoblinGoal(uid) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* rider = riderOf(*m);
        if (!rider || !validTarget(*m)) return false;
        int t = spearTimer(*rider);
        return t > 0 && t < kHeavySpearTimerStart;
    }
    void tick() override {
        if (auto* m = mob()) fe::mobs::stopNavigation(*m);
    }
};

// ---- BlockChainGoblin -------------------------------------------------------------------------------------

struct Chain {
    int   recoilCounter   = 0;
    float chainAngle      = 0.0F;
    float chainMoveLength = 0.0F;
    bool  throwing        = false;
    int   cooldown        = 0;
    bool  swinging        = false;
    ge::PartHandle block;
};

std::unordered_map<std::int64_t, Chain>& chains() {
    static std::unordered_map<std::int64_t, Chain> m;
    return m;
}

Chain& chainOf(Actor& a) { return chains()[fe::uidOf(a)]; }

bool isSwingingChain(Mob& m, Chain const& c) { return c.swinging || (fe::mobs::target(m) && c.recoilCounter == 0); }
float chainLength(Mob& m, Chain const& c) { return isSwingingChain(m, c) ? 0.9F : 0.3F; }

void setThrowing(Mob& m, Chain& c, bool t) {
    c.throwing = t;
    ge::setProperty(m, "twilightforest:throwing", t);
}

/// The spike block's position (BlockChainGoblin#tick).
Vec3 ballPosition(Mob& m, Chain const& c) {
    auto p = m.getFeetPos();
    if (c.chainMoveLength > 0) {
        // getThrowPos: the view vector at the throw length, eye height
        float yaw   = m.getRotation().y * std::numbers::pi_v<float> / 180.0F;
        float pitch = m.getRotation().x * std::numbers::pi_v<float> / 180.0F;
        float vx    = -std::sin(yaw) * std::cos(pitch);
        float vz    = std::cos(yaw) * std::cos(pitch);
        Vec3  blockPos{p.x + vx * c.chainMoveLength, m.getEyePos().y, p.z + vz * c.chainMoveLength};
        auto  b   = m.getAABB();
        float sy2 = p.y + (b.max.y - b.min.y) - 0.1F;
        float oy2 = sy2 - blockPos.y - 0.25F;
        return Vec3{blockPos.x, sy2 - oy2, blockPos.z};
    }
    // getChainPosition(angle, length)
    float len = chainLength(m, c);
    return Vec3{static_cast<float>(p.x + std::cos(c.chainAngle * std::numbers::pi / 180.0) * len), static_cast<float>(p.y + 1.5 - len / 4.0),
                static_cast<float>(p.z + std::sin(c.chainAngle * std::numbers::pi / 180.0) * len)};
}

/// BlockChainGoblin#applyBlockCollisions.
void applyBlockCollisions(Mob& m, Chain& c) {
    Vec3  ball = ballPosition(m, c);
    float half = 0.375F;
    auto  list = actorsInBox(static_cast<int>(m.getDimensionId()), Vec3{ball.x - half - 0.2f, ball.y, ball.z - half - 0.2f},
                             Vec3{ball.x + half + 0.2f, ball.y + 0.75f, ball.z + half + 0.2f}, "");
    auto* part = c.block.valid() ? ge::partActor(c.block) : nullptr;
    for (auto* e : list) {
        if (e == part || e == &m) continue;
        if (e->getTypeName() == "minecraft:item" || e->getTypeName() == "minecraft:xp_orb") continue; // not pushable
        // collided.push(collider)
        auto   ep = e->getFeetPos();
        double dx = ep.x - ball.x, dz = ep.z - ball.z;
        double d  = std::max(std::abs(dx), std::abs(dz));
        if (d >= 0.01) {
            d          = std::sqrt(d);
            dx        /= d;
            dz        /= d;
            double inv = std::min(1.0, 1.0 / d);
            fe::push(*e, Vec3{static_cast<float>(dx * inv * 0.05), 0.0f, static_cast<float>(dz * inv * 0.05)});
        }
        if (!e->hasCategory(ActorCategory::Mob)) continue;
        bool hurt;
        {
            VanillaMelee guard;
            hurt = fe::mobs::doHurtTarget(m, *e);
        }
        if (hurt) {
            fe::push(*e, Vec3{0.0f, 0.4f, 0.0f});
            playSound(m, "item.twilightforest.block_and_chain.hit", 1.0F, 1.0F);
            c.recoilCounter = 40;
            if (c.throwing) setThrowing(m, c, false);
        }
    }
    // a thrown ball in a wall stops the throw
    if (c.throwing) {
        BlockPos at{static_cast<int>(std::floor(ball.x)), static_cast<int>(std::floor(ball.y + 0.375f)), static_cast<int>(std::floor(ball.z))};
        auto&    region = m.getDimensionBlockSource();
        if (!fe::mobs::emptyCollision(region, at) && !region.getBlock(at).isAir()) {
            setThrowing(m, c, false);
            gf::fx::sound(m.getDimension(), "item.twilightforest.block_and_chain.collide", ball, 0.65F, 0.75F);
        }
    }
}

class ThrowSpikeBlockGoal final : public GoblinGoal {
public:
    explicit ThrowSpikeBlockGoal(std::int64_t uid) : GoblinGoal(uid) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto& c      = chainOf(*m);
        auto* target = fe::mobs::target(*m);
        if (!target || fe::mobs::distanceSqr(*m, *target) > 42 || c.cooldown > 0) {
            c.cooldown--;
            return false;
        }
        return m->isAlive() && fe::mobs::hasLineOfSight(*m, *target) && levelRandom().nextInt(56) == 0;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && chainOf(*m).chainMoveLength > 0;
    }
    void start() override {
        auto* m = mob();
        if (!m) return;
        auto& c = chainOf(*m);
        setThrowing(*m, c, true);
        c.cooldown = 100 + levelRandom().nextInt(100);
        gf::log().info("stronghold: blockchain goblin throws its spike block");
    }
};

void chainTick(Actor& self) {
    auto* m = fe::asMob(self);
    if (!m) return;
    auto& c = chainOf(self);
    if (c.recoilCounter > 0) c.recoilCounter--;
    c.chainAngle += 16.0F;
    c.chainAngle = std::fmod(c.chainAngle, 360.0F);
    // DATA_CHAINLENGTH / DATA_CHAINPOS for the renderers
    ge::setProperty(self, "twilightforest:chain_length", static_cast<int>(std::floor(chainLength(*m, c) * 127.0F)));
    ge::setProperty(self, "twilightforest:chain_pos", static_cast<int>(std::floor(c.chainAngle / 360.0F * 255.0F)));
    ge::setProperty(self, "twilightforest:throw_length", static_cast<int>(std::floor(c.chainMoveLength * 10.0F)));
    if (m->isAlive() && c.chainMoveLength >= 6.0F) setThrowing(*m, c, false);
    // collide things with the block
    if (m->isAlive() && (c.throwing || isSwingingChain(*m, c))) applyBlockCollisions(*m, c);
    // chainMove
    if (c.throwing) c.chainMoveLength = std::clamp(c.chainMoveLength + 0.5F, 0.0F, 6.0F);
    else c.chainMoveLength = std::clamp(c.chainMoveLength - 1.5F, 0.0F, 6.0F);
}

// ---- HelmetCrab --------------------------------------------------------------------------------------------

/// Bedrock mobs have no armour attribute: Java's Attributes.ARMOR of the stronghold mobs.
float baseArmorOf(Actor& victim) {
    auto const& t = victim.getTypeName();
    if (t == id::HelmetCrab) return 6.0F;
    if (t == id::BlockChainGoblin) return 11.0F;
    if (t == id::LowerGoblinKnight) return hasArmor(victim) ? 17.0F : 0.0F;
    if (t == id::UpperGoblinKnight) return upperHasArmor(victim) ? 20.0F : 0.0F;
    return 0.0F;
}

} // namespace

float chainLengthOf(Actor& goblin) {
    auto* m = fe::asMob(goblin);
    return m ? chainLength(*m, chainOf(goblin)) : 0.0F;
}

float throwLengthOf(Actor& goblin) { return chainOf(goblin).chainMoveLength; }

bool isThrowing(Actor& goblin) { return chainOf(goblin).throwing; }

void startThrowForTest(Mob& goblin) { setThrowing(goblin, chainOf(goblin), true); }

bool isBlue(Actor const& crab) { return flag(const_cast<Actor&>(crab), "helmet_crab:blue", false); }

void setBlueForTest(Actor& crab, bool blue) { setFlag(crab, "helmet_crab:blue", "twilightforest:blue", blue); }

void install() {
    // ---- LowerGoblinKnight
    ge::registerType(id::LowerGoblinKnight,
                     {
                         .onSpawn =
                             [](Actor& self) {
                                 auto* m = fe::asMob(self);
                                 if (!m) return;
                                 ge::setProperty(self, "twilightforest:armor", hasArmor(self));
                                 ge::addGoal(*m, 0, std::make_shared<RiderSpearAttackGoal>(fe::uidOf(self)));
                             },
                         .tick =
                             [](Actor& self) {
                                 auto* m = fe::asMob(self);
                                 if (!m || !m->isAlive()) return;
                                 ambientVoice(*m, hasArmor(self) ? "entity.twilightforest.goblin_knight.muffled.ambient" : "entity.twilightforest.goblin_knight.ambient");
                                 // the rider's body follows the mount (positionRider)
                                 if (auto* rider = riderOf(*m)) {
                                     rider->setYBodyRotation(m->getRotation().y);
                                     const_cast<Vec2&>(rider->getRotation()) = Vec2{rider->getRotation().x, m->getRotation().y};
                                 }
                                 ge::setProperty(self, "twilightforest:ridden", riderOf(*m) != nullptr);
                             },
                         .hurt =
                             [](Actor& self, float& amount, std::string const& type, Actor* attacker) {
                                 auto* m = fe::asMob(self);
                                 if (!m || !attacker) return true;
                                 float difference = attackDifference(*m, *attacker);
                                 // shield?
                                 if (auto* upper = riderOf(*m); upper && upperHasShield(*upper) && difference > 150 && difference < 230)
                                     if (takeHitOnShield(*upper, attacker, amount)) return false;
                                 // break armor?
                                 if (hasArmor(self) && (difference > 300 || difference < 60)) {
                                     breakEffects(self);
                                     setFlag(self, "goblin:has_armor", "twilightforest:armor", false);
                                 }
                                 (void)type;
                                 return true;
                             },
                         .die = [](Actor& self, std::string const&, Actor*) {
                             playSound(self, hasArmor(self) ? "entity.twilightforest.goblin_knight.muffled.death" : "entity.twilightforest.goblin_knight.death", 1.0F,
                                       voicePitch(levelRandom()));
                         },
                     });
    // ---- UpperGoblinKnight
    ge::registerType(id::UpperGoblinKnight,
                     {
                         .onSpawn =
                             [](Actor& self) {
                                 auto* m = fe::asMob(self);
                                 if (!m) return;
                                 ge::setProperty(self, "twilightforest:armor", upperHasArmor(self));
                                 ge::setProperty(self, "twilightforest:shield", upperHasShield(self));
                                 ge::setProperty(self, "twilightforest:shield_disabled", shieldDisabled(self));
                                 ge::addGoal(*m, 0, std::make_shared<HeavySpearAttackGoal>(fe::uidOf(self)));
                             },
                         .tick =
                             [](Actor& self) {
                                 auto* m = fe::asMob(self);
                                 if (!m || !m->isAlive()) return;
                                 // customServerAiStep
                                 if (shieldDisabled(self)) {
                                     int t = intOf(self, "goblin:shield_disabled_ticks");
                                     dataOf(self, "goblin:shield_disabled_ticks") = t + 1;
                                     if (t >= 100) {
                                         dataOf(self, "goblin:shield_disabled_ticks") = 0;
                                         setFlag(self, "goblin:shield_disabled", "twilightforest:shield_disabled", false);
                                     }
                                 }
                                 // synch target with lower goblin
                                 if (auto* mount = mountOf(self); mount && !fe::mobs::target(*m))
                                     if (auto* t = fe::mobs::target(*mount)) m->setTarget(t);
                                 if (!self.getVehicle() && upperHasShield(self)) breakShield(self);
                                 // the disabled shield's splash particles at the eyes
                                 if (shieldDisabled(self)) {
                                     auto& r = levelRandom();
                                     auto  p = self.getFeetPos();
                                     auto  b = self.getAABB();
                                     float w = b.max.x - b.min.x;
                                     float a = r.nextFloat(), c = r.nextFloat(), d = r.nextFloat(), e = r.nextFloat();
                                     gf::fx::particle(self.getDimension(), "minecraft:splash",
                                                      Vec3{p.x + (a - 0.5f) * w * 0.25f, self.getEyePos().y, p.z + (c - 0.5f) * w * 0.25f},
                                                      Vec3{(d - 0.5f) * 0.75f, 0.0f, (e - 0.5f) * 0.75f});
                                 }
                             },
                         .aiStep =
                             [](Actor& self) {
                                 // aiStep: the spear timer counts down (after the goals)
                                 if (int t = spearTimer(self); t > 0) setSpearTimer(self, t - 1);
                             },
                         .hurt =
                             [](Actor& self, float& amount, std::string const& type, Actor* attacker) {
                                 auto* m = fe::asMob(self);
                                 // don't take suffocation damage while riding
                                 if (type == "minecraft:in_wall" && self.getVehicle()) return false;
                                 if (!m || !attacker) return true;
                                 float difference = attackDifference(*m, *attacker);
                                 if (upperHasShield(self) && difference > 150 && difference < 230) {
                                     if (takeHitOnShield(*m, attacker, amount)) return false;
                                 } else if (upperHasShield(self) && levelRandom().nextBoolean()) {
                                     damageShield(self);
                                 }
                                 if (upperHasArmor(self) && (difference > 300 || difference < 60)) breakArmor(self);
                                 return true;
                             },
                     });
    // ---- BlockChainGoblin
    ge::registerType(id::BlockChainGoblin, {
                                               .onSpawn =
                                                   [](Actor& self) {
                                                       auto* m = fe::asMob(self);
                                                       if (!m) return;
                                                       chains()[fe::uidOf(self)] = Chain{};
                                                       auto& c = chainOf(self);
                                                       c.block = ge::addPart(*m, "spike_block", {0.75f, 0.75f}, [](Mob& parent) {
                                                           auto ball = ballPosition(parent, chainOf(parent));
                                                           auto p    = parent.getFeetPos();
                                                           return Vec3{ball.x - p.x, ball.y - p.y, ball.z - p.z};
                                                       });
                                                       ge::addGoal(*m, 4, std::make_shared<ThrowSpikeBlockGoal>(fe::uidOf(self)));
                                                   },
                                               .tick = chainTick,
                                               .die  = [](Actor& self, std::string const&, Actor*) {
                                                   // the ball falls and clangs once on landing
                                                   auto* m = fe::asMob(self);
                                                   if (m) gf::fx::sound(self.getDimension(), "item.twilightforest.block_and_chain.collide",
                                                                        ballPosition(*m, chainOf(self)), 0.65F, 0.75F);
                                                   chains().erase(fe::uidOf(self));
                                               },
                                           });
    // ---- HelmetCrab
    ge::registerType(id::HelmetCrab, {
                                         .onSpawn =
                                             [](Actor& self) {
                                                 auto* m = fe::asMob(self);
                                                 if (!m) return;
                                                 // finalizeSpawn: blue with chance 1 / 10000 (kept in the save)
                                                 auto& blue = ge::data(self, "helmet_crab:blue");
                                                 if (!blue.is_boolean()) blue = levelRandom().nextInt(10000) == 0;
                                                 ge::setProperty(self, "twilightforest:blue", blue.get<bool>());
                                                 ge::addGoal(*m, 1, ge::goals::alwaysWatchTarget(*m));
                                             },
                                     });
    // the melee overrides and the armour attributes
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (tVanillaMelee == 0 && d.attacker && d.direct == d.attacker && d.type == "minecraft:mob_attack") {
            auto const& at = d.attacker->getTypeName();
            if (at == id::LowerGoblinKnight) {
                // LowerGoblinKnight#doHurtTarget: a ridden knight's melee is its rider's
                if (auto* lower = fe::asMob(*d.attacker))
                    if (auto* upper = riderOf(*lower)) {
                        if (upperMelee(*upper)) {
                            VanillaMelee guard;
                            gf::combat::hurt(d.victim, 8.0F, "minecraft:mob_attack", upper, upper);
                        }
                        return gf::combat::Verdict::Cancel;
                    }
            } else if (at == id::UpperGoblinKnight) {
                if (auto* upper = fe::asMob(*d.attacker); upper && !upperMelee(*upper)) return gf::combat::Verdict::Cancel;
            } else if (at == id::BlockChainGoblin) {
                // doHurtTarget: spiked damage, direct = the spike block
                Actor* part = nullptr;
                auto&  c    = chainOf(*d.attacker);
                if (c.block.valid()) part = ge::partActor(c.block);
                VanillaMelee guard;
                gf::combat::hurt(d.victim, d.amount, "twilightforest:spiked", d.attacker, part ? part : d.attacker);
                return gf::combat::Verdict::Cancel;
            }
        }
        if (float armor = baseArmorOf(d.victim); armor > 0.0F && !gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_armor", d.type))
            d.amount *= armorFactor(d.amount, armor, 0.0F);
        return gf::combat::Verdict::Pass;
    });
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        // the lower knight's hurt voice depends on its armour
        if (d.victim.getTypeName() == id::LowerGoblinKnight && d.victim.isAlive())
            playSound(d.victim, hasArmor(d.victim) ? "entity.twilightforest.goblin_knight.muffled.hurt" : "entity.twilightforest.goblin_knight.hurt", 1.0F,
                      voicePitch(levelRandom()));
    });
    for (char const* t : {id::BlockChainGoblin, id::LowerGoblinKnight, id::UpperGoblinKnight, id::HelmetCrab})
        gf::spawn::registerPlacement(t, gf::spawn::PlacementRule::OnGround);
    game::db::onWorldClosed([] { chains().clear(); });
}

} // namespace gf::feature::stronghold::goblins
