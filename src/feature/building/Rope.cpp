// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/RopeBlock.java
//   src/main/java/twilightforest/item/RopeItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the rope. Its strands (states x / y / z) follow the clicked face's axis; a strand
// survives while its chain of ropes ends on support (leaves, a rope of the same axis, a block with a
// sturdy centre face; never below); one tick after a placement or a neighbour change the
// unsupported strands go, one rope dropped per strand (the block breaks when none is left). A rope
// item used on a rope extends it (RopeItem#updatePlacementContext / #getForward): up to
// EXTEND_RANGE horizontal blocks, any length downwards.

#include "feature/building/Building.h"

#include <algorithm>
#include <set>

#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/WorldDb.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::building {

namespace ia = interactions;

namespace {

constexpr char const* kRope = "twilightforest:rope";

std::vector<ia::HandlerId>& handlers() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

std::set<core::GlobalPos>& pending() {
    static std::set<core::GlobalPos> p;
    return p;
}

std::string axisState(int axis) { return std::string(kNs) + axisName(axis); }

bool isRope(Block const* b) { return b && idOf(*b) == kRope; }

} // namespace

bool ropeHasAxis(Block const& rope, int axis) { return boolState(rope, axisState(axis)); }

bool ropeCanConnectTo(BlockSource& region, BlockPos const& pos, int dir) {
    // RopeBlock.canConnectTo(state at pos, dir): never downwards; leaves, a rope along the axis or a
    // sturdy centre face towards the rope
    if (dir == Down) return false;
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    auto id = idOf(*b);
    // LeavesBlock: the leaves tag (TF's leaves), vanilla's leaves by id
    if (tags::has(tags::Registry::Block, "minecraft:leaves", id) || (id.starts_with("minecraft:") && id.ends_with("leaves"))) return true;
    if (id == kRope) return ropeHasAxis(*b, axisOf(dir));
    return sturdyCenter(region, pos, opposite(dir));
}

bool ropeCheckConnection(BlockSource& region, BlockPos const& pos, int dir) {
    // RopeBlock#checkConnection: along consecutive ropes until a support (true) or a gap (false)
    BlockPos m = pos;
    for (int guard = 0; guard < 4096; ++guard) {
        if (!isRope(blockAt(region, m))) return true;
        m = relative(m, dir);
        if (!ropeCanConnectTo(region, m, dir)) return false;
    }
    return false;
}

bool ropeCanSurvive(BlockSource& region, BlockPos const& pos, Block const& rope) {
    if (ropeHasAxis(rope, 0) && (ropeCanConnectTo(region, relative(pos, West), West) || ropeCanConnectTo(region, relative(pos, East), East))) return true;
    if (ropeHasAxis(rope, 1) && ropeCanConnectTo(region, relative(pos, Up), Up)) return true;
    if (ropeHasAxis(rope, 2) && (ropeCanConnectTo(region, relative(pos, North), North) || ropeCanConnectTo(region, relative(pos, South), South))) return true;
    return false;
}

int ropeTick(BlockSource& region, BlockPos const& pos) {
    // RopeBlock#tick
    auto const* b = blockAt(region, pos);
    if (!isRope(b)) return 0;
    bool flag  = true;
    int  drops = 0, axes = 0;
    Json next  = Json::object();
    struct Strand {
        int axis, a, b;
    };
    for (auto s : {Strand{0, West, East}, Strand{1, Up, Up}, Strand{2, North, South}}) {
        if (!ropeHasAxis(*b, s.axis)) continue;
        ++axes;
        if (!ropeCheckConnection(region, pos, s.a) && (s.a == s.b || !ropeCheckConnection(region, pos, s.b))) {
            next[axisState(s.axis)] = false;
            ++drops;
        } else {
            flag = false;
        }
    }
    auto rope = game::items::make(kRope, 1);
    if (flag) {
        // level.destroyBlock(pos, true): the loot table drops one rope per strand
        game::clearBlock(region, pos);
        if (rope)
            for (int i = 0; i < axes; ++i) popResource(region, pos, *rope);
        log().info("building: rope at {} {} {} broke ({} strands)", pos.x, pos.y, pos.z, axes);
        return axes;
    }
    if (drops > 0) {
        setStates(region, pos, next);
        if (rope)
            for (int i = 0; i < drops; ++i) popResource(region, pos, *rope);
        log().info("building: rope at {} {} {} lost {} strands", pos.x, pos.y, pos.z, drops);
    }
    return drops;
}

namespace {

/// level.scheduleTick(pos, this, 1)
void scheduleTick(BlockSource& region, BlockPos const& pos) {
    auto gp = game::globalPos(region, pos);
    if (!pending().insert(gp).second) return;
    ia::nextTick([gp] {
        pending().erase(gp);
        if (auto* r = game::regionOf(gp.dim)) ropeTick(*r, BlockPos{gp.x, gp.y, gp.z});
    });
}

bool replaceableFor(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    return b && (isRope(b) || replaceable(region, pos));
}

/// RopeItem#getForward
int ropeForward(BlockSource& region, BlockPos const& pos, Block const& state, int face, Player const& player, bool sneaking) {
    if (ropeCanConnectTo(region, relative(pos, face), face)) return face;
    face = opposite(face);
    if (ropeCanConnectTo(region, relative(pos, face), face)) return face;
    auto dirs = nearestLooking(player);
    if (sneaking) std::reverse(dirs.begin(), dirs.end());
    for (int i = 0; i < 3; ++i) {
        int dir = dirs[i];
        if (ropeHasAxis(state, 1)) return Down;
        if ((axisOf(dir) == 0 && ropeHasAxis(state, 0)) || (axisOf(dir) == 2 && ropeHasAxis(state, 2))) {
            for (int j = 0; j < 2; ++j) {
                if (j == 1) dir = opposite(dir);
                BlockPos m = pos;
                for (int k = 1; k < kRopeExtendRange; ++k) {
                    m = relative(m, dir);
                    if (replaceableFor(region, m)) return dir;
                    break; // a non-rope that cannot be replaced
                }
            }
        }
    }
    return -1;
}

void tooHigh(Player& player, int maxHeight) {
    // build.tooHigh in red on the action bar
    auto                           pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(std::string("\xC2\xA7") + "c" + trFor(player, "build.tooHigh", "Height limit for building is {}", maxHeight - 1));
    pkt.mBody = message;
    pkt.sendTo(player);
}

/// A rope placed (or a strand added) at `pos` along `dir`'s axis: RopeItem#getPlacementState.
bool putRope(BlockSource& region, BlockPos const& pos, int dir) {
    auto const* b = blockAt(region, pos);
    if (!b) return false;
    if (isRope(b)) {
        // BlockItem#canPlace: the rope with the added strand must survive here
        auto const* added = game::blockWithStates(kRope,
            Json{{axisState(0), axisOf(dir) == 0 || ropeHasAxis(*b, 0)}, {axisState(1), axisOf(dir) == 1 || ropeHasAxis(*b, 1)}, {axisState(2), axisOf(dir) == 2 || ropeHasAxis(*b, 2)}});
        if (!added || !ropeCanSurvive(region, pos, *added)) return false;
        return setStates(region, pos, {{axisState(axisOf(dir)), true}});
    }
    Json s{{axisState(0), axisOf(dir) == 0}, {axisState(1), axisOf(dir) == 1}, {axisState(2), axisOf(dir) == 2}};
    auto const* rope = game::blockWithStates(kRope, s);
    if (!rope || !ropeCanSurvive(region, pos, *rope)) return false; // BlockItem#canPlace
    return game::setBlock(region, pos, *rope);
}

/// RopeItem#updatePlacementContext + #getPlacementState: a rope item used on a rope.
ia::Result extendRope(ia::UseOnContext const& c) {
    if (!c.firstEvent) return ia::Result::Consume;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos pos = game::blockPos(c.pos);
    auto const* state = blockAt(*region, pos);
    if (!isRope(state)) return ia::Result::Pass;
    int dir = ropeForward(*region, pos, *state, c.face, c.player, c.sneaking);
    if (dir < 0) return ia::Result::Deny;
    if (dir == Up) dir = Down;
    BlockPos m = pos;
    int      i = 0;
    while (i < kRopeExtendRange) {
        if (m.y < region->getMinHeight() || m.y >= region->getMaxHeight()) {
            if (m.y >= region->getMaxHeight()) tooHigh(c.player, region->getMaxHeight());
            break;
        }
        auto const* b = blockAt(*region, m);
        if (!b) break;
        if (!isRope(b)) {
            if (!replaceable(*region, m)) break;
        } else if (ropeHasAxis(*b, axisOf(dir))) {
            m = relative(m, dir);
            if (horizontal(dir)) ++i;
            continue;
        }
        if (!putRope(*region, m, dir)) return ia::Result::Deny;
        // SoundEvents.WOOL_PLACE (volume 1, pitch 0.8): Bedrock's wool place sound is "use.cloth"
        fx::sound(region->getDimension(), "use.cloth", Vec3{m.x + .5f, m.y + .5f, m.z + .5f}, 1.0f, 0.8f);
        if (!c.player.isCreative()) {
            auto& held = ia::mutableItem(c.item);
            held.remove(1);
            if (held.mCount <= 0) held = ItemStack::EMPTY_ITEM();
            c.player.sendInventory(false);
        }
        scheduleTick(*region, m);
        log().info("building: rope extended to {} {} {} ({})", m.x, m.y, m.z, dirName(dir));
        return ia::Result::Consume;
    }
    return ia::Result::Deny;
}

void placeRope(ia::PlaceContext const& c) {
    // RopeBlock#getStateForPlacement: the clicked face's axis; BlockItem#canPlace: canSurvive
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos  = game::blockPos(c.pos);
    int      axis = axisOf(c.face >= 0 && c.face < 6 ? c.face : Up);
    Json     s{{axisState(0), axis == 0}, {axisState(1), axis == 1}, {axisState(2), axis == 2}};
    auto const* rope = game::blockWithStates(kRope, s);
    if (!rope || !ropeCanSurvive(*region, pos, *rope)) {
        game::clearBlock(*region, pos);
        if (!c.player.isCreative())
            if (auto item = game::items::make(kRope, 1)) give(c.player, *item);
        log().info("building: rope cannot hang at {} {} {}", pos.x, pos.y, pos.z);
        return;
    }
    game::setBlock(*region, pos, *rope);
    scheduleTick(*region, pos);
}

} // namespace

void installRope() {
    handlers().push_back(ia::onUseOn({.block = kRope, .item = kRope}, extendRope, ia::priority::High));
    handlers().push_back(ia::onPlace({.block = kRope}, placeRope));
    blockevents::onPlaced(kRope, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { scheduleTick(region, pos); });
    // a tick callback dropped with its world must not leave its position pending
    game::db::onWorldClosed([] { pending().clear(); });
    blockevents::onNeighborChanged(kRope, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { scheduleTick(region, pos); });
}

void uninstallRope() {
    for (auto id : handlers()) ia::remove(id);
    handlers().clear();
    pending().clear();
}

} // namespace gf::feature::building
