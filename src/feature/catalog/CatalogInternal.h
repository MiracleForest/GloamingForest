#pragma once

// Internals of the catalog runtime shared with F9's extension headers and selftests.

#include <functional>
#include <string>
#include <vector>

class ItemStackBase;

namespace gf::catalog::internal {

/// The groups are read from the creative registry again at the next query.
void invalidate();
/// Calls `fn` with every creative catalog entry of `itemId` (the stack the server sends to clients
/// and gives creative players). Returns the number of entries.
int forEachEntry(std::string const& itemId, std::function<void(ItemStackBase&)> const& fn);
/// Every group name of the creative registry (diagnostics).
[[nodiscard]] std::vector<std::string> groupNames();
/// Applies the default data of the catalog entries (showEnchantmentsOnItems, units' defaults).
/// Returns the entries changed. Runs when a world is ready.
int applyDefaults();

} // namespace gf::catalog::internal
