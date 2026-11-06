// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/PeacockFanItem.java
//   src/main/java/twilightforest/dispenser/FeatherFanDispenseBehavior.java
//   src/main/java/twilightforest/events/CapabilityEvents.java (feather fan fall)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:peacock_feather_fan. Java moves the fanning player on its own client (the jump
// in the air, the elytra boost); here the server sends the motion (gf::movement::applyMotion),
// for the client mod and vanilla clients alike. The feather_fan_falling attachment (the next
// landing takes no fall damage) is kept per player while they are online.

#include "feature/weapons/Weapons.h"

#include "feature/ext/entity/EntityExt.h"

#include <cctype>

#include <unordered_map>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/ext/item/Item.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/movement/Movement.h"
#include "feature/progression/Progression.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

namespace {

struct Falling {
    bool          flagged = false;
    std::uint64_t landed  = 0; ///< the tick the flag was cleared on landing (the fall damage of that landing is still ignored)
};
std::unordered_map<std::int64_t, Falling>& falling() {
    static std::unordered_map<std::int64_t, Falling> m;
    return m;
}


AABB effectBox(Player& player) {
    auto eye  = eyeOf(player);
    auto look = lookOf(player);
    Vec3 d{eye.x + look.x * 3.0f, eye.y + look.y * 3.0f, eye.z + look.z * 3.0f};
    return AABB{Vec3{d.x - 2.0f, d.y - 2.0f, d.z - 2.0f}, Vec3{d.x + 2.0f, d.y + 2.0f, d.z + 2.0f}};
}

/// PeacockFanItem#fanBlock.
int fanBlock(Player& player, BlockSource& region, BlockPos const& pos) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    if (gf::tags::has(gf::tags::Registry::Block, "minecraft:small_flowers", id)) {
        if (world::random().nextInt(3) == 0 && mayBreak(player, pos)) {
            region.getLevel().destroyBlock(region, pos, true, BlockChangeContext{});
            return 1;
        }
        return 0;
    }
    auto states = game::statesOf(b);
    if ((id.ends_with("candle") || id.ends_with("candle_cake")) && states.contains("lit") && states["lit"] == "true") {
        Json s = Json::object();
        for (auto const& [k, v] : states) s[k] = v == "true" ? Json(true) : v == "false" ? Json(false) : (std::isdigit(static_cast<unsigned char>(v[0])) ? Json(std::stoi(v)) : Json(v));
        s["lit"] = false;
        if (auto const* off = game::blockWithStates(id, s)) {
            game::setBlock(region, pos, *off);
            gf::fx::sound(region.getDimension(), "minecraft:block.candle.extinguish", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 1.0F, 1.0F);
        }
        return 0;
    }
    // TF's LightableBlock (candelabra, skull candles, ...): lighting normal / dim -> off
    if (auto it = states.find("twilightforest:lighting"); it != states.end() && (it->second == "normal" || it->second == "dim")) {
        Json s = Json::object();
        for (auto const& [k, v] : states) s[k] = v == "true" ? Json(true) : v == "false" ? Json(false) : (std::isdigit(static_cast<unsigned char>(v[0])) ? Json(std::stoi(v)) : Json(v));
        s["twilightforest:lighting"] = "off";
        if (auto const* off = game::blockWithStates(id, s)) {
            game::setBlock(region, pos, *off);
            gf::fx::sound(region.getDimension(), "minecraft:block.candle.extinguish", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 1.0F, 1.0F);
        }
    }
    return 0;
}

bool pushable(Actor& e) {
    if (e.isPlayer() && static_cast<Player&>(e).isSpectator()) return false;
    if (isLiving(e)) return true;
    auto t = e.getEntityTypeId();
    return t == ActorType::ItemEntity || t == ActorType::BoatRideable || t == ActorType::ChestBoatRideable || t == ActorType::MinecartRideable ||
           gf::projectile::ext::isProjectile(e) || t == ActorType::Arrow;
}

} // namespace

void setFanFalling(Player& player, bool on) { falling()[world::uidOf(player)].flagged = on; }

bool fanFalling(Player const& player) {
    auto it = falling().find(world::uidOf(player));
    return it != falling().end() && it->second.flagged;
}

int fanUse(Player& player) {
    auto& region = player.getDimensionBlockSource();
    auto  box    = effectBox(player);
    int   fanned = 0;
    for (auto const& p : cw::allInBox(v3(box.min), v3(box.max))) fanned += fanBlock(player, region, BlockPos{p.x, p.y, p.z});
    auto look = lookOf(player);
    Vec3 move{look.x * 2.0f, look.y * 2.0f, look.z * 2.0f};
    for (auto* e : world::actorsIn(region, static_cast<int>(player.getDimensionId()), box)) {
        if (!e || e == &player) continue;
        if (pushable(*e)) {
            if (gf::projectile::ext::isProjectile(*e)) gf::projectile::ext::setVelocity(*e, move);
            else if (!e->isPlayer()) world::setMotion(*e, move);
            ++fanned;
        }
        if (e->isPlayer() && !e->isSneaking()) {
            // MovePlayerPacket: the pushed player's client moves by the look * 2
            gf::movement::applyMotion(static_cast<Player&>(*e), move);
            gf::item::cooldown(player, kFan, 40);
            fanned += 2;
        }
    }
    return fanned;
}

void registerFan() {
    gf::item::registerItem(
        kFan,
        {.use = [](Player& player, ItemStack& stack) {
            bool flag   = !player.isOnGround() && !player.isSwimming() && !fanFalling(player);
            int  fanned = fanUse(player);
            hurtAndBreak(stack, fanned + 1, &player);
            auto look = lookOf(player);
            auto v    = player.getPosDelta();
            if (player.isGliding()) {
                // the elytra boost: like a rocket, a little faster, and 1.25 up
                v = Vec3{v.x + look.x * 0.1f + (look.x * 2.0f - v.x) * 0.5f, v.y + look.y * 0.1f + (look.y * 2.0f - v.y) * 0.5f + 1.25f,
                         v.z + look.z * 0.1f + (look.z * 2.0f - v.z) * 0.5f};
                gf::movement::applyMotion(player, v);
            }
            if (flag) {
                setFanFalling(player, true);
                gf::movement::applyMotion(player, Vec3{v.x * 1.05f, 1.5f, v.z * 1.05f});
            } else {
                auto  box = effectBox(player);
                auto& r   = world::random();
                for (int i = 0; i < 30; ++i) {
                    Vec3 at{box.min.x + r.nextFloat() * (box.max.x - box.min.x), box.min.y + r.nextFloat() * (box.max.y - box.min.y),
                            box.min.z + r.nextFloat() * (box.max.z - box.min.z)};
                    gf::fx::particle(player.getDimension(), "minecraft:cloud", at, look);
                }
            }
            auto& r = world::random();
            gf::fx::sound(player.getDimension(), "item.twilightforest.fan.whoosh", player.getFeetPos(), 1.0F + r.nextFloat(), r.nextFloat() * 0.7F + 0.3F);
            log().info("weapons: {} fanned ({} things)", player.getRealName(), fanned);
            return gf::item::Result::Consume;
        }}
    );
    gf::item::ext::setRarity(kFan, gf::item::ext::Rarity::Rare);
    // CapabilityEvents#updatePlayerCaps: the flag goes on landing, swimming or in water
    gf::progression::events::onTick([](Player& player) {
        auto it = falling().find(world::uidOf(player));
        if (it == falling().end() || !it->second.flagged) return;
        if (player.isOnGround() || player.isSwimming() || player.isInWater()) {
            it->second.flagged = false;
            it->second.landed  = world::currentTick();
        }
    });
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (d.type != "minecraft:fall" || !d.victim.isPlayer()) return gf::combat::Verdict::Pass;
        auto it = falling().find(world::uidOf(d.victim));
        if (it == falling().end()) return gf::combat::Verdict::Pass;
        if (it->second.flagged || world::currentTick() - it->second.landed <= 2) return gf::combat::Verdict::Cancel;
        return gf::combat::Verdict::Pass;
    });
    // FeatherFanDispenseBehavior
    gf::item::registerDispense(kFan, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        static constexpr int kDx[] = {0, 0, 0, 0, -1, 1}, kDy[] = {-1, 1, 0, 0, 0, 0}, kDz[] = {0, 0, -1, 1, 0, 0};
        if (facing < 0 || facing > 5) return false;
        BlockPos front{at.x + kDx[facing], at.y + kDy[facing], at.z + kDz[facing]};
        int      max    = stack.mItem ? stack.mItem->getMaxDamage() : 0;
        int      damage = max - stack.getDamageValue();
        AABB     box{Vec3{front.x - 3.0f, front.y - 3.0f, front.z - 3.0f}, Vec3{front.x + 4.0f, front.y + 4.0f, front.z + 4.0f}};
        std::vector<Actor*> things;
        for (auto* e : world::actorsIn(region, static_cast<int>(region.getDimensionId()), box))
            if (e && isLiving(*e) && !(e->isPlayer() && static_cast<Player&>(*e).isSpectator())) things.push_back(e);
        if (static_cast<int>(things.size()) >= damage) return false;
        Vec3 n{static_cast<float>(kDx[facing]), static_cast<float>(kDy[facing]), static_cast<float>(kDz[facing])};
        for (auto* e : things) {
            if (e->isPlayer()) gf::movement::applyMotion(static_cast<Player&>(*e), n);
            else world::setMotion(*e, n);
            stack.hurtAndBreak(1, nullptr);
        }
        auto& r = world::random();
        gf::fx::sound(region.getDimension(), "item.twilightforest.fan.whoosh", Vec3{at.x + .5f, at.y + .5f, at.z + .5f}, 1.0F + r.nextFloat(), r.nextFloat() * 0.7F + 0.3F);
        // FeatherFanDispenseBehavior#playAnimation: 30 clouds out of the front
        double cx = front.x + kDx[facing] * 0.6 + 0.5, cy = front.y + kDy[facing] * 0.6 + 0.5, cz = front.z + kDz[facing] * 0.6 + 0.5;
        for (int i = 0; i < 30; ++i) {
            double d4 = r.nextDouble() * 0.2 + 0.01;
            Vec3   at2{static_cast<float>(cx + kDx[facing] * 0.01 + (r.nextDouble() - 0.5) * kDz[facing] * 0.5),
                       static_cast<float>(cy + kDy[facing] * 0.01 + (r.nextDouble() - 0.5) * kDy[facing] * 0.5),
                       static_cast<float>(cz + kDz[facing] * 0.01 + (r.nextDouble() - 0.5) * kDx[facing] * 0.5)};
            Vec3   v{static_cast<float>(kDx[facing] * d4 + r.nextGaussian() * 0.01), static_cast<float>(kDy[facing] * d4 + r.nextGaussian() * 0.01),
                     static_cast<float>(kDz[facing] * d4 + r.nextGaussian() * 0.01)};
            gf::fx::particle(region.getDimension(), "minecraft:cloud", at2, v);
        }
        return true;
    });
}

} // namespace gf::feature::weapons
