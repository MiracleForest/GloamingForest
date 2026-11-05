#pragma once

// Contract extension for U3 (rule R18: new files; A8 folds it into a W0 contract, e.g. the
// interactions or item contract). Java's Block#getCloneItemStack lets a block give another item to
// pick block (middle click): TF's hardened dark leaves give dark leaves. Bedrock picks a block's own
// item and does nothing for a block without one (hardened dark leaves have no item, owners.tsv: block
// only). No W0 contract handles the client's BlockPickRequestPacket, so this extension hooks
// ServerNetworkHandler::$handle(BlockPickRequestPacket) once (the only hook of that event) and lets
// owners map a block id to the item pick block gives, as Java's Inventory#setPickedItem places it:
//
//     gf::trees_ext::setPickItem("twilightforest:hardened_dark_leaves", "twilightforest:dark_leaves");
//
// A picked block with a mapping is handled here (the game's own handler is skipped for it): the
// item is selected when the hotbar holds it, moved into the hotbar from the inventory, or, in
// creative mode, put into the selected slot when empty, else the next empty hotbar slot, else the
// selected slot (Inventory#getSuitableHotbarSlot); the client is resynced.

#include <string>

namespace gf::trees_ext {

/// Pick block on `blockId` gives `itemId` (Block#getCloneItemStack).
void setPickItem(std::string const& blockId, std::string const& itemId);
/// The item pick block gives for `blockId` ("" when the game decides).
[[nodiscard]] std::string pickItem(std::string const& blockId);

} // namespace gf::trees_ext

class Player;
class BlockPos;

namespace gf::trees_ext {
/// Runs the pick of the block at `pos` for `player` as the hook does (selftests); false when the
/// block has no mapping.
bool pickBlock(Player& player, BlockPos const& pos);
} // namespace gf::trees_ext
