// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/CubeOfAnnihilationItem.java
//   src/main/java/twilightforest/entity/projectile/CubeOfAnnihilation.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:cube_of_annihilation (the item and its ThrowableProjectile, no gravity). Every
// server tick the cube heads for the point 16 blocks along its thrower's look (or the thrower's
// eye once it returns) at up to 0.5 blocks a tick and annihilates the blocks its box (inflated by
// 0.2) touches: #twilightforest:annihilation_inclusions, or Java explosion resistance < 8 and
// breakable. A block it may not annihilate (or a protected one) makes it return; it returns too
// when its owner stops using the item. Living things it touches take 10 (player / mob attack).

#include "feature/weapons/Weapons.h"

#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gAnnihilated = 0; ///< blocks annihilated (selftests)

namespace {

constexpr char kKey[] = "weapons:cube";

Json& stateOf(Actor& cube) {
    auto& d = gf::entity::data(cube, kKey);
    if (!d.is_object()) d = Json{{"obstacle", false}};
    return d;
}

Actor* ownerOf(Actor& cube) {
    auto const& o = stateOf(cube)["owner"];
    return o.is_number_integer() ? world::actorById(o.get<std::int64_t>()) : nullptr;
}

/// CubeOfAnnihilation#isReturning.
bool returning(Actor& cube) {
    auto* owner = ownerOf(cube);
    if (stateOf(cube).value("obstacle", false) || !owner || !owner->isPlayer()) return true;
    return usedItemId(static_cast<Player&>(*owner)) != kCube;
}

void removeCube(Actor& cube) {
    auto& st = stateOf(cube);
    if (st.value("removed", false)) return;
    st["removed"] = true;
    auto* owner   = ownerOf(cube);
    if (owner && owner->isPlayer() && usedItemId(static_cast<Player&>(*owner)) == kCube) static_cast<Player&>(*owner).stopUsingItem();
    world::removeLater(world::uidOf(cube));
}

/// CubeOfAnnihilation#canAnnihilate.
bool canAnnihilate(Block const& block) {
    if (gf::tags::has(gf::tags::Registry::Block, "twilightforest:annihilation_inclusions", game::blockId(block))) return true;
    return javaExplosionResistance(block) < 8.0F && block.getBlockType().getDestroySpeed() >= 0.0F;
}

/// CubeOfAnnihilation#annihilateParticles: 27 annihilate particles, gaussian 0.2 velocity.
void particles(Actor& cube, BlockPos const& pos) {
    auto& r = world::random();
    for (int dx = 0; dx < 3; ++dx)
        for (int dy = 0; dy < 3; ++dy)
            for (int dz = 0; dz < 3; ++dz) {
                Vec3 at{pos.x + (dx + 0.5F) / 4.0F, pos.y + (dy + 0.5F) / 4.0F, pos.z + (dz + 0.5F) / 4.0F};
                Vec3 v{static_cast<float>(r.nextGaussian() * 0.2), static_cast<float>(r.nextGaussian() * 0.2), static_cast<float>(r.nextGaussian() * 0.2)};
                gf::fx::particle(cube.getDimension(), "twilightforest:annihilate", at, v);
            }
}

/// CubeOfAnnihilation#affectBlocksInAABB.
void affectBlocks(Actor& cube) {
    auto* owner = ownerOf(cube);
    if (!owner || !owner->isPlayer()) return;
    auto& player = static_cast<Player&>(*owner);
    auto& region = cube.getDimensionBlockSource();
    auto  bb     = cube.getAABB();
    for (auto const& p : cw::allInBox({bb.min.x - 0.2, bb.min.y - 0.2, bb.min.z - 0.2}, {bb.max.x + 0.2, bb.max.y + 0.2, bb.max.z + 0.2})) {
        BlockPos     pos{p.x, p.y, p.z};
        Block const& block = region.getBlock(pos);
        if (block.isAir()) continue;
        if (mayBreak(player, pos) && canAnnihilate(block)) {
            region.removeBlock(pos, BlockChangeContext{});
            ++gAnnihilated;
            gf::fx::sound(cube.getDimension(), "block.twilightforest.generic.annihilation", cube.getPosition(), 0.125F, world::random().nextFloat() * 0.25F + 0.75F);
            particles(cube, pos);
        } else {
            stateOf(cube)["obstacle"] = true;
        }
    }
}

/// CubeOfAnnihilation#onHitEntity: living things the cube touches take 10.
void hitEntities(Actor& cube, Actor* owner) {
    auto const& bb = cube.getAABB();
    AABB        box{Vec3{bb.min.x - 0.3f, bb.min.y - 0.3f, bb.min.z - 0.3f}, Vec3{bb.max.x + 0.3f, bb.max.y + 0.3f, bb.max.z + 0.3f}};
    std::string type = owner && owner->isPlayer() ? "minecraft:player_attack" : owner ? "minecraft:mob_attack" : "minecraft:thrown";
    for (auto* e : world::actorsIn(cube.getDimensionBlockSource(), static_cast<int>(cube.getDimensionId()), box)) {
        if (!e || e == owner || e == &cube || !e->isAlive() || !isLiving(*e)) continue;
        if (e->isPlayer() && static_cast<Player&>(*e).isSpectator()) continue;
        gf::combat::hurt(*e, 10.0F, type, owner ? owner : &cube, owner ? owner : &cube);
    }
}

/// CubeOfAnnihilation#tick (server). The cube moves itself (no gravity; Java's ThrowableProjectile
/// keeps moving through what it hits, annihilating it or turning back).
void tick(Actor& cube) {
    auto& st = stateOf(cube);
    if (st.value("removed", false)) return;
    if (auto& launch = gf::entity::data(cube, "weapons:launch"); launch.is_array() && launch.size() == 3) {
        st["vel"] = launch;
        launch    = nullptr;
    }
    // the move of this tick with last tick's velocity, then the hits
    if (auto const& v = st["vel"]; v.is_array() && v.size() == 3) {
        auto p = cube.getPosition();
        Vec3 d{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
        cube.moveTo(Vec3{p.x + d.x, p.y + d.y, p.z + d.z}, cube.getRotation());
    }
    auto* owner = ownerOf(cube);
    hitEntities(cube, owner);
    if (!owner) {
        removeCube(cube);
        return;
    }
    auto   eye  = eyeOf(*owner);
    cw::V3 dest = v3(eye);
    double dist = std::sqrt(world::distanceSqr(cube, owner->getFeetPos()));
    if (returning(cube)) {
        if (dist < 2.0) {
            removeCube(cube);
            return;
        }
    } else {
        dest = cw::add(dest, cw::scale(v3(lookOf(*owner)), 16.0));
    }
    auto const& bb     = cube.getAABB();
    cw::V3      centre = {cube.getFeetPos().x, cube.getFeetPos().y + (bb.max.y - bb.min.y) / 2.0, cube.getFeetPos().z};
    auto v    = cw::cubeVelocity(centre, dest);
    st["vel"] = {v.x, v.y, v.z};
    affectBlocks(cube);
}

} // namespace

std::int64_t throwCube(Player& player, ItemStack& stack) {
    auto thrown = gf::item::getData(stack, kThrownKey);
    if (thrown.is_number_integer() && world::actorById(thrown.get<std::int64_t>())) return 0;
    // ThrowableProjectile(type, shooter): at the eye - 0.1, shot from the rotation at 1.5
    auto   feet = player.getFeetPos();
    Actor* cube = world::shootFrom(player.getDimensionBlockSource(), Vec3{feet.x, eyeOf(player).y - 0.1f, feet.z}, kCube, lookOf(player), 1.5F, 1.0F);
    if (!cube) return 0;
    stateOf(*cube)["owner"] = world::uidOf(player);
    stateOf(*cube)["stack"] = game::items::toJson(stack);
    gf::item::setData(stack, kThrownKey, world::uidOf(*cube));
    log().info("weapons: {} threw a cube of annihilation", player.getRealName());
    return world::uidOf(*cube);
}

void registerCube() {
    gf::entity::registerType(kCube, {.tick = tick});
    gf::entity::ext::setCanUsePortal(kCube, false);
    auto inventoryTick = [](ItemStack& stack, Player&, int, bool) {
        auto thrown = gf::item::getData(stack, kThrownKey);
        if (thrown.is_number_integer() && !world::actorById(thrown.get<std::int64_t>())) gf::item::setData(stack, kThrownKey, nullptr);
    };
    gf::item::registerItem(
        kCube,
        {
            .use = [](Player& player, ItemStack& stack) { return throwCube(player, stack) ? gf::item::Result::Pass : gf::item::Result::Fail; },
            .inventoryTick = inventoryTick,
        }
    );
    // CubeOfAnnihilationItem#canDisableShield: hitting a blocking player disables the shield
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        Actor* causing = d.attacker; // DamageSource#getEntity
        Actor* direct  = d.direct;   // #getDirectEntity
        if (!causing || !causing->isPlayer() || !d.victim.isPlayer() || causing != direct) return gf::combat::Verdict::Pass;
        if (game::items::idOf(static_cast<Player&>(*causing).getCarriedItem()) != kCube) return gf::combat::Verdict::Pass;
        auto& victim = static_cast<Player&>(d.victim);
        if (isBlocking(victim)) disableShield(victim);
        return gf::combat::Verdict::Pass;
    });
}

} // namespace gf::feature::weapons
