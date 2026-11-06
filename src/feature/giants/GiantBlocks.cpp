// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/GiantBlock.java
//   src/main/java/twilightforest/block/GiantLeavesBlock.java
//   src/main/java/twilightforest/item/GiantPickItem.java
//   src/main/java/twilightforest/events/ToolEvents.java (handleGiantPickaxeMining)
//   src/main/java/twilightforest/components/entity/GiantPickaxeMiningAttachment.java
//   src/main/java/twilightforest/loot/conditions/GiantPickUsedCondition.java
//   src/main/java/twilightforest/loot/modifiers/GiantToolGroupingModifier.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Giant blocks fill an aligned 4 x 4 x 4 cell. Their texture tiling (GiantBlockModel) is the block
// state twilightforest:giant_cell (1..64 = the position in the cell, 0 = untiled: the item and a
// block nobody fixed yet), whose permutations pick a geometry with that face's 4 x 4 sub-square
// (assets/pack/rp/models/blocks/giants/giant_cell.geo.json); every client, vanilla included, draws
// the tiling. Blocks placed by world generation get their state when they are placed (onPlaced) or
// at their next random tick.
#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "core/giants/Giants.h"
#include "feature/common/Interactions.h"
#include "feature/ext/giants/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/giants/Giants.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::giants {

namespace cg = core::giants;
namespace ia = gf::interactions;
namespace fe = gf::feature::entity;

namespace {

constexpr char const* kGiantBlocks[] = {id::GiantCobblestone, id::GiantLog, id::GiantLeaves, id::GiantObsidian};

/// GiantBlock#isSelfDestructing (one flag for the four blocks: the cascade never nests).
bool& selfDestructing() {
    static bool b = false;
    return b;
}

template <class Fn>
void forCell(BlockPos const& pos, Fn&& fn) {
    auto o = cg::cellOrigin(pos.x, pos.y, pos.z);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z) fn(BlockPos{o[0] + x, o[1] + y, o[2] + z});
}

BlockPos offsetByFace(BlockPos const& p, int face) {
    switch (face) {
    case 0: return {p.x, p.y - 1, p.z};
    case 1: return {p.x, p.y + 1, p.z};
    case 2: return {p.x, p.y, p.z - 1};
    case 3: return {p.x, p.y, p.z + 1};
    case 4: return {p.x - 1, p.y, p.z};
    case 5: return {p.x + 1, p.y, p.z};
    default: return p;
    }
}

/// GiantBlock#setGiantBlockToAir: the cell's other blocks of the type go, without drops.
void destroyCell(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    selfDestructing() = true;
    forCell(pos, [&](BlockPos const& p) {
        if (p == pos || !region.hasChunksAt(p, 0, false)) return;
        if (game::blockId(region.getBlock(p)) == blockId) region.getLevel().destroyBlock(region, p, false, BlockChangeContext{});
    });
    selfDestructing() = false;
}

// ---- the giant pickaxe ------------------------------------------------------------------------------

std::unordered_map<std::int64_t, cg::GiantPickMining>& minings() {
    static std::unordered_map<std::int64_t, cg::GiantPickMining> m;
    return m;
}
struct PendingBreak {
    core::GlobalPos pos;
    std::string     blockId;
    int             face = 1;
};
std::unordered_map<std::int64_t, PendingBreak>& pending() {
    static std::unordered_map<std::int64_t, PendingBreak> m;
    return m;
}

long long gameTime() { return static_cast<long long>(fe::currentTick()); }

/// EventHooks.doPlayerHarvestCheck: the block needs no tool, or the held item is a correct one.
bool canHarvest(ItemStack const& tool, Block const& block) {
    if (!block.getBlockType().mRequiresCorrectToolForDrops) return true;
    auto const* item = tool.mItem.get();
    return item && item->canDestroySpecial(block);
}

/// ToolEvents#handleGiantPickaxeMining, before the break: the cell's conversion is decided and the
/// rest of the cell is broken after the block itself (onDestroyed).
ia::Result beforeBreak(ia::DestroyContext const& c) {
    auto  uid = fe::uidOf(c.player);
    auto& m   = minings()[uid];
    auto  now = gameTime();
    if (m.breaking && m.mining != now) m.breaking = false; // a loop that never finished (the pick broke)
    // GiantPickItem#canAttackBlock: a new mining tick
    if (m.mining != now) {
        m.mining     = now;
        m.breaking   = false;
        m.conversion = 0;
    }
    if (m.breaking) return ia::Result::Pass; // a block of the loop itself
    if (!canHarvest(c.item, c.block)) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    m.breaking  = true;
    // all 64 blocks of the cell are the broken block's type: every convertible drop goes into one
    // giant block (GiantToolGroupingModifier checks the drop itself)
    bool allTheSame = true;
    BlockPos at     = game::blockPos(c.pos);
    forCell(at, [&](BlockPos const& p) {
        if (allTheSame && game::blockId(region->getBlock(p)) != c.blockId) allTheSame = false;
    });
    m.conversion = allTheSame ? 64 : 0;
    pending()[uid] = PendingBreak{c.pos, c.blockId, c.face};
    return ia::Result::Pass;
}

void afterBreak(ia::DestroyContext const& c, bool destroyed) {
    auto uid = fe::uidOf(c.player);
    auto it  = pending().find(uid);
    if (it == pending().end() || !(it->second.pos == c.pos)) return;
    auto pend = it->second;
    pending().erase(it);
    auto& m = minings()[uid];
    if (destroyed) {
        auto* region = game::regionOf(pend.pos.dim);
        auto* gm     = c.player.mGameMode.get();
        if (region && gm) {
            BlockPos origin = game::blockPos(pend.pos);
            int      count  = 1;
            forCell(origin, [&](BlockPos const& p) {
                if (p == origin || game::blockId(region->getBlock(p)) != pend.blockId) return;
                if (gm->destroyBlock(p, static_cast<uchar>(pend.face))) ++count;
            });
            gf::log().info("giants: giant pickaxe broke {} {} block(s) at {} {} {} (conversion {})", count, pend.blockId, origin.x, origin.y, origin.z,
                           m.conversion > 0 ? "on" : "off");
        }
    }
    m.breaking = false;
}

/// GiantPickUsedCondition#test.
bool giantPickUsed(Actor const* entity) {
    if (!entity || !entity->isPlayer()) return false;
    auto it = minings().find(fe::uidOf(*entity));
    if (it == minings().end()) return false;
    return it->second.mining == gameTime() && it->second.conversion > 0;
}

} // namespace

bool isGiantBlock(std::string const& blockId) {
    return std::find_if(std::begin(kGiantBlocks), std::end(kGiantBlocks), [&](char const* g) { return blockId == g; }) != std::end(kGiantBlocks);
}

Block const* giantBlockAt(std::string const& blockId, int x, int y, int z) {
    return game::blockWithStates(blockId, {{id::GiantCell, cg::cellState(x, y, z)}});
}

void fillGiantCell(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    forCell(pos, [&](BlockPos const& p) {
        if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) return;
        if (auto const* b = giantBlockAt(blockId, p.x, p.y, p.z)) game::setBlock(region, p, *b);
    });
}

void fixGiantCell(BlockSource& region, BlockPos const& pos) {
    forCell(pos, [&](BlockPos const& p) {
        if (!region.hasChunksAt(p, 0, false)) return;
        auto const& b   = region.getBlock(p);
        auto        bid = game::blockId(b);
        if (!isGiantBlock(bid)) return;
        int want = cg::cellState(p.x, p.y, p.z);
        if (game::intState(b, id::GiantCell).value_or(0) == want) return;
        if (auto const* fixed = giantBlockAt(bid, p.x, p.y, p.z)) game::setBlock(region, p, *fixed);
    });
}

bool giantCellFilled(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    bool filled = true;
    forCell(pos, [&](BlockPos const& p) {
        if (filled && game::blockId(region.getBlock(p)) != blockId) filled = false;
    });
    return filled;
}

cg::GiantPickMining& pickMining(std::int64_t playerUid) { return minings()[playerUid]; }

float giantPickDestroySpeed(std::string const& blockId, float base) {
    float speed = base;
    // extra 64X strength vs giant obsidian
    if (blockId == id::GiantObsidian) speed *= 64;
    // 64x strength vs giant blocks
    return isGiantBlock(blockId) ? speed * 64 : speed;
}

void installGiantBlocks() {
    for (char const* g : kGiantBlocks) {
        std::string gid = g;
        // GiantBlock#getStateForPlacement: every block of the cell must be replaceable
        ia::onUseOn({.item = gid}, [](ia::UseOnContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos clicked = game::blockPos(c.pos);
            BlockPos at      = game::replaceable(*region, clicked) ? clicked : offsetByFace(clicked, c.face);
            bool     free    = true;
            forCell(at, [&](BlockPos const& p) {
                if (!free) return;
                if (p.y < region->getMinHeight() || p.y >= region->getMaxHeight() || !region->hasChunksAt(p, 0, false) || !game::replaceable(*region, p)) free = false;
            });
            return free ? ia::Result::Pass : ia::Result::Deny;
        }, ia::priority::Low);
        // GiantBlock#setPlacedBy: the server fills the cell (next tick, R16)
        ia::onPlace({.block = gid}, [gid](ia::PlaceContext const& c) {
            auto pos = c.pos;
            ia::nextTick([pos, gid] {
                auto* region = game::regionOf(pos.dim);
                if (!region) return;
                BlockPos at = game::blockPos(pos);
                if (game::blockId(region->getBlock(at)) != gid) return;
                fillGiantCell(*region, at, gid);
                gf::log().info("giants: {} placed, cell at {} {} {} filled", gid, at.x & ~3, at.y & ~3, at.z & ~3);
            });
        });
        // GiantBlock#onRemove: an incomplete cell goes completely, without drops
        gf::blockevents::onRemoved(gid, [gid](BlockSource& region, BlockPos const& pos, Block const&) {
            if (selfDestructing() || giantCellFilled(region, pos, gid)) return;
            destroyCell(region, pos, gid);
        });
        // the tiling state of blocks placed without it (world generation, commands)
        gf::blockevents::onPlaced(gid, [](BlockSource& region, BlockPos const& pos, Block const& block, Actor*) {
            int want = cg::cellState(pos.x, pos.y, pos.z);
            if (game::intState(block, id::GiantCell).value_or(0) == want) return;
            if (auto const* fixed = giantBlockAt(game::blockId(block), pos.x, pos.y, pos.z)) game::setBlock(region, pos, *fixed);
        });
        gf::blockevents::onRandomTick(gid, [](BlockSource& region, BlockPos const& pos, Block const& block, gf::blockevents::RandomSource&) {
            if (game::intState(block, id::GiantCell).value_or(0) != cg::cellState(pos.x, pos.y, pos.z)) fixGiantCell(region, pos);
        });
    }

    // GiantPickItem
    gf::item::registerItem(id::GiantPickaxe, {
        .destroySpeed = [](ItemStack const&, Block const& block) -> std::optional<float> {
            auto bid = game::blockId(block);
            if (!isGiantBlock(bid)) return std::nullopt;
            return giantPickDestroySpeed(bid, 4.0f); // TFToolMaterials.GIANT speed 4 on mineable/pickaxe
        },
        .repairWith = [](ItemStack const&, ItemStack const& material) { return game::items::idOf(material) == id::GiantCobblestone; },
    });
    gf::item::registerItem(id::GiantSword, {
        .repairWith = [](ItemStack const&, ItemStack const& material) {
            auto m = game::items::idOf(material);
            return m == "twilightforest:ironwood_ingot" || m == id::GiantCobblestone;
        },
    });
    gf::item::addTooltip(id::GiantPickaxe, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
        auto text = viewer ? gf::trFor(*viewer, "item.twilightforest.giant_pickaxe.desc", "Breaks giant blocks")
                           : gf::tr("item.twilightforest.giant_pickaxe.desc", "Breaks giant blocks");
        lines.push_back("§7" + text);
    });

    ia::onDestroy({.item = id::GiantPickaxe}, beforeBreak);
    ia::onDestroyed({.item = id::GiantPickaxe}, afterBreak);
    gf::loot::registerCondition("twilightforest:giant_pick_used", [](gf::loot::LootContext const& ctx, gf::loot::Json const&, gf::loot::RandomSource&) {
        return giantPickUsed(ctx.thisEntity ? ctx.thisEntity : ctx.player);
    });
    // GiantToolGroupingModifier (data: twilightforest:giant_pick_grouping, condition giant_pick_used)
    gf::loot::addGlobalModifier("twilightforest:giant_block_grouping", [](std::vector<ItemStack>& loot, gf::loot::LootContext const& ctx, std::string const&) {
        Actor const* who = ctx.thisEntity ? ctx.thisEntity : ctx.player;
        if (!giantPickUsed(who) || loot.empty()) return;
        auto giant = cg::giantConversionOf(game::items::idOf(loot.front()));
        if (giant.empty()) return;
        auto& m = minings()[fe::uidOf(*who)];
        if (cg::takeGiantConversion(m)) {
            auto stack = game::items::make(giant, 1);
            loot.clear();
            if (stack) loot.push_back(*stack);
        } else {
            loot.clear();
        }
    });
}

} // namespace gf::feature::giants
