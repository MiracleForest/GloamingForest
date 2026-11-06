// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/IceSwordItem.java
//   src/main/java/twilightforest/item/GlassSwordItem.java
//   src/main/java/twilightforest/item/KnightmetalShieldItem.java
//   src/main/java/twilightforest/events/EntityEvents.java (onParryProjectile)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The ice and glass swords (CustomDamageSwordItem attack numbers in their BP items plus the
// fraction through attackBonus), the knightmetal shield and TF's shield parrying.
//
// The knightmetal shield is a BP item, which Bedrock's engine never treats as a shield: its
// blocking is LivingEntity#isDamageSourceBlocked / #blockUsingShield here (gf::combat), raised by
// using it (Java's right-click, the BLOCK animation) or, Bedrock's way, by sneaking with it in a
// hand; either way after 5 ticks. Parrying (onParryProjectile): a projectile hitting a player who
// raised a shield at most shieldParryTicksArrow ticks ago is sent back along the player's look
// (AIM_DEFLECT) and does no damage; TF's projectiles always, others with parryNonTwilightAttacks.

#include "feature/weapons/Weapons.h"

#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/item/Item.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/progression/Progression.h"
#include "feature/projectile/Projectile.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "feature/common/Interactions.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gGlassBreaks = 0; ///< selftests
int gParries     = 0;
int gShieldBlocks = 0;

namespace {

/// The tracker particles of a sword hit around `target` (ParticlePacket to trackers).
void hitParticles(Actor& target, std::string const& particle, Json const& params, bool gaussian) {
    auto&       r  = world::random();
    auto const& bb = target.getAABB();
    float       w  = bb.max.x - bb.min.x, h = bb.max.y - bb.min.y;
    auto        p  = target.getFeetPos();
    for (int i = 0; i < 20; ++i) {
        Vec3 at = gaussian ? Vec3{static_cast<float>(p.x + r.nextGaussian() * w * 0.5), static_cast<float>(p.y + h * 0.5F + r.nextGaussian() * h * 0.5),
                                  static_cast<float>(p.z + r.nextGaussian() * w * 0.5)}
                           : Vec3{p.x + r.nextFloat() * w * 2.0F - w, p.y + r.nextFloat() * h, p.z + r.nextFloat() * w * 2.0F - w};
        gf::fx::particle(target.getDimension(), particle, at, {}, 1, params);
    }
}

/// GlassSwordItem#hurtEnemy.
bool glassHit(ItemStack& stack, Actor& target, Actor& attacker) {
    hitParticles(target, "minecraft:block", Json{{"block", "minecraft:white_stained_glass"}}, false);
    if (!attacker.isPlayer()) return false;
    auto& player = static_cast<Player&>(attacker);
    if (player.isCreative()) return false;
    if (!gf::item::getData(stack, kInfiniteGlassKey).is_null()) return false;
    // EnchantmentHelper#processDurabilityChange: Unbreaking keeps the point with 1 - 1 / (lvl + 1)
    int unbreaking = EnchantUtils::getEnchantLevel(Enchant::Type::Unbreaking, stack);
    if (unbreaking > 0 && world::random().nextInt(unbreaking + 1) > 0) return false;
    gf::progression::trigger(player, "twilightforest:broke_glass_sword");
    gf::fx::sound(player.getDimension(), "item.twilightforest.glass_sword.break", player.getFeetPos(), 1.0F, 0.5F);
    stack = ItemStack::EMPTY_ITEM();
    if (game::items::idOf(player.getCarriedItem()) == kGlassSword) player.setCarriedItem(ItemStack::EMPTY_ITEM());
    player.sendInventory(false);
    ++gGlassBreaks;
    log().info("weapons: {}'s glass sword shattered", player.getRealName());
    return false; // the stack is gone: no post-hit durability
}

/// The horizontal LivingEntity#isDamageSourceBlocked test: the source is in front of the player.
bool inFront(Player& player, Vec3 const& source) {
    auto   pos = player.getFeetPos();
    cw::V3 to  = cw::normalize({pos.x - source.x, 0.0, pos.z - source.z});
    auto   view = v3(lookOf(player));
    return cw::dot(to, view) < 0.0;
}

bool isAxe(ItemStack const& s) {
    auto id = game::items::idOf(s);
    return id.ends_with("_axe") || gf::tags::has(gf::tags::Registry::Item, "minecraft:axes", id);
}

gf::combat::Verdict blockAndParry(gf::combat::DamageInfo& d) {
    if (!d.victim.isPlayer()) return gf::combat::Verdict::Pass;
    auto& player = static_cast<Player&>(d.victim);
    int   ticks  = 0;
    if (!isBlocking(player, &ticks)) return gf::combat::Verdict::Pass;
    auto [causing, direct] = world::causingAndDirect(d.attacker, d.direct);
    Actor* source          = direct ? direct : causing;
    if (!source) return gf::combat::Verdict::Pass;
    // parrying: a projectile within the window
    bool projectile = direct && direct != causing && !isLiving(*direct);
    if (projectile && ticks <= config().shieldParryTicksArrow) {
        bool tf = gf::projectile::ext::isProjectile(*direct);
        if (tf || config().parryNonTwilightAttacks) {
            auto   v     = tf ? gf::projectile::ext::velocity(*direct) : direct->getPosDelta();
            double speed = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
            auto   look  = lookOf(player);
            Vec3   nv{static_cast<float>(look.x * speed), static_cast<float>(look.y * speed), static_cast<float>(look.z * speed)};
            // the projectile kit handles the cancelled hit after this (an arrow's REVERSE
            // deflection): the parry's aim goes in on the next tick. Bedrock's kit has no owner
            // setter, so the projectile keeps its shooter (Java makes the blocker its owner).
            auto uid = world::uidOf(*direct);
            if (tf) {
                gf::projectile::ext::setVelocity(*direct, nv);
                gf::interactions::nextTick([uid, nv] {
                    if (auto* a = world::actorById(uid)) gf::projectile::ext::setVelocity(*a, nv);
                });
            } else world::setMotion(*direct, nv);
            ++gParries;
            log().info("weapons: {} parried a {}", player.getRealName(), direct->getTypeName());
            return gf::combat::Verdict::Cancel;
        }
    }
    // the knightmetal shield blocks (Bedrock's engine blocks with its own shield)
    if (!handHolding(player, kShield) || player.isBlocking()) return gf::combat::Verdict::Pass;
    if (!inFront(player, source->getFeetPos())) return gf::combat::Verdict::Pass;
    // LivingEntity#hurtCurrentlyUsedShield
    if (d.amount >= 3.0F) {
        bool main = game::items::idOf(player.getCarriedItem()) == kShield;
        auto s    = handStack(player, main);
        hurtAndBreak(s, 1 + static_cast<int>(std::floor(d.amount)), &player);
        setHandStack(player, main, s);
    }
    gf::fx::sound(player.getDimension(), "minecraft:item.shield.block", player.getFeetPos(), 1.0F, 0.8F + world::random().nextFloat() * 0.4F);
    // LivingEntity#blockUsingShield: a melee attacker is knocked back, an axe disables the shield
    if (causing && causing == direct && isLiving(*causing)) {
        auto a = causing->getFeetPos(), p = player.getFeetPos();
        cw::V3 k = cw::normalize({a.x - p.x, 0, a.z - p.z});
        world::push(*causing, Vec3{static_cast<float>(k.x * 0.5), 0.1F, static_cast<float>(k.z * 0.5)});
        auto weapon = causing->isPlayer() ? static_cast<Player&>(*causing).getCarriedItem() : causing->getCarriedItem();
        // Item#canDisableShield: axes and the cube of annihilation
        if (isAxe(weapon) || game::items::idOf(weapon) == kCube) disableShield(player);
    }
    ++gShieldBlocks;
    return gf::combat::Verdict::Cancel;
}

} // namespace

void registerSwords() {
    gf::item::registerItem(
        kIceSword,
        {
            .hurtEnemy =
                [](ItemStack&, Actor& target, Actor&) {
                    chillAura(target, 200, 2, true);
                    hitParticles(target, "twilightforest:snow", {}, true);
                    return true;
                },
            // ICE: attack 3.5 + sword 3 + 1 = 7.5; the BP item's damage gives 7 (6 + the player's 1)
            .attackBonus = [](ItemStack const&, Actor&, Player&) { return 0.5F; },
            .repairWith  = [](ItemStack const&, ItemStack const& m) { return gf::tags::has(gf::tags::Registry::Item, "twilightforest:repairs_ice_tools", game::items::idOf(m)); },
        }
    );
    gf::item::registerItem(kGlassSword, {.hurtEnemy = glassHit, .repairWith = [](ItemStack const&, ItemStack const&) { return false; }});
    gf::item::ext::setRarity(kGlassSword, gf::item::ext::Rarity::Uncommon);
    gf::item::addTooltip(kGlassSword, [](ItemStack const& stack, std::vector<std::string>& lines, Player const*) {
        if (!gf::item::getData(stack, kInfiniteGlassKey).is_null()) lines.push_back("§7" + gf::tr("item.twilightforest.glass_sword.desc", "Creative Mode-only"));
    });
}

void registerShield() {
    gf::item::registerItem(
        kShield,
        {
            .use = [](Player&, ItemStack&) { return gf::item::Result::Pass; }, // raise it (the BP use)
            .repairWith =
                [](ItemStack const&, ItemStack const& m) {
                    // knightmetal ingots; ShieldItem's planks are excluded
                    return gf::tags::has(gf::tags::Registry::Item, "c:ingots/knightmetal", game::items::idOf(m));
                },
        }
    );
    gf::combat::onIncoming(gf::combat::priority::High + 10, blockAndParry);
    // the raise time of a sneak-raised shield counts from the tick the sneak started
    gf::progression::events::onTick([](Player& player) { (void)trackSneak(player); });
    game::db::onWorldClosed([] {
        forgetPlayers();
        forgetFanFlags();
    });
}

} // namespace gf::feature::weapons
