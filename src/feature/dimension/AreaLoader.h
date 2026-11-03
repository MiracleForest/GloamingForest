#pragma once

// Internal to unit F4. Loads an area of a dimension without a player in it, as Java's chunk
// tickets do for the teleporter (TFTeleporter loads the destination synchronously and keeps it with
// a PORTAL ticket for 300 ticks): a transient ticking area of the engine (not saved with the world,
// not counted against the 10 ticking areas of /tickingarea) around the area, removed again when the
// last handle to it is released and its own loading is over.

#include <memory>
#include <string>

#include "ll/api/coro/CoroTask.h"

#include "mc/platform/UUID.h"
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
/// Released areas that still finish their loading before they go (at most 10 s): an area removed
/// halfway through the engine's chunk generation would strand the chunks it was bringing in.
[[nodiscard]] int settlingAreas();
/// Tests: false removes a released area at once, as before (the hand-over test's control).
void setSettleOnReleaseForTest(bool settle);
/// What the engine does about an area that does not load (diagnostics): its ticking areas (active
/// and pending, per dimension; `area` marks one of them) and the load state of each chunk within
/// `radius` blocks of `center` in `dim`.
[[nodiscard]] std::string loadReport(int dim, BlockPos center, int radius, mce::UUID const* area = nullptr);

} // namespace gf::feature::dimension
