#pragma once

// Contract "families+blockevents" (header owned by F0, implemented by F11; rule R8).
//
// Block families: ~400 upstream blocks are plain variations of vanilla shapes. A unit declares them as
// data in assets/data/families/<u>.json:
//     [{"id": "gloamingforest:canopy_stairs", "family": "stairs", "params": {"base": "gloamingforest:canopy_planks"}}]
// families: stairs | slab | fence | fence_gate | wall | door | trapdoor | button | pressure_plate |
// pane | pillar | log | wood | leaves | sapling | potted | sign | hanging_sign | chest |
// trapped_chest | horizontal | directional | waterloggable | snowloggable | climbable | lightable |
// connected_texture. tools/families generates the BP JSON at assemble time when no manual file
// exists, and F11 implements the generic behaviours (slab doubling, door halves, leaf decay,
// stripping, ...).
//
// Block events: the one central dispatch of block callbacks (custom blocks get no Java-style
// overrides). A target is a block id or, with a leading '#', a block tag:
//
//     gf::blockevents::onRandomTick("gloamingforest:time_log_core", [](auto& region, auto const& pos, auto const& block, auto& random) { ... });
//     gf::blockevents::onEntityInside("#gloamingforest:thorns", hurtEntity);

#include <functional>
#include <string>

#include "core/jrand/JRand.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;

namespace gf::blockevents {

using core::jrand::RandomSource;

using RandomTickFn     = std::function<void(BlockSource& region, BlockPos const& pos, Block const& block, RandomSource& random)>;
using NeighborFn       = std::function<void(BlockSource& region, BlockPos const& pos, BlockPos const& neighbor)>;
using EntityFn         = std::function<void(BlockSource& region, BlockPos const& pos, Actor& entity)>;
using PlacedFn         = std::function<void(BlockSource& region, BlockPos const& pos, Block const& block, Actor* placer)>;
using RemovedFn        = std::function<void(BlockSource& region, BlockPos const& pos, Block const& previous)>;

/// A random tick of a block (randomTick; the block must tick, see its family / BP JSON).
void onRandomTick(std::string const& target, RandomTickFn fn);
/// A neighbour changed (neighborChanged / updateShape).
void onNeighborChanged(std::string const& target, NeighborFn fn);
/// An entity is inside the block's shape (entityInside).
void onEntityInside(std::string const& target, EntityFn fn);
/// An entity walks on top of the block (stepOn).
void onSteppedOn(std::string const& target, EntityFn fn);
/// A projectile hit the block (onProjectileHit); `entity` is the projectile.
void onProjectileHit(std::string const& target, EntityFn fn);
/// The block was placed (setPlacedBy / onPlace); `placer` may be null.
void onPlaced(std::string const& target, PlacedFn fn);
/// The block was removed or replaced by another block type (onRemove).
void onRemoved(std::string const& target, RemovedFn fn);

} // namespace gf::blockevents
