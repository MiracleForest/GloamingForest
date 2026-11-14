// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/CrumbleHornItem.java
//   src/main/java/gloamingforest/dispenser/CrumbleDispenseBehavior.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:crumble_horn: held (use duration 72000), it crumbles the blocks around the point
// 3 blocks ahead every 5 use ticks, by the gloamingforest:crumble_horn data map (rule R7: U8's;
// the data's ids are the game's, gf::datamaps::ext converts crumble_to).

#include "feature/weapons/Weapons.h"

#include <cctype>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/datamaps/DataMaps.h"
#include "feature/ext/item/Item.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/stats/Stats.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

namespace {

constexpr char kMap[] = "gloamingforest:crumble_horn";

struct Crumble {
    std::string result;
    float       chance = 0;
};

std::optional<Crumble> crumbleOf(std::string const& blockId) {
    auto const* v = gf::datamaps::get(kMap, blockId);
    if (!v || !v->is_object()) return std::nullopt;
    Crumble c;
    c.result = v->value("crumble_to", std::string("minecraft:air"));
    c.chance = v->value("crumble_chance", 0.0F);
    return c;
}

/// Block#withPropertiesOf: the result with the old block's state values that it has.
Block const* withPropertiesOf(std::string const& result, Block const& old) {
    Json states = Json::object();
    for (auto const& [k, v] : game::statesOf(old)) {
        if (v == "true" || v == "false") states[k] = v == "true";
        else if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-')) states[k] = std::stoi(v);
        else states[k] = v;
    }
    if (auto const* b = game::blockWithStates(result, states)) return b;
    return game::blockWith(result);
}

void replaceKeeping(BlockSource& region, BlockPos const& pos, Block const& old, std::string const& result) {
    if (auto const* b = withPropertiesOf(result, old)) {
        game::setBlock(region, pos, *b);
        destroyParticles(region, pos, old);
    }
}

/// CrumbleHornItem#crumbleBlock for a player.
bool crumbleBlock(Player& player, BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return false;
    auto c = crumbleOf(game::blockId(block));
    if (!c) return false;
    if (!mayBreak(player, pos)) return false;
    if (c->result == "minecraft:air") {
        if (world::random().nextFloat() < c->chance) {
            // Block#canHarvestBlock: the horn is no tool, so only blocks that need none
            if (!block.getBlockType().mRequiresCorrectToolForDrops) {
                region.getLevel().destroyBlock(region, pos, !player.isCreative(), BlockChangeContext{});
                return true;
            }
        }
    } else if (world::random().nextFloat() < c->chance) {
        replaceKeeping(region, pos, block, c->result);
        return true;
    }
    return false;
}

int crumble(Player& player, ItemStack& stack) {
    auto eye    = eyeOf(player);
    auto look   = lookOf(player);
    auto center = cw::containing({eye.x + look.x * 3.0, eye.y + look.y * 3.0, eye.z + look.z * 3.0});
    auto [lo, hi] = cw::crumbleBox(center);
    auto& region  = player.getDimensionBlockSource();
    int   n       = 0;
    int   max     = stack.mItem ? stack.mItem->getMaxDamage() : 0;
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x) {
                if (!crumbleBlock(player, region, BlockPos{x, y, z})) continue;
                ++n;
                gf::stats::award(player, "gloamingforest:blocks_crumbled");
                hurtAndBreak(stack, 1, &player);
                if (game::items::isEmpty(stack) || stack.getDamageValue() >= max) return n;
            }
    return n;
}

} // namespace

int crumbleAround(Actor& living, ItemStack& stack, std::int64_t seed) {
    if (!living.isPlayer()) return 0;
    world::seedRandom(seed);
    return crumble(static_cast<Player&>(living), stack);
}

void registerHorn() {
    gf::item::registerItem(
        kCrumbleHorn,
        {
            .use =
                [](Player& player, ItemStack&) {
                    gf::fx::sound(player.getDimension(), "entity.gloamingforest.quest_ram.ambient", player.getFeetPos(), 1.0F, 0.8F);
                    return gf::item::Result::Pass; // starts using (toot)
                },
            .useTick =
                [](Player& player, ItemStack& stack, int remaining) {
                    if (remaining > 10 && remaining % 5 == 0) {
                        crumble(player, stack);
                        gf::fx::sound(player.getDimension(), "entity.gloamingforest.quest_ram.ambient", player.getFeetPos(), 1.0F, 0.8F);
                    }
                },
        }
    );
    gf::item::ext::setRarity(kCrumbleHorn, gf::item::ext::Rarity::Rare);
    // CrumbleDispenseBehavior
    gf::item::registerDispense(kCrumbleHorn, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        static constexpr int kDx[] = {0, 0, 0, 0, -1, 1}, kDy[] = {-1, 1, 0, 0, 0, 0}, kDz[] = {0, 0, -1, 1, 0, 0};
        if (facing < 0 || facing > 5) return false;
        BlockPos pos{at.x + kDx[facing], at.y + kDy[facing], at.z + kDz[facing]};
        int      max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
        if (max == stack.getDamageValue() + 1) return false;
        Block const& block = region.getBlock(pos);
        auto         c     = crumbleOf(game::blockId(block));
        if (!c) return false;
        if (c->result == "minecraft:air") region.getLevel().destroyBlock(region, pos, true, BlockChangeContext{});
        else replaceKeeping(region, pos, block, c->result);
        stack.hurtAndBreak(1, nullptr);
        return true;
    });
}

} // namespace gf::feature::weapons
