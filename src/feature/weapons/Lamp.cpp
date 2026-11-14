// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/LampOfCindersItem.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:lamp_of_cinders: used on brown or green thorns (U30's blocks, by id) it burns
// them; held for more than 12 ticks and released it burns every thorn within 4 of the point 2
// blocks ahead and sets the mobs there on fire for 5 s. upstream 4.8 takes no durability for either.

#include "feature/weapons/Weapons.h"

#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/ext/item/Item.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/progression/Progression.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gLampBurns = 0; ///< selftests

namespace {

constexpr int kFiringTime = 12;

/// LampOfCindersItem#burnBlock.
bool burnBlock(BlockSource& region, BlockPos const& pos) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    if (id != "gloamingforest:brown_thorns" && id != "gloamingforest:green_thorns") return false;
    Json states = Json::object();
    for (auto const& [k, v] : game::statesOf(b)) states[k] = v == "true" ? Json(true) : v == "false" ? Json(false) : Json(v);
    auto const* burnt = game::blockWithStates("gloamingforest:burnt_thorns", states);
    if (!burnt) burnt = game::blockWith("gloamingforest:burnt_thorns");
    if (!burnt) return false;
    game::setBlock(region, pos, *burnt);
    ++gLampBurns;
    return true;
}

} // namespace

void lampBurn(Player& player) {
    auto  eye    = eyeOf(player);
    auto  look   = lookOf(player);
    auto  c      = cw::containing({eye.x + look.x * 2.0, eye.y + look.y * 2.0, eye.z + look.z * 2.0});
    auto& region = player.getDimensionBlockSource();
    constexpr int range = 4;
    gf::fx::sound(player.getDimension(), "item.gloamingforest.lamp.burn", player.getFeetPos(), 1.5F, 0.8F);
    for (int dx = -range; dx <= range; ++dx)
        for (int dy = -range; dy <= range; ++dy)
            for (int dz = -range; dz <= range; ++dz) burnBlock(region, BlockPos{c.x + dx, c.y + dy, c.z + dz});
    auto& r = world::random();
    for (int i = 0; i < 6; ++i) {
        int x = c.x + world::diffI(r, range);
        int y = c.y + r.nextInt(2);
        int z = c.z + world::diffI(r, range);
        region.getLevel().broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesMobBlockSpawn, Vec3{x + .5f, y + .5f, z + .5f}, 0, nullptr);
    }
    // burn mobs: new AABB(pos.below(2)).inflate(4)
    AABB box{Vec3{c.x - 4.0f, c.y - 2 - 4.0f, c.z - 4.0f}, Vec3{c.x + 5.0f, c.y - 2 + 5.0f, c.z + 5.0f}};
    for (auto* e : world::actorsIn(region, static_cast<int>(player.getDimensionId()), box))
        if (e && !e->isPlayer() && isLiving(*e)) e->setOnFire(5, true);
    log().info("weapons: {} used the lamp of cinders", player.getRealName());
}

void registerLamp() {
    gf::item::registerItem(
        kLamp,
        {
            .use     = [](Player&, ItemStack&) { return gf::item::Result::Pass; },
            .release =
                [](Player& player, ItemStack& stack, int used) {
                    int max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
                    if (used > kFiringTime && stack.getDamageValue() + 1 < max) lampBurn(player);
                },
        }
    );
    gf::item::ext::setRarity(kLamp, gf::item::ext::Rarity::Uncommon);
    gf::interactions::onUseOn({.item = kLamp}, [](gf::interactions::UseOnContext const& c) {
        if (!c.firstEvent) return gf::interactions::Result::Pass;
        auto* region = game::regionOf(c.pos.dim);
        auto  pos    = game::blockPos(c.pos);
        if (!region) return gf::interactions::Result::Pass;
        auto id = c.blockId;
        if (id != "gloamingforest:brown_thorns" && id != "gloamingforest:green_thorns") return gf::interactions::Result::Pass;
        auto uid = world::uidOf(c.player);
        gf::interactions::nextTick([uid, pos, dim = c.pos.dim] {
            auto* player = world::playerById(uid);
            auto* rg     = game::regionOf(dim);
            if (!player || !rg || !burnBlock(*rg, pos)) return;
            gf::progression::trigger(*player, "minecraft:placed_block", Json{{"block", "gloamingforest:burnt_thorns"}});
            gf::fx::sound(player->getDimension(), "item.gloamingforest.lamp.burn", player->getFeetPos(), 0.5F, 1.5F);
            auto& r = world::random();
            for (int i = 0; i < 10; ++i) {
                Vec3 at{pos.x + 0.5F + (world::diffF(r)) * 0.75F, pos.y + 0.5F + (world::diffF(r)) * 0.75F,
                        pos.z + 0.5F + (world::diffF(r)) * 0.75F};
                gf::fx::particle(player->getDimension(), "minecraft:smoke", at);
                gf::fx::particle(player->getDimension(), "minecraft:flame", at);
            }
        });
        return gf::interactions::Result::Consume;
    });
}

} // namespace gf::feature::weapons
