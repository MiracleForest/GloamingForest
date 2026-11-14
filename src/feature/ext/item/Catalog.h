#pragma once

// Extension of contract "data" (creative catalog) by F9 (rule R18; to be folded into
// feature/catalog/Catalog.h by A8).
//
// upstream's tabs list some items with data (GloamingCreativeTabs: the keepsake casket at damage 0..2, the
// skull candles with one plain candle, the glass sword's lore version, ...). The behavior pack's
// item_catalog lists items by id only, so the owner of the item gives the data of its catalog
// entries here; the server writes it into its CreativeItemRegistry when a world is ready (the
// creative inventory of every client shows and gives that stack):
//
//     gf::catalog::ext::setDefaultData("gloamingforest:skull_candle", [](ItemStack& s) { gf::item::setData(s, "candles", {...}); });
//
// Register at static init or in install(). Bedrock's server cannot add catalog entries at runtime
// (CreativeItemRegistry::newItemEntry is client-only): extra variants of an item need their own
// entries in the item_catalog data.

#include <functional>
#include <string>

class ItemStack;

namespace gf::catalog::ext {

using DataFn = std::function<void(ItemStack& stack)>;
/// Every catalog entry of `itemId` gets `fn` applied (its user data is kept).
void setDefaultData(std::string const& itemId, DataFn fn);

} // namespace gf::catalog::ext
