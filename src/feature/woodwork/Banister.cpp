// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/BanisterBlock.java
//   src/main/java/gloamingforest/enums/BanisterShape.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Banisters (19 BanisterBlocks): F11's horizontal family sets the facing on placement (the look
// direction, BanisterBlock.getStateForPlacement) and keeps the water layer; this file adds the
// shape (connected under another banister), the axe cycle, the un-extension over a non-short
// banister (updateShape) and the item tooltip. The shapes per state are the block's permutations
// (tools/import/overrides/woodwork.json).

#include "feature/woodwork/Woodwork.h"

#include <algorithm>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::woodwork {

namespace ia = interactions;

namespace {

constexpr char kShape[]    = "gloamingforest:shape";
constexpr char kExtended[] = "gloamingforest:extended";

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

/// BlockTagGenerator.BANISTERS (block tag #gloamingforest:banisters), else one of the 19 ids.
bool isBanister(Block const& block) {
    auto id = idOf(block);
    if (tags::has(tags::Registry::Block, "gloamingforest:banisters", id)) return true;
    return std::find(banisterIds().begin(), banisterIds().end(), id) != banisterIds().end();
}

/// BanisterShape in enum order (SHORT, TALL, CONNECTED); `state.cycle(SHAPE)`.
std::string nextShape(std::string const& shape) {
    if (shape == "short") return "tall";
    if (shape == "tall") return "connected";
    return "short";
}

BlockPos below(BlockPos const& p) { return BlockPos{p.x, p.y - 1, p.z}; }
BlockPos above(BlockPos const& p) { return BlockPos{p.x, p.y + 1, p.z}; }

/// BanisterBlock.getStateForPlacement: the shape (the facing and the water are the family's).
void onPlace(ia::PlaceContext const& c) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos     pos   = game::blockPos(c.pos);
    Block const& block = region->getBlock(pos);
    if (!isBanister(block)) return;
    bool         connected = isBanister(region->getBlock(above(pos)));
    Block const* placed    = withStates(block, {{kShape, connected ? "connected" : "tall"}, {kExtended, false}});
    if (placed && placed != &block) game::setBlock(*region, pos, *placed);
}

/// BanisterBlock.useItemOn: an axe cycles the shape (and on a full cycle the extension).
ia::Result onUseOn(ia::UseOnContext const& c) {
    if (!isAxe(c.itemId)) return ia::Result::Pass;
    if (c.sneaking) return ia::Result::Pass; // Java: sneaking with an item skips the block's use
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    auto&    region = c.player.getDimensionBlockSource();
    BlockPos pos    = game::blockPos(c.pos);
    auto     shape  = nextShape(stateOf(c.block, kShape));
    playSound(region, pos, "item.axe.strip", 1.0f, 1.0f); // SoundEvents.AXE_STRIP
    Block const& belowBlock = region.getBlock(below(pos));
    Json         changes{{kShape, shape}};
    if (isBanister(belowBlock) && stateOf(belowBlock, kShape) != "short") {
        // a non-short banister below: never extended
        changes[kExtended] = false;
    } else if (shape == "tall") {
        // back at TALL: a full cycle, the extension cycles too
        changes[kExtended] = stateOf(c.block, kExtended) != "true";
    }
    if (auto const* next = withStates(c.block, changes)) game::setBlock(region, pos, *next);
    log().info("woodwork: {} turned the banister at {} {} {} to {}", c.player.getRealName(), pos.x, pos.y, pos.z, shape);
    return ia::Result::Consume;
}

/// BanisterBlock.updateShape: a non-short banister below an extended one un-extends it.
void onNeighborChanged(BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
    if (neighbor.x != pos.x || neighbor.z != pos.z || neighbor.y != pos.y - 1) return;
    Block const& self = region.getBlock(pos);
    if (!isBanister(self) || stateOf(self, kExtended) != "true") return;
    Block const& n = region.getBlock(neighbor);
    if (!isBanister(n) || stateOf(n, kShape) == "short") return;
    if (auto const* next = withStates(self, {{kExtended, false}})) game::setBlock(region, pos, *next);
}

} // namespace

void installBanisters() {
    static bool once = false;
    for (auto const& id : banisterIds()) {
        ia::Match m;
        m.block = id;
        handlerIds().push_back(ia::onPlace(m, onPlace));
        handlerIds().push_back(ia::onUseOn(m, onUseOn));
        if (!once) {
            // registrations of the dispatch and the item contract last for the process
            blockevents::onNeighborChanged(id, onNeighborChanged);
            item::addTooltip(id, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
                // BanisterBlock.appendHoverText: gray
                lines.push_back("§7" + (viewer ? trFor(*viewer, "block.gloamingforest.banister.cycle", "Right-click with axe to cycle")
                                               : tr("block.gloamingforest.banister.cycle", "Right-click with axe to cycle")));
            });
        }
    }
    once = true;
}

void uninstallBanisters() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::woodwork
