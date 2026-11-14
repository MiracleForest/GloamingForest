// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/MoonwormQueenItem.java
//   src/main/java/gloamingforest/entity/projectile/MoonwormShot.java
//   src/main/java/gloamingforest/item/recipe/MoonwormQueenRepairRecipe.java
//   src/main/java/gloamingforest/dispenser/GloamingDispenserBehaviors.java (moonworm queen)
//   src/main/java/gloamingforest/dispenser/DamageableStackDispenseBehavior.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:moonworm_queen places U6's gloamingforest:moonworm block (by id) like a block
// item and, held for more than 12 ticks, shoots gloamingforest:moonworm_shot (a GloamingThrowable,
// gravity 0.03) that sticks a moonworm where it lands. The repair recipe
// (gloamingforest:moonworm_queen_repair_recipe: one damaged queen and 1..8 torchberries) is a
// special crafting recipe on the BP layouts moonworm_queen_repair_recipe_1.._8.

#include "feature/weapons/Weapons.h"

#include "feature/ext/entity/EntityExt.h"

#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/item/Recipes.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

int gMoonwormsPlaced = 0; ///< selftests

namespace {

constexpr int  kFiringTime = 12;
constexpr char kFailedLoot[] = "gloamingforest:blocks/moonworm_failed_to_place";
constexpr char const* kFaces[] = {"down", "up", "north", "south", "west", "east"};
constexpr int kDx[] = {0, 0, 0, 0, -1, 1}, kDy[] = {-1, 1, 0, 0, 0, 0}, kDz[] = {0, 0, -1, 1, 0, 0};

/// MoonwormBlock#canSurvive: the block behind it (against `face`) holds it.
bool canSurvive(BlockSource& region, BlockPos const& pos, int face) {
    BlockPos behind{pos.x - kDx[face], pos.y - kDy[face], pos.z - kDz[face]};
    return world::legacySolid(region, behind);
}

bool placeMoonworm(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = game::blockWithStates(kMoonworm, Json{{"gloamingforest:facing", kFaces[face]}});
    if (!b) b = game::blockWithStates(kMoonworm, Json{{"facing_direction", face}});
    if (!b) b = game::blockWith(kMoonworm);
    if (!b || !game::setBlock(region, pos, *b)) return false;
    ++gMoonwormsPlaced;
    return true;
}

void failedDrops(Actor& shot) {
    gf::loot::LootContext ctx;
    ctx.region     = &shot.getDimensionBlockSource();
    ctx.origin     = shot.getPosition();
    ctx.thisEntity = &shot;
    ctx.damageType = "minecraft:fall";
    auto lv        = ll::service::getLevel();
    if (!lv) return;
    for (auto const& s : gf::loot::roll(kFailedLoot, ctx)) lv->getSpawner().spawnItem(*ctx.region, s, nullptr, shot.getPosition(), 0);
}

/// MoonwormShot#onHit: always gone, with 8 slime block particles.
void squishParticles(Actor& shot) {
    auto p = shot.getPosition();
    gf::fx::particle(shot.getDimension(), "minecraft:block", Vec3{p.x, p.y + 0.1f, p.z}, {}, 8, Json{{"block", "minecraft:slime"}});
}

bool onHitBlock(Actor& shot, BlockPos const& hit, int face) {
    auto&    region = shot.getDimensionBlockSource();
    BlockPos pos{hit.x + kDx[face], hit.y + kDy[face], hit.z + kDz[face]};
    auto     id = game::blockIdAt(region, pos);
    bool     ok = game::replaceable(region, pos) && id != "minecraft:fire" && id != "minecraft:soul_fire" && id != "minecraft:lava" &&
              id != "minecraft:flowing_lava" && canSurvive(region, pos, face);
    if (ok && placeMoonworm(region, pos, face)) {
        gf::fx::sound(shot.getDimension(), "item.gloamingforest.moonworm.squish", Vec3{hit.x + .5f, hit.y + .5f, hit.z + .5f}, 1.0F, 1.0F);
    } else {
        failedDrops(shot);
        gf::fx::sound(shot.getDimension(), "block.gloamingforest.bug.squish", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 1.0F, 1.0F);
    }
    squishParticles(shot);
    return true;
}

bool onHitEntity(Actor& shot, Actor& target) {
    if (target.isPlayer() && game::items::isEmpty(target.getArmor(SharedTypes::Legacy::ArmorSlot::Head))) {
        if (auto worm = game::items::make(kMoonworm)) {
            static_cast<Player&>(target).setArmor(SharedTypes::Legacy::ArmorSlot::Head, *worm);
            static_cast<Player&>(target).sendInventory(false);
        }
    } else {
        float dmg = world::random().nextInt(3) == 0 ? 1.0F : 0.0F;
        gf::combat::hurt(target, dmg, "gloamingforest:moonworm", gf::projectile::ext::owner(shot), &shot);
        failedDrops(shot);
        gf::fx::sound(shot.getDimension(), "block.gloamingforest.bug.squish", target.getFeetPos(), 1.0F, 1.0F);
    }
    squishParticles(shot);
    return true;
}

/// The direction of a dispenser's FacingID (Direction#getStep*), with Java's +0.1 y.
Vec3 dispenseDir(int facing) { return Vec3{static_cast<float>(kDx[facing]), kDy[facing] + 0.1f, static_cast<float>(kDz[facing])}; }

} // namespace

bool moonwormRepairMatches(std::vector<ItemStack const*> const& slots) {
    ItemStack const* queen   = nullptr;
    int              berries = 0;
    for (auto const* s : slots) {
        if (!s || game::items::isEmpty(*s)) continue;
        auto id = game::items::idOf(*s);
        if (id == kMoonwormQueen && s->isDamaged()) {
            if (queen) return false;
            queen = s;
        } else if (id == kTorchberries) ++berries;
        else return false;
    }
    return queen && berries > 0;
}

ItemStack moonwormRepairAssemble(std::vector<ItemStack const*> const& slots) {
    ItemStack const* queen   = nullptr;
    int              berries = 0;
    for (auto const* s : slots) {
        if (!s || game::items::isEmpty(*s)) continue;
        auto id = game::items::idOf(*s);
        if (id == kMoonwormQueen) {
            if (queen) return ItemStack::EMPTY_ITEM();
            queen = s;
        }
        if (id == kTorchberries) ++berries;
    }
    if (!berries || !queen || !queen->isDamaged()) return ItemStack::EMPTY_ITEM();
    auto fresh = game::items::make(kMoonwormQueen);
    if (!fresh) return ItemStack::EMPTY_ITEM();
    fresh->setDamageValue(static_cast<short>(std::max(0, cw::moonwormRepairDamage(queen->getDamageValue(), berries))));
    return *fresh;
}

void registerMoonworm() {
    gf::projectile::registerType(kMoonwormShot, {.gravity = 0.03F, .drag = 0.99F, .onHitEntity = onHitEntity, .onHitBlock = onHitBlock, .tick = applyLaunch});
    gf::item::registerItem(
        kMoonwormQueen,
        {
            .use =
                [](Player&, ItemStack& stack) {
                    int max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
                    return stack.getDamageValue() == max ? gf::item::Result::Fail : gf::item::Result::Pass;
                },
            .release =
                [](Player& player, ItemStack& stack, int used) {
                    int max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
                    if (used <= kFiringTime || stack.getDamageValue() + 1 >= max) return;
                    if (!gf::projectile::shoot(player, kMoonwormShot, lookOf(player), 1.5F, 1.0F)) return;
                    hurtButDontBreak(stack, 2, &player);
                    gf::fx::sound(player.getDimension(), "item.gloamingforest.moonworm.squish", player.getFeetPos(), 1.0F, 1.0F);
                },
        }
    );
    gf::item::ext::setRarity(kMoonwormQueen, gf::item::ext::Rarity::Rare);
    // MoonwormQueenItem#useOn: places a moonworm like a block item, 1 durability (never breaking)
    gf::interactions::onUseOn({.item = kMoonwormQueen}, [](gf::interactions::UseOnContext const& c) {
        if (!c.firstEvent) return gf::interactions::Result::Consume;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return gf::interactions::Result::Pass;
        auto& stack = c.item;
        int   max   = stack.mItem ? stack.mItem->getMaxDamage() : 0;
        if (stack.getDamageValue() >= max) return gf::interactions::Result::Deny;
        BlockPos pos = game::blockPos(c.pos);
        if (!game::replaceable(*region, pos)) pos = BlockPos{pos.x + kDx[c.face], pos.y + kDy[c.face], pos.z + kDz[c.face]};
        if (!game::replaceable(*region, pos) || !canSurvive(*region, pos, c.face)) return gf::interactions::Result::Deny;
        auto uid  = world::uidOf(c.player);
        int  face = c.face;
        bool off  = c.offhand;
        gf::interactions::nextTick([uid, pos, face, off, dim = c.pos.dim] {
            auto* player = world::playerById(uid);
            auto* rg     = game::regionOf(dim);
            if (!player || !rg || !game::replaceable(*rg, pos) || !placeMoonworm(*rg, pos, face)) return;
            gf::fx::sound(player->getDimension(), "minecraft:block.stone.place", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 1.0F, 0.8F);
            bool main = !off;
            auto s    = handStack(*player, main);
            if (game::items::idOf(s) != kMoonwormQueen) return;
            hurtButDontBreak(s, 1, player);
            setHandStack(*player, main, s);
            player->stopUsingItem();
        });
        return gf::interactions::Result::Consume;
    });
    // GloamingDispenserBehaviors: a DamageableStackDispenseBehavior shooting moonworm shots
    gf::item::registerDispense(kMoonwormQueen, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        if (facing < 0 || facing > 5) return false;
        int max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
        if (max < stack.getDamageValue() + 2) return false;
        Vec3 pos{at.x + 0.5f + 0.7f * kDx[facing], at.y + 0.5f + 0.7f * kDy[facing] - (kDy[facing] == 0 ? 0.125f : 0.0f), at.z + 0.5f + 0.7f * kDz[facing]};
        if (!world::shootFrom(region, pos, kMoonwormShot, dispenseDir(facing), 1.1F, 18.0F)) return false;
        hurtButDontBreak(stack, 1, nullptr);
        gf::fx::sound(region.getDimension(), "item.gloamingforest.moonworm.squish", Vec3{at.x + .5f, at.y + .5f, at.z + .5f}, 1.0F, 1.0F);
        return true;
    });
    // MoonwormQueenRepairRecipe (BP layouts: 1..8 torchberries)
    gf::recipes::registerSpecialCrafting(
        "gloamingforest:moonworm_queen_repair_recipe", [](gf::recipes::CraftingGrid const& g) { return moonwormRepairMatches(g.slots); },
        [](gf::recipes::CraftingGrid const& g) { return moonwormRepairAssemble(g.slots); }
    );
    for (int n = 1; n <= 8; ++n) gf::recipes::ext::addSpecialLayout("gloamingforest:moonworm_queen_repair_recipe", "gloamingforest:moonworm_queen_repair_recipe_" + std::to_string(n));
}

} // namespace gf::feature::weapons
