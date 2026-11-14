// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/FireBeetle.java
//   src/main/java/gloamingforest/entity/monster/SlimeBeetle.java
//   src/main/java/gloamingforest/entity/monster/PinchBeetle.java
//   src/main/java/gloamingforest/entity/projectile/SlimeProjectile.java
//   src/main/java/gloamingforest/util/entities/EntityUtil.java (properlyApplyCustomDamageSource)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hollow-hill beetles. Float, look and the targets are the BP's behaviours; upstream's goals run
// natively and the BP's melee / stroll (gf:vanilla_move) wait while they hold MOVE:
//   fire beetle   2 BreathAttack (F8) 5 / 30 / 0.1: 2 scorched damage, 10 s of fire, flames;
//   slime beetle  2 AvoidEntity Player 3 (1.25 / 2.0), 3 RangedAttack 1 / 30 / 10: slime blobs;
//   pinch beetle  2 ChargeAttack 1.5 (F8): its hit grabs the target as its rider (IHostileMount,
//                 F8's mount::grab) and squeezes with gloamingforest:clamped.

#include <cmath>
#include <memory>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::hostiles::beetles {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;
namespace ia   = gf::feature::common::interactions;

namespace {


// ---- FireBeetle ---------------------------------------------------------------------------------------------

/// FireBeetle#aiStep while breathing: 2 flames 0.9 ahead at y + 0.25, the shoot sound.
void fireBreath(Mob& m) {
    auto&  r    = randomOf(m);
    Vec3   look = m.getViewVector(1.0f);
    auto   p    = m.getFeetPos();
    double dist = 0.9;
    Vec3   at{float(p.x + look.x * dist), float(p.y + 0.25 + look.y * dist), float(p.z + look.z * dist)};
    for (int i = 0; i < 2; i++) {
        double dx       = look.x;
        double dy       = look.y;
        double dz       = look.z;
        double spread   = 5 + r.nextDouble() * 2.5;
        double velocity = 0.15 + r.nextDouble() * 0.15;
        // spread flame
        dx += r.nextGaussian() * 0.0075 * spread;
        dy += r.nextGaussian() * 0.0075 * spread;
        dz += r.nextGaussian() * 0.0075 * spread;
        dx *= velocity;
        dy *= velocity;
        dz *= velocity;
        gf::fx::particle(m.getDimension(), "minecraft:flame", at, Vec3{float(dx), float(dy), float(dz)});
    }
    playSound(m, "entity.gloamingforest.fire_beetle.shoot", r.nextFloat() * 0.5F, r.nextFloat() * 0.5F);
}

void fireBeetleTick(Actor& self) {
    if (despawnInPeaceful(self)) return;
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (!nativeGoalsPresent(self))
        ge::addGoal(*m, 2, ge::goals::breath(*m, {.range = 5.0f, .durationTicks = 30, .chance = 0.1f, .damageType = "gloamingforest:scorched"}));
    bool breathing = ge::ext::isBreathing(self);
    setPropertyCached(self, "gloamingforest:breathing", breathing);
    setGroup(self, "vanilla_move", !breathing);
    if (breathing) fireBreath(*m);
}

// ---- SlimeBeetle / SlimeProjectile -----------------------------------------------------------------------------

/// SlimeProjectile#die: the squish, 8 slime particles, gone.
void blobDie(Actor& blob) {
    auto& r = levelRandom();
    gf::fx::sound(blob.getDimension(), "minecraft:entity.slime.squish", blob.getFeetPos(), 1.0F, 1.0F / (r.nextFloat() * 0.4F + 0.8F));
    for (int i = 0; i < 8; ++i)
        gf::fx::particle(blob.getDimension(), "minecraft:item_slime", blob.getFeetPos(),
                         Vec3{float(r.nextGaussian() * 0.05), float(r.nextDouble() * 0.2), float(r.nextGaussian() * 0.05)});
}

void slimeBeetleTick(Actor& self) {
    if (despawnInPeaceful(self)) return;
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (!nativeGoalsPresent(self)) {
        ge::addGoal(*m, 2, std::make_shared<AvoidEntityGoal>(*m, [](Actor& a) { return a.isPlayer(); }, 3.0F, 1.25, 2.0));
        ge::addGoal(*m, 3, std::make_shared<RangedAttackGoal>(*m, 1.0, 30, 30, 10.0F, [](Mob& b, Actor& t, float) { slimeShoot(b, t); }));
    }
    syncVanillaMove(*m);
}

// ---- PinchBeetle -------------------------------------------------------------------------------------------------

/// The boat's planks (Boat.Type#getPlanks) by Bedrock's boat variant.
char const* boatPlanks(int variant) {
    static char const* const kPlanks[] = {"minecraft:oak_planks",    "minecraft:spruce_planks",   "minecraft:birch_planks",
                                          "minecraft:jungle_planks", "minecraft:acacia_planks",   "minecraft:dark_oak_planks",
                                          "minecraft:mangrove_planks", "minecraft:bamboo_planks", "minecraft:cherry_planks",
                                          "minecraft:pale_oak_planks"};
    return variant >= 0 && variant < 10 ? kPlanks[variant] : kPlanks[0];
}

void spawnAt(Actor& at, char const* item) {
    auto level = ll::service::getLevel();
    if (!level) return;
    if (auto s = gf::game::items::make(item)) level->getSpawner().spawnItem(at.getDimensionBlockSource(), *s, nullptr, at.getFeetPos(), 0);
}

void pinchBeetleTick(Actor& self) {
    if (despawnInPeaceful(self)) return;
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (!nativeGoalsPresent(self)) ge::addGoal(*m, 2, std::make_shared<LockedGoal>(*m, ge::goals::charge(*m, {.speed = 1.5, .canBreak = false})));
    syncVanillaMove(*m);
    // PinchBeetle#startRiding(Boat): the boat breaks (3 planks, 2 sticks) and the ride is refused
    if (Actor* v = self.getVehicle(); v && (v->getTypeName() == "minecraft:boat" || v->getTypeName() == "minecraft:chest_boat")) {
        self.stopRiding(true, false, false, false);
        if (doEntityDrops()) {
            for (int i = 0; i < 3; i++) spawnAt(self, boatPlanks(v->getVariant()));
            for (int j = 0; j < 2; j++) spawnAt(self, "minecraft:stick");
        }
        v->remove(); // Boat#kill: gone, without dropping itself
        playSound(self, "minecraft:entity.zombie.break_wooden_door", 1.0F, voicePitch(self));
    }
    // PinchBeetle#aiStep: the held passenger
    Actor* passenger = self.getFirstPassenger();
    setGroup(self, "carrying", passenger != nullptr); // 2.2 x 1.6, no knockback while it holds one
    if (!passenger) return;
    mobs::lookAt(*m, *passenger, 100.0F, 100.0F);
    // always set our passenger as our target
    if (isLiving(*passenger) && targetOf(*m) != passenger) m->setTarget(passenger);
    // if our held player switches gamemodes let them go
    if (passenger->isPlayer()) {
        auto& player = static_cast<Player&>(*passenger);
        if (player.isCreative() || player.isSpectator()) {
            ge::ext::hostileDismount(player);
            m->setTarget(nullptr);
            log().info("hostiles: pinch beetle released {}", player.getRealName());
        }
    }
}

/// PinchBeetle#doHurtTarget: grab, then properlyApplyCustomDamageSource(CLAMPED).
void pinchHit(std::int64_t beetleId, std::int64_t victimId) {
    auto* b = actorById(beetleId);
    auto* v = actorById(victimId);
    if (!b || !v || !b->isAlive() || !v->isAlive()) return;
    auto* beetle = asMob(*b);
    if (!beetle) return;
    if (!beetle->getFirstPassenger()) {
        Actor* vehicle = v->getVehicle();
        if (!vehicle || !gf::tags::has(gf::tags::Registry::EntityType, "gloamingforest:rides_obstruct_snatching", vehicle->getTypeName())) {
            // pluck them from the boat, minecart, donkey, or whatever
            if (v->isPlayer()) ge::mount::grab(*beetle, static_cast<Player&>(*v));
            else {
                if (vehicle) v->stopRiding(true, false, false, false);
                v->startRiding(*beetle, true);
            }
        }
    }
    // the attack damage (no weapon enchantments: the beetle holds nothing); its knockback attribute
    // is 0, so no fling
    gf::combat::hurt(*v, attackDamage(*beetle, 4.0F), "gloamingforest:clamped", beetle);
}

} // namespace

void slimeShoot(Mob& beetle, Actor& target) {
    auto& r = randomOf(beetle);
    playSound(beetle, "entity.gloamingforest.slime_beetle.squish", 1.0F, 1.0F / (r.nextFloat() * 0.4F + 0.8F));
    auto   tp   = target.getFeetPos();
    auto   p    = beetle.getFeetPos();
    double py   = beetle.getEyePos().y - 0.1; // ThrowableProjectile(type, owner): at the eyes - 0.1
    double tx   = tp.x - p.x;
    double ty   = tp.y + (fe::eyeOf(target).y - tp.y) - 1.1 - py;
    double tz   = tp.z - p.z;
    float  heightOffset = std::sqrt(static_cast<float>(tx * tx + tz * tz)) * 0.2F;
    gf::projectile::shoot(beetle, id::SlimeBlob, Vec3{float(tx), float(ty + heightOffset), float(tz)}, 0.6F, 6.0F);
}

void install() {
    ge::registerType(id::FireBeetle, {.tick = fireBeetleTick, .die = forgetActor});
    // FireBeetle#doBreathAttack: 2 scorched damage to a target that is not fire immune, then 10 s of fire
    ge::ext::onBreathAttack(id::FireBeetle, [](Mob& beetle, Actor& target) {
        if (!target.isFireImmune() && gf::combat::hurt(target, 2.0F, "gloamingforest:scorched", &beetle)) target.setOnFire(10, true);
    });
    // FireBeetle#doHurtTarget while breathing: 2 scorched damage only
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (!isType(d.attacker, id::FireBeetle) || (d.direct && d.direct != d.attacker) || d.type != "minecraft:mob_attack") return gf::combat::Verdict::Pass;
        if (!ge::ext::isBreathing(*d.attacker)) return gf::combat::Verdict::Pass;
        auto victim = uidOf(d.victim);
        auto beetle = uidOf(*d.attacker);
        ia::nextTick([victim, beetle] {
            auto* v = actorById(victim);
            auto* b = actorById(beetle);
            if (v && b && v->isAlive()) gf::combat::hurt(*v, 2.0F, "gloamingforest:scorched", b);
        });
        return gf::combat::Verdict::Cancel;
    });

    ge::registerType(id::SlimeBeetle, {.tick = slimeBeetleTick, .die = forgetActor});
    // SlimeProjectile: gravity 0.006, a trail of 2 slime particles; 4 thrown damage to a living target
    gf::projectile::Spec blob;
    blob.gravity     = 0.006f;
    blob.drag        = 0.99f;
    blob.tick        = [](Actor& p) { gf::projectile::ext::makeTrail(p, "minecraft:item_slime", 2); };
    blob.onHitEntity = [](Actor& p, Actor& target) {
        if (isLiving(target)) gf::combat::hurt(target, 4.0F, "minecraft:thrown", gf::projectile::ext::owner(p), &p);
        blobDie(p);
        return true;
    };
    blob.onHitBlock = [](Actor& p, BlockPos const&, int) {
        blobDie(p);
        return true;
    };
    gf::projectile::registerType(id::SlimeBlob, blob);
    // SlimeProjectile#hurt: any damage kills it
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (typeOf(d.victim) != id::SlimeBlob) return gf::combat::Verdict::Pass;
        if (d.victim.isAlive()) {
            blobDie(d.victim);
            fe::removeLater(uidOf(d.victim));
        }
        return gf::combat::Verdict::Cancel;
    });

    ge::registerType(id::PinchBeetle, {.tick = pinchBeetleTick, .die = [](Actor& a, std::string const& t, Actor* k) {
                                               // PinchBeetle#die: every passenger gets off
                                               while (Actor* p = a.getFirstPassenger()) {
                                                   ge::ext::hostileDismount(*p);
                                                   if (a.getFirstPassenger() == p) {
                                                       p->stopRiding(true, false, false, false);
                                                       if (a.getFirstPassenger() == p) break;
                                                   }
                                               }
                                               forgetActor(a, t, k);
                                           }});
    ge::ext::markHostileMount(id::PinchBeetle);
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (!isType(d.attacker, id::PinchBeetle) || (d.direct && d.direct != d.attacker) || d.type != "minecraft:mob_attack") return gf::combat::Verdict::Pass;
        auto victim = uidOf(d.victim);
        auto beetle = uidOf(*d.attacker);
        ia::nextTick([victim, beetle] { pinchHit(beetle, victim); });
        return gf::combat::Verdict::Cancel;
    });
}

} // namespace gf::feature::hostiles::beetles
