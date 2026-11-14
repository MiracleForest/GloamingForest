// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/TrollsteinnBlock.java
//   src/main/java/gloamingforest/block/TrollRootBlock.java
//   src/main/java/gloamingforest/block/UnripeTorchClusterBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U27's blocks. The trollsteinn's six face states follow the light of its neighbours (placement and
// random ticks), its comparator output is the brightest neighbour; the lit / dark texture per face
// is the imported multipart model (pure data, every client). The troll roots (trollvidr, trollber,
// unripe trollber) hang under base stone or another root: their BP placement filter pops them when
// the block above changes (TrollRootBlock#updateShape); a trollber is harvested by hand, an unripe
// one ripens in light >= 6.
#include "feature/caves/Caves.h"

#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/caves/Caves.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/stats/Stats.h"
#include "feature/ext/caves/W0Internals.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace ia = gf::feature::common::interactions;

namespace {

/// Direction.values(): down, up, north, south, west, east.
struct Face {
    char const* state;
    int         dx, dy, dz;
};
constexpr Face kFaces[] = {
    {"gloamingforest:down", 0, -1, 0},  {"gloamingforest:up", 0, 1, 0},    {"gloamingforest:north", 0, 0, -1},
    {"gloamingforest:south", 0, 0, 1}, {"gloamingforest:west", -1, 0, 0}, {"gloamingforest:east", 1, 0, 0},
};

bool isTrollRoot(std::string const& id) { return id == kTrollvidr || id == kTrollber || id == kUnripeTrollber; }

} // namespace

int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return 0;
    auto pair = region.getBrightnessPair(pos);
    return core::caves::rawBrightness(static_cast<int>(pair.block->mValue), static_cast<int>(pair.sky->mValue), gf::world::ext::skyDarken(region));
}

Block const* trollsteinnFor(BlockSource& region, BlockPos const& pos) {
    nlohmann::json states = nlohmann::json::object();
    for (auto const& f : kFaces)
        states[f.state] = maxLocalRawBrightness(region, BlockPos{pos.x + f.dx, pos.y + f.dy, pos.z + f.dz}) > core::caves::kTrollsteinnLightThreshold;
    return game::blockWithStates(kTrollsteinn, states);
}

bool canPlaceRootBelow(BlockSource& region, BlockPos const& above) {
    Block const& b  = region.getBlock(above);
    auto         id = game::blockId(b);
    return isTrollRoot(id) || gf::structure::blocks::inTag(b, "minecraft:base_stone_overworld");
}

bool trollsteinnRandomTick(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.getTypeName() != kTrollsteinn) return false;
    auto const* lit = trollsteinnFor(region, pos);
    if (!lit || lit == &block) return false;
    return game::setBlock(region, pos, *lit);
}

bool unripeTrollberRandomTick(BlockSource& region, BlockPos const& pos) {
    if (maxLocalRawBrightness(region, pos) < core::caves::kRipenThreshold) return false;
    // ripen!
    auto const* ber = game::blockWith(kTrollber);
    return ber && game::setBlock(region, pos, *ber);
}

std::optional<int> trollsteinnComparator(BlockSource& region, BlockPos const& pos) {
    int peak = 0;
    for (auto const& f : kFaces) peak = std::max(maxLocalRawBrightness(region, BlockPos{pos.x + f.dx, pos.y + f.dy, pos.z + f.dz}), peak);
    return peak;
}

void installBlocks() {
    // TrollsteinnBlock#getStateForPlacement (a player's placement; the server's sky darkening)
    ia::onPlace({.block = kTrollsteinn}, [](ia::PlaceContext const& ctx) {
        auto pos = ctx.pos;
        ia::nextTick([pos] {
            auto* region = game::regionOf(pos.dim);
            if (!region) return;
            BlockPos p{pos.x, pos.y, pos.z};
            if (game::blockIdAt(*region, p) != kTrollsteinn) return;
            if (auto const* lit = trollsteinnFor(*region, p); lit && lit != &region->getBlock(p)) game::setBlock(*region, p, *lit);
        });
    });
    // TrollsteinnBlock#randomTick: every face again by getMaxLocalRawBrightness
    gf::blockevents::onRandomTick(kTrollsteinn, [](BlockSource& region, BlockPos const& pos, Block const&, gf::blockevents::RandomSource&) {
        trollsteinnRandomTick(region, pos);
    });
    // TrollsteinnBlock#getAnalogOutputSignal: the brightest neighbour
    gf::feature::common::redstone::registerAnalog(kTrollsteinn, [](BlockSource& region, BlockPos const& pos, Block const&) -> std::optional<int> {
        return trollsteinnComparator(region, pos);
    });
    // UnripeTorchClusterBlock#randomTick
    gf::blockevents::onRandomTick(kUnripeTrollber, [](BlockSource& region, BlockPos const& pos, Block const&, gf::blockevents::RandomSource&) {
        unripeTrollberRandomTick(region, pos);
    });
    // TrollRootBlock#updateShape (UP): the BP placement filter pops a root whose support changes;
    // this also covers blocks placed by commands and features that never ask the filter
    gf::blockevents::onNeighborChanged(kTrollvidr, [](BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
        if (neighbor.y == pos.y + 1 && !canPlaceRootBelow(region, neighbor)) game::clearBlock(region, pos);
    });
    gf::blockevents::onNeighborChanged(kTrollber, [](BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
        if (neighbor.y == pos.y + 1 && !canPlaceRootBelow(region, neighbor)) game::clearBlock(region, pos);
    });
    gf::blockevents::onNeighborChanged(kUnripeTrollber, [](BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
        if (neighbor.y == pos.y + 1 && !canPlaceRootBelow(region, neighbor)) game::clearBlock(region, pos);
    });
    // TrollRootBlock#useWithoutItem: picking a trollber
    ia::onUseOn({.block = kTrollber}, [](ia::UseOnContext const& ctx) {
        // a sneaking player with an item in hand skips the block's own use (Java's secondary use)
        if (ctx.sneaking && !ctx.itemId.empty()) return ia::Result::Pass;
        if (!ctx.firstEvent) return ia::Result::Consume;
        harvestTrollber(ctx.player, ctx.pos);
        return ia::Result::Consume;
    });
}

bool harvestTrollber(Player& player, core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return false;
    BlockPos p{pos.x, pos.y, pos.z};
    if (game::blockIdAt(*region, p) != kTrollber) return false;
    auto const* vidr = game::blockWith(kTrollvidr);
    if (!vidr) return false;
    ia::nextTick([pos] {
        auto* r = game::regionOf(pos.dim);
        if (!r) return;
        BlockPos at{pos.x, pos.y, pos.z};
        if (game::blockIdAt(*r, at) != kTrollber) return;
        if (auto const* v = game::blockWith(kTrollvidr)) game::setBlock(*r, at, *v);
        gf::fx::sound(r->getDimension(), "block.gloamingforest.torchberry.harvest", Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 1.0f, 1.0f);
        if (auto berries = game::items::make("gloamingforest:torchberries", 1))
            r->getLevel().getSpawner().spawnItem(*r, *berries, nullptr, Vec3{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f}, 0);
    });
    gf::stats::award(player, "gloamingforest:torchberries_harvested");
    gf::log().info("caves: {} picked a trollber at {} {} {}", player.getRealName(), pos.x, pos.y, pos.z);
    return true;
}

} // namespace gf::feature::caves
