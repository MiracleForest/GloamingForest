#pragma once

// Creative entry variants of the catalog (F9, contract "data"; next to F0's Catalog.h, which A8 may
// fold this into).
//
// upstream's tabs list a few items twice, the second time as a stack with data
// (GloamingCreativeTabs#createGlassSwordAndLoreVer: the glass sword, then its creative-only lore version).
// The behavior pack's item_catalog lists item ids only, so the owner of the item registers the
// variant here: a second creative entry of the item, right after the item's own entry in its group,
// carrying the data `fn` writes on a copy of the item's entry. It is added to the server's creative
// registry when a world is ready (or at once when one already is); every client's creative
// inventory shows and gives it. An entry with the same data that is already there is not added
// again. Register at static init or in install().
//
//     gf::catalog::addVariant("gloamingforest:glass_sword", "lore", [](ItemStack& s) {
//         gf::item::setData(s, "infinite_glass_sword", true); ... });

#include <functional>
#include <string>

class ItemStack;

namespace gf::catalog {

/// Writes the data of a creative entry variant.
using VariantFn = std::function<void(ItemStack& stack)>;
/// Adds the creative entry variant `key` of `itemId` (one per item and key; registering a key
/// again replaces its fn for the next world).
void addVariant(std::string const& itemId, std::string const& key, VariantFn fn);

} // namespace gf::catalog
