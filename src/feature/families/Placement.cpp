// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/GloamingHorizontalBlock.java
//   src/main/java/gloamingforest/block/MiniatureStructureBlock.java
//   src/main/java/gloamingforest/block/BanisterBlock.java
//   src/main/java/gloamingforest/block/EtchedNagastoneBlock.java
//   src/main/java/gloamingforest/block/StrongholdShieldBlock.java
//   src/main/java/gloamingforest/block/DirectionalRotatedPillarBlock.java
//   src/main/java/gloamingforest/block/BinaryRotatedBlock.java
//   src/main/java/gloamingforest/block/ConnectableRotatedPillarBlock.java
//   src/main/java/gloamingforest/block/GloamingBushBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11): placement. Bedrock places a block's default permutation; right after a
// player placed a family block (interactions::onPlace), the states are set as the block's Java
// getStateForPlacement would have chosen them (BlockPlaceContext: the clicked face, the click
// position, the player's look). Placements Java refuses (no state for placement: a button with
// nothing to hang on) are undone and the item handed back.

#include <algorithm>
#include <cmath>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace detail {
Block const* replacedBlock(GlobalPos const& pos);
bool         pillarConnects(BlockSource& region, BlockPos const& pos, int dir, Entry const& e, int axis);
bool         buttonCanSurvive(BlockSource& region, BlockPos const& pos, std::string const& face, int facing);
bool         plateCanSurvive(BlockSource& region, BlockPos const& pos);
void         placeSign(ia::PlaceContext const& c);
void         placeChest(ia::PlaceContext const& c);
} // namespace detail

namespace {

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

struct Placing {
    ia::PlaceContext const& c;
    BlockSource&            region;
    BlockPos                pos;
    double                  relY;           ///< the click height relative to the placed block
    bool                    replaceClicked; ///< the clicked block was replaced (Java replaceClicked)
};

/// Java BlockPlaceContext.getNearestLookingDirections: the opposite of the clicked face first.
std::array<int, 6> placeLooking(Placing const& p) {
    auto dirs = nearestLooking(p.c.player);
    if (p.replaceClicked || p.c.face < 0 || p.c.face > 5) return dirs;
    int want = opposite(p.c.face);
    int i    = 0;
    while (i < 6 && dirs[i] != want) ++i;
    if (i > 0 && i < 6) {
        for (int j = i; j > 0; --j) dirs[j] = dirs[j - 1];
        dirs[0] = want;
    }
    return dirs;
}

/// Java: getStateForPlacement returned null; the block goes and the item comes back.
void refuse(Placing const& p, char const* why) {
    game::clearBlock(p.region, p.pos);
    if (!p.c.player.isCreative()) give(p.c.player, makeItem(p.c.itemId.empty() ? p.c.blockId : p.c.itemId));
    log().info("families: {} could not be placed at {} {} {} ({})", p.c.blockId, p.pos.x, p.pos.y, p.pos.z, why);
}

/// Java half from the click: (face != DOWN && (face == UP || y <= 0.5)) ? BOTTOM : TOP.
bool clickedTop(Placing const& p) { return !(p.c.face != Down && (p.c.face == Up || p.relY <= 0.5)); }

void placeStairs(Placing const& p) {
    // Java StairBlock.getStateForPlacement
    setStates(p.region, p.pos, {{state::Facing, dirName(lookDir(p.c.player))}, {state::Half, clickedTop(p) ? "top" : "bottom"}});
    if (auto const* b = blockAt(p.region, p.pos)) setStates(p.region, p.pos, {{state::Shape, stairsShape(p.region, p.pos, *b)}});
}

void placeSlab(Placing const& p) {
    // Java SlabBlock.getStateForPlacement (a second slab into a single one: Shapes.cpp's use-on)
    setStates(p.region, p.pos, {{state::Type, clickedTop(p) ? "top" : "bottom"}});
}

void placeGate(Placing const& p) {
    // Java FenceGateBlock.getStateForPlacement
    bool power = hasNeighborSignal(p.region, p.pos);
    setStates(p.region, p.pos, {{state::Facing, dirName(lookDir(p.c.player))}, {state::Open, power}, {state::Powered, power}});
    updateShape(p.region, p.pos);
}

void placeTrapdoor(Placing const& p) {
    // Java TrapDoorBlock.getStateForPlacement
    Json s;
    if (!p.replaceClicked && horizontal(p.c.face)) {
        s = {{state::Facing, dirName(p.c.face)}, {state::Half, p.relY > 0.5 ? "top" : "bottom"}};
    } else {
        s = {{state::Facing, dirName(opposite(lookDir(p.c.player)))}, {state::Half, p.c.face == Up ? "bottom" : "top"}};
    }
    if (hasNeighborSignal(p.region, p.pos)) {
        s[state::Open]    = true;
        s[state::Powered] = true;
    }
    setStates(p.region, p.pos, s);
}

void placeButton(Placing const& p) {
    // Java FaceAttachedHorizontalDirectionalBlock.getStateForPlacement: the first looking
    // direction it can hang on
    for (int d : placeLooking(p)) {
        std::string face;
        int         facing;
        if (!horizontal(d)) {
            face   = d == Up ? "ceiling" : "floor";
            facing = lookDir(p.c.player);
        } else {
            face   = "wall";
            facing = opposite(d);
        }
        if (detail::buttonCanSurvive(p.region, p.pos, face, facing)) {
            setStates(p.region, p.pos, {{state::Face, face}, {state::Facing, dirName(facing)}});
            return;
        }
    }
    refuse(p, "nothing to hang on");
}

void placePillar(Placing const& p, Entry const& e) {
    // Java RotatedPillarBlock.getStateForPlacement: the clicked face's axis
    Json s{{state::Axis, axisName(axisOf(p.c.face >= 0 && p.c.face < 6 ? p.c.face : Up))}};
    // upstream DirectionalRotatedPillarBlock: reversed when the clicked face points to the negative side
    if (e.str("class") == "DirectionalRotatedPillarBlock") s["gloamingforest:reversed"] = p.c.face == Down || p.c.face == North || p.c.face == West;
    setStates(p.region, p.pos, s);
}

void placeConnectablePillar(Placing const& p, Entry const& e) {
    // upstream ConnectableRotatedPillarBlock.getStateForPlacement: the axis and the four sides (up and
    // down follow in updateShape)
    int  axis = axisOf(p.c.face >= 0 && p.c.face < 6 ? p.c.face : Up);
    Json s{{state::Axis, axisName(axis)}};
    for (int d : kHorizontal) s[state::side(d)] = detail::pillarConnects(p.region, p.pos, d, e, axis);
    setStates(p.region, p.pos, s);
}

void placeHorizontal(Placing const& p, Entry const& e) {
    // GloamingHorizontalBlock / MiniatureStructureBlock: facing the player (the opposite of the look);
    // BanisterBlock: the look direction (its shape is the owner's); params.placement overrides
    auto cls  = e.str("class");
    auto mode = e.str("placement", cls == "BanisterBlock" ? "look" : (cls == "ClimbableHollowLogBlock" ? "none" : "opposite"));
    if (mode == "none") return;
    int look = lookDir(p.c.player);
    setStates(p.region, p.pos, {{state::Facing, dirName(mode == "look" ? look : opposite(look))}});
}

void placeDirectional(Placing const& p, Entry const& e) {
    // EtchedNagastoneBlock: the nearest looking direction; StrongholdShieldBlock: the same with
    // up / down turned over; Java DirectionalBlock (DispenserBlock, ...): the opposite of the
    // nearest looking direction. BlockPlaceContext.getNearestLookingDirection is the player's look
    // alone (Direction.orderedByNearest(player)[0]), not the clicked face's opposite first.
    auto cls  = e.str("class");
    auto mode = e.str("placement", cls == "EtchedNagastoneBlock" ? "nearest" : (cls == "StrongholdShieldBlock" ? "nearest_y_flipped" : "nearest_opposite"));
    if (mode == "none") return;
    int d = nearestLooking(p.c.player)[0];
    if (mode == "nearest_opposite") d = opposite(d);
    else if (mode == "nearest_y_flipped" && !horizontal(d)) d = opposite(d);
    setStates(p.region, p.pos, {{state::Facing, dirName(d)}});
}

void placeSnowy(Placing const& p) {
    // GloamingBushBlock.getStateForPlacement: placed into one layer of snow, it keeps the layer
    auto const* was = detail::replacedBlock(p.c.pos);
    if (!was || was->getTypeName() != "minecraft:snow_layer") return;
    if (intState(*was, "height").value_or(-1) == 0) setStates(p.region, p.pos, {{state::Layers, 1}});
}

void onPlace(ia::PlaceContext const& c) {
    auto const* e = entryOf(c.block);
    if (!e) return;
    auto* region = regionOf(c.pos.dim);
    if (!region) return;
    BlockPos bp   = game::blockPos(c.pos);
    double   absY = c.clicked.y + c.hit[1];
    Placing  p{c, *region, bp, absY - c.pos.y, c.clicked == c.pos};
    switch (e->family) {
    case Family::Stairs: placeStairs(p); break;
    case Family::Slab: placeSlab(p); break;
    case Family::Fence:
    case Family::Pane:
    case Family::Wall:
    case Family::ConnectedTexture: updateShape(*region, bp); break;
    case Family::FenceGate: placeGate(p); break;
    case Family::Door: placeDoor(c.pos, &c.player, c.clicked.x + c.hit[0], c.clicked.z + c.hit[2]); break;
    case Family::Trapdoor: placeTrapdoor(p); break;
    case Family::Button: placeButton(p); break;
    case Family::PressurePlate:
        if (!detail::plateCanSurvive(*region, bp)) refuse(p, "nothing to stand on");
        break;
    case Family::Pillar:
    case Family::Log:
    case Family::Wood: placePillar(p, *e); break;
    case Family::WallPillar:
    case Family::ConnectablePillar: placeConnectablePillar(p, *e); break;
    case Family::Horizontal: placeHorizontal(p, *e); break;
    case Family::Directional: placeDirectional(p, *e); break;
    case Family::BinaryRotated:
        // upstream BinaryRotatedBlock: rotated when the player looks along z
        setStates(*region, bp, {{state::Rotated, axisOf(lookDir(c.player)) == 2}});
        break;
    case Family::Leaves:
        // Java LeavesBlock.getStateForPlacement: persistent, its distance
        setStates(*region, bp, {{state::Persistent, true}});
        updateLeafDistance(*region, bp);
        break;
    case Family::Chest:
    case Family::TrappedChest: detail::placeChest(c); break;
    case Family::Sign:
    case Family::WallSign:
    case Family::HangingSign:
    case Family::WallHangingSign: detail::placeSign(c); break;
    default: break;
    }
    if (e->has(Snowloggable) && blockAt(*region, bp) && entryOf(*blockAt(*region, bp)) == e) placeSnowy(p);
}

} // namespace

void installPlacement() { handlerIds().push_back(ia::onPlace({}, onPlace)); }

void uninstallPlacement() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
