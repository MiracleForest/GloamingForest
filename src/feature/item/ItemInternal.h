#pragma once

// Internals of the item runtime shared by F9's runtimes (enchantments write lore through it), its
// client part and its selftests.

#include <optional>
#include <string>
#include <vector>

#include "feature/item/Item.h"

class Item;
class ItemStack;
class ItemStackBase;
class Player;

namespace gf::item::internal {

/// A stack of our own from any ItemStackBase (an ItemInstance, a renderer's copy): item, count,
/// aux, block and user data.
[[nodiscard]] ItemStack stackOf(ItemStackBase const& base);

/// The tooltip lines of a stack: the registered tooltip functions' lines, then the enchantment
/// lines of gf::enchant (Java's order: the item's appendHoverText, then the enchantments).
[[nodiscard]] std::vector<std::string> tooltipLines(ItemStack const& stack, Player const* viewer);
/// Writes the tooltip lines as the stack's lore (replacing the lines it wrote before, keeping
/// other lore). Lines without a leading colour code are gray, as TF writes them. True when the
/// stack changed.
bool refreshLore(ItemStack& stack, Player const* viewer);
/// Whether the stack's id has tooltip functions or the stack carries GloamingForest lore.
[[nodiscard]] bool hasTooltip(ItemStack const& stack);

/// The bar of a stack (setBar), nullopt when none is registered or it is hidden.
[[nodiscard]] std::optional<Bar> bar(ItemStack const& stack);
/// Whether an item has a bar function (the client renderer asks per slot and frame: by the item's
/// name hash, no string).
[[nodiscard]] bool hasBar(std::string const& itemId);
[[nodiscard]] bool hasBar(Item const* item);

/// The registered behaviour of an item (nullptr: none).
[[nodiscard]] Behaviour const* behaviourOf(std::string const& itemId);
/// Runs the per-tick item work for one player now (inventory / armour ticks, equip changes,
/// use ticks, the lore refresh when `lore`); the module does it every tick.
void tickPlayer(Player& player, bool lore);
/// Dispenses through the registered DispenseFn (the hook's path); nullopt when none is registered.
[[nodiscard]] std::optional<bool> dispense(BlockSource& region, BlockPos const& dispenser, int facing, ItemStack& stack);
/// Removes a registration (selftests): behaviour, tooltips, dispenser and bar of `itemId`.
void unregister(std::string const& itemId);

/// The lore data key ("gf:lore" in the user data): the lines GloamingForest wrote.
inline constexpr char kLoreKey[] = "lore";

} // namespace gf::item::internal
