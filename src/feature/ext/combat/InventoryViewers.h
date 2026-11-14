#pragma once

// Contract extension of "inventory" by F10 (rule R18; to be folded into
// feature/inventory/Inventory.h by A8).
//
// The openers of a container (Java's ContainerOpenersCounter): the players with its screen open
// (the client mod's chest screen, the vanilla chest screen proxy, the form). A trapped chest's
// redstone output is the count (TrappedChestBlockEntity#signalOpenCount, U4 through
// redstone::registerEmitter), and a chest's lid opens while it is above zero.
//
//     gf::inventory::onViewersChanged("gloamingforest:gloaming_oak_trapped_chest", [](auto const& pos, int n) {
//         gf::redstone::...;   // the signal changed
//     });

#include <functional>
#include <string>

#include "core/GlobalPos.h"

namespace gf::inventory {

/// The number of players with a screen of the container at `pos` open.
[[nodiscard]] int viewers(core::GlobalPos const& pos);
/// `fn(pos, count)` runs when the opener count of a container of block `blockId` changes (the
/// first opener, another one, the last one gone: count 0).
using ViewersFn = std::function<void(core::GlobalPos const& pos, int count)>;
void onViewersChanged(std::string const& blockId, ViewersFn fn);

} // namespace gf::inventory
