#pragma once

// Unit U4 (woodwork): the drying rack's state (DryingRackBlockEntity), shared by the unit's server
// files, its selftests and its display. Internal to the unit.

#include <functional>
#include <optional>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "core/GlobalPos.h"
#include "feature/recipes/Recipes.h"

namespace gf::feature::woodwork::rack {

using core::GlobalPos;

/// The block data key of a rack's state.
inline constexpr char kKey[] = "drying_rack";
/// DryingRackBlockEntity.DEFAULT_DRYING_TIME: 20 * 60 * 5 ticks.
inline constexpr int kDefaultDryingTime = 20 * 60 * 5;

/// DryingRackBlockEntity's fields (+ the water seen at the last tick).
struct RackState {
    ItemStack item;
    int       dryTime      = 0;
    int       totalDryTime = 0;
    bool      drying       = false;
    bool      waterlogged  = false;
};

/// The rack at `pos` (nullopt: no rack there or its dimension is not loaded).
[[nodiscard]] std::optional<RackState> at(GlobalPos const& pos);
/// The drying recipe of `item` (nullptr: none).
[[nodiscard]] recipes::CraftingRecipe const* recipeFor(ItemStack const& item);
/// Called after a rack's item or drying flag changed (display sync).
using ChangeFn = std::function<void(GlobalPos const& pos)>;
void onChange(ChangeFn fn);
/// The racks that hold an item (they tick).
[[nodiscard]] std::vector<GlobalPos> activeRacks();

} // namespace gf::feature::woodwork::rack
