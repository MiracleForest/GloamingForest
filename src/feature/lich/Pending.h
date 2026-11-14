#pragma once

// U21 (lich): world edits a structure piece cannot make while the chunk generates (the generation
// region has no block entities and no entities): upstream's pieces fill block entities (lectern books,
// brewing stands, jars, shelves, sinister spawners) and add entities (Death Tome mimics, trapped
// zombies, the gallery's magic painting) inside postProcess. The piece queues them here with the
// values it rolled; the server thread applies them once the chunk is loaded, at most a second
// later. The queue is kept in the world DB (gf:lich:pending) across restarts.

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

class BlockSource;

namespace gf::feature::lich::pending {

using Json = nlohmann::json;

/// Applies one queued edit at `pos`; false keeps it queued (retried later, dropped after a minute).
using Apply = std::function<bool(BlockSource& region, BlockPos const& pos, Json const& data)>;
void registerKind(std::string const& kind, Apply fn);

/// Queues `kind` at `pos` of dimension `dim` (any thread).
void add(int dim, BlockPos const& pos, std::string const& kind, Json data);

/// Applies the queued edits whose chunks are loaded (the level tick calls it).
void tick();
/// Loads / stores the queue (world lifecycle). load() adds the saved entries in front of the ones
/// queued before the world was ready; clear() empties it when a world closes.
void load();
void save();
void clear();
/// The number of queued edits (selftests).
[[nodiscard]] std::size_t size();

/// While one lives, `add` on this thread queues nothing (a selftest placing a layout).
struct Suppress {
    Suppress();
    ~Suppress();
    Suppress(Suppress const&)            = delete;
    Suppress& operator=(Suppress const&) = delete;
};

} // namespace gf::feature::lich::pending
