#pragma once

// Contract extension for U3 (rule R18: new files; A8 folds it into a W0 contract, e.g. the
// families/blockevents contract next to onRandomTick). TF's TimeLogCoreBlock runs, for every block
// it hits, `state.getTicker(level, type)` of the block entity there (BlockEntityTicker#tick), so it
// speeds up every ticking block entity, TF's own included: the drying rack, cinder furnace,
// brazier, carminite reactor, ghast trap, antibuilder, firefly / cicada / moonworm jars, the boss
// spawners, TF smoker, ... (22 TF classes override getTicker). Bedrock's custom blocks have no block
// entities: each unit runs its block's "ticker" itself (BlockData + its own timer), so the
// timewood clock can't reach it unless the owner tells it how. The owner registers its block's
// ticker here, by block id (rule R2: ids only):
//
//     gf::trees_ext::registerTimeCoreTicker("twilightforest:drying_rack",
//         [](BlockSource& region, BlockPos const& pos, bool simulate) {
//             if (!hasTicker(region, pos)) return false; // Java: getTicker returned null
//             if (!simulate) runOneTick(region, pos);    // BlockEntityTicker#tick
//             return true;
//         });
//
// The clock calls it with simulate = false once per hit (the time core's 480 random hits per cycle
// over its cube) and shows its particle when it returns true; simulate = true only asks whether the
// block ticks in its current state (selftests). Vanilla block entities are ticked by the clock
// itself. Registrations are made on the server thread (a module's install()).

#include <functional>
#include <string>

class BlockPos;
class BlockSource;

namespace gf::trees_ext {

/// BlockEntityTicker#tick of a block: false when the block at `pos` has no ticker in its current
/// state (nothing done), else true (and, unless `simulate`, one tick was run).
using TimeCoreTicker = std::function<bool(BlockSource& region, BlockPos const& pos, bool simulate)>;

/// Registers (replaces) the ticker of `blockId`.
void registerTimeCoreTicker(std::string const& blockId, TimeCoreTicker ticker);
/// Removes the ticker of `blockId` (a module's uninstall(), selftests).
void unregisterTimeCoreTicker(std::string const& blockId);
/// The ticker of `blockId` (nullptr: none registered).
[[nodiscard]] TimeCoreTicker const* timeCoreTicker(std::string const& blockId);

} // namespace gf::trees_ext
