#pragma once

// Contract extension of "inventory" for the families kit (F11, rule R18: new files only; A8 folds
// it into src/feature/inventory/Inventory.h as a per-position slot count). gf::inventory::Handler
// has one `slots` per block id, but a family chest's size depends on its position: a single chest
// has 27 slots, a double chest 54 from either half (Java CompoundContainer: the first, RIGHT half's
// slots first). The families kit registers its chests with `slots = 27`; the handler's get / set /
// extract / insert take every slot of the double chest. Consumers that walk a container's slots
// (hoppers, comparators, loot checks) ask here first:
//
//     auto h = gf::inventory::find(region, pos);
//     int  n = gf::families_ext::containerSize(gpos).value_or(h->slots);
//     for (int i = 0; i < n; ++i) ... h->get(gpos, i) ...
//
// Server thread only.

#include <optional>

#include "core/GlobalPos.h"

namespace gf::families_ext {

/// The slot count of the family container (chest, trapped chest) at `pos`: 27, or 54 for a
/// double chest; nullopt when no family container is there (or its chunk is not loaded).
[[nodiscard]] std::optional<int> containerSize(core::GlobalPos const& pos);

} // namespace gf::families_ext
