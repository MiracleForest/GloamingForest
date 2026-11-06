// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/TwilightWandItem.java
//   src/main/java/twilightforest/item/LifedrainScepterItem.java
//   src/main/java/twilightforest/item/ZombieWandItem.java
//   src/main/java/twilightforest/item/FortificationWandItem.java
//   src/main/java/twilightforest/item/ExanimateEssenceItem.java
//   src/main/java/twilightforest/enchantment/RechargeScepterEffect.java
//   src/main/java/twilightforest/util/TFItemStackUtils.java
//   src/main/java/twilightforest/dispenser/DamageableStackDispenseBehavior.java
//   src/main/java/twilightforest/dispenser/TFDispenserBehaviors.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The four scepters (durability = charges; a depleted scepter refuses to work outside creative and
// never breaks), their renewal recharge (U10's enchantment, by id), the mystic crown's bonuses, the
// twilight scepter's dispenser behaviour and the exanimate essence.
//
// The lifedrain scepter is used over time (BP minecraft:use_modifiers, BOW animation); its red beam
// is drawn from the server for every client with twilightforest:magic_effect particles (the
// effect's colour key: a downward velocity is red, TF's makeRedMagicTrail; upward is the twilight
// scepter's teal), from the hand position of LifedrainScepterItem#getPlayerHandPos's third-person
// branch.
#include <cfloat>
#include <cmath>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia    = gf::feature::common::interactions;
namespace items = game::items;

namespace {

constexpr char kRenewal[] = "twilightforest:renewal";

int maxDamage(ItemStack const& stack) {
    auto const* item = stack.mItem.get();
    return item ? item->getMaxDamage() : 0;
}

bool depleted(ItemStack const& stack) { return stack.getDamageValue() >= maxDamage(stack); }

/// Adds a vanilla effect (MobEffectInstance with Java's ambient / visible flags).
void addEffect(Actor& actor, char const* name, int ticks, int amp, bool visible = true) {
    auto const* e = MobEffect::getByName(name);
    if (!e) return;
    MobEffectInstance inst(e->mId, EffectDuration{ticks});
    inst.mAmplifier     = amp;
    inst.mEffectVisible = visible;
    inst.mAmbient       = false;
    actor.addEffect(inst);
}

float soundPitch(Actor& a) {
    auto& r = levelRandom();
    return (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F;
}

/// The renewal enchantment of U10 (inventoryTick of every scepter, every 20 ticks, unselected).
void renewalTick(ItemStack& stack, Player& player, bool selected) {
    if (selected || tickCountOf(player) % 20 != 0) return;
    if (gf::enchant::level(stack, kRenewal) > 0) applyRecharge(stack, player);
}

/// The player's block interaction ray (Item#getPlayerPOVHitResult, no fluids): the hit point.
std::optional<Vec3> povHit(Player& player) {
    Vec3   eye   = player.getEyePos();
    Vec3   look  = viewVector(player);
    double reach = player.isCreative() ? 5.0 : 4.5; // Player#blockInteractionRange
    Vec3   to{static_cast<float>(eye.x + look.x * reach), static_cast<float>(eye.y + look.y * reach), static_cast<float>(eye.z + look.z * reach)};
    HitResult hr = player.getDimensionBlockSource().clip(eye, to, false, ShapeType::Outline, 200, false, false, nullptr,
                                                         [](BlockSource const&, Block const&, bool) { return true; }, false);
    if (hr.mType != HitResultType::Tile) return std::nullopt;
    return hr.mPos;
}

/// LifedrainScepterItem#getPlayerHandPos (third person: the body yaw, the arm holding the scepter).
Vec3 handPos(Player& player) {
    int hand = 1; // a right-handed player
    if (items::idOf(mainHand(player)) != kLifedrainScepter) hand = -hand;
    float  yRot   = player.getYBodyRotation() * std::numbers::pi_v<float> / 180.0F;
    double sin    = std::sin(yRot), cos = std::cos(yRot);
    double offset = hand * 0.35, factor = 0.8;
    float  crouch = player.isSneaking() ? -0.1875F : 0.0F;
    Vec3   eye    = player.getEyePos();
    return Vec3{static_cast<float>(eye.x - cos * offset - sin * factor), static_cast<float>(eye.y + crouch - 0.45),
                static_cast<float>(eye.z - sin * offset + cos * factor)};
}

/// LifedrainParticlePacket / makeRedMagicTrail: the red trail from the hand to the target's eyes.
void redTrail(Player& player, Vec3 const& target) {
    Vec3   hand     = handPos(player);
    double dx       = hand.x - target.x, dy = hand.y - target.y, dz = hand.z - target.z;
    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance < 1e-6) return;
    for (double i = 0; i <= distance * 3; i++) {
        double k = i / (distance * 3);
        Vec3   p{static_cast<float>(hand.x - dx * k), static_cast<float>(hand.y - dy * k), static_cast<float>(hand.z - dz * k)};
        // colour (1.0, 0.5, 0.5): the effect reads a downward velocity as red
        particle(player, "twilightforest:magic_effect", p, Vec3{0.0F, -1e-4F, 0.0F});
    }
}

/// LifedrainScepterItem#animateTargetShatter: rotten flesh item particles over the target.
void animateTargetShatter(Actor& target) {
    auto&  r     = levelRandom();
    bool   big   = r.nextInt(100) == 0;
    double power = big ? 1.0 : 0.3;
    float  w     = bbWidth(target), h = bbHeight(target);
    int    n     = 50 + static_cast<int>(w) * (big ? 75 : 25);
    auto   pos   = target.getFeetPos();
    Json   params{{"item", "minecraft:rotten_flesh"}};
    for (int i = 0; i < n; ++i) {
        double gx = r.nextGaussian() * 0.01, gy = r.nextGaussian() * 0.01, gz = r.nextGaussian() * 0.01;
        double speed = r.nextFloat() * power;
        double x     = r.nextFloat() * w * 1.5F - w - gx * 5.0 + (r.nextGaussian() * gx);
        double y     = r.nextFloat() * h - gy * 5.0 + (r.nextGaussian() * gy);
        double z     = r.nextFloat() * w * 1.5F - w - gz * 5.0 + (r.nextGaussian() * gz);
        particle(target, "minecraft:item", Vec3{static_cast<float>(pos.x + x), static_cast<float>(pos.y + y), static_cast<float>(pos.z + z)},
                 Vec3{static_cast<float>(x * speed), static_cast<float>(y * speed), static_cast<float>(z * speed)}, params);
    }
}

/// Mob#spawnAnim: 20 poof particles around the mob.
void spawnAnim(Actor& mob) {
    auto& r   = randomOf(mob);
    auto  pos = mob.getFeetPos();
    float w = bbWidth(mob), h = bbHeight(mob);
    for (int i = 0; i < 20; ++i) {
        double vx = r.nextGaussian() * 0.02, vy = r.nextGaussian() * 0.02, vz = r.nextGaussian() * 0.02;
        Vec3   at{static_cast<float>(pos.x + (2.0 * r.nextDouble() - 1.0) * w - vx * 10.0), static_cast<float>(pos.y + r.nextDouble() * h - vy * 10.0),
                  static_cast<float>(pos.z + (2.0 * r.nextDouble() - 1.0) * w - vz * 10.0)};
        particle(mob, "minecraft:poof", at, Vec3{static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)});
    }
}

/// The scepter tooltip line (appendHoverText).
void scepterTooltip(ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
    auto n = std::to_string(charges(stack));
    lines.push_back("§7" + (viewer ? trFor(*viewer, "item.twilightforest.scepter.desc", "%s charges left", n) : tr("item.twilightforest.scepter.desc", "%s charges left", n)));
}

/// Whether the crown spares a charge (5 % for the twilight and lifedrain scepters).
bool spendCharge(Player& player) { return !creative(player) && (!wearsCrown(player) || levelRandom().nextFloat() > 0.05F); }

} // namespace

int charges(ItemStack const& stack) { return std::max(0, maxDamage(stack) - stack.getDamageValue()); }

void hurtButDontBreak(ItemStack& stack, int amount) {
    if (!stack.isDamageableItem()) return;
    int dmg = std::min(maxDamage(stack), stack.getDamageValue() + amount);
    stack.setDamageValue(static_cast<short>(dmg));
}

void applyRecharge(ItemStack& item, Player& player) {
    if (!depleted(item)) return;
    auto id  = items::idOf(item);
    auto& inv = player.getInventory();
    std::vector<int> slotsToConsume;
    for (auto const& recipe : scepterRepairs()) {
        if (recipe.scepter != id) continue;
        // ingredientCopy: the recipe's ingredients not found yet (by index)
        std::vector<std::size_t> copy;
        for (std::size_t k = 0; k < recipe.ingredients.size(); ++k) copy.push_back(k);
        bool done = false;
        for (int i = 0; i < 36 && !done; ++i) {
            auto const& stack = inv.getItem(i);
            if (items::isEmpty(stack)) continue;
            if (items::idOf(stack) == kEssence) {
                ItemStack s = stack;
                s.remove(1);
                inv.setItem(i, s);
                item.setDamageValue(0);
                log().info("artifacts: {} recharged a {} with an exanimate essence", player.getRealName(), id);
                return;
            }
            for (std::size_t k = 0; k < recipe.ingredients.size(); ++k) {
                auto it = std::find(copy.begin(), copy.end(), k);
                if (it != copy.end() && ingredientMatches(recipe.ingredients[k], stack)) {
                    copy.erase(it);
                    slotsToConsume.push_back(i);
                    if (copy.empty()) {
                        done = true;
                        break;
                    }
                }
            }
        }
        if (slotsToConsume.size() == recipe.ingredients.size()) {
            for (int slot : slotsToConsume) {
                ItemStack s = inv.getItem(slot);
                s.remove(1);
                inv.setItem(slot, s);
            }
            item.setDamageValue(static_cast<short>(std::max(0, item.getDamageValue() - recipe.durability)));
            log().info("artifacts: {} recharged a {} from its repair ingredients", player.getRealName(), id);
        }
    }
}

Actor* lookTarget(Actor& user, double range) {
    Vec3 src  = user.getEyePos();
    Vec3 look = viewVector(user);
    Vec3 dest{static_cast<float>(src.x + look.x * range), static_cast<float>(src.y + look.y * range), static_cast<float>(src.z + look.z * range)};
    auto b = user.getAABB();
    // getBoundingBox().expandTowards(look * range).inflate(1)
    AABB box{Vec3{std::min(b.min.x, b.min.x + static_cast<float>(look.x * range)) - 1.0F, std::min(b.min.y, b.min.y + static_cast<float>(look.y * range)) - 1.0F,
                  std::min(b.min.z, b.min.z + static_cast<float>(look.z * range)) - 1.0F},
             Vec3{std::max(b.max.x, b.max.x + static_cast<float>(look.x * range)) + 1.0F, std::max(b.max.y, b.max.y + static_cast<float>(look.y * range)) + 1.0F,
                  std::max(b.max.z, b.max.z + static_cast<float>(look.z * range)) + 1.0F}};
    Actor* pointed = nullptr;
    double hitDist = 0;
    for (auto* e : actorsIn(user.getDimensionBlockSource(), dimOf(user), box)) {
        if (!e || e == &user || !e->isAlive()) continue;
        if (!isLiving(*e) && !gf::projectile::ext::isProjectile(*e)) continue; // Entity#isPickable
        if (e->isPlayer() && static_cast<Player*>(e)->isSpectator()) continue;
        float border = 0.0F; // Entity#getPickRadius
        auto  eb     = e->getAABB();
        AABB  bb{Vec3{eb.min.x - border, eb.min.y - border, eb.min.z - border}, Vec3{eb.max.x + border, eb.max.y + border, eb.max.z + border}};
        bool  inside = src.x >= bb.min.x && src.x <= bb.max.x && src.y >= bb.min.y && src.y <= bb.max.y && src.z >= bb.min.z && src.z <= bb.max.z;
        if (inside) {
            pointed = e;
            hitDist = 0.0;
            continue;
        }
        // AABB#clip (slab test)
        double tmin = 0.0, tmax = 1.0;
        bool   miss = false;
        for (int axis = 0; axis < 3 && !miss; ++axis) {
            double s0 = axis == 0 ? src.x : axis == 1 ? src.y : src.z;
            double d  = (axis == 0 ? dest.x : axis == 1 ? dest.y : dest.z) - s0;
            double lo = axis == 0 ? bb.min.x : axis == 1 ? bb.min.y : bb.min.z;
            double hi = axis == 0 ? bb.max.x : axis == 1 ? bb.max.y : bb.max.z;
            if (std::abs(d) < 1e-12) {
                if (s0 < lo || s0 > hi) miss = true;
                continue;
            }
            double t1 = (lo - s0) / d, t2 = (hi - s0) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) miss = true;
        }
        if (miss) continue;
        double dist = tmin * range;
        if (dist < hitDist || hitDist == 0.0) {
            pointed = e;
            hitDist = dist;
        }
    }
    return pointed;
}

void lifedrainTick(Player& player, ItemStack& stack, int count) {
    if (depleted(stack)) {
        player.stopUsingItem();
        return;
    }
    if (count % 5 != 0) return;
    Actor* target = lookTarget(player);
    if (!target || !isLiving(*target) || target->getTypeName() == "minecraft:armor_stand") return;
    if (target->isAlive()) {
        redTrail(player, target->getEyePos());
        playSound(player, "item.twilightforest.life_scepter.drain", 1.0F, 1.0F);
    }
    std::int64_t tid = uidOf(*target);
    if (gf::combat::hurt(*target, 1.0F, "twilightforest:lifedrain", &player)) {
        target = actorById(tid);
        if (!target) return;
        if (target->getHealth() <= 1 && !gf::tags::has(gf::tags::Registry::EntityType, "c:bosses", target->getTypeName())) {
            if (!gf::tags::has(gf::tags::Registry::EntityType, "twilightforest:lifedrain_drops_no_flesh", target->getTypeName())) {
                gf::loot::LootContext ctx;
                ctx.region     = &target->getDimensionBlockSource();
                ctx.origin     = target->getEyePos();
                ctx.thisEntity = target;
                ctx.player     = &player;
                ctx.attacker   = &player;
                ctx.direct     = &player;
                ctx.damageType = "twilightforest:lifedrain";
                for (auto& s : gf::loot::roll("twilightforest:items/lifedrain_scepter_kill_bonus", ctx))
                    if (!items::isEmpty(s)) player.getLevel().getSpawner().spawnItem(*ctx.region, s, nullptr, target->getPosition(), 0);
                animateTargetShatter(*target);
            }
            if (!target->isPlayer()) spawnAnim(*target);
            if (target->isAlive()) {
                if (target->isPlayer()) gf::combat::hurt(*target, FLT_MAX, "twilightforest:lifedrain", &player);
                else {
                    gf::combat::hurt(*target, FLT_MAX, "twilightforest:lifedrain", &player);
                    if (auto* again = actorById(tid)) again->remove(); // die + discard: no death animation
                }
            }
            log().info("artifacts: {} drained the life of {}", player.getRealName(), tid);
        } else {
            addEffect(*target, "slowness", 20, 2);
            if (count % 10 == 0) {
                player.heal(1);
                player.eat(1, 0.1F);
            }
        }
        if (spendCharge(player)) hurtButDontBreak(stack, 1);
        target = actorById(tid);
    }
    // only the lifting effect on creatures weaker than the player
    if (target && target->isAlive() && target->getHealth() <= player.getHealth()) setMotion(*target, Vec3{0.0F, 0.15F, 0.0F});
}

Mob* summonLoyalZombie(Player& player, ItemStack& stack) {
    auto hit = povHit(player);
    if (!hit) return nullptr;
    auto& region = player.getDimensionBlockSource();
    // Level#noCollision(zombie, box): the 0.6 x 1.95 box at the hit point
    AABB box{Vec3{hit->x - 0.3F, hit->y, hit->z - 0.3F}, Vec3{hit->x + 0.3F, hit->y + 1.95F, hit->z + 0.3F}};
    std::vector<AABB> shapes;
    region.fetchAABBs(shapes, box, false);
    for (auto const& s : shapes)
        if (s.min.x < box.max.x && s.max.x > box.min.x && s.min.y < box.max.y && s.max.y > box.min.y && s.min.z < box.max.z && s.max.z > box.min.z)
            return nullptr;
    Mob* zombie = spawnMob(region, kLoyalZombie, *hit, player.getRotation().y);
    if (!zombie) return nullptr;
    spawnAnim(*zombie);
    tameLoyalZombie(*zombie, player);
    addEffect(*zombie, "strength", 1200, 1);
    if (wearsCrown(player) && levelRandom().nextFloat() <= 0.1F) setLoyalZombieBaby(*zombie, true);
    if (!creative(player)) hurtButDontBreak(stack, 1);
    playSound(*zombie, "item.twilightforest.zombie_scepter.use", 1.0F, 1.0F);
    log().info("artifacts: {} summoned a loyal zombie", player.getRealName());
    return zombie;
}

Actor* shootWandBolt(Actor& shooter, float inaccuracy) {
    // TwilightWandBolt(level, thrower): shootFromRotation(xRot, yRot, 0, 1.5, inaccuracy)
    return gf::projectile::shoot(shooter, kWandBolt, viewVector(shooter), 1.5F, inaccuracy);
}

void registerScepters() {
    // ---- the twilight scepter
    gf::item::registerItem(kTwilightScepter, {
        .use = [](Player& player, ItemStack& stack) {
            if (depleted(stack) && !creative(player)) return gf::item::Result::Fail;
            playSound(player, "item.twilightforest.twilight_scepter.use", 1.0F, soundPitch(player));
            shootWandBolt(player);
            if (spendCharge(player)) hurtButDontBreak(stack, 1);
            return gf::item::Result::Success;
        },
        .inventoryTick = [](ItemStack& stack, Player& player, int, bool selected) { renewalTick(stack, player, selected); },
    });
    // DamageableStackDispenseBehavior (inaccuracy 6, speed 1.1, the use sound)
    gf::item::registerDispense(kTwilightScepter, [](BlockSource& region, BlockPos const& pos, int facing, ItemStack& stack) {
        if (stack.getDamageValue() + 1 > maxDamage(stack)) return false;
        // FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
        static constexpr int dx[6] = {0, 0, 0, 0, -1, 1}, dy[6] = {-1, 1, 0, 0, 0, 0}, dz[6] = {0, 0, -1, 1, 0, 0};
        int  f = std::clamp(facing, 0, 5);
        Vec3 at{pos.x + 0.5F + 0.7F * dx[f], pos.y + 0.5F + 0.7F * dy[f], pos.z + 0.5F + 0.7F * dz[f]};
        // a projectile needs a shooter for the simulation: the nearest player, who does not own it
        Actor* anchor = nullptr;
        double best   = 64.0 * 64.0;
        for (auto* p : playersIn(static_cast<int>(region.getDimensionId()))) {
            auto   q = p->getPosition();
            double d = (q.x - at.x) * (q.x - at.x) + (q.y - at.y) * (q.y - at.y) + (q.z - at.z) * (q.z - at.z);
            if (d < best) best = d, anchor = p;
        }
        if (!anchor) return false;
        Actor* bolt = gf::projectile::shoot(*anchor, kWandBolt, Vec3{static_cast<float>(dx[f]), dy[f] + 0.1F, static_cast<float>(dz[f])}, 1.1F, 6.0F);
        if (!bolt) return false;
        bolt->teleportTo(at, false, 0, 1, false);
        setBoltOwner(*bolt, nullptr); // a dispensed bolt has no owner
        // Projectile#shoot from the dispense position along the dispenser's facing
        auto& r      = levelRandom();
        double len   = std::sqrt(double(dx[f]) * dx[f] + double(dy[f] + 0.1) * (dy[f] + 0.1) + double(dz[f]) * dz[f]);
        double sprd  = 0.0172275 * 6.0;
        gf::projectile::ext::setVelocity(*bolt, Vec3{static_cast<float>((dx[f] / len + r.triangle(0, sprd)) * 1.1), static_cast<float>(((dy[f] + 0.1) / len + r.triangle(0, sprd)) * 1.1),
                                                     static_cast<float>((dz[f] / len + r.triangle(0, sprd)) * 1.1)});
        hurtButDontBreak(stack, 1);
        gf::fx::sound(bolt->getDimension(), "item.twilightforest.twilight_scepter.use", Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, 1.0F, 1.0F);
        return true;
    });
    // ---- the lifedrain scepter (used over time: BP use_modifiers, 72000 ticks)
    gf::item::registerItem(kLifedrainScepter, {
        .use = [](Player& player, ItemStack& stack) {
            if (depleted(stack) && !creative(player)) return gf::item::Result::Fail;
            return gf::item::Result::Pass; // startUsingItem: the game's use duration
        },
        .useTick = [](Player& player, ItemStack& stack, int remaining) { lifedrainTick(player, stack, remaining); },
        .inventoryTick = [](ItemStack& stack, Player& player, int, bool selected) { renewalTick(stack, player, selected); },
    });
    // ---- the zombie scepter
    gf::item::registerItem(kZombieScepter, {
        .use = [](Player& player, ItemStack& stack) {
            if (depleted(stack) && !creative(player)) return gf::item::Result::Fail;
            if (!summonLoyalZombie(player, stack)) return gf::item::Result::Pass;
            return gf::item::Result::Success;
        },
        .inventoryTick = [](ItemStack& stack, Player& player, int, bool selected) { renewalTick(stack, player, selected); },
    });
    // a click on a block reaches the zombie scepter as a use too (Bedrock's use-on falls back to it)
    // ---- the fortification scepter
    gf::item::registerItem(kFortificationScepter, {
        .use = [](Player& player, ItemStack& stack) {
            if (depleted(stack) && !creative(player)) return gf::item::Result::Fail;
            if (gf::item::cooldownLeft(player, kFortificationScepter) > 0) return gf::item::Result::Fail;
            setShields(player, 5, true);
            if (!creative(player)) hurtButDontBreak(stack, 1);
            playSound(player, "entity.twilightforest.fortification_shield.add", 1.0F, soundPitch(player));
            if (!player.isCreative()) gf::item::cooldown(player, kFortificationScepter, 1200);
            log().info("artifacts: {} raised 5 fortification shields", player.getRealName());
            return gf::item::Result::Success;
        },
        .inventoryTick = [](ItemStack& stack, Player& player, int, bool selected) { renewalTick(stack, player, selected); },
    });
    for (char const* id : {kTwilightScepter, kLifedrainScepter, kZombieScepter, kFortificationScepter}) gf::item::addTooltip(id, scepterTooltip);

    // ---- the exanimate essence (ExanimateEssenceItem#useOn)
    ia::onUseOn({.item = kEssence}, [](ia::UseOnContext const& c) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        BlockPos pos = game::blockPos(c.pos);
        bool     flag = false;
        auto&    r    = levelRandom();
        float    pitch = (r.nextFloat() - r.nextFloat()) * 0.2F + 0.75F;
        std::string const& id = c.blockId;
        bool candle = id == "minecraft:candle" || (id.starts_with("minecraft:") && id.ends_with("_candle"));
        if (candle && game::intState(c.block, "lit").value_or(0) != 0) {
            std::string ominous = id == "minecraft:candle" ? "twilightforest:ominous_candle" : "twilightforest:ominous_" + id.substr(10);
            int         candles = game::intState(c.block, "candles").value_or(0) + 1;
            if (auto const* b = game::blockWithStates(ominous, Json{{"twilightforest:candles", candles}})) {
                gf::fx::sound(c.player.getDimension(), "block.twilightforest.ominous_fire", Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, 1.5F, pitch);
                game::setBlock(*region, pos, *b);
                for (int j = 0; j < 5 * candles; ++j)
                    particle(c.player, "twilightforest:ominous_flame", Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F},
                             Vec3{static_cast<float>((r.nextDouble() - 0.5) * 0.05), 0.015F, static_cast<float>((r.nextDouble() - 0.5) * 0.05)});
                flag = true;
            }
        }
        if (!flag) {
            static constexpr int fx[6] = {0, 0, 0, 0, -1, 1}, fy[6] = {-1, 1, 0, 0, 0, 0}, fz[6] = {0, 0, -1, 1, 0, 0};
            int      f = std::clamp(c.face, 0, 5);
            BlockPos at{pos.x + fx[f], pos.y + fy[f], pos.z + fz[f]};
            BlockPos below{at.x, at.y - 1, at.z};
            auto const& under = region->getBlock(below);
            bool sturdy = !under.isAir() && game::blockIdAt(*region, below) != "minecraft:water" && game::blockIdAt(*region, below) != "minecraft:lava";
            AABB shape  = under.getBlockType().getCollisionShape(under, *region, below, nullptr);
            sturdy      = sturdy && shape.max.y >= 0.999F + below.y && shape.max.x - shape.min.x >= 0.999F && shape.max.z - shape.min.z >= 0.999F;
            bool dry    = !region->getLiquidBlock(at).isAir() ? false : true;
            if (game::replaceable(*region, at) && sturdy && dry) {
                if (auto const* fire = game::blockWithStates("twilightforest:ominous_fire", Json::object())) {
                    gf::fx::sound(c.player.getDimension(), "block.twilightforest.ominous_fire", Vec3{at.x + 0.5F, at.y + 0.5F, at.z + 0.5F}, 1.5F, pitch);
                    game::setBlock(*region, at, *fire);
                    flag = true;
                }
            }
        }
        if (!flag) return ia::Result::Deny;
        if (!creative(c.player)) {
            auto& held = ia::mutableItem(c.item);
            held.remove(1);
            c.player.sendInventory(false);
        }
        return ia::Result::Consume;
    });
}

} // namespace gf::feature::artifacts

