// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/IceBombItem.java
//   src/main/java/gloamingforest/entity/projectile/IceBomb.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:ice_bomb throws gloamingforest:thrown_ice (also thrown by U27's alpha yeti, by
// string): a TFThrowable with gravity 0.025 until it lands. On a block it freezes the terrain
// around (radius 2, then 3 at tick 99) and stays 100 ticks, freezing the living things in
// +-3 / +-2 / +-3 every 20 ticks; yetis become two blocks of ice.

#include "feature/weapons/Weapons.h"

#include "feature/ext/entity/EntityExt.h"

#include <cmath>
#include <numbers>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gIceZoneHits = 0; ///< selftests

namespace {

constexpr char kKey[] = "weapons:ice_bomb";

Json& stateOf(Actor& bomb) {
    auto& d = gf::entity::data(bomb, kKey);
    if (!d.is_object()) d = Json{{"zone", 101}, {"hit", false}};
    return d;
}

/// SnowLayerBlock#canSurvive: a full top face below that is no ice, packed ice or barrier.
bool snowSurvives(BlockSource& region, BlockPos const& pos) {
    BlockPos below{pos.x, pos.y - 1, pos.z};
    auto     id = game::blockIdAt(region, below);
    if (id == "minecraft:ice" || id == "minecraft:packed_ice" || id == "minecraft:barrier") return false;
    if (id == "minecraft:honey_block" || id == "minecraft:soul_sand") return true;
    if (id == "minecraft:snow_layer") return game::intState(region.getBlock(below), "height").value_or(0) == 7;
    return world::legacySolid(region, below);
}

void setId(BlockSource& region, BlockPos const& pos, char const* id) {
    if (auto const* b = game::blockWith(id)) game::setBlock(region, pos, *b);
}

/// IceBomb#doTerrainEffect.
void terrainEffect(BlockSource& region, BlockPos const& pos) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    if (id == "minecraft:water" || id == "minecraft:flowing_water") setId(region, pos, "minecraft:ice");
    if (id == "minecraft:lava" && game::intState(b, "liquid_depth").value_or(0) == 0) setId(region, pos, "minecraft:obsidian");
    if (region.getBlock(pos).isAir() && snowSurvives(region, pos)) setId(region, pos, "minecraft:snow_layer");
    if (gf::tags::has(gf::tags::Registry::Block, "gloamingforest:ice_bomb_replaceables", id)) setId(region, pos, snowSurvives(region, pos) ? "minecraft:snow_layer" : "minecraft:air");
    if (id == "minecraft:snow_layer") {
        int h = game::intState(b, "height").value_or(0);
        if (h < 7)
            if (auto const* more = game::blockWithStates("minecraft:snow_layer", Json{{"height", h + 1}})) game::setBlock(region, pos, *more);
    }
}

/// IceBomb#inflictDamage.
void inflict(Actor& bomb, Actor& entity, int mult) {
    if (freezeImmune(entity)) return;
    float dmg = (freezeHurtsExtra(entity) ? 5.0F : 1.0F) * static_cast<float>(mult);
    gf::combat::hurt(entity, dmg, "gloamingforest:frozen", gf::projectile::ext::owner(bomb), &bomb);
    chillAura(entity, 100 * mult, 0, true);
}

/// IceBomb#hitNearbyEntities.
void hitNearby(Actor& bomb) {
    auto  bb     = bomb.getAABB();
    AABB  box{Vec3{bb.min.x - 3, bb.min.y - 2, bb.min.z - 3}, Vec3{bb.max.x + 3, bb.max.y + 2, bb.max.z + 3}};
    auto* owner  = gf::projectile::ext::owner(bomb);
    auto& region = bomb.getDimensionBlockSource();
    for (auto* e : world::actorsIn(region, static_cast<int>(bomb.getDimensionId()), box)) {
        if (!e || e == owner || !isLiving(*e) || !e->isAlive()) continue;
        if (e->getTypeName() == "gloamingforest:yeti") {
            auto p = game::blockPosOf(e->getFeetPos());
            setId(region, p, "minecraft:ice");
            setId(region, BlockPos{p.x, p.y + 1, p.z}, "minecraft:ice");
            e->remove();
            log().info("weapons: an ice bomb froze a yeti");
        } else {
            inflict(bomb, *e, 1);
        }
        ++gIceZoneHits;
    }
}

bool onHitBlock(Actor& bomb, BlockPos const&, int) {
    auto& st = stateOf(bomb);
    if (st.value("hit", false)) return false;
    st["hit"] = true;
    iceTerrain(bomb.getDimensionBlockSource(), bomb.getPosition(), 2);
    return false; // stays where it landed
}

bool onHitEntity(Actor& bomb, Actor& target) {
    if (isLiving(target)) inflict(bomb, target, 2);
    return false;
}

void tick(Actor& bomb) {
    applyLaunch(bomb);
    auto& st = stateOf(bomb);
    if (st.value("removed", false)) return;
    auto& r = world::random();
    if (st.value("hit", false)) {
        int zone   = st.value("zone", 101) - 1;
        st["zone"] = zone;
        auto pos   = bomb.getPosition();
        // makeIceZone, client part: 16 snow_guardian particles in a 3.5 radius
        for (int i = 0; i < 16; ++i)
            gf::fx::particle(bomb.getDimension(), "gloamingforest:snow_guardian",
                             Vec3{pos.x + (world::diffF(r)) * 3.5F, pos.y + (world::diffF(r)) * 3.5F, pos.z + (world::diffF(r)) * 3.5F});
        if (zone == 99) iceTerrain(bomb.getDimensionBlockSource(), pos, 3);
        if (zone % 20 == 0) hitNearby(bomb);
        if (zone <= 0) {
            auto p = game::blockPosOf(pos);
            if (auto const* ice = game::blockWith("minecraft:ice")) destroyParticles(bomb.getDimensionBlockSource(), p, *ice);
            st["removed"] = true;
            world::removeLater(world::uidOf(bomb));
        }
    } else {
        auto* owner = gf::projectile::ext::owner(bomb);
        int   n     = owner && owner->getTypeName() == "gloamingforest:alpha_yeti" ? 2 : 5;
        auto  pos   = bomb.getPosition();
        for (int i = 0; i < n; ++i)
            gf::fx::particle(bomb.getDimension(), "gloamingforest:snow_guardian",
                             Vec3{static_cast<float>(pos.x + 0.5 * (world::diffD(r))), static_cast<float>(pos.y + 0.5 * (world::diffD(r)) + 0.5),
                                  static_cast<float>(pos.z + 0.5 * (world::diffD(r)))});
    }
}

} // namespace

void iceTerrain(BlockSource& region, Vec3 const& pos, int range) {
    auto c = cw::containing(v3(pos));
    for (auto const& o : cw::iceBombOffsets(range)) terrainEffect(region, BlockPos{c.x + o.x, c.y + o.y, c.z + o.z});
}

void registerIceBomb() {
    gf::projectile::registerType(kThrownIce, {.gravity = 0.025F, .drag = 0.99F, .onHitEntity = onHitEntity, .onHitBlock = onHitBlock, .tick = tick});
    gf::item::registerItem(kIceBomb, {.use = [](Player& player, ItemStack& stack) {
                                          gf::fx::sound(player.getDimension(), "item.gloamingforest.ice_bomb.fired", player.getFeetPos(), 0.5F,
                                                        0.4F / (world::random().nextFloat() * 0.4F + 0.8F));
                                          if (!player.isCreative()) stack.remove(1);
                                          // shootFromRotation(xRot, yRot, -5, 1.25, 1): the pitch offset bends only y
                                          auto  rot = player.getRotation();
                                          float d   = std::numbers::pi_v<float> / 180.0F;
                                          Vec3  dir{-std::sin(rot.y * d) * std::cos(rot.x * d), -std::sin((rot.x - 5.0F) * d), std::cos(rot.y * d) * std::cos(rot.x * d)};
                                          gf::projectile::shoot(player, kThrownIce, dir, 1.25F, 1.0F);
                                          if (stack.mCount == 0) stack = ItemStack::EMPTY_ITEM();
                                          return gf::item::Result::Consume;
                                      }});
    // DispenserBlock#registerProjectileBehavior(ICE_BOMB)
    gf::item::registerDispense(kIceBomb, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        static constexpr int kDx[] = {0, 0, 0, 0, -1, 1}, kDy[] = {-1, 1, 0, 0, 0, 0}, kDz[] = {0, 0, -1, 1, 0, 0};
        if (facing < 0 || facing > 5) return false;
        Vec3 pos{at.x + 0.5f + 0.7f * kDx[facing], at.y + 0.5f + 0.7f * kDy[facing], at.z + 0.5f + 0.7f * kDz[facing]};
        Vec3 dir{static_cast<float>(kDx[facing]), kDy[facing] + 0.1f, static_cast<float>(kDz[facing])};
        if (!world::shootFrom(region, pos, kThrownIce, dir, 1.1F, 6.0F)) return false;
        stack.remove(1);
        if (stack.mCount == 0) stack = ItemStack::EMPTY_ITEM();
        return true;
    });
}

} // namespace gf::feature::weapons
