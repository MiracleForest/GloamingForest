#pragma once

// Contract extension of "inventory" by F10 (rule R18; to be folded into
// feature/inventory/Inventory.h by A8).
//
// Stacks of a native container still in block data: a container block that got
// minecraft:block_entity after stacks were stored for it (TF's chests before tools/families wrote
// them in format 1.26.50, spike S7) has a native container (the block actor's) and its old stacks in
// the BlockData backend's layout ("inventory": {"items": [...]}). Until they move, the contract
// reads both: a read shows each waiting stack where a move would put it (its own slot when that is
// empty, else the first empty one), so a hopper's neighbour, a comparator or a screen of a chunk
// that never ticked sees the stacks; the first write through the contract moves them, so there is
// one copy before anything changes. migrateBlockDataToNative moves them now (F11 does it when a
// chunk loads).
//
//     if (gf::inventory::ext::hasBlockDataStacks(pos)) gf::inventory::ext::migrateBlockDataToNative(pos);

#include "core/GlobalPos.h"

namespace gf::inventory::ext {

/// Whether stacks of the container at `pos` still wait in block data ("items" with a stack)
/// while the block has a native container.
[[nodiscard]] bool hasBlockDataStacks(core::GlobalPos const& pos);
/// Moves the stacks waiting in block data into the native container of `pos`: each into its own
/// slot when that is empty, else into the first empty slot; with `allowDrops` a stack that finds no
/// room drops at the block, without it nothing moves unless every stack finds room (returns 0). The
/// block data keeps what is not a slot (a pending loot table). Returns the stacks moved (0: none
/// waiting, no native container yet, or no room without `allowDrops`). Server thread.
int migrateBlockDataToNative(core::GlobalPos const& pos, bool allowDrops = true);

} // namespace gf::inventory::ext
