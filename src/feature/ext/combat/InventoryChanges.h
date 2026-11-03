#pragma once

// Contract extension of "inventory" by F10 (rule R18; to be folded into
// feature/inventory/Inventory.h by A8).
//
// Change notices (Java's AbstractContainerMenu#broadcastChanges for the client mod's chest screen):
// every write to a container of the inventory contract bumps its change counter: a handler's set /
// insert / extract (find / handlerFor), the BlockData backend's writes and every setItem of a
// native container (the block actor's game container: hoppers, hopper minecarts, droppers,
// crafters, any game code). At the end of the tick each client mod chest screen ("inventory.chest")
// that shows a container changed since its last data gets fresh data ("screen.update", in the
// context the screen was opened with: a double chest opened by code without a click included). No
// screen is looked at while nothing changed, and a screen that just got its data (its own click)
// gets none again. Vanilla screens (the chest minecart proxy) follow the storage on their own.
//
// A unit that writes a container's storage behind the contract (its own block data layout, a
// Container it keeps) calls markChanged so the screens follow.
//
//     auto seen = gf::inventory::ext::changeCount(pos);
//     ...
//     if (gf::inventory::ext::changeCount(pos) != seen) recompute();

#include <cstdint>
#include <optional>

#include "core/GlobalPos.h"

class Player;

namespace gf::inventory::ext {

/// The change counter of the container at `pos`: the number of its last change (one sequence for
/// every container, never reused). Compare it for equality only: a counter not looked at for a long
/// time may be forgotten (0), which reads as a change.
[[nodiscard]] std::uint64_t changeCount(core::GlobalPos const& pos);
/// The container at `pos` changed behind the contract: its client mod screens get fresh data at the
/// end of the tick (server thread).
void markChanged(core::GlobalPos const& pos);
/// The position the player's client mod chest screen was opened for (the screen's context "pos": a
/// single container's own, the half a double chest was opened through), nullopt: no chest screen.
[[nodiscard]] std::optional<core::GlobalPos> chestScreenOf(Player& player);

} // namespace gf::inventory::ext
