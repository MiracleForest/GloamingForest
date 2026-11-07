// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/TripleBowItem.java
//   src/main/java/twilightforest/item/SeekerBowItem.java
//   src/main/java/twilightforest/item/IceBowItem.java
//   src/main/java/twilightforest/item/EnderBowItem.java
//   src/main/java/twilightforest/entity/projectile/SeekerArrow.java
//   src/main/java/twilightforest/entity/projectile/IceArrow.java
//   src/main/java/twilightforest/events/ToolEvents.java (onEnderBowHit)
//   src/main/java/twilightforest/events/EntityEvents.java (triple bow invulnerableTime)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's four bows over BowItem#releaseUsing: the draw starts with the item's use (the BP food
// component, bow animation), the release shoots. The ammunition is Player#getProjectile's: an
// arrow in the other hand / this hand, else the first in the inventory (creative: a plain arrow).
//   - triple_bow: vanilla arrows, three per shot (the outer two creative-only pickup, +-0.15 y),
//     speed power * 2.5; its arrows strip the victim's hurt cooldown;
//   - ender_bow: a vanilla arrow tagged twilightforest:ender: the shooter and a hit living non-boss
//     swap places;
//   - seeker_bow / ice_bow: TF's own arrow entities (F8's TFArrow kit), seeker_arrow homing
//     (SeekerArrow#updateTarget, steering), ice_arrow chilling.

#include "feature/weapons/Weapons.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/projectile/AbstractArrow.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/item/Item.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gEnderSwaps = 0; ///< selftests
int gPunches    = 0; ///< selftests: Punch knockbacks of vanilla arrows

namespace {

constexpr char kEnderTag[] = "twilightforest:ender";
constexpr char kSeekKey[]  = "weapons:seeker";
constexpr char kPunchKey[] = "weapons:punch";

/// Player#getProjectile: where the arrow comes from.
struct Ammo {
    ItemStack stack;
    int       slot    = -1; ///< inventory slot (-1: a hand / none)
    int       hand    = -1; ///< 1 main hand, 0 off hand
    bool      virtual_ = false; ///< creative without arrows: a plain arrow from nowhere
};

bool isArrow(ItemStack const& s) { return game::items::idOf(s) == "minecraft:arrow"; }

std::optional<Ammo> findAmmo(Player& player) {
    // ProjectileWeaponItem#getHeldProjectile: the off hand, then the main hand
    if (isArrow(player.getOffhandSlot())) return Ammo{player.getOffhandSlot(), -1, 0};
    if (isArrow(player.getCarriedItem())) return Ammo{player.getCarriedItem(), -1, 1};
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (isArrow(inv.getItem(i))) return Ammo{inv.getItem(i), i, -1};
    if (player.isCreative()) {
        if (auto a = game::items::make("minecraft:arrow")) return Ammo{*a, -1, -1, true};
    }
    return std::nullopt;
}

void consumeAmmo(Player& player, Ammo const& ammo) {
    if (ammo.virtual_) return;
    if (ammo.hand >= 0) {
        auto s = handStack(player, ammo.hand == 1);
        s.remove(1);
        setHandStack(player, ammo.hand == 1, s.mCount == 0 ? ItemStack::EMPTY_ITEM() : s);
        return;
    }
    auto& inv = player.getInventory();
    auto  s   = inv.getItem(ammo.slot);
    s.remove(1);
    inv.setItem(ammo.slot, s.mCount == 0 ? ItemStack::EMPTY_ITEM() : s);
    player.sendInventory(false);
}

/// A vanilla minecraft:arrow (Bedrock's Arrow entity) fired by `player`.
Actor* vanillaArrow(Player& player, ItemStack const& bow, ItemStack const& arrowStack, Vec3 const& dir, float speed, float inaccuracy, bool crit, bool creativeOnly) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto  feet = player.getFeetPos();
    Vec3  pos{feet.x, player.getPosition().y - 0.1f, feet.z};
    auto* a = level->getSpawner().spawnProjectile(player.getDimensionBlockSource(), ActorDefinitionIdentifier("minecraft:arrow"), &player, pos, dir);
    if (!a) return nullptr;
    auto& arrow = static_cast<AbstractArrow&>(*a);
    arrow.shoot(dir, speed, inaccuracy, Vec3{0, 0, 0});
    arrow.mIsPlayerOwned = true;
    arrow.mIsCreative    = creativeOnly || player.isCreative();
    if (int aux = arrowStack.getAuxValue(); aux > 0) arrow.setAuxValue(aux);
    if (auto pc = a->getEntityContext().tryGetComponent<ProjectileComponent>()) {
        int power = EnchantUtils::getEnchantLevel(Enchant::Type::Power, bow);
        if (power > 0) pc->mEnchantPower = power;
        if (crit) pc->mCritParticleOnHurt = true;
    }
    // Punch (the bow's minecraft:knockback post-attack effect in 1.21): the game's own bow sets it
    // in BowItem::releaseUsing, which TF's bows replace, so the hit applies it (onDealt below)
    if (int punch = EnchantUtils::getEnchantLevel(Enchant::Type::Punch, bow); punch > 0) gf::entity::data(*a, kPunchKey) = punch;
    return a;
}

/// AbstractArrow#doKnockback of a vanilla arrow with Punch `punch` hitting `victim`.
void doKnockback(Actor& arrow, Actor& victim, int punch) {
    double resist = 0.0;
    auto   attr   = victim.getAttribute(SharedAttributes::KNOCKBACK_RESISTANCE());
    if (attr.mPtr) resist = attr.mPtr->mCurrentValue;
    auto   v = arrow.getPosDelta();
    double h = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
    if (h <= 1e-9) return;
    double f = punch * 0.6 * std::max(0.0, 1.0 - resist) / h;
    world::push(victim, Vec3{static_cast<float>(v.x * f), 0.1F, static_cast<float>(v.z * f)});
    ++gPunches;
}

/// The enchantments of the bow on one of TF's own arrows (Power, Punch, Flame).
void enchantArrow(Actor& arrow, ItemStack const& bow) {
    int power = EnchantUtils::getEnchantLevel(Enchant::Type::Power, bow);
    if (power > 0) gf::projectile::ext::setBaseDamage(arrow, gf::projectile::ext::baseDamage(arrow) + power * 0.5 + 0.5);
    int punch = EnchantUtils::getEnchantLevel(Enchant::Type::Punch, bow);
    if (punch > 0) gf::projectile::ext::setKnockback(arrow, punch);
    if (EnchantUtils::getEnchantLevel(Enchant::Type::Flame, bow) > 0) arrow.setOnFire(100, true);
}

} // namespace

void bowRelease(Player& player, ItemStack& bow, int used) {
    auto ammo = findAmmo(player);
    if (!ammo) return;
    float f = cw::bowPower(used);
    if (f < 0.1F) return;
    std::string id       = game::items::idOf(bow);
    bool        infinite = player.isCreative() || (EnchantUtils::getEnchantLevel(Enchant::Type::Infinity, bow) > 0 && ammo->stack.getAuxValue() == 0);
    Vec3        dir      = lookOf(player);
    bool        crit     = f == 1.0F;
    auto        arrowJson = game::items::toJson(game::items::withCount(ammo->stack, 1));
    if (id == kTripleBow) {
        auto lift = cw::tripleBowLift();
        for (int j = -1; j < 2; ++j) {
            Actor* a = vanillaArrow(player, bow, ammo->stack, dir, f * 2.5F, 1.0F, crit, j != 0 || infinite);
            if (!a) continue;
            auto v = a->getPosDelta();
            world::setMotion(*a, Vec3{v.x, v.y + static_cast<float>(lift[j + 1]), v.z});
            a->addTag("twilightforest:triple");
            if (EnchantUtils::getEnchantLevel(Enchant::Type::Flame, bow) > 0) a->setOnFire(100, true);
        }
    } else if (id == kEnderBow) {
        Actor* a = vanillaArrow(player, bow, ammo->stack, dir, f * 3.0F, 1.0F, crit, infinite);
        if (a) {
            a->addTag(kEnderTag);
            if (EnchantUtils::getEnchantLevel(Enchant::Type::Flame, bow) > 0) a->setOnFire(100, true);
        }
    } else {
        std::string type = id == kSeekerBow ? kSeekerArrow : kIceArrow;
        Actor*      a    = gf::projectile::shoot(player, type, dir, f * 3.0F, 1.0F, Json{{"pickupItem", arrowJson}});
        if (a) {
            if (crit) gf::projectile::ext::setCritArrow(*a, true);
            if (infinite) gf::projectile::ext::setPickup(*a, gf::projectile::ext::Pickup::CreativeOnly);
            enchantArrow(*a, bow);
        }
    }
    // the bow's durability: one per arrow drawn
    hurtAndBreak(bow, 1, &player);
    if (!infinite) consumeAmmo(player, *ammo);
    gf::fx::sound(player.getDimension(), "minecraft:entity.arrow.shoot", player.getFeetPos(), 1.0F, 1.0F / (world::random().nextFloat() * 0.4F + 1.2F) + f * 0.5F);
    log().info("weapons: {} shot the {} (power {:.2f})", player.getRealName(), id, f);
}

namespace {

bool isMonster(Actor& a) { return a.hasCategory(ActorCategory::Monster); }
/// NeutralMob among Java's monsters.
bool isNeutralMonster(Actor& a) {
    static constexpr std::string_view kNeutral[] = {"minecraft:enderman", "minecraft:zombie_pigman", "minecraft:zombified_piglin", "minecraft:piglin"};
    for (auto n : kNeutral)
        if (a.getTypeName() == n) return true;
    return false;
}

/// SeekerArrow#updateTarget.
void updateTarget(Actor& arrow, Json& st) {
    Actor* target = st.value("target", std::int64_t{0}) ? world::actorById(st["target"].get<std::int64_t>()) : nullptr;
    if (target && !target->isAlive()) {
        target       = nullptr;
        st["target"] = 0;
    }
    if (target) return;
    auto   motion = gf::projectile::ext::velocity(arrow);
    auto   pos    = arrow.getPosition();
    auto [lo, hi] = cw::seekerBox(v3(pos), v3(motion));
    AABB   box{vec(lo), vec(hi)};
    auto*  owner  = gf::projectile::ext::owner(arrow);
    std::vector<Actor*> living;
    for (auto* e : world::actorsIn(arrow.getDimensionBlockSource(), static_cast<int>(arrow.getDimensionId()), box))
        if (e && e->isAlive() && isLiving(*e)) living.push_back(e);
    // monsters targeting the owner, then monsters that see the arrow (not neutral)
    for (auto* m : living)
        if (isMonster(*m) && owner && static_cast<Mob*>(m)->mTargetId->rawID == world::uidOf(*owner)) {
            st["target"] = world::uidOf(*m);
            return;
        }
    for (auto* m : living) {
        if (!isMonster(*m) || isNeutralMonster(*m)) continue;
        if (world::hasLineOfSight(*static_cast<Mob*>(m), arrow)) {
            st["target"] = world::uidOf(*m);
            return;
        }
    }
    double closest = -1.0;
    Actor* best    = nullptr;
    for (auto* l : living) {
        if (!world::hasLineOfSight(*static_cast<Mob*>(l), arrow)) continue;
        if (l == owner) continue;
        if (owner && l->isTame() && l->getOwnerId().rawID == world::uidOf(*owner)) continue;
        auto   lp  = l->getFeetPos();
        cw::V3 to  = cw::normalize({lp.x - pos.x, (lp.y + (eyeOf(*l).y - lp.y)) - pos.y, lp.z - pos.z});
        double dot = cw::dot(cw::normalize(v3(motion)), to);
        if (dot > std::max(closest, 0.5)) closest = dot, best = l;
    }
    if (best) st["target"] = world::uidOf(*best);
}

std::optional<Vec3> seekerSteering(Actor& arrow, std::string const& type) {
    if (type != kSeekerArrow) return std::nullopt;
    auto v = gf::projectile::ext::velocity(arrow);
    if (gf::projectile::ext::inGround(arrow) || double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z <= 1.0) return std::nullopt;
    auto& st = gf::entity::data(arrow, kSeekKey);
    if (!st.is_object()) st = Json{{"target", 0}};
    updateTarget(arrow, st);
    // the trail (client side in Java): 4 witch particles
    auto pos = arrow.getPosition();
    for (int i = 0; i < 4; ++i)
        gf::fx::particle(arrow.getDimension(), "minecraft:witch", Vec3{pos.x + v.x * i / 4.0f, pos.y + v.y * i / 4.0f, pos.z + v.z * i / 4.0f}, Vec3{-v.x, -v.y + 0.2f, -v.z});
    Actor* target = st.value("target", std::int64_t{0}) ? world::actorById(st["target"].get<std::int64_t>()) : nullptr;
    if (!target) return std::nullopt;
    auto   tp    = target->getFeetPos();
    auto   eye   = eyeOf(*target);
    cw::V3 to{tp.x - pos.x, eye.y - pos.y, tp.z - pos.z};
    auto   steer = cw::seekerSteer(v3(v), to);
    if (!steer) {
        st["target"] = 0;
        return std::nullopt;
    }
    return vec(*steer);
}

void enderSwap(Player& player, Actor& living) {
    auto  srcPos = player.getFeetPos();
    auto  srcRot = player.getRotation();
    auto  dstPos = living.getFeetPos();
    auto  dstRot = living.getRotation();
    auto  dim    = player.getDimensionId();
    player.teleport(dstPos, dim, Vec2{srcRot.x, dstRot.y});
    player.mInvulnerableTime = 40;
    gf::fx::sound(player.getDimension(), "minecraft:item.chorus_fruit.teleport", dstPos, 1.0F, 1.0F);
    living.teleportTo(srcPos, true, 0, 0, false);
    const_cast<Vec2&>(living.getRotation()) = Vec2{srcRot.x, srcRot.y};
    gf::fx::sound(living.getDimension(), "minecraft:item.chorus_fruit.teleport", srcPos, 1.0F, 1.0F);
    ++gEnderSwaps;
    log().info("weapons: ender bow swapped {} with {}", player.getRealName(), living.getTypeName());
}

} // namespace

void registerBows() {
    for (char const* id : {kTripleBow, kSeekerBow, kIceBow, kEnderBow}) {
        gf::item::registerItem(
            id,
            {
                .use =
                    [](Player& player, ItemStack&) {
                        // BowItem#use: no arrow, no draw (creative always draws)
                        return findAmmo(player) ? gf::item::Result::Pass : gf::item::Result::Fail;
                    },
                .release = [](Player& player, ItemStack& stack, int used) { bowRelease(player, stack, used); },
                .repairWith =
                    [id = std::string(id)](ItemStack const&, ItemStack const& m) {
                        // IceBowItem: the ICE tier's repair ingredient; the others none
                        return id == kIceBow && gf::tags::has(gf::tags::Registry::Item, "twilightforest:repairs_ice_tools", game::items::idOf(m));
                    },
            }
        );
    }
    gf::projectile::ext::registerArrow(kSeekerArrow, {.drag = 0.99F}, {.baseDamage = 1.0, .beforeHitEntity = [](Actor& arrow, Actor&) {
                                                                       // SeekerArrow#onHitEntity: never critical
                                                                       gf::projectile::ext::setCritArrow(arrow, false);
                                                                   }});
    gf::projectile::ext::registerArrow(
        kIceArrow,
        {
            .drag = 0.99F,
            .onHitEntity =
                [](Actor&, Actor& target) {
                    if (isLiving(target)) chillAura(target, 200, 2, true);
                    return true;
                },
            .tick =
                [](Actor& arrow) {
                    if (gf::projectile::ext::inGround(arrow)) return;
                    auto v   = gf::projectile::ext::velocity(arrow);
                    auto pos = arrow.getPosition();
                    for (int i = 0; i < 4; ++i)
                        gf::fx::particle(arrow.getDimension(), "minecraft:falling_dust", Vec3{pos.x + v.x * i / 4.0f, pos.y + v.y * i / 4.0f, pos.z + v.z * i / 4.0f},
                                         Vec3{-v.x, -v.y + 0.2f, -v.z}, 1, Json{{"block", "minecraft:snow"}});
                },
        }
    );
    gf::projectile::addSteering(seekerSteering);
    for (char const* id : {kTripleBow, kSeekerBow, kIceBow, kEnderBow}) gf::item::ext::setRarity(id, gf::item::ext::Rarity::Uncommon);

    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        auto [causing, direct] = causingAndDirect(d.attacker, d.direct);
        if (!causing || !causing->isPlayer() || !direct || direct == causing) return;
        auto& player = static_cast<Player&>(*causing);
        // EntityEvents#entityHurts: an arrow from a player whose used hand holds the triple bow
        if (d.type == "minecraft:arrow" && game::items::idOf(player.getCarriedItem()) == kTripleBow) d.victim.mInvulnerableTime = 0;
        // Punch of the triple / ender bow's vanilla arrows (once per arrow, as its hit)
        if (direct->getTypeName() == "minecraft:arrow" && isLiving(d.victim) && d.victim.isAlive()) {
            auto& punch = gf::entity::data(*direct, kPunchKey);
            if (punch.is_number_integer() && punch.get<int>() > 0) {
                int level = punch.get<int>();
                punch     = nullptr;
                doKnockback(*direct, d.victim, level);
            }
        }
        // ToolEvents#onEnderBowHit
        if (direct->hasTag(kEnderTag) && isLiving(d.victim) && &d.victim != causing &&
            !gf::tags::has(gf::tags::Registry::EntityType, "c:bosses", d.victim.getTypeName())) {
            direct->removeTag(kEnderTag);
            auto uid = world::uidOf(d.victim);
            auto pid = world::uidOf(player);
            gf::interactions::nextTick([uid, pid] {
                auto* p = world::playerById(pid);
                auto* v = world::actorById(uid);
                if (p && v) enderSwap(*p, *v);
            });
        }
    });
}

} // namespace gf::feature::weapons
