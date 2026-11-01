#pragma once

// F10-internal parts of gf::inventory (the native container backend, loot, selftests).

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/inventory/Inventory.h"

class BlockActor;
class Container;

namespace gf::inventory {

/// A chest-like container declared as data (<mod>/data/gf/containers/<u>.json) or in code.
struct Declared {
    std::string block;
    int         rows  = 3;
    int         slots = 27;
    std::string title; ///< translation key
    std::string skin;  ///< JSON-UI / native skin id ("" = vanilla chest)
};

/// The registered handler of `blockId` with its defaults filled (nullopt: not a container).
[[nodiscard]] std::optional<Handler> handlerFor(std::string const& blockId);
/// Removes a registration (selftests).
void unregisterBlock(std::string const& blockId);
/// Declares a chest-like container (what the data files do): storage, opening on use, contents
/// dropped when broken.
void declare(Declared const& d);
/// The declaration of `blockId` (nullptr: none).
[[nodiscard]] Declared const* declared(std::string const& blockId);

/// The game container a data-driven block actor carries for `pos` (the native backend; nullptr
/// when the block has no `minecraft:block_entity` or is not a registered container).
[[nodiscard]] Container* nativeContainer(BlockSource& region, BlockPos const& pos);
/// Spike S3: asks the server to open the game's container screen of the injected container at
/// `pos` as ContainerType `containerType` (0 Container, 37 DataDrivenContainer); the ContainerID or -1.
int openNativeScreen(Player& player, GlobalPos const& pos, int containerType);
/// Every slot of the container at `pos` (empty when none).
[[nodiscard]] std::vector<ItemStack> contents(GlobalPos const& pos);
/// Rolls a pending loot table of the container at `pos` into it (RandomizableContainer#unpackLootTable).
bool unpackLoot(GlobalPos const& pos, Player* opener);
/// The pending loot table at `pos` (selftests).
[[nodiscard]] std::optional<std::pair<std::string, std::int64_t>> pendingLoot(GlobalPos const& pos);

} // namespace gf::inventory
