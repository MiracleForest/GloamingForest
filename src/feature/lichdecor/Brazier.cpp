// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/BrazierBlock.java
//   src/main/java/gloamingforest/block/entity/BrazierBlockEntity.java
//   src/main/java/gloamingforest/enums/BrazierLight.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the two-block brazier. Placing needs a replaceable block above (the upper half
// is added as setPlacedBy does); the halves keep each other as doors do (updateShape: a missing
// partner or, for the lower half, a non-sturdy floor destroys the half, with drops: Java's
// updateOrDestroy); a creative break of the upper half removes the lower one without drops
// (playerWillDestroy). Flint and steel / a fire charge on the upper half cycles `light` off -> dim
// -> half -> bright -> full (upstream cycles only the upper half's state: the lower half's
// `getBlockState(below).cycle(LIGHT)` result is discarded); a water bucket on a lit brazier puts it
// out. The light level is the upper half's `light` (BP). BrazierBlockEntity#tick only counts the
// client's smoke rate: the smoke above the fire is client-side.

#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/families/BlockEvents.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"

namespace gf::feature::lichdecor::light {

namespace {

constexpr char const* kLights[] = {"off", "dim", "half", "bright", "full"};

int lightIndex(Block const& b) {
    auto s = state(b, "light");
    for (int i = 0; i < 5; ++i)
        if (s == kLights[i]) return i;
    return 0;
}

bool isUpper(Block const& b) { return state(b, "half") == "upper"; }

/// Lower halves to remove without drops (a creative break of the upper half).
std::set<GlobalPos>& silentRemovals() {
    static std::set<GlobalPos> s;
    return s;
}

void onPlaced(ia::PlaceContext const& c) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(c.pos);
    BlockPos up  = relative(pos, 1);
    // getStateForPlacement: below the build limit with a replaceable block above
    if (pos.y >= region->getMaxHeight() - 1 || !game::replaceable(*region, up)) {
        game::clearBlock(*region, pos);
        if (!c.player.isCreative() && c.itemBefore) giveOrDrop(c.player, game::items::withCount(*c.itemBefore, 1));
        return;
    }
    auto const* lower = game::blockWithStates(kBrazier, Json{{"gloamingforest:half", "lower"}, {"gloamingforest:light", "off"}});
    auto const* upper = game::blockWithStates(kBrazier, Json{{"gloamingforest:half", "upper"}, {"gloamingforest:light", "off"}});
    if (!lower || !upper) return;
    game::setBlock(*region, pos, *lower);
    game::setBlock(*region, up, *upper); // setPlacedBy
}

void onNeighbor(BlockSource& region, BlockPos const& pos, BlockPos const&) {
    Block const& b = region.getBlock(pos);
    if (b.getTypeName() != kBrazier) return;
    bool     upper   = isUpper(b);
    BlockPos partner = relative(pos, upper ? 0 : 1);
    Block const& p   = region.getBlock(partner);
    bool     keep    = p.getTypeName() == kBrazier && isUpper(p) != upper;
    // the lower half also needs a sturdy floor (canSurvive)
    if (keep && !upper) keep = region.getBlock(relative(pos, 0)).canProvideFullSupport(1);
    if (keep) return;
    GlobalPos gp{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
    bool      drop = !upper && !silentRemovals().erase(gp);
    game::clearBlock(region, pos);
    // Level#destroyBlock(pos, true): the loot table drops the brazier from the lower half
    if (drop)
        if (auto item = game::items::make(kBrazier, 1)) popResource(region, pos, *item);
}

ia::Result useUpper(ia::UseOnContext const& c) {
    if (!isUpper(c.block)) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos pos   = game::blockPos(c.pos);
    int      light = lightIndex(c.block);
    if (light != 4 && isFirestarter(c.itemId)) {
        if (!c.firstEvent) return ia::Result::Consume;
        setStates(*region, pos, c.block, Json{{"light", kLights[light + 1]}});
        useFirestarter(c.player, c.item, c.itemId);
        playSound(*region, centre(pos), "minecraft:item.flintandsteel.use", 1.0F, 1.0F);
        return ia::Result::Consume;
    }
    if (light > 0 && c.itemId == "minecraft:water_bucket") {
        if (!c.firstEvent) return ia::Result::Consume;
        // FluidUtil.tryEmptyContainer(1000 mB): the bucket is emptied (kept in creative)
        if (!c.player.isCreative()) {
            if (auto bucket = game::items::make("minecraft:bucket", 1)) {
                ia::mutableItem(c.item) = *bucket;
                c.player.sendInventory(false);
            }
        }
        setStates(*region, pos, c.block, Json{{"light", "off"}});
        playSound(*region, centre(pos), "minecraft:block.fire.extinguish", 1.0F, 1.0F);
        return ia::Result::Consume;
    }
    return ia::Result::Pass;
}

} // namespace

void registerBrazier() {
    ia::onPlace({.block = kBrazier}, onPlaced);
    ia::onUseOn({.block = kBrazier}, useUpper);
    gf::blockevents::onNeighborChanged(kBrazier, onNeighbor);
    // playerWillDestroy: a creative break of the upper half takes the lower half without drops
    ia::onDestroy({.block = kBrazier}, [](ia::DestroyContext const& c) {
        if (!c.player.isCreative() || !isUpper(c.block)) return ia::Result::Pass;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        BlockPos below = relative(game::blockPos(c.pos), 0);
        Block const& b = region->getBlock(below);
        if (b.getTypeName() == kBrazier && !isUpper(b)) silentRemovals().insert(GlobalPos{c.pos.dim, below.x, below.y, below.z});
        return ia::Result::Pass;
    }, ia::priority::Fallback);
}

} // namespace gf::feature::lichdecor::light
