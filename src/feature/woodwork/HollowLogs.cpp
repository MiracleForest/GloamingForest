// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/HorizontalHollowLogBlock.java
//   src/main/java/twilightforest/block/VerticalHollowLogBlock.java
//   src/main/java/twilightforest/block/ClimbableHollowLogBlock.java
//   src/main/java/twilightforest/item/HollowLogItem.java
//   src/main/java/twilightforest/enums/HollowLogVariants.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Hollow logs (18 woods x horizontal, vertical, climbable). The item (HollowLogItem) is a BP block
// placer of the vertical log; a placement on a side face becomes the horizontal log here
// (getPlacementState by the clicked face's axis). Java's `waterlogged` / variant `waterlogged` /
// `ladder_waterlogged` are Bedrock's water layer (liquid detection); the variants follow it where
// Java's setWaterlog would change them. Shapes and collisions are the blocks' permutations
// (tools/import/overrides/woodwork.json); the climbable block is climbable through F11's
// climbable modifier (assets/data/families: BlockProperty::Climbable on the server and in the
// client mod).

#include "feature/woodwork/Woodwork.h"

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::woodwork {

namespace ia = interactions;

namespace {

constexpr char kAxis[]    = "twilightforest:axis";
constexpr char kVariant[] = "twilightforest:variant";
constexpr char kFacing[]  = "twilightforest:facing";

constexpr char kMossPatch[]  = "twilightforest:moss_patch";
constexpr char kShortGrass[] = "minecraft:short_grass";

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

bool isHorizontalFace(int face) { return face >= 2 && face <= 5; }

/// HorizontalHollowLogBlock.isInside: 2..14 px on y and on the cross axis.
bool insideHorizontal(ia::Vec const& hit, std::string const& axis) {
    if (0.124 > hit[1] || hit[1] > 0.876) return false;
    if (axis == "x") return 0.124 <= hit[2] && hit[2] <= 0.876;
    if (axis == "z") return 0.124 <= hit[0] && hit[0] <= 0.876;
    return false;
}

/// VerticalHollowLogBlock / ClimbableHollowLogBlock.isInside: 2..14 px on x and z.
bool insideVertical(ia::Vec const& hit) { return 0.124 <= hit[0] && hit[0] <= 0.876 && 0.124 <= hit[2] && hit[2] <= 0.876; }

/// HorizontalHollowLogBlock.canChangeVariant: empty, or waterlogged with no water at either end.
bool canChangeVariant(BlockSource& region, BlockPos const& pos, std::string const& variant, std::string const& axis) {
    if (variant == "empty") return true;
    if (variant != "waterlogged") return false;
    BlockPos neg = axis == "x" ? BlockPos{pos.x - 1, pos.y, pos.z} : BlockPos{pos.x, pos.y, pos.z - 1};
    BlockPos posi = axis == "x" ? BlockPos{pos.x + 1, pos.y, pos.z} : BlockPos{pos.x, pos.y, pos.z + 1};
    return !waterSourceAt(region, neg) && !waterSourceAt(region, posi);
}

/// HorizontalHollowLogBlock.playPlaceSound / playBreakSound: the block's sound, volume (v + 1) / 2,
/// pitch p * 0.8 (every sound type used here has volume 1 and pitch 1).
void blockSound(BlockSource& region, BlockPos const& pos, char const* bedrockName) { playSound(region, pos, bedrockName, 1.0f, 0.8f); }

void dropAt(BlockSource& region, BlockPos const& pos, char const* itemId) {
    if (auto s = game::items::make(itemId)) spawnItem(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, *s);
}

void setState(BlockSource& region, BlockPos const& pos, Block const& block, Json const& changes) {
    if (auto const* b = withStates(block, changes)) game::setBlock(region, pos, *b);
}

/// Puts the water layer back into a block that keeps it (Java: the waterlogged state carries over).
void putWater(BlockSource& region, BlockPos const& pos) {
    if (auto const* water = game::blockWith("minecraft:water")) region.setExtraBlock(pos, *water, 3);
}

// ---- HollowLogItem -------------------------------------------------------------------------------------

/// HollowLogItem.getPlacementState: the BP item places the vertical log; a side face makes it the
/// horizontal one (its axis the face's, `waterlogged` in water).
void onPlaceVertical(ia::PlaceContext const& c) {
    auto const* set = hollowLogOf(c.blockId);
    if (!set || c.blockId != set->vertical) return;
    if (!isHorizontalFace(c.face)) return;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(c.pos);
    if (idOf(region->getBlock(pos)) != set->vertical) return;
    bool        wet = waterSourceAt(*region, pos);
    std::string axis = (c.face == 4 || c.face == 5) ? "x" : "z";
    if (auto const* h = game::blockWithStates(set->horizontal, {{kAxis, axis}, {kVariant, wet ? "waterlogged" : "empty"}})) {
        game::setBlock(*region, pos, *h);
        if (wet) putWater(*region, pos);
    }
}

// ---- HorizontalHollowLogBlock --------------------------------------------------------------------------

ia::Result onUseHorizontal(ia::UseOnContext const& c) {
    auto axis = stateOf(c.block, kAxis);
    if (!insideHorizontal(c.hit, axis)) return ia::Result::Pass;
    auto& region  = c.player.getDimensionBlockSource();
    auto  pos     = game::blockPos(c.pos);
    auto  variant = stateOf(c.block, kVariant);
    auto  pass    = [&] { return ia::Result::Pass; };
    if (c.itemId == kMossPatch) {
        if (!canChangeVariant(region, pos, variant, axis)) return pass();
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        setState(region, pos, c.block, {{kVariant, "moss"}});
        blockSound(region, pos, "dig.moss"); // MOSS_CARPET place
        consumeOne(c.player, c.item);
        log().info("woodwork: {} put moss into {}", c.player.getRealName(), c.blockId);
        return ia::Result::Consume;
    }
    if (c.itemId == kShortGrass || c.itemId == "minecraft:tallgrass") {
        if (variant != "moss") return pass();
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        setState(region, pos, c.block, {{kVariant, "moss_and_grass"}});
        blockSound(region, pos, "dig.grass"); // GRASS place
        consumeOne(c.player, c.item);
        return ia::Result::Consume;
    }
    if (c.itemId == "minecraft:snowball") {
        if (!canChangeVariant(region, pos, variant, axis)) return pass();
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        setState(region, pos, c.block, {{kVariant, "snow"}});
        blockSound(region, pos, "dig.snow"); // SNOW place
        consumeOne(c.player, c.item);
        return ia::Result::Consume;
    }
    if (isShovel(c.itemId)) {
        if (variant != "snow") return pass();
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        setState(region, pos, c.block, {{kVariant, "empty"}});
        blockSound(region, pos, "dig.snow"); // SNOW break
        if (!c.player.isCreative()) {
            hurtTool(c.player, c.item);
            dropAt(region, pos, "minecraft:snowball");
        }
        return ia::Result::Consume;
    }
    if (isShears(c.itemId)) {
        if (variant != "moss" && variant != "moss_and_grass") return pass();
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        setState(region, pos, c.block, {{kVariant, "empty"}});
        playSound(region, pos, "entity.sheep.shear", 1.0f, 1.0f);
        if (!c.player.isCreative()) {
            hurtTool(c.player, c.item);
            dropAt(region, pos, kMossPatch);
            if (variant == "moss_and_grass") dropAt(region, pos, kShortGrass);
        }
        return ia::Result::Consume;
    }
    // useWithoutItem: sneaking with an empty hand inside the log, facing along it, at its height,
    // within 0.81 of its bottom centre: Java sets the swimming (crawling) pose. Bedrock has no pose
    // a server can force on a player; the player crawls through the 14 px tunnel with the client
    // mod's movement only (docs/units/woodwork.md).
    return pass();
}

// ---- VerticalHollowLogBlock ----------------------------------------------------------------------------

ia::Result onUseVertical(ia::UseOnContext const& c) {
    if (!insideVertical(c.hit)) return ia::Result::Pass;
    auto const* set = hollowLogOf(c.blockId);
    if (!set) return ia::Result::Pass;
    bool vine   = c.itemId == "minecraft:vine";
    bool ladder = c.itemId == "minecraft:ladder";
    if (!vine && !ladder) return ia::Result::Pass;
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    auto& region = c.player.getDimensionBlockSource();
    auto  pos    = game::blockPos(c.pos);
    bool  wet    = waterSourceAt(region, pos);
    // directionUtil.horizontalOrElse(hit face, the player's direction's opposite)
    int         facing  = isHorizontalFace(c.face) ? c.face : oppositeFace(lookFace(c.player));
    std::string variant = vine ? "vine" : (wet ? "ladder_waterlogged" : "ladder");
    auto const* b       = game::blockWithStates(set->climbable, {{kVariant, variant}, {kFacing, faceName(facing)}});
    if (!b) return ia::Result::Pass;
    game::setBlock(region, pos, *b);
    if (variant == "ladder_waterlogged") putWater(region, pos);
    blockSound(region, pos, vine ? "dig.grass" : "dig.wood"); // VINE / LADDER place
    consumeOne(c.player, c.item);
    log().info("woodwork: {} made {} {}", c.player.getRealName(), set->climbable, variant);
    return ia::Result::Consume;
}

// ---- ClimbableHollowLogBlock ---------------------------------------------------------------------------

ia::Result onUseClimbable(ia::UseOnContext const& c) {
    if (!insideVertical(c.hit) || !isShears(c.itemId)) return ia::Result::Pass;
    auto const* set = hollowLogOf(c.blockId);
    if (!set) return ia::Result::Pass;
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    auto& region  = c.player.getDimensionBlockSource();
    auto  pos     = game::blockPos(c.pos);
    auto  variant = stateOf(c.block, kVariant);
    if (auto const* v = game::blockWith(set->vertical)) {
        game::setBlock(region, pos, *v);
        if (variant == "ladder_waterlogged") putWater(region, pos);
    }
    playSound(region, pos, "entity.sheep.shear", 1.0f, 1.0f);
    if (!c.player.isCreative()) {
        hurtTool(c.player, c.item);
        dropAt(region, pos, variant == "vine" ? "minecraft:vine" : "minecraft:ladder");
    }
    log().info("woodwork: {} sheared {} back to {}", c.player.getRealName(), c.blockId, set->vertical);
    return ia::Result::Consume;
}

// ---- water (WaterloggedBlock.setWaterlog) --------------------------------------------------------------

/// A bucket emptied into or filled from a hollow log: the variant follows the water layer on the
/// next tick (HorizontalHollowLogBlock: empty <-> waterlogged; ClimbableHollowLogBlock: a vine
/// variant becomes a waterlogged vertical log, ladder <-> ladder_waterlogged).
ia::Result onBucket(ia::UseOnContext const& c) {
    if (c.itemId != "minecraft:water_bucket" && c.itemId != "minecraft:bucket") return ia::Result::Pass;
    auto gp = c.pos;
    ia::nextTick([gp] {
        auto* region = game::regionOf(gp.dim);
        if (!region) return;
        BlockPos     pos   = game::blockPos(gp);
        Block const& block = region->getBlock(pos);
        auto         id    = idOf(block);
        auto const*  set   = hollowLogOf(id);
        if (!set) return;
        bool wet     = waterSourceAt(*region, pos);
        auto variant = stateOf(block, kVariant);
        if (id == set->horizontal) {
            if (wet && variant == "empty") setState(*region, pos, block, {{kVariant, "waterlogged"}});
            else if (!wet && variant == "waterlogged") setState(*region, pos, block, {{kVariant, "empty"}});
        } else if (id == set->climbable) {
            if (wet && variant == "vine") {
                if (auto const* v = game::blockWith(set->vertical)) {
                    game::setBlock(*region, pos, *v);
                    putWater(*region, pos);
                }
            } else if (wet && variant == "ladder") {
                setState(*region, pos, block, {{kVariant, "ladder_waterlogged"}});
            } else if (!wet && variant == "ladder_waterlogged") {
                setState(*region, pos, block, {{kVariant, "ladder"}});
            }
        }
    });
    return ia::Result::Pass;
}

} // namespace

void installHollowLogs() {
    for (auto const& h : hollowLogs()) {
        handlerIds().push_back(ia::onPlace({.block = h.vertical}, onPlaceVertical));
        handlerIds().push_back(ia::onUseOn({.block = h.horizontal}, onUseHorizontal));
        handlerIds().push_back(ia::onUseOn({.block = h.vertical}, onUseVertical));
        handlerIds().push_back(ia::onUseOn({.block = h.climbable}, onUseClimbable));
        for (auto const* id : {&h.horizontal, &h.climbable}) handlerIds().push_back(ia::onUseOn({.block = *id}, onBucket, ia::priority::High));
    }
}

void uninstallHollowLogs() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::woodwork
