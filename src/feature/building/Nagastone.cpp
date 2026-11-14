// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/NagastoneBlock.java
//   src/main/java/gloamingforest/enums/NagastoneVariant.java
//   src/main/java/gloamingforest/block/SpiralBrickBlock.java
//   src/main/java/gloamingforest/enums/Diagonals.java
//   src/main/java/gloamingforest/block/IronLadderBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the naga courtyard's nagastone (its variant follows the neighbouring nagastone and
// the heads facing it), the spiral bricks' placement and the iron ladder (LadderBlock placement and
// survival, the left / right connections of IronLadderBlock#updateShape).

#include "feature/building/Building.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::building {

namespace ia = interactions;

namespace {

constexpr char const* kNagastone = "gloamingforest:nagastone";
constexpr char const* kHead      = "gloamingforest:nagastone_head";
constexpr char const* kSpiral    = "gloamingforest:spiral_bricks";
constexpr char const* kLadder    = "gloamingforest:iron_ladder";

std::vector<ia::HandlerId>& handlers() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

std::string ns(char const* s) { return std::string(kNs) + s; }

} // namespace

std::string nagastoneVariant(int first, int second) {
    // NagastoneVariant.getVariantFromDoubleFacing
    if (first == -1) return "solid";
    if (second == -1) second = first;
    if (axisOf(first) == axisOf(second)) return std::string("axis_") + axisName(axisOf(first));
    // one vertical and one horizontal: the elbow; two horizontal on different axes: solid
    int vertical = !horizontal(first) ? first : (!horizontal(second) ? second : -1);
    int side     = horizontal(first) ? first : (horizontal(second) ? second : -1);
    if (vertical < 0 || side < 0) return "solid";
    return std::string(dirName(side)) + (vertical == Up ? "_up" : "_down");
}

std::string nagastoneVariantAt(BlockSource& region, BlockPos const& pos) {
    // NagastoneBlock#getVariant: up to two connections in Direction.values() order (D U N S W E)
    int facings[2]{-1, -1};
    int count = 0;
    for (int side = 0; side < 6 && count < 2; ++side) {
        auto const* n = blockAt(region, relative(pos, side));
        if (!n) continue;
        auto id = idOf(*n);
        if (id == kNagastone || (id == kHead && dirFromName(stateOf(*n, ns("facing"))) == side)) facings[count++] = side;
    }
    return count == 0 ? "solid" : nagastoneVariant(facings[0], count == 2 ? facings[1] : -1);
}

std::string spiralDiagonal(bool sneaking, int horizontalDir, int playerDir, bool upper) {
    if (sneaking) {
        // convertVerticalDirectionToDiagonal
        switch (horizontalDir) {
        case South: return "bottom_left";
        case East: return "top_left";
        case West: return "bottom_right";
        default: return "top_right";
        }
    }
    // getHorizontalDiagonalFromPlayerPlacement: Diagonals.getDiagonalFromUpDownLeftRight(isLeft, upper)
    bool left = (horizontalDir == North || horizontalDir == East) ? playerDir == horizontalDir : playerDir != horizontalDir;
    if (upper) return left ? "top_left" : "top_right";
    return left ? "bottom_left" : "bottom_right";
}

namespace {

void updateNagastone(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    if (!b || idOf(*b) != kNagastone) return;
    setStates(region, pos, {{ns("variant"), nagastoneVariantAt(region, pos)}});
}

/// LadderBlock#canSurvive: the block behind (opposite the facing) has a sturdy face towards it.
bool ladderSurvives(BlockSource& region, BlockPos const& pos, int facing) {
    return sturdy(region, relative(pos, opposite(facing)), facing);
}

/// IronLadderBlock#updateShape's left / right.
void updateLadderSides(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    if (!b || idOf(*b) != kLadder) return;
    int  facing = dirFromName(stateOf(*b, ns("facing")));
    if (facing < 2) return;
    auto sameLadder = [&](int side) {
        auto const* n = blockAt(region, relative(pos, side));
        return n && idOf(*n) == kLadder && dirFromName(stateOf(*n, ns("facing"))) == facing;
    };
    setStates(region, pos, {{ns("left"), sameLadder(counterClockWise(facing))}, {ns("right"), sameLadder(clockWise(facing))}});
}

void placeLadder(ia::PlaceContext const& c) {
    // LadderBlock#getStateForPlacement: the clicked side when it is horizontal, else the first
    // horizontal looking direction the ladder can hang on (facing its opposite)
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(c.pos);
    int      facing = -1;
    bool     replaceClicked = c.clicked == c.pos;
    if (!replaceClicked && horizontal(c.face) && ladderSurvives(*region, pos, c.face)) facing = c.face;
    if (facing < 0) {
        auto dirs = nearestLooking(c.player);
        if (!replaceClicked && c.face >= 0 && c.face < 6) {
            int want = opposite(c.face), i = 0;
            while (i < 6 && dirs[i] != want) ++i;
            for (int j = i; j > 0 && j < 6; --j) dirs[j] = dirs[j - 1];
            if (i < 6) dirs[0] = want;
        }
        for (int d : dirs) {
            if (!horizontal(d)) continue;
            if (ladderSurvives(*region, pos, opposite(d))) {
                facing = opposite(d);
                break;
            }
        }
    }
    if (facing < 0) {
        // getStateForPlacement returned null: nothing placed, the item stays
        game::clearBlock(*region, pos);
        if (!c.player.isCreative())
            if (auto item = game::items::make(kLadder, 1)) give(c.player, *item);
        return;
    }
    setStates(*region, pos, {{ns("facing"), dirName(facing)}});
}

void placeSpiral(ia::PlaceContext const& c) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos     = game::blockPos(c.pos);
    int      facing  = horizontalFacing(c.player); // BlockPlaceContext#getHorizontalDirection
    double   clickY  = c.clicked.y + c.hit[1];
    bool     upper   = clickY - pos.y > 0.5;
    auto     diag    = spiralDiagonal(c.sneaking, facing, horizontalFacing(c.player), upper);
    char const* axis = c.sneaking ? "y" : axisName(axisOf(facing));
    setStates(*region, pos, {{ns("axis"), axis}, {ns("diagonal"), diag}});
}

} // namespace

void installNagastone() {
    handlers().push_back(ia::onPlace({.block = kNagastone}, [](ia::PlaceContext const& c) {
        if (auto* region = game::regionOf(c.pos.dim)) updateNagastone(*region, game::blockPos(c.pos));
    }));
    // updateShape: every neighbour change (of nagastone and heads alike)
    blockevents::onNeighborChanged(kNagastone, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { updateNagastone(region, pos); });
    handlers().push_back(ia::onPlace({.block = kSpiral}, placeSpiral));
    handlers().push_back(ia::onPlace({.block = kLadder}, placeLadder));
    blockevents::onNeighborChanged(kLadder, [](BlockSource& region, BlockPos const& pos, BlockPos const& neighbor) {
        auto const* b = blockAt(region, pos);
        if (!b || idOf(*b) != kLadder) return;
        int facing = dirFromName(stateOf(*b, ns("facing")));
        // LadderBlock#updateShape: the support behind gone -> the ladder breaks (with its drop)
        if (facing >= 2 && neighbor == relative(pos, opposite(facing)) && !ladderSurvives(region, pos, facing)) {
            game::clearBlock(region, pos);
            if (auto item = game::items::make(kLadder, 1)) popResource(region, pos, *item);
            log().info("building: iron ladder at {} {} {} lost its support", pos.x, pos.y, pos.z);
            return;
        }
        updateLadderSides(region, pos);
    });
}

void uninstallNagastone() {
    for (auto id : handlers()) ia::remove(id);
    handlers().clear();
}

} // namespace gf::feature::building
