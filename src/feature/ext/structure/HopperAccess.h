// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded by A8 in W2b, for example
// into F10's loot / inventory contracts). Rule R8 keeps one engine hook per event: these are the
// hooks on a vanilla hopper's container access, its own tick, its removal and its comparator
// reading, with one dispatch to listeners, so no unit hooks HopperBlockActor / HopperBlock itself.
// The structure kit uses it to unpack a template hopper's loot table where Java unpacks a
// RandomizableContainer's (Bedrock's hopper has no loot table field).
//
// Cost: every hopper access in the world tests a lock-free counting filter of the watched
// positions (a hash of x, y, z: one atomic load); only a hit (a watched position, or another
// position with the same hash) takes the lock for the exact lookup. Listeners run on the server
// thread only (the client build's own hoppers, read on its client thread, dispatch nothing).

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

class BlockPos;
class BlockSource;

namespace gf::structure::ext::hoppers {

/// What touches a watched hopper.
enum class Access {
    /// Its container is read or written from outside its own tick: HopperBlockActor's
    /// Container#getItem / #setItem (another hopper pushing into it or pulling from it, any
    /// container read or write, Container#dropContents, a command). Container#isEmpty, #removeItem
    /// and #getSlots go through them.
    Container,
    /// Its own tick starts (HopperBlockActor#tick). Its own reads and writes during that tick are
    /// not Container accesses.
    Tick,
    /// The block is removed by any cause (HopperBlock#onRemove: a player, an explosion, a command, a
    /// piston), before its contents drop.
    Removed,
    /// A comparator reads it (HopperBlock#getComparatorSignal), before the slots are read.
    Comparator,
};

/// fn(region, pos, access): runs on the server thread before the access, for watched positions
/// only. Accesses a listener causes while it runs (filling the container) are not dispatched again.
using AccessFn = std::function<void(BlockSource& region, BlockPos const& pos, Access access)>;
/// Adds a listener (for the life of the process; register once).
void onAccess(AccessFn fn);

/// Watches the hopper position `pos` of dimension `dim` (counted: as many unwatch calls as watch
/// calls end it). Any thread.
void watch(int dim, BlockPos const& pos);
/// Ends one watch of `pos` of `dim` (nothing when it is not watched).
void unwatch(int dim, BlockPos const& pos);
/// Whether `pos` of `dim` is watched (exact).
[[nodiscard]] bool watched(int dim, BlockPos const& pos);
/// The watched positions, {dim, x, y, z} each.
[[nodiscard]] std::vector<std::array<int, 4>> watchedPositions();

} // namespace gf::structure::ext::hoppers
