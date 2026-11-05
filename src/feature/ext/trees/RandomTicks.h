#pragma once

// Contract extension of "families+blockevents" for U3 (rule R18: new file; A8 folds it into
// src/feature/families/BlockEvents.h). The tree of time gives random ticks to the blocks around it
// (TimeLogCoreBlock#performTreeEffect: state.randomTick for every block that ticks randomly). Custom
// blocks tick through the block event dispatch of F11, whose own random tick scan is the only one
// that knows their handlers; these two functions of that dispatch (defined in
// src/feature/families/BlockEvents.cpp, used by F11's selftests) give a single random tick to a
// position, so a unit can tick TF's blocks the way Java's randomTick would:
//
//     if (gf::feature::families::detail::randomTicked(block.getBlockType()))
//         gf::feature::families::detail::randomTickAt(region, pos);

class BlockPos;
class BlockSource;
class BlockType;

namespace gf::feature::families::detail {

/// One random tick of the block at `pos`: the onRandomTick handlers registered for it.
void randomTickAt(BlockSource& region, BlockPos const& pos);
/// Whether blocks of `type` have random tick handlers (the dispatch would tick them).
[[nodiscard]] bool randomTicked(BlockType const& type);

} // namespace gf::feature::families::detail
