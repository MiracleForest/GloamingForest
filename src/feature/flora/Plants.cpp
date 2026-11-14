// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/GloamingPlantBlock.java
//   src/main/java/gloamingforest/block/PatchBlock.java
//   src/main/java/gloamingforest/block/MossPatchBlock.java
//   src/main/java/gloamingforest/block/MayappleBlock.java
//   src/main/java/gloamingforest/block/FiddleheadBlock.java
//   src/main/java/gloamingforest/block/MushgloomBlock.java
//   src/main/java/gloamingforest/block/FallenLeavesBlock.java
//   src/main/java/gloamingforest/block/RootStrandBlock.java
//   src/main/java/gloamingforest/block/TorchberryPlantBlock.java
//   src/main/java/gloamingforest/block/LiverootBlock.java
//   src/main/java/gloamingforest/block/HugeLilyPadBlock.java
//   src/main/java/gloamingforest/block/HugeWaterLilyBlock.java
//   src/main/java/gloamingforest/block/TrollRootBlock.java (canSurvive of the roots U6's feature places)
//   src/main/java/gloamingforest/item/HugeLilyPadItem.java
//   src/main/java/gloamingforest/item/EssenceBerryItem.java
//   src/main/java/gloamingforest/item/StackableEffectItem.java
//   src/main/java/gloamingforest/init/GloamingItems.java (fallen leaves item, torchberries)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora: the small plants. Custom Bedrock blocks get no Java callbacks, so the canSurvive rules
// of upstream's BushBlocks run here: a placement where the plant can't stay is undone (BlockItem#canPlace),
// a plant whose support goes pops with its drops (BushBlock#updateShape), and world generation
// sweeps the plants the vanilla features of the features contract placed without asking them
// (SimpleBlockFeature asks BlockState#canSurvive, which custom blocks do not answer).

#include "feature/flora/Flora.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"

#include "core/flora/FloraMath.h"
#include "feature/common/Interactions.h"
#include "feature/effects/Effects.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/net/Net.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::flora {

namespace ia = interactions;

namespace {

bool isTrollRoot(std::string const& id) {
    return id == "gloamingforest:trollvidr" || id == "gloamingforest:trollber" || id == "gloamingforest:unripe_trollber";
}

/// Java BushBlock#mayPlaceOn: #minecraft:dirt or farmland.
bool bushGround(Block const& below) { return tagged(below, "minecraft:dirt") || below.getTypeName() == "minecraft:farmland"; }

bool waterBlock(Block const& b) { return b.getTypeName() == "minecraft:water"; }

bool iceBlock(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:ice" || n == "minecraft:packed_ice" || n == "minecraft:blue_ice" || n == "minecraft:frosted_ice";
}

/// Java's sturdy up face of a block (Block.isFaceSturdy(UP, FULL)).
bool sturdyUp(Block const& b) { return !b.isAir() && b.canProvideSupport(1, BlockSupportType::Edge); }

/// The neighbourhood of a block as a world or a world generation region sees it.
struct View {
    std::function<Block const*(BlockPos const&)> block;
    std::function<bool(BlockPos const&)>         water;  ///< Fluids.WATER source (or a water layer) at the position
    std::function<bool(BlockPos const&)>         fluid;  ///< any fluid at the position
};

/// Java canSurvive of the flora block `id` at `pos` (nullopt: not one of ours).
std::optional<bool> survivesWith(View const& v, BlockPos const& pos, std::string const& id, Block const& block) {
    auto below = [&] { return v.block(relative(pos, Down)); };
    auto above = [&] { return v.block(relative(pos, Up)); };
    if (id == ids::Mayapple || id == ids::Fiddlehead || id == ids::CloverPatch) {
        auto const* b = below();
        return b && bushGround(*b);
    }
    if (id == ids::MossPatch) {
        auto const* b = below();
        return b && sturdyUp(*b);
    }
    if (id == ids::Mushgloom) {
        auto const* b = below();
        return b && (sturdyUp(*b) || b->getTypeName() == ids::UberousSoil);
    }
    if (id == ids::FallenLeaves) {
        auto const* b = below();
        return b && (sturdyUp(*b) || v.water(relative(pos, Down)));
    }
    if (id == ids::RootStrand) {
        auto const* a = above();
        return a && (tagged(*a, "gloamingforest:plants_hang_on") || a->getTypeName() == ids::RootStrand);
    }
    if (id == ids::TorchberryPlant) {
        auto const* a = above();
        return a && tagged(*a, "gloamingforest:plants_hang_on");
    }
    if (isTrollRoot(id)) {
        // TrollRootBlock.canPlaceRootBelow(pos.above())
        auto const* a = above();
        return a && (tagged(*a, "minecraft:base_stone_overworld") || isTrollRoot(a->getTypeName()));
    }
    if (id == ids::HugeLilyPad || id == ids::HugeWaterLily) {
        // WaterlilyBlock#mayPlaceOn: water (or ice) below, no fluid above it
        auto const* b = below();
        return b && (v.water(relative(pos, Down)) || iceBlock(*b)) && !v.fluid(pos);
    }
    if (critters::isCritter(id)) {
        int facing = dirFromName(stateOf(block, st::Facing));
        if (facing < 0) facing = Up;
        auto const* rest = v.block(relative(pos, opposite(facing)));
        if (!rest || rest->isAir()) return false;
        return rest->canProvideSupport(static_cast<uchar>(facing), BlockSupportType::Center) || features::kit::isLeaves(*rest);
    }
    return std::nullopt;
}

} // namespace


std::optional<bool> survives(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto const& id = block.getTypeName();
    if (bushes::isBush(id)) return bushes::canSurvive(region, pos, block);
    View v{[&](BlockPos const& p) { return blockAt(region, p); }, [&](BlockPos const& p) { return hasWater(region, p); },
           [&](BlockPos const& p) { return hasFluid(region, p); }};
    return survivesWith(v, pos, id, block);
}

std::optional<bool> survivesIn(worldgen::Region& region, BlockPos const& pos, Block const& block) {
    auto const& id = block.getTypeName();
    if (bushes::isBush(id)) {
        // GloamingBushBlock#canSurvive without the light (the bush features place grown bushes by tag)
        auto const& below = region.getBlock(relative(pos, Down));
        auto        kind  = bushes::kindOf(id);
        char const* tag   = kind == bushes::Kind::Overworld ? "gloamingforest:gf_berry_bushes_survive"
                          : kind == bushes::Kind::DarkTower ? "gloamingforest:dark_tower_berry_bushes_survive"
                                                            : "gloamingforest:oreberry_bushes_survive";
        return features::kit::hasTag(below, tag) || (below.getTypeName() == id && intState(below, st::Age, 0) >= 2);
    }
    auto inside = [&](BlockPos const& p) { return p.y >= region.minY() && p.y < region.maxY() && region.isInside(p.x, p.z); };
    View v{[&](BlockPos const& p) -> Block const* { return inside(p) ? &region.getBlock(p) : nullptr; },
           [&](BlockPos const& p) { return inside(p) && features::kit::isWater(region.getBlock(p)); },
           [&](BlockPos const& p) { return inside(p) && features::kit::isFluid(region.getBlock(p)); }};
    return survivesWith(v, pos, id, block);
}

namespace {

// ---- placement and support ------------------------------------------------------------------------------

/// The flora blocks that pop when their support goes (BushBlock#updateShape, CritterBlock#updateShape).
constexpr std::array<char const*, 10> kSupported{ids::Mayapple,  ids::Fiddlehead,   ids::CloverPatch, ids::MossPatch,   ids::Mushgloom,
                                                  ids::FallenLeaves, ids::RootStrand, ids::TorchberryPlant, ids::HugeLilyPad, ids::HugeWaterLily};

void checkSupport(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b) return;
    auto ok = survives(region, pos, *b);
    if (!ok || *ok) return;
    destroyBlock(region, pos, true);
}

/// BlockItem#canPlace: the placed plant stays only where it survives (else undone, item refunded).
void undoUnsupportedPlacement(ia::PlaceContext const& c) {
    auto&        region = c.player.getDimensionBlockSource();
    BlockPos     pos    = game::blockPos(c.pos);
    Block const* b      = blockAt(region, pos);
    if (!b) return;
    auto ok = survives(region, pos, *b);
    if (!ok || *ok) return;
    removeBlock(region, pos);
    if (!c.player.isCreative()) {
        if (auto s = makeItem(c.itemId.empty() ? b->getTypeName() : c.itemId)) give(c.player, *s);
    }
}

// ---- patches -----------------------------------------------------------------------------------------

/// PatchBlock#updateShape: a side is connected when the neighbour is the same patch.
void updatePatch(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b) return;
    auto const& id = b->getTypeName();
    if (id != ids::MossPatch && id != ids::CloverPatch) return;
    auto same = [&](int d) { return idAt(region, relative(pos, d)) == id; };
    setStates(region, pos, {{st::North, same(North)}, {st::South, same(South)}, {st::West, same(West)}, {st::East, same(East)}});
}

// ---- fallen leaves ---------------------------------------------------------------------------------------

int layersOf(Block const& b) { return std::max(1, intState(b, st::Layers, 1)); }

/// A ray from the eyes along the look (Item#getPlayerPOVHitResult with ClipContext.Fluid.SOURCE_ONLY,
/// reach 4.5 / 5 in creative): the first water source or block that is not air.
std::optional<std::pair<BlockPos, int>> clipSourceOnly(Player& player) {
    auto&  region = player.getDimensionBlockSource();
    Vec3   eye    = player.getEyePos();
    Vec3   dir    = player.getViewVector(1.0f);
    double reach  = player.isCreative() ? 5.0 : 4.5;
    BlockPos last{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    for (double t = 0.0; t <= reach; t += 0.02) {
        BlockPos p{static_cast<int>(std::floor(eye.x + dir.x * t)), static_cast<int>(std::floor(eye.y + dir.y * t)), static_cast<int>(std::floor(eye.z + dir.z * t))};
        if (p == last && t > 0.0) continue;
        Block const* b = blockAt(region, p);
        if (!b) return std::nullopt;
        bool source = waterBlock(*b) && intState(*b, "liquid_depth", 0) == 0;
        bool solid  = !b->isAir() && !waterBlock(*b) && b->getTypeName() != "minecraft:flowing_water";
        if (source || solid) {
            // the face entered through: the axis whose cell boundary the ray crossed last
            int face = Up;
            if (p.y != last.y) face = p.y > last.y ? Down : Up;
            else if (p.x != last.x) face = p.x > last.x ? West : East;
            else if (p.z != last.z) face = p.z > last.z ? North : South;
            return std::make_pair(p, face);
        }
        last = p;
    }
    return std::nullopt;
}

/// FallenLeavesBlock#canBeReplaced / getStateForPlacement and BlockItem#place for a fallen leaves
/// item used at `clicked` (`face`); true when it placed.
bool placeFallenLeaves(Player& player, ItemStack const& held, BlockSource& region, BlockPos const& clicked, int face) {
    Block const* c = blockAt(region, clicked);
    if (!c) return false;
    auto canReplace = [&](BlockPos const& p, bool clickedOn) {
        Block const* b = blockAt(region, p);
        if (!b) return false;
        if (b->getTypeName() == ids::FallenLeaves) {
            Block const* under = blockAt(region, relative(p, Down));
            bool         waterBelow = under && (waterBlock(*under) || under->getTypeName() == "minecraft:flowing_water" || hasWater(region, relative(p, Down)));
            if (waterBelow) return false;
            if (layersOf(*b) < 8) return clickedOn ? face == Up : true;
            return false;
        }
        return replaceable(region, p);
    };
    BlockPos target = clicked;
    if (!canReplace(clicked, true)) {
        target = relative(clicked, face);
        if (!canReplace(target, false)) return false;
    }
    Block const* there = blockAt(region, target);
    if (!there) return false;
    if (there->getTypeName() == ids::FallenLeaves) {
        setStates(region, target, {{st::Layers, std::min(8, layersOf(*there) + 1)}});
    } else {
        auto const* leaves = game::blockWithStates(ids::FallenLeaves, {{st::Layers, 1}});
        if (!leaves) return false;
        auto ok = survives(region, target, *leaves);
        if (!ok || !*ok) return false;
        game::setBlock(region, target, *leaves);
    }
    sound(region, target, "block.azalea_leaves.place");
    consumeHeld(player, held);
    return true;
}

/// The players a fallen leaf from `entity` goes to: companions near it (Java's trackers and self).
void sendFallenLeaf(Actor& entity, BlockPos const& pos) {
    auto& level = entity.getLevel();
    Vec3  at    = entity.getPosition();
    Vec3  delta = entity.getPosDelta();
    Json  msg{{"pos", {pos.x, pos.y, pos.z}}, {"motion", {delta.x, delta.y, delta.z}}};
    level.forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (p.getDimensionId() != entity.getDimensionId()) return true;
        Vec3 pp = p.getPosition();
        if ((pp.x - at.x) * (pp.x - at.x) + (pp.z - at.z) * (pp.z - at.z) > 64.0 * 64.0) return true;
        if (net::isCompanion(p)) net::send(p, "flora.fallen_leaf", msg);
        return true;
    }));
}

// ---- berries ---------------------------------------------------------------------------------------

struct StackableEffect {
    char const* effect;      ///< Bedrock / Java effect id
    unsigned    bedrockId;   ///< Bedrock's numeric effect id (the current duration)
    int         extraTicks;  ///< seconds * 20
    float       chance;
};

/// StackableEffectItem#applyOrStackEffect: the duration adds to the active one.
void stackEffect(Actor& actor, StackableEffect const& e) {
    int current = 0;
    if (auto const* active = actor.getEffect(e.bedrockId)) current = active->mDuration->mValue;
    effects::apply(actor, e.effect, current + e.extraTicks, 0);
}

void registerBerry(std::string const& itemId, std::vector<StackableEffect> effects) {
    item::registerItem(itemId, {.finishUsing = [effects = std::move(effects)](Player& player, ItemStack&) {
                           auto& random = levelRandom();
                           for (auto const& e : effects)
                               if (e.chance >= random.nextFloat()) stackEffect(player, e);
                       }});
}

} // namespace

ia::Result useFallenLeavesOn(ia::UseOnContext const& c) {
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    auto&    region  = c.player.getDimensionBlockSource();
    BlockPos clicked = game::blockPos(c.pos);
    if (c.blockId == ids::FallenLeaves) {
        // useOn: the clicked block is fallen leaves (BlockItem#place)
        placeFallenLeaves(c.player, c.item, region, clicked, c.face);
        return ia::Result::Consume;
    }
    // use: the look's hit (water sources too) and the block above it
    auto hit = clipSourceOnly(c.player);
    if (hit) placeFallenLeaves(c.player, c.item, region, relative(hit->first, Up), hit->second);
    return ia::Result::Consume;
}

void installPlants() {
    // ---- support and placement of every plant
    std::set<std::string> supported(kSupported.begin(), kSupported.end());
    for (auto const& id : supported) {
        blockevents::onNeighborChanged(id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { checkSupport(region, pos); });
        if (id != ids::HugeLilyPad && id != ids::FallenLeaves) ia::onPlace({.block = id}, undoUnsupportedPlacement);
    }

    // ---- patches: connections (PatchBlock#updateShape) and the moss patch's mycelium (client animateTick)
    for (char const* id : {ids::MossPatch, ids::CloverPatch}) {
        blockevents::onNeighborChanged(id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { updatePatch(region, pos); });
        blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { updatePatch(region, pos); });
    }

    // ---- fallen leaves: the item (GloamingItems FALLEN_LEAVES BlockItem#useOn / #use)
    // the item places nothing itself (no block placer): blocks that take the click keep it
    ia::onUnusedUseOn({.item = ids::FallenLeaves}, useFallenLeavesOn);
    ia::onUse({.item = ids::FallenLeaves}, [](ia::UseContext const& c) {
        if (c.onBlock) return ia::Result::Pass;
        auto hit = clipSourceOnly(c.player);
        if (!hit) return ia::Result::Pass;
        placeFallenLeaves(c.player, c.item, c.player.getDimensionBlockSource(), relative(hit->first, Up), hit->second);
        return ia::Result::Consume;
    });
    // FallenLeavesBlock#entityInside: slowed above 2 layers; moving living entities kick up leaves
    blockevents::onEntityInside(ids::FallenLeaves, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
        Block const* b = blockAt(region, pos);
        if (!b) return;
        int layers = layersOf(*b);
        if (layers > 2 && !entity.isPlayer()) {
            // makeStuckInBlock(state, (1 - 0.05 * (layers - 2), 1, same)): the horizontal motion
            double f = core::flora::fallenLeavesFactor(layers);
            Vec3   d = entity.getPosDelta();
            const_cast<Vec3&>(entity.getPosDelta()) = Vec3{static_cast<float>(d.x * f), d.y, static_cast<float>(d.z * f)};
        }
        Vec3 d = entity.getPosDelta();
        if (!entity.hasCategory(ActorCategory::Mob) && !entity.isPlayer()) return;
        if ((d.x != 0 || d.z != 0) && levelRandom().nextBoolean()) sendFallenLeaf(entity, pos);
    });

    // ---- root strands: bone meal grows the strand down (RootStrandBlock)
    ia::onUseOn({.block = ids::RootStrand, .item = "minecraft:bone_meal"}, [](ia::UseOnContext const& c) {
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        auto&    region = c.player.getDimensionBlockSource();
        BlockPos p      = game::blockPos(c.pos);
        do {
            p = relative(p, Down);
        } while (idAt(region, p) == ids::RootStrand);
        if (!(isAir(region, p) || replaceable(region, p))) return ia::Result::Pass; // not a valid target: the item's own use
        consumeHeld(c.player, c.item);
        boneMealEffect(region, game::blockPos(c.pos));
        place(region, p, ids::RootStrand);
        return ia::Result::Consume;
    });

    // ---- torchberry plant: harvest (useWithoutItem) and bone meal
    ia::onUseOn({.block = ids::TorchberryPlant}, [](ia::UseOnContext const& c) {
        bool emptyHands = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
        if (c.sneaking && !emptyHands) return ia::Result::Pass;
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        auto&        region = c.player.getDimensionBlockSource();
        BlockPos     pos    = game::blockPos(c.pos);
        Block const* b      = blockAt(region, pos);
        if (!b) return ia::Result::Pass;
        if (boolState(*b, st::Berries)) {
            setStates(region, pos, {{st::Berries, false}});
            sound(region, pos, "block.gloamingforest.torchberry.harvest");
            if (auto s = makeItem(ids::Torchberries)) spawnItemAt(region, pos.x + .5f, static_cast<float>(pos.y), pos.z + .5f, *s);
            stats::award(c.player, "gloamingforest:torchberries_harvested");
            log().info("flora: {} harvested torchberries at {} {} {}", c.player.getRealName(), pos.x, pos.y, pos.z);
            return ia::Result::Consume;
        }
        if (c.itemId == "minecraft:bone_meal") {
            consumeHeld(c.player, c.item);
            boneMealEffect(region, pos);
            setStates(region, pos, {{st::Berries, true}});
            return ia::Result::Consume;
        }
        return ia::Result::Pass;
    });

    // ---- liveroot block: stripping with an axe (LiverootBlock#useItemOn)
    ia::onUseOn({.block = ids::LiverootBlock}, [](ia::UseOnContext const& c) {
        bool axe = tags::has(tags::Registry::Item, "minecraft:axes", c.itemId) || c.itemId.ends_with("_axe");
        if (!axe) return ia::Result::Pass;
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        auto&    region = c.player.getDimensionBlockSource();
        BlockPos pos    = game::blockPos(c.pos);
        place(region, pos, ids::Root);
        int roots   = 1;
        int fortune = enchant::level(c.item, "minecraft:fortune");
        if (fortune > 0) {
            // ApplyBonusCount.OreDrops
            int i = std::max(0, levelRandom().nextInt(fortune + 2) - 1);
            roots *= i + 1;
        }
        if (auto s = makeItem(ids::Liveroot, roots)) spawnItemAt(region, static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z), *s);
        sound(region, pos, "item.axe.strip");
        if (!c.player.isCreative()) {
            ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
            c.player.sendInventory(false);
        }
        log().info("flora: {} stripped a liveroot block at {} {} {} ({} liveroot)", c.player.getRealName(), pos.x, pos.y, pos.z, roots);
        return ia::Result::Consume;
    });

    // ---- huge lily pad: the 2x2 item (HugeLilyPadItem#use) and the pieces breaking together
    auto useLilyPad = [](Player& player, ItemStack const& held) {
        auto hit = clipSourceOnly(player);
        if (!hit) return false;
        auto&    region = player.getDimensionBlockSource();
        BlockPos at     = hit->first;
        auto waterOrIce = [&](BlockPos const& p) {
            Block const* b = blockAt(region, p);
            return b && (hasWater(region, p) || iceBlock(*b));
        };
        auto empty = [&](BlockPos const& p) { return isAir(region, p); };
        BlockPos up = relative(at, Up);
        if (!(waterOrIce(at) && empty(up) && waterOrIce(offset(at, 1, 0, 0)) && empty(offset(up, 1, 0, 0)) && waterOrIce(offset(at, 0, 0, 1))
              && empty(offset(up, 0, 0, 1)) && waterOrIce(offset(at, 1, 0, 1)) && empty(offset(up, 1, 0, 1))))
            return false;
        // Player#getDirection: the horizontal facing of the yaw
        static constexpr char const* kFacing[4]{"south", "west", "north", "east"};
        std::string facing = kFacing[game::playerFacing(player) & 3];
        place(region, up, ids::HugeLilyPad, {{st::Facing, facing}, {st::Piece, "nw"}});
        place(region, offset(up, 1, 0, 0), ids::HugeLilyPad, {{st::Facing, facing}, {st::Piece, "ne"}});
        place(region, offset(up, 1, 0, 1), ids::HugeLilyPad, {{st::Facing, facing}, {st::Piece, "se"}});
        place(region, offset(up, 0, 0, 1), ids::HugeLilyPad, {{st::Facing, facing}, {st::Piece, "sw"}});
        consumeHeld(player, held);
        sound(region, at, "block.lily_pad.place");
        log().info("flora: {} placed a huge lily pad at {} {} {}", player.getRealName(), up.x, up.y, up.z);
        return true;
    };
    ia::onUnusedUseOn({.item = ids::HugeLilyPad}, [useLilyPad](ia::UseOnContext const& c) {
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        // PlaceOnWaterBlockItem#useOn passes: the item's use places on the water the player looks at
        useLilyPad(c.player, c.item);
        return ia::Result::Consume;
    });
    ia::onUse({.item = ids::HugeLilyPad}, [useLilyPad](ia::UseContext const& c) {
        if (c.onBlock) return ia::Result::Pass;
        return useLilyPad(c.player, c.item) ? ia::Result::Consume : ia::Result::Pass;
    });
    // HugeLilyPadBlock#onRemove: the other three pieces go too (without drops)
    blockevents::onRemoved(ids::HugeLilyPad, [](BlockSource& region, BlockPos const& pos, Block const& previous) {
        auto piece = stateOf(previous, st::Piece);
        BlockPos nw = piece == "ne" ? offset(pos, -1, 0, 0) : piece == "se" ? offset(pos, -1, 0, -1) : piece == "sw" ? offset(pos, 0, 0, -1) : pos;
        for (BlockPos p : {nw, offset(nw, 0, 0, 1), offset(nw, 1, 0, 0), offset(nw, 1, 0, 1)}) {
            if (p == pos) continue;
            if (idAt(region, p) == ids::HugeLilyPad) destroyBlock(region, p, false);
        }
    });

    // ---- huge water lily: placed on the water looked at (PlaceOnWaterBlockItem#use)
    auto useWaterLily = [](Player& player, ItemStack const& held) {
        auto hit = clipSourceOnly(player);
        if (!hit) return false;
        auto&       region = player.getDimensionBlockSource();
        BlockPos    up     = relative(hit->first, Up);
        auto const* lily   = game::blockWith(ids::HugeWaterLily);
        if (!lily || !replaceable(region, up)) return false;
        auto ok = survives(region, up, *lily);
        if (!ok || !*ok) return false;
        game::setBlock(region, up, *lily);
        consumeHeld(player, held);
        sound(region, up, "block.lily_pad.place");
        return true;
    };
    ia::onUnusedUseOn({.item = ids::HugeWaterLily}, [useWaterLily](ia::UseOnContext const& c) {
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        useWaterLily(c.player, c.item);
        return ia::Result::Consume;
    });
    ia::onUse({.item = ids::HugeWaterLily}, [useWaterLily](ia::UseContext const& c) {
        if (c.onBlock) return ia::Result::Pass;
        return useWaterLily(c.player, c.item) ? ia::Result::Consume : ia::Result::Pass;
    });

    // ---- essence berry: an experience orb of 6..19 (EssenceBerryItem#use)
    item::registerItem(ids::EssenceBerry, {.use = [](Player& player, ItemStack& stack) {
                           consumeHeld(player, stack);
                           Vec3 at = player.getPosition();
                           // Player#getY is the feet: Bedrock's position is at the eyes
                           Vec3 feet{at.x, at.y - 1.62f, at.z};
                           int  xp = 6 + levelRandom().nextInt(14);
                           ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x, feet.y + 1.0f, feet.z}, xp);
                           return item::Result::Consume;
                       }});

    // ---- berries: StackableEffectItem's stacking effects
    registerBerry("gloamingforest:blightberry", {{"minecraft:regeneration", 10, 8 * 20, 1.0f}, {"minecraft:poison", 19, 5 * 20, 0.75f}, {"minecraft:wither", 20, 5 * 20, 0.15f}});
    registerBerry("gloamingforest:duskberry", {{"minecraft:night_vision", 16, 15 * 20, 1.0f}, {"minecraft:blindness", 15, 3 * 20, 0.75f}});
    registerBerry("gloamingforest:skyberry", {{"minecraft:jump_boost", 8, 8 * 20, 1.0f}, {"minecraft:slowness", 2, 3 * 20, 0.75f}});
    registerBerry("gloamingforest:stingberry", {{"minecraft:strength", 5, 10 * 20, 1.0f}, {"minecraft:mining_fatigue", 4, 10 * 20, 0.75f}});

    // ---- world generation: the plants the vanilla features placed where they can't stay
    worldgen::addStage(worldgen::Stage::Features, "flora.survival", 1000, [](worldgen::Chunk&, worldgen::Region& region) {
        static std::set<std::string> const kSwept{ids::Mayapple, ids::Fiddlehead, ids::CloverPatch, ids::MossPatch, ids::Mushgloom, ids::FallenLeaves, ids::HugeWaterLily};
        int cx = region.centerChunkX() * 16, cz = region.centerChunkZ() * 16;
        for (int x = cx; x < cx + 16; ++x)
            for (int z = cz; z < cz + 16; ++z) {
                if (!region.isInside(x, z)) continue;
                int top = region.height(worldgen::Heightmap::WorldSurface, x, z);
                for (int y = std::min(top, region.maxY() - 1); y >= std::max(region.minY(), top - 12); --y) {
                    BlockPos     p{x, y, z};
                    Block const& b = region.getBlock(p);
                    if (b.isAir() || !kSwept.contains(b.getTypeName())) continue;
                    auto ok = survivesIn(region, p, b);
                    if (ok && !*ok) {
                        if (auto const* air = features::kit::javaBlock("minecraft:air")) region.setBlock(p, *air, 2);
                    }
                }
            }
    });
}

} // namespace gf::feature::flora
