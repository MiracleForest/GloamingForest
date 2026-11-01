#pragma once

// Internals of the enchantment runtime shared with the item runtime (lore) and F9's selftests.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "feature/enchant/Enchant.h"

class ItemStack;
class Player;

namespace gf::enchant::internal {

/// The lore lines of the stack's custom enchantments ("Fire React II", gray as Java lists them),
/// in the viewer's language (the server's default without a viewer).
[[nodiscard]] std::vector<std::string> loreLines(ItemStack const& stack, Player const* viewer);
/// The custom enchantments stored on a stack (user data "gf:enchant"), by id.
[[nodiscard]] std::map<std::string, int> custom(ItemStack const& stack);
/// Whether `enchantmentId` is a registered custom (non-vanilla) enchantment.
[[nodiscard]] bool isCustom(std::string const& enchantmentId);
/// The spec of a custom enchantment (nullopt: unknown).
[[nodiscard]] std::optional<Spec> specOf(std::string const& enchantmentId);
/// Whether a custom enchantment can go on an item id (supportedItems; books take anything).
[[nodiscard]] bool supports(std::string const& enchantmentId, std::string const& itemId);
/// Java's anvil merge of the custom enchantments of `second` into `out` (the copy of `first` the
/// anvil made, the vanilla enchantments merged by the engine): returns the extra anvil cost. The
/// anvil hook's path; `creative`: the player builds instantly (no supported-items check).
int mergeAnvil(ItemStack const& first, ItemStack const& second, ItemStack& out, bool bookEnchant, bool creative = false);
/// The grindstone: removes every custom enchantment (none of TF's is a curse). True when any was.
bool grind(ItemStack& stack);
/// The experience the grindstone gives for the custom enchantments (Java: the min cost of each).
[[nodiscard]] int grindExperience(ItemStack const& stack);
/// The enchanting table: the custom enchantments Java's selectEnchantment would add for an option
/// of `cost` on `itemId` (seeded like the table), vanilla candidates of the item included.
[[nodiscard]] std::map<std::string, int> rollTable(std::string const& itemId, int enchantValue, int cost, int seed, int option);
/// Removes a registered enchantment spec (selftests).
void unregisterType(std::string const& enchantmentId);
/// Roman numerals as Java's enchantment.level.N ("I" .. "X", then digits).
[[nodiscard]] std::string levelName(int level);
/// The glint marker: an empty vanilla enchantment list makes clients draw the glint without a
/// vanilla enchantment line (spike S6). Set when the stack has custom enchantments and no vanilla
/// ones, removed otherwise.
void updateGlint(ItemStack& stack);

} // namespace gf::enchant::internal
