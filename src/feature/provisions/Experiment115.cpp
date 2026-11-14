// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/Experiment115Block.java
//   src/main/java/gloamingforest/item/Experiment115Item.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions): Experiment 115, the cake you can put back together.
// - The block: states omnomnom (bites taken 0..7; the BP's default 0 is a full block, a placed
//   slice is 7 as upstream's default state) and regenerate; F1's permutations draw the slices and give
//   their boxes; no loot (noLootTable: the BP override drops nothing). It survives only on a solid
//   block below (a neighbour change removes it otherwise).
// - Use (core::provisions::e115Action, upstream's order): an E115 item adds a slice; redstone on a full,
//   non-regenerating block makes it regenerate; sneaking with empty hands takes a slice back; any
//   other use eats one (4 / 0.3, the e115_slices_eaten stat, the consume_item trigger with 8 - bites
//   items). Random ticks regrow a regenerating block; it is a redstone source of 15 - 2 x bites
//   while regenerating and its comparator output is (8 - bites) + 7 when regenerating.
// - The item: used on another block (not sneaking) it places one slice on a solid block (after the
//   clicked block's own use passed: interactions::onUnusedUseOn); otherwise it is food (the BP's
//   minecraft:food) and eaten. Its BP block_placer exists for the block's item only (use_on lists
//   experiment_115 itself, whose clicks are handled here): it never places by itself.
#include <cstdint>
#include <mutex>
#include <random>
#include <unordered_map>
#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/provisions/Provisions.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "feature/provisions/Internal.h"
#include "feature/stats/Stats.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::provisions {

namespace cp = core::provisions;
namespace ia = common::interactions;

namespace {

constexpr char kBites[]      = "gloamingforest:omnomnom";
constexpr char kRegenerate[] = "gloamingforest:regenerate";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// BlockBehaviour.BlockStateBase#isSolid (calculateSolid): a collision shape whose bounds average
/// 0.729 of a block or that is a full block high.
bool solidAt(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight() || !region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    std::vector<AABB> boxes;
    b.getBlockType().addCollisionShapes(b, region, pos, nullptr, boxes, {});
    if (boxes.empty()) return false;
    AABB bounds = boxes.front();
    for (auto const& box : boxes) {
        bounds.min.x = std::min(bounds.min.x, box.min.x), bounds.min.y = std::min(bounds.min.y, box.min.y), bounds.min.z = std::min(bounds.min.z, box.min.z);
        bounds.max.x = std::max(bounds.max.x, box.max.x), bounds.max.y = std::max(bounds.max.y, box.max.y), bounds.max.z = std::max(bounds.max.z, box.max.z);
    }
    float sx = bounds.max.x - bounds.min.x, sy = bounds.max.y - bounds.min.y, sz = bounds.max.z - bounds.min.z;
    return (sx + sy + sz) / 3.0f >= 0.7291666f || sy >= 1.0f;
}

Vec3 center(BlockPos const& p) { return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f}; }

void consumeOne(Player& player) {
    if (player.isCreative()) return; // ItemStack#consume: infinite materials
    auto held = player.getSelectedItem();
    int  n    = game::items::countOf(held);
    player.setSelectedItem(n <= 1 ? ItemStack::EMPTY_ITEM() : game::items::withCount(held, n - 1));
    player.sendInventory(false);
}

void giveSlice(Player& player) {
    auto stack = game::items::make(ids::kE115);
    if (!stack) return;
    if (!player.add(*stack)) player.drop(*stack, false);
    player.sendInventory(false);
}

/// The result of each player's last first use-on event of the block (the repeats while the button
/// is held answer the same, without acting).
std::mutex&                                    lastMutex() { static std::mutex m; return m; }
std::unordered_map<std::int64_t, ia::Result>& lastResult() { static std::unordered_map<std::int64_t, ia::Result> m; return m; }

ia::Result decide(ia::UseOnContext const& c);

ia::Result onUseBlock(ia::UseOnContext const& c) {
    auto uid = c.player.getOrCreateUniqueID().rawID;
    if (!c.firstEvent && !c.offhand) {
        std::lock_guard lock(lastMutex());
        auto it = lastResult().find(uid);
        auto r  = it == lastResult().end() ? ia::Result::Pass : it->second;
        return r == ia::Result::Pass && c.itemId == ids::kE115 ? ia::Result::Deny : r;
    }
    auto r = decide(c);
    if (!c.offhand) {
        std::lock_guard lock(lastMutex());
        lastResult()[uid] = r;
    }
    return r;
}

ia::Result decide(ia::UseOnContext const& c) {
    bool holdsE115 = c.itemId == ids::kE115;
    if (c.offhand) return holdsE115 ? ia::Result::Deny : ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    auto pos   = game::blockPos(c.pos);
    auto state = e115Of(c.block);
    if (state.bites < 0) return ia::Result::Pass;

    cp::E115Use use;
    use.holdsE115     = holdsE115;
    use.holdsRedstone = c.itemId == "minecraft:redstone";
    use.emptyHands    = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
    use.sneaking      = c.sneaking;
    use.canEat        = c.player.isCreative() || foodLevel(c.player) < 20.0f;
    cp::E115 before{state.bites, state.regenerate};
    auto     action = cp::e115Action(before, use);
    auto&    dim    = c.player.getDimension();
    switch (action) {
    case cp::E115Action::Pass: return holdsE115 ? ia::Result::Deny : ia::Result::Pass;
    case cp::E115Action::Fail: return ia::Result::Deny;
    case cp::E115Action::AddSlice:
    case cp::E115Action::Regenerate: {
        auto after = *cp::e115After(before, action);
        setE115(*region, pos, {after.bites, after.regenerate});
        ::gf::fx::sound(dim, "use.cloth", center(pos), 1.0f, 1.0f);
        consumeOne(c.player);
        if (action == cp::E115Action::AddSlice)
            ::gf::progression::trigger(c.player, "minecraft:placed_block", {{"block", ids::kE115}, {"pos", {pos.x, pos.y, pos.z}}, {"item", ids::kE115}});
        log().info("provisions: {} {} experiment 115 at {} {} {}", c.player.getRealName(), action == cp::E115Action::AddSlice ? "added a slice to" : "made regenerating",
                   pos.x, pos.y, pos.z);
        return ia::Result::Consume;
    }
    case cp::E115Action::TakeSlice: {
        auto after = cp::e115After(before, action);
        setE115(*region, pos, after ? E115State{after->bites, after->regenerate} : E115State{});
        ::gf::fx::soundTo(c.player, "minecraft:entity.item.pickup", 0.5f, 1.0f);
        if (!c.player.isCreative()) giveSlice(c.player);
        log().info("provisions: {} took a slice of experiment 115", c.player.getRealName());
        return ia::Result::Consume;
    }
    case cp::E115Action::Eat: {
        ::gf::stats::award(c.player, ids::kSlicesEaten);
        c.player.eat(4, 0.3f);
        float pitch = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()) * 0.1f + 0.9f;
        ::gf::fx::sound(dim, "minecraft:entity.generic.eat", center(pos), 0.5f, pitch);
        auto after = cp::e115After(before, action);
        setE115(*region, pos, after ? E115State{after->bites, after->regenerate} : E115State{});
        ::gf::progression::trigger(c.player, "minecraft:consume_item", {{"item", ids::kE115}, {"count", cp::e115EatenCount(before)}});
        log().info("provisions: {} ate a slice of experiment 115", c.player.getRealName());
        return ia::Result::Consume;
    }
    }
    return ia::Result::Pass;
}

/// Experiment115Item#useOn on another block (after that block's own use passed).
ia::Result onUseItem(ia::UseOnContext const& c) {
    if (c.offhand || c.sneaking || c.blockId == ids::kE115) return ia::Result::Pass;
    if (!c.firstEvent) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    if (!placeE115(c.player, *region, game::blockPos(c.pos), c.face, c.player.isCreative())) return ia::Result::Pass; // eaten instead (food)
    consumeOne(c.player);
    return ia::Result::Consume;
}

} // namespace

E115State e115Of(Block const& block) {
    if (game::blockId(block) != ids::kE115) return {};
    E115State s;
    s.bites      = game::intState(block, kBites).value_or(0);
    auto states  = game::statesOf(block);
    auto it      = states.find(kRegenerate);
    s.regenerate = it != states.end() && (it->second == "true" || it->second == "1");
    return s;
}

E115State e115At(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return {};
    return e115Of(region.getBlock(pos));
}

bool setE115(BlockSource& region, BlockPos const& pos, E115State state) {
    if (state.bites < 0) return game::clearBlock(region, pos);
    auto const* b = game::blockWithStates(ids::kE115, {{kBites, state.bites}, {kRegenerate, state.regenerate}});
    return b && game::setBlock(region, pos, *b);
}

bool placeE115(Player& player, BlockSource& region, BlockPos const& clicked, int face, bool) {
    BlockPos target = clicked;
    if (!game::replaceable(region, target)) {
        static constexpr int off[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        if (face < 0 || face > 5) return false;
        target = BlockPos{clicked.x + off[face][0], clicked.y + off[face][1], clicked.z + off[face][2]};
        if (!game::replaceable(region, target)) return false;
    }
    if (target.y < region.getMinHeight() || target.y >= region.getMaxHeight()) return false;
    if (player.isAdventure()) return false; // Player#mayUseItemAt
    if (::gf::landmark::isProtectedFor(player, target)) return false;
    if (!solidAt(region, BlockPos{target.x, target.y - 1, target.z})) return false; // canSurvive
    if (!setE115(region, target, {7, false})) return false;
    ::gf::fx::sound(player.getDimension(), "use.cloth", center(target), 1.0f, 0.8f);
    ::gf::progression::trigger(player, "minecraft:placed_block", {{"block", ids::kE115}, {"pos", {target.x, target.y, target.z}}, {"item", ids::kE115}});
    log().info("provisions: {} placed experiment 115 at {} {} {}", player.getRealName(), target.x, target.y, target.z);
    return true;
}

void installE115() {
    ia::onUseOn({.block = ids::kE115}, &onUseBlock, ia::priority::Normal + 1);
    ia::onUnusedUseOn({.item = ids::kE115}, &onUseItem);
    ::gf::blockevents::onRandomTick(ids::kE115, [](BlockSource& region, BlockPos const& pos, Block const& block, ::gf::blockevents::RandomSource&) {
        auto s = e115Of(block);
        if (s.bites < 0) return;
        if (auto n = cp::e115RandomTick({s.bites, s.regenerate})) setE115(region, pos, {n->bites, n->regenerate});
    });
    ::gf::blockevents::onNeighborChanged(ids::kE115, [](BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
        // updateShape(DOWN): the block below is no longer solid
        if (neighbor.x != pos.x || neighbor.z != pos.z || neighbor.y != pos.y - 1) return;
        if (e115At(region, pos).bites < 0) return;
        if (!solidAt(region, neighbor)) game::clearBlock(region, pos);
    });
    ::gf::redstone::registerEmitter(ids::kE115, [](BlockSource&, BlockPos const&, Block const& block, int) {
        auto s = e115Of(block);
        return s.bites < 0 ? 0 : cp::e115Signal({s.bites, s.regenerate});
    });
    ::gf::redstone::registerAnalog(ids::kE115, [](BlockSource&, BlockPos const&, Block const& block) -> std::optional<int> {
        auto s = e115Of(block);
        if (s.bites < 0) return std::nullopt;
        return cp::e115Comparator({s.bites, s.regenerate});
    });
}

} // namespace gf::feature::provisions
