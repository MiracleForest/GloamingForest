#pragma once

// Contract extension of "families+blockevents" (F11, rule R18: new files only; A8 folds it into
// src/feature/families/BlockEvents.h). The tree of a sapling is data (tools/families/README.md,
// params.grower: Java's TreeGrower with its secondary chance, mega trees of 2x2 saplings and
// secondary trees); what data cannot express registers here by string id (rule R2):
//
//     // TF StructureTreeGrower (hollow oak saplings): params.grower.structure names the structure
//     gf::families_ext::registerStructureGrower("twilightforest:hollow_tree", [](auto& region, auto const& pos, auto const& id, auto& random) {
//         return growHollowTree(region, pos, random);   // true when the structure was placed
//     });
//     // a sapling that grows its own way (replaces its data's grower)
//     gf::families_ext::registerGrower("twilightforest:some_sapling", [](auto& region, auto const& pos, Block const& sapling, auto& random) { ... });
//
// Both run on the server thread when the sapling advances past its last stage (Java
// SaplingBlock.advanceTree: random ticks, bone meal); the sapling is still in place when they
// are called and returning false leaves it there.

#include <functional>
#include <string>

#include "core/jrand/JRand.h"

class Block;
class BlockPos;
class BlockSource;

namespace gf::families_ext {

using RandomSource = core::jrand::RandomSource;

/// Grows the sapling at `pos` (Java TreeGrower.growTree); true when a tree grew.
using GrowerFn = std::function<bool(BlockSource& region, BlockPos const& pos, Block const& sapling, RandomSource& random)>;
/// Grows structure `structureId` from the sapling at `pos` (TF StructureTreeGrower); true when it grew.
using StructureGrowerFn = std::function<bool(BlockSource& region, BlockPos const& pos, std::string const& structureId, RandomSource& random)>;

/// Replaces the data grower of sapling `saplingId` (a later registration replaces an earlier one).
void registerGrower(std::string const& saplingId, GrowerFn fn);
/// The grower of the saplings whose params.grower.structure is `structureId`.
void registerStructureGrower(std::string const& structureId, StructureGrowerFn fn);

/// The registered growers (nullptr when none).
[[nodiscard]] GrowerFn const*          growerOf(std::string const& saplingId);
[[nodiscard]] StructureGrowerFn const* structureGrowerOf(std::string const& structureId);

} // namespace gf::families_ext
