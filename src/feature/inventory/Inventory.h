#pragma once

// Contract "inventory" (header owned by F0, implemented by F10; spike S3 picks the backend).
//
// Containers on custom blocks (upstream's chests, casket, drying racks, jars, uncrafting table, cinder
// furnace, ...). Custom Bedrock blocks have no block entities, so the backend is either
// data-driven block actors or BlockData plus a container entity; this API hides which one S3
// chose. Simple chest-like containers are declared as data in assets/data/containers/<u>.json.
//
//     gf::inventory::registerBlock("gloamingforest:keepsake_casket", {.slots = 27, .open = ...});
//     if (auto h = gf::inventory::find(region, pos)) h->insert(gpos, stack, false);

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/GlobalPos.h"

class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::inventory {

using core::GlobalPos;

/// Storage of one block id. Members left empty get the default backend behaviour.
struct Handler {
    int slots = 27;
    std::function<ItemStack(GlobalPos const& pos, int slot)>                                    get;
    std::function<void(GlobalPos const& pos, int slot, ItemStack const& stack)>                 set;
    /// Inserts as much of `stack` as fits; returns the remainder (empty: all inserted).
    std::function<ItemStack(GlobalPos const& pos, ItemStack const& stack, bool simulate)>       insert;
    /// Takes up to `count` items from `slot`; returns what was taken.
    std::function<ItemStack(GlobalPos const& pos, int slot, int count, bool simulate)>          extract;
    /// Sets a loot table rolled when first opened (RandomizableContainer).
    std::function<void(GlobalPos const& pos, std::string const& tableId, std::int64_t seed)>    setLootTable;
    /// Opens the container screen for `player` (native screen or a vanilla container); false if refused.
    std::function<bool(Player& player, GlobalPos const& pos)>                                   open;
};
void registerBlock(std::string const& blockId, Handler handler);
/// The handler of the block at `pos` (nullopt: not a container).
[[nodiscard]] std::optional<Handler> find(BlockSource& region, BlockPos const& pos);
/// Opens a generic chest-like screen of `rows` rows (1..6) titled `titleKey` for the container
/// stored at `pos`; false when it can't open.
bool openChestLike(Player& player, GlobalPos const& pos, int rows, std::string const& titleKey);

} // namespace gf::inventory
