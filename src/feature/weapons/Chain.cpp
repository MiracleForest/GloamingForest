// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/ChainBlockItem.java
//   src/main/java/twilightforest/entity/projectile/ChainBlock.java
//   src/main/java/twilightforest/enchantment/SmashBlocksEffect.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:block_and_chain and its projectile twilightforest:chain_block, a
// ThrowableProjectile simulated by F8's projectile kit (gravity 0.05). The chain's own state
// (the thrower's stack, returning, the saved bounce velocity, its tick count, blocks smashed)
// lives in data(chain, "weapons:chain"); the owner and hand are synced for the client mod's chain
// renderer. The stack the chain was thrown from is the one in the thrower's hand whose
// twilightforest:thrown_projectile data names the chain (Java keeps a reference to it).
//
// Destruction (U10's enchantment) is read by id; its HIT_BLOCK effect, SmashBlocksEffect(max 12,
// radius 1, immune #block_and_chain_never_breaks), runs here on the chain's block hits as Java's
// EnchantmentHelper#onHitBlock does. Java reads the blocks smashed for the return durability cost
// from the *owner's* SMASH_BLOCKS attachment, which only the chain ever writes: that cost never
// applies in TF 4.8, and not here either.

#include "feature/weapons/Weapons.h"

#include <algorithm>
#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "feature/combat/Combat.h"
#include "feature/enchant/Enchant.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

namespace {

constexpr int    kMaxStuckTicks = 100;
constexpr double kMaxChain      = 16.0;
constexpr float  kGravity       = 0.05F;
constexpr char   kKey[]         = "weapons:chain";

Json& stateOf(Actor& chain) {
    auto& d = gf::entity::data(chain, kKey);
    if (!d.is_object()) d = Json{{"returning", false}, {"hitEntity", false}, {"stuck", 0}, {"age", 0}, {"smashed", 0}, {"vel", {0, 0, 0}}, {"main", true}};
    return d;
}

cw::V3 savedVel(Json const& st) {
    auto const& v = st["vel"];
    return {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()};
}

/// The stack the chain was thrown from: the thrower's hand stack whose thrown_projectile is the chain.
std::optional<bool> stackHand(Player& owner, Actor& chain) {
    auto uid = world::uidOf(chain);
    for (bool main : {true, false}) {
        auto s = handStack(owner, main);
        if (game::items::idOf(s) == kBlockAndChain && gf::item::getData(s, kThrownKey) == Json(uid)) return main;
    }
    return std::nullopt;
}

void hurtThrownStack(Actor& chain, int amount) {
    auto* owner = gf::projectile::ext::owner(chain);
    if (!owner || !owner->isPlayer()) return;
    auto& player = static_cast<Player&>(*owner);
    auto  hand   = stackHand(player, chain);
    if (!hand) return;
    auto s = handStack(player, *hand);
    hurtAndBreak(s, amount, &player);
    setHandStack(player, *hand, s);
}

ItemStack thrownStack(Actor& chain) {
    auto& st = stateOf(chain);
    if (st.contains("stack") && st["stack"].is_object()) return game::items::fromJson(st["stack"]);
    return ItemStack::EMPTY_ITEM();
}

/// Entity#remove: the thrower stops using the block and chain.
void removeChain(Actor& chain) {
    auto* owner = gf::projectile::ext::owner(chain);
    if (owner && owner->isPlayer() && usedItemId(static_cast<Player&>(*owner)) == kBlockAndChain) static_cast<Player&>(*owner).stopUsingItem();
    world::removeLater(world::uidOf(chain));
    stateOf(chain)["removed"] = true;
}

/// Tiers#WOOD / STONE / IRON#createToolProperties(#mineable_with_block_and_chain)#isCorrectForDrops.
bool correctForDrops(int destruction, std::string const& blockId) {
    using gf::tags::Registry;
    if (!gf::tags::has(Registry::Block, "twilightforest:mineable_with_block_and_chain", blockId)) return false;
    auto needs = [&](char const* tag) { return gf::tags::has(Registry::Block, tag, blockId); };
    if (needs("minecraft:needs_diamond_tool") || needs("minecraft:incorrect_for_iron_tool")) return false;
    if (destruction <= 2 && (needs("minecraft:needs_iron_tool") || needs("minecraft:incorrect_for_stone_tool"))) return false;
    if (destruction <= 1 && (needs("minecraft:needs_stone_tool") || needs("minecraft:incorrect_for_wooden_tool"))) return false;
    return true;
}

/// ChainBlock#canBreakBlockAt (the stack is thrown: ChainBlockItem#isCorrectToolForDrops needs
/// Destruction).
bool canBreakBlockAt(ItemStack const& stack, Block const& block) {
    auto id          = game::blockId(block);
    int  destruction = gf::enchant::level(stack, "twilightforest:destruction");
    if (destruction <= 0 || !correctForDrops(destruction, id)) return false;
    return !gf::tags::has(gf::tags::Registry::Block, "twilightforest:block_and_chain_never_breaks", id);
}

/// SmashBlocksEffect#apply (Destruction: max 12, radius 1).
void smash(Actor& chain, ItemStack const& stack, Player& player, Vec3 const& at) {
    auto& st      = stateOf(chain);
    int   smashed = st.value("smashed", 0);
    if (smashed >= 12) return;
    auto& region = chain.getDimensionBlockSource();
    auto  start  = game::blockPosOf(at);
    for (int x = -1; x <= 1 && smashed < 12; ++x)
        for (int z = -1; z <= 1 && smashed < 12; ++z) {
            BlockPos     p{start.x + x, start.y, start.z + z};
            Block const& b = region.getBlock(p);
            if (b.isAir()) continue;
            if (!canBreakBlockAt(stack, b) || !mayBreak(player, p)) continue;
            gf::game::compat::destroyBlock(region, p, !player.isCreative(), BlockChangeContext{});
            ++smashed;
        }
    st["smashed"] = smashed;
}

/// ChainBlock#bounce.
void bounce(Actor& chain, int face) {
    auto& st = stateOf(chain);
    if (!st.value("returning", false) && !st.value("hitEntity", false))
        gf::fx::sound(chain.getDimension(), "item.twilightforest.block_and_chain.collide", chain.getPosition(), 0.125F, world::random().nextFloat());
    st["returning"] = true;
    auto v          = cw::chainBounce(savedVel(st), face);
    st["vel"]       = {v.x, v.y, v.z};
}

bool onHitEntity(Actor& chain, Actor& target) {
    auto* owner = gf::projectile::ext::owner(chain);
    if (&target == owner) return false;
    auto& st     = stateOf(chain);
    auto  stack  = thrownStack(chain);
    float damage = 10.0F;
    if (!game::items::isEmpty(stack)) {
        // EnchantmentHelper#modifyDamage: Sharpness (and Destruction's -1.5 per level)
        int sharp = EnchantUtils::getEnchantLevel(Enchant::Type::Sharpness, stack);
        if (sharp > 0) damage += 1.0F + 0.5F * static_cast<float>(sharp - 1);
        damage += -1.5F * static_cast<float>(gf::enchant::level(stack, "twilightforest:destruction"));
    }
    // properly disable shields
    if (target.isPlayer()) {
        auto& p = static_cast<Player&>(target);
        if (isBlocking(p)) {
            auto used = usedItemId(p);
            bool main = used.empty() ? game::items::idOf(p.getCarriedItem()) == kShield || game::items::idOf(p.getCarriedItem()) == "minecraft:shield"
                                     : game::items::idOf(p.getCarriedItem()) == used;
            auto s    = handStack(p, main);
            hurtAndBreak(s, 5, &p);
            setHandStack(p, main, s);
            disableShield(p);
        }
    }
    if (damage > 0.0F && gf::combat::hurt(target, damage, "twilightforest:spiked", owner, &chain)) {
        gf::fx::sound(chain.getDimension(), "item.twilightforest.block_and_chain.hit", chain.getPosition(), 1.0F, world::random().nextFloat());
        st["hitEntity"] = true;
        st["returning"] = true;
        st["age"]       = st.value("age", 0) + 60;
        hurtThrownStack(chain, 1);
        log().info("weapons: block and chain hit {}", target.getTypeName());
    }
    return false;
}

bool onHitBlock(Actor& chain, BlockPos const& pos, int face) {
    auto&        region = chain.getDimensionBlockSource();
    Block const& block  = region.getBlock(pos);
    auto         stack  = thrownStack(chain);
    if (block.isAir() || game::items::isEmpty(stack)) return false;
    auto& st = stateOf(chain);
    if (!canBreakBlockAt(stack, block) || st.value("smashed", 0) >= 12) bounce(chain, face);
    // EnchantmentHelper#onHitBlock: Destruction's SmashBlocksEffect around the clamped hit point
    auto* owner = gf::projectile::ext::owner(chain);
    if (owner && owner->isPlayer() && gf::enchant::level(stack, "twilightforest:destruction") > 0)
        smash(chain, stack, static_cast<Player&>(*owner), Vec3{pos.x + 0.0f, pos.y + 0.0f, pos.z + 0.5f});
    // the chain flies on (ThrowableProjectile keeps moving after onHitBlock): a hit that sets the
    // velocity flies on with it, one that does not rests at the hit point
    gf::projectile::ext::setVelocity(chain, gf::projectile::ext::velocity(chain));
    return false;
}

/// ChainBlock#tick (server).
void tick(Actor& chain) {
    auto& st = stateOf(chain);
    if (st.value("removed", false)) return;
    auto* owner = gf::projectile::ext::owner(chain);
    if (!owner) {
        removeChain(chain);
        return;
    }
    int age   = st.value("age", 0) + 1;
    st["age"] = age;
    auto pos  = chain.getPosition();
    if (!st.value("returning", false)) {
        if (st.contains("last")) {
            double dx = std::abs(pos.x - st["last"][0].get<double>());
            double dz = std::abs(pos.z - st["last"][1].get<double>());
            if ((dx > 0 || dz > 0) && dx < 0.003F && dz < 0.003F) st["stuck"] = st.value("stuck", 0) + 1;
        }
        if (st.value("stuck", 0) >= kMaxStuckTicks) st["returning"] = true;
    }
    st["last"]     = {pos.x, pos.z};
    auto   ownPos  = owner->getFeetPos();
    double dist    = std::sqrt(world::distanceSqr(chain, ownPos));
    if (!st.value("returning", false) && dist > kMaxChain) st["returning"] = true;
    if (st.value("returning", false)) {
        if (dist < 2.0) {
            removeChain(chain);
            return;
        }
        auto eye  = eyeOf(*owner);
        auto back = cw::V3{eye.x - pos.x, eye.y - pos.y, eye.z - pos.z};
        auto v    = cw::chainReturn(savedVel(st), back, age, kGravity);
        gf::projectile::ext::setVelocity(chain, vec(v));
    }
}

} // namespace

std::int64_t throwChain(Player& player, ItemStack& stack) {
    auto thrown = gf::item::getData(stack, kThrownKey);
    if (thrown.is_number_integer() && world::actorById(thrown.get<std::int64_t>())) return 0;
    {
        gf::fx::sound(player.getDimension(), "item.twilightforest.block_and_chain.fire", player.getFeetPos(), 0.5F, 1.0F / (world::random().nextFloat() * 0.4F + 1.2F));
        auto   look  = lookOf(player);
        bool   main  = game::items::idOf(player.getCarriedItem()) == kBlockAndChain;
        Actor* chain = gf::projectile::shoot(player, kChainBlock, look, 1.5F, 1.0F);
        if (!chain) return 0;
        auto& st     = stateOf(*chain);
        auto  v      = gf::projectile::ext::velocity(*chain);
        st["vel"]    = {v.x, v.y, v.z};
        st["main"]   = main;
        st["owner"]  = world::uidOf(player);
        st["foil"]   = stack.isEnchanted();
        st["stack"]  = game::items::toJson(stack);
        gf::entity::sync(*chain, kKey);
        gf::entity::setProperty(*chain, "twilightforest:foil", stack.isEnchanted());
        gf::item::setData(stack, kThrownKey, world::uidOf(*chain));
        log().info("weapons: {} threw a block and chain", player.getRealName());
        return world::uidOf(*chain);
    }
}

void registerChain() {
    gf::projectile::registerType(kChainBlock, {.gravity = kGravity, .drag = 0.99F, .onHitEntity = onHitEntity, .onHitBlock = onHitBlock, .tick = tick});
    gf::entity::ext::setCanUsePortal(kChainBlock, false);
    gf::item::registerItem(
        kBlockAndChain,
        {
            // the player keeps using the item (Bedrock's use starts with the food component)
            .use = [](Player& player, ItemStack& stack) { return throwChain(player, stack) ? gf::item::Result::Pass : gf::item::Result::Fail; },
            .inventoryTick =
                [](ItemStack& stack, Player&, int, bool) {
                    auto thrown = gf::item::getData(stack, kThrownKey);
                    if (thrown.is_number_integer() && !world::actorById(thrown.get<std::int64_t>())) gf::item::setData(stack, kThrownKey, nullptr);
                },
            .repairWith = [](ItemStack const&, ItemStack const& m) { return gf::tags::has(gf::tags::Registry::Item, "c:ingots/knightmetal", game::items::idOf(m)); },
        }
    );
}

} // namespace gf::feature::weapons
