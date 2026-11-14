#pragma once

// Contract extension of "families+blockevents" (F11, rule R18: new files only; A8 folds it into
// feature/families/BlockEvents.h). The gf::blockevents registrations last for the process; these
// register the same handlers with a handle that removes them again (a unit's uninstall, a selftest
// that listens only while it runs):
//
//     auto h = gf::blockevents::ext::addSteppedOn("gloamingforest:fiery_block", burn);
//     gf::blockevents::ext::remove(h);
//
// A removed handler is never called again, also not by a dispatch that is running when it goes (a
// handler may remove itself); its function is destroyed at the next server tick.

#include <cstdint>
#include <string>

#include "feature/families/BlockEvents.h"

namespace gf::blockevents::ext {

/// A block event registration (0: none, e.g. an empty target or function).
using HandlerHandle = std::uint64_t;

[[nodiscard]] HandlerHandle addRandomTick(std::string const& target, RandomTickFn fn);
[[nodiscard]] HandlerHandle addNeighborChanged(std::string const& target, NeighborFn fn);
[[nodiscard]] HandlerHandle addEntityInside(std::string const& target, EntityFn fn);
[[nodiscard]] HandlerHandle addSteppedOn(std::string const& target, EntityFn fn);
[[nodiscard]] HandlerHandle addProjectileHit(std::string const& target, EntityFn fn);
[[nodiscard]] HandlerHandle addPlaced(std::string const& target, PlacedFn fn);
[[nodiscard]] HandlerHandle addRemoved(std::string const& target, RemovedFn fn);
/// Removes a registration of any of the kinds (false: unknown or removed already).
bool remove(HandlerHandle handle);

} // namespace gf::blockevents::ext
