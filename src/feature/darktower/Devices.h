#pragma once

// U26 (darktower): the tower devices (vanishing family, carminite builder and built blocks,
// antibuilder, carminite reactor and its debris, fake gold / diamond, ghast trap, infested
// towerwood). Unit-internal API for the unit's mobs, structure and selftests.

#include <optional>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"
#include "core/darktower/Devices.h"

class Actor;
class BlockSource;

namespace gf::feature::darktower::devices {

using core::GlobalPos;

/// VanishingBlock#areBlocksLocked: a locked locked_vanishing_block in the connected family.
[[nodiscard]] bool areBlocksLocked(BlockSource& region, BlockPos const& start);
/// VanishingBlock#activate (no-op unless an inactive, non-vanished family block).
void activateVanishing(BlockSource& region, BlockPos const& pos);
/// BuilderBlock#activateBuiltBlocks.
void activateBuiltBlocks(BlockSource& region, BlockPos const& pos);

/// The device states (selftests).
[[nodiscard]] core::darktower::BuilderState const*     builderState(GlobalPos const& pos);
[[nodiscard]] core::darktower::AntibuilderState const* antibuilderState(GlobalPos const& pos);
[[nodiscard]] std::optional<int>                       reactorCounter(GlobalPos const& pos);
/// The ghast trap's recorded dying ghastlings (min(3, n) is its charge) and active counter.
[[nodiscard]] int trapDyingGhasts(GlobalPos const& pos);
[[nodiscard]] int trapCounter(GlobalPos const& pos);

/// The ghast traps known in dimension `dim` within `radius` of `center` (the POI scan of
/// UrGhast#scanForTraps without the sky test).
[[nodiscard]] std::vector<BlockPos> ghastTraps(int dim, BlockPos const& center, int radius);

/// Registers the devices' handlers (module install).
void install();

} // namespace gf::feature::darktower::devices
