#pragma once

// Unit U4 (woodwork): the drying rack's state (DryingRackBlockEntity), shared by the unit's server
// files, its selftests and its display. Internal to the unit.

#include <cstdint>
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

/// The display entity of the item on racks (DryingRackDisplay.cpp): installed once; refreshDisplay
/// makes the rack at `pos` show its item (or nothing).
void installDisplay();
void refreshDisplay(GlobalPos const& pos);
/// The unique id of the display entity of the rack at pos (0: none).
[[nodiscard]] std::int64_t displayIdAt(GlobalPos const& pos);

/// The client mod draws the item itself (DryingRackRenderer, src/client/woodwork): every companion
/// gets "woodwork.rack" {"racks": [{"pos": [dim, x, y, z], "facing", "item" (items::toJson),
/// "lower" (#gloamingforest:lower_on_drying_rack), "display" (the display entity's unique id, which
/// it hides)}], "gone": [[dim, x, y, z]], "reset": true (the whole list, when it joins)}.

} // namespace gf::feature::woodwork::rack
