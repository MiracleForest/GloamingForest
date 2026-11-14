#pragma once

// Contract "enchant+effects", enchantments (header owned by F0, implemented by F9; spike S6).
//
// Bedrock has a fixed set of enchantment ids (vanilla clients know 42), so upstream's four enchantments
// (fire_react, chill_aura, destruction, renewal) live in the item's user data, with
// server-written lore and the glint for vanilla clients and exact tooltips with the client mod.
// F9 hooks the enchanting table, anvil, grindstone and enchanted books so they behave as on Java.
//
//     gf::enchant::registerType("gloamingforest:fire_react", {.maxLevel = 3, .supportedItems = "#minecraft:enchantable/armor"});
//     int lvl = gf::enchant::level(stack, "gloamingforest:fire_react");

#include <map>
#include <string>
#include <vector>

class ItemStack;

namespace gf::enchant {

/// Java's Enchantment.Cost: base + perLevelAboveFirst * (level - 1).
struct Cost {
    int base               = 1;
    int perLevelAboveFirst = 10;
};

/// The data of an enchantment (upstream's data/gloamingforest/enchantment/*.json).
struct Spec {
    int                      maxLevel = 1;
    int                      weight   = 1;
    int                      anvilCost = 1;
    Cost                     minCost;
    Cost                     maxCost{6, 10};
    std::string              supportedItems;  ///< item tag ("#minecraft:enchantable/armor") or item id
    std::string              primaryItems;    ///< table-eligible items (empty: supportedItems)
    std::vector<std::string> exclusiveWith;   ///< enchantment ids / tags it can't combine with
    bool                     treasure     = false;
    bool                     tradeable    = true;
    bool                     discoverable = true;
    std::string              nameKey;         ///< "enchantment.gloamingforest.fire_react"
};
void registerType(std::string const& enchantmentId, Spec spec);

/// The level of `enchantmentId` on a stack (0: none). Works for vanilla ids too.
[[nodiscard]] int level(ItemStack const& stack, std::string const& enchantmentId);
/// Sets (0 removes) an enchantment, updating lore and glint.
void set(ItemStack& stack, std::string const& enchantmentId, int level);
/// Every enchantment of a stack (vanilla and custom) by id.
[[nodiscard]] std::map<std::string, int> all(ItemStack const& stack);

} // namespace gf::enchant
