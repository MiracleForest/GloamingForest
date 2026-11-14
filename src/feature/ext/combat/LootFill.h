#pragma once

// Contract extension of "loot" by F10 (rule R18; to be folded into feature/loot/Loot.h by A8).
//
// LootTable#fill for containers whose slots a unit keeps itself (a vanilla hopper's container, a
// block's own storage): rolls the table and spreads the stacks over the container's empty slots as
// Java does (LootTable#fill: stacks split over the free slots, shuffled), with one random for the
// roll and the spreading (the context's seed, 0 = random).
//
//     gf::loot::fill("gloamingforest:chests/hill_1", ctx, container.getContainerSize(),
//                    [&](int i) -> ItemStack const& { return container.getItem(i); },
//                    [&](int i, ItemStack const& s) { container.setItem(i, s); });

#include <functional>
#include <string>

#include "feature/loot/Loot.h"

namespace gf::loot {

/// LootTable#fill: rolls `tableId` and spreads the stacks over the empty slots of a container of
/// `size` slots, as Java does (split stacks, shuffled slots). `get` / `set` access the slots.
void fill(
    std::string const&                                tableId,
    LootContext const&                                ctx,
    int                                               size,
    std::function<ItemStack const&(int)> const&       get,
    std::function<void(int, ItemStack const&)> const& set
);

} // namespace gf::loot
