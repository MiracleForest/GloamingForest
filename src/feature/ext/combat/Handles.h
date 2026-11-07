#pragma once

// Contract extension of F10 (rule R18: new files only, folded into the W0 headers by A8 in W2b).
// The combat and loot contracts register handlers for the life of the process; these entry points
// register the same handlers with a handle that removes them again (a unit's uninstall, a selftest
// that listens only while it runs, a behaviour that comes and goes):
//
//     auto h = gf::combat::ext::addDealt(gf::combat::priority::Normal, [](auto const& d) { ... });
//     gf::combat::ext::remove(h);
//
// A handler removed while the pipeline runs still finishes the damage it is part of (each damage
// works on the handlers it started with).

#include <cstdint>

#include "feature/combat/Combat.h"
#include "feature/loot/Loot.h"

namespace gf::combat::ext {

/// A combat handler registration (0: none).
using HandlerHandle = std::uint64_t;
/// gf::combat::onIncoming with a handle.
[[nodiscard]] HandlerHandle addIncoming(int priority, IncomingFn fn);
/// gf::combat::onDealt with a handle.
[[nodiscard]] HandlerHandle addDealt(int priority, DealtFn fn);
/// Removes an incoming or dealt handler (false: unknown or removed already).
bool remove(HandlerHandle handle);

} // namespace gf::combat::ext

namespace gf::loot::ext {

/// An onEntityDrops registration (0: none).
using DropsHandle = std::uint64_t;
/// gf::loot::onEntityDrops with a handle.
[[nodiscard]] DropsHandle addEntityDrops(EntityDropsFn fn);
/// Removes an entity drops handler (false: unknown or removed already).
bool removeEntityDrops(DropsHandle handle);

} // namespace gf::loot::ext
