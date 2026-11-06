// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/ThornsBlock.java
//   src/main/java/twilightforest/block/BurntThornsBlock.java
//   src/main/java/twilightforest/block/ConnectableRotatedPillarBlock.java
//   src/main/java/twilightforest/block/ThornRoseBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): brown, green and burnt thorns and the thorn rose.
//
// - Thorns connect to thorns, the thorn rose, thorn leaves and weathered deadrock; a neighbour turning
//   into air keeps the connection (updateShape returns the state unchanged for air). Their family is
//   declared as a plain pillar (assets/data/families/finalcastle.json) so the families kit does not
//   apply ConnectableRotatedPillarBlock's generic rule (sturdy faces) over ThornsBlock's.
// - Brown / green thorns hurt anything inside them (and Y-axis thorns anything standing on them) for
//   4 twilightforest:thorns, items of #twilightforest:immune_to_thorns excepted; a survival player
//   cannot break them: the block stays and grows a thorn burst of green thorns (deferred to the next
//   tick, R16). Creative players break them normally.
// - Burnt thorns dissolve (destroyBlock without drops) when a living entity or a projectile is inside.
// - The thorn rose faces the clicked face and survives on thorns or a sturdy face behind it.
// - Thorn leaves decay by the families kit's leaves (brown / green thorns count as their stems:
//   params.log of the family data).
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::blocks {

namespace ia = gf::interactions;

namespace {

constexpr char  kAxis[]       = "twilightforest:axis";
constexpr char  kFacing[]     = "twilightforest:facing";
constexpr float kThornDamage  = 4.0f;

bool isLiving(Actor const& e) { return e.hasCategory(ActorCategory::Mob); }
bool isProjectile(Actor const& e) { return (static_cast<int>(e.getEntityTypeId()) & static_cast<int>(ActorType::Projectile)) == static_cast<int>(ActorType::Projectile); }

/// ThornsBlock#entityInside.
void thornsHurt(Actor& entity) {
    if (entity.isType(ActorType::ItemEntity)) {
        auto const& item = static_cast<ItemActor&>(entity).item();
        if (tags::has(tags::Registry::Item, "twilightforest:immune_to_thorns", game::items::idOf(item))) return;
    }
    combat::hurt(entity, kThornDamage, "twilightforest:thorns");
}

/// BurntThornsBlock#entityInside: dissolve.
void burntDissolve(BlockSource& region, BlockPos const& pos, Actor& entity) {
    if (!isLiving(entity) && !isProjectile(entity)) return;
    if (idAt(region, pos) != id::BurntThorns) return;
    destroy(region, pos, false);
}

/// growThorns(level, pos, dir).
void growThorns(BlockSource& region, BlockPos const& pos, int dir) {
    int length = 1 + random().nextInt(3);
    for (int i = 1; i < length; ++i) {
        BlockPos d = offset(pos, dir, i);
        if (!loaded(region, d) || !isAir(region, d)) break;
        set(region, d, id::GreenThorns, {{kAxis, axisName(axisOf(dir))}});
    }
}

/// The six connections of a thorns block at `pos` as ThornsBlock#getStateForPlacement decides them.
Json placementConnections(BlockSource& region, BlockPos const& pos) {
    Json s = Json::object();
    for (int d = 0; d < 6; ++d) s[sideState(d)] = thornsConnectTo(region.getBlock(offset(pos, d)));
    return s;
}

} // namespace

bool thornsConnectTo(Block const& neighbour) {
    auto id = game::blockId(neighbour);
    return isThorns(id) || id == id::ThornRose || id == id::ThornLeaves || id == id::Deadrock;
}

void thornBurst(BlockSource& region, BlockPos const& pos, int axis) {
    switch (axis) {
    case 1:
        growThorns(region, pos, Up);
        growThorns(region, pos, Down);
        break;
    case 0:
        growThorns(region, pos, East);
        growThorns(region, pos, West);
        break;
    default:
        growThorns(region, pos, North);
        growThorns(region, pos, South);
        break;
    }
    // also try three random directions (Direction.getRandom)
    for (int i = 0; i < 3; ++i) growThorns(region, pos, random().nextInt(6));
}

bool roseCanSurvive(BlockSource& region, BlockPos const& pos, int facing) {
    BlockPos     behind = offset(pos, opposite(facing));
    auto         id     = idAt(region, behind);
    if (id == id::BrownThorns || id == id::GreenThorns) return true;
    // blockstate.isFaceSturdy(level, pos, facing): the behind block's face towards the rose
    return sturdy(region, behind, facing);
}

void installThorns() {
    for (char const* thorns : kThorns) {
        // ThornsBlock#getStateForPlacement: the clicked face's axis and all six connections (after
        // the families kit's pillar placement, which sets the axis only)
        ia::onPlace({.block = thorns}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            auto bp = game::blockPos(c.pos);
            Json s  = placementConnections(*region, bp);
            s[kAxis] = axisName(axisOf(c.face >= 0 && c.face < 6 ? c.face : Up));
            setStates(*region, bp, s);
        }, ia::priority::Low);
        // updateShape: air keeps the state, any other neighbour sets the side's connection
        blockevents::onNeighborChanged(thorns, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
            for (int d = 0; d < 6; ++d) {
                BlockPos n = offset(pos, d);
                if (n.x != from.x || n.y != from.y || n.z != from.z) continue;
                Block const& nb = region.getBlock(n);
                if (nb.isAir()) return;
                bool on = thornsConnectTo(nb);
                if (boolStateAt(region, pos, sideState(d).c_str()) != on) setStates(region, pos, {{sideState(d), on}});
                return;
            }
        });
    }
    for (char const* thorns : {id::BrownThorns, id::GreenThorns}) {
        blockevents::onEntityInside(thorns, [](BlockSource&, BlockPos const&, Actor& entity) { thornsHurt(entity); });
        blockevents::onSteppedOn(thorns, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
            if (textState(region.getBlock(pos), kAxis) == "y") thornsHurt(entity);
        });
        // onDestroyedByPlayer: survival players make it burst instead of breaking it
        ia::onDestroy({.block = thorns}, [](ia::DestroyContext const& c) {
            if (creative(c.player)) return ia::Result::Pass;
            int  axis = axisFromName(textState(c.block, kAxis));
            auto pos  = c.pos;
            ia::nextTick([pos, axis] {
                auto* region = game::regionOf(pos.dim);
                if (!region) return;
                auto bp = game::blockPos(pos);
                if (!loaded(*region, bp) || !isThorns(idAt(*region, bp))) return;
                thornBurst(*region, bp, axis);
            });
            gf::log().info("finalcastle: {} made the thorns at {} burst", c.player.getRealName(), c.pos.key());
            return ia::Result::Deny;
        });
    }
    // BurntThornsBlock: dissolves when walked into (stepOn of a Y-axis block calls entityInside too)
    blockevents::onEntityInside(id::BurntThorns, burntDissolve);
    blockevents::onSteppedOn(id::BurntThorns, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
        if (textState(region.getBlock(pos), kAxis) == "y") burntDissolve(region, pos, entity);
    });
    blockevents::onProjectileHit(id::BurntThorns, burntDissolve);

    // ThornRoseBlock: facing the clicked face; survival (BushBlock#updateShape / canSurvive)
    ia::onPlace({.block = id::ThornRose}, [](ia::PlaceContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region || c.face < 0 || c.face > 5) return;
        auto bp = game::blockPos(c.pos);
        setStates(*region, bp, {{kFacing, dirName(c.face)}});
        if (roseCanSurvive(*region, bp, c.face)) return;
        // BlockItem#place refuses a state that cannot survive: undo the placement
        game::clearBlock(*region, bp);
        if (!creative(c.player)) {
            if (auto item = game::items::make(id::ThornRose)) c.player.add(*item);
            c.player.sendInventory(false);
        }
    }, ia::priority::Low);
    blockevents::onNeighborChanged(id::ThornRose, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
        auto facing = dirFromName(textState(region.getBlock(pos), kFacing)).value_or(Up);
        if (!roseCanSurvive(region, pos, facing)) destroy(region, pos, true);
    });
}

} // namespace gf::feature::finalcastle::blocks
