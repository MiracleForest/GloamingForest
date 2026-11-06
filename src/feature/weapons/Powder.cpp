// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/TransformPowderItem.java
//   src/main/java/twilightforest/util/entities/EntityUtil.java (convertEntity)
//   src/main/java/twilightforest/dispenser/TransformationDispenseBehavior.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:transformation_powder turns a living, unowned mob into the type the
// twilightforest:transformation_powder data map gives it (R7: U8's map; the ids are the game's).
// EntityUtil#convertEntity: the new mob takes the old one's place, rotation and name, at full
// health; the old one is discarded.

#include "feature/weapons/Weapons.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/weapons/World.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

namespace {

constexpr char kMap[] = "twilightforest:transformation_powder";

std::string targetOf(std::string const& type) {
    auto const* v = gf::datamaps::get(kMap, type);
    if (!v || !v->is_object()) return {};
    return v->value("transform_to", std::string());
}

} // namespace

std::int64_t transform(Actor& target, ItemStack* powder, bool shrink) {
    if (!target.isAlive() || !isLiving(target) || target.isPlayer()) return 0;
    // OwnableEntity with an owner: never
    if (target.isTame() && target.getOwnerId().rawID != -1 && target.getOwnerId().rawID != 0) return 0;
    auto to = targetOf(target.getTypeName());
    if (to.empty()) return 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto& region = target.getDimensionBlockSource();
    auto  pos    = target.getFeetPos();
    auto  rot    = target.getRotation();
    Mob*  mob    = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(to), nullptr, pos, false, false, true);
    if (!mob) {
        log().warn("weapons: transformation powder: cannot spawn {}", to);
        return 0;
    }
    const_cast<Vec2&>(mob->getRotation()) = rot;
    if (auto const& name = target.getNameTag(); !name.empty()) mob->setNameTag(name);
    gf::fx::sound(target.getDimension(), "item.twilightforest.transformation_powder.use", pos, 1.0F, 1.0F);
    auto& r = world::random();
    for (int i = 0; i < 20; ++i) {
        auto const& bb = mob->getAABB();
        Vec3        at{bb.min.x + r.nextFloat() * (bb.max.x - bb.min.x), bb.min.y + r.nextFloat() * (bb.max.y - bb.min.y), bb.min.z + r.nextFloat() * (bb.max.z - bb.min.z)};
        gf::fx::particle(target.getDimension(), "minecraft:poof", at);
    }
    auto oldId = target.getTypeName();
    target.remove();
    if (powder && shrink) powder->remove(1);
    log().info("weapons: transformation powder turned {} into {}", oldId, to);
    return world::uidOf(*mob);
}

void registerPowder() {
    gf::interactions::onInteract({.item = kPowder}, [](gf::interactions::EntityContext const& c) {
        if (!c.entity.isAlive()) return gf::interactions::Result::Pass;
        if (targetOf(c.entity.getTypeName()).empty()) return gf::interactions::Result::Pass;
        auto& stack = gf::interactions::mutableItem(c.item);
        if (!transform(c.entity, &stack, !c.player.isCreative())) return gf::interactions::Result::Pass;
        if (stack.mCount == 0) stack = ItemStack::EMPTY_ITEM();
        c.player.setCarriedItem(stack);
        c.player.sendInventory(false);
        return gf::interactions::Result::Consume;
    });
    // TransformPowderItem#use: crit particles in the 2x2x2 box 2 blocks ahead (Java's client side)
    gf::item::registerItem(kPowder, {.use = [](Player& player, ItemStack&) {
                                         auto  eye  = eyeOf(player);
                                         auto  look = lookOf(player);
                                         Vec3  d{eye.x + look.x * 2.0f, eye.y + look.y * 2.0f, eye.z + look.z * 2.0f};
                                         auto& r = world::random();
                                         for (int i = 0; i < 30; ++i)
                                             gf::fx::particle(player.getDimension(), "minecraft:crit",
                                                              Vec3{d.x - 1.0f + r.nextFloat() * 2.0f, d.y - 1.0f + r.nextFloat() * 2.0f, d.z - 1.0f + r.nextFloat() * 2.0f});
                                         return gf::item::Result::Consume;
                                     }});
    // TransformationDispenseBehavior
    gf::item::registerDispense(kPowder, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        static constexpr int kDx[] = {0, 0, 0, 0, -1, 1}, kDy[] = {-1, 1, 0, 0, 0, 0}, kDz[] = {0, 0, -1, 1, 0, 0};
        if (facing < 0 || facing > 5) return false;
        BlockPos f{at.x + kDx[facing], at.y + kDy[facing], at.z + kDz[facing]};
        AABB     box{Vec3{f.x + 0.0f, f.y + 0.0f, f.z + 0.0f}, Vec3{f.x + 1.0f, f.y + 1.0f, f.z + 1.0f}};
        bool     fired = false;
        for (auto* e : world::actorsIn(region, static_cast<int>(region.getDimensionId()), box)) {
            if (!e || (e->isPlayer() && static_cast<Player&>(*e).isSpectator())) continue;
            if (stack.mCount == 0) break;
            if (transform(*e, &stack, true)) fired = true;
        }
        return fired;
    });
}

} // namespace gf::feature::weapons
