#pragma once

// Internal to unit F4. Loads an area of a dimension without a player in it, as Java's chunk
// tickets do for the teleporter (TFTeleporter loads the destination synchronously and keeps it with
// a PORTAL ticket for 300 ticks): a transient ticking area of the engine (not saved with the world,
// not counted against the 10 ticking areas of /tickingarea) around the area, removed again when the
// last handle to it is released.

#include <memory>

#include "ll/api/coro/CoroTask.h"

#include "mc/world/level/BlockPos.h"

namespace gf::feature::dimension {

/// Keeps an area loaded while it lives.
class AreaHold {
public:
    virtual ~AreaHold() = default;
};
using AreaHandle = std::shared_ptr<AreaHold>;

/// Loads the chunks within `radius` blocks (horizontally) of `center` in dimension `dim` and waits
/// until they are loaded; nullptr when that took over `timeoutTicks` ticks or the dimension is
/// not available. The area stays loaded while the handle lives.
ll::coro::CoroTask<AreaHandle> loadArea(int dim, BlockPos center, int radius, int timeoutTicks = 200);
/// Keeps `handle`'s area loaded for `ticks` more ticks, then lets it go (Java's ticket timeout).
void holdFor(AreaHandle handle, int ticks);
/// Areas held right now (tests, diagnostics).
[[nodiscard]] int heldAreas();

} // namespace gf::feature::dimension
