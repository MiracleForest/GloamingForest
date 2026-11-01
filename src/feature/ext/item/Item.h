#pragma once

// Extension of contract "item" by F9 (rule R18; to be folded into feature/item/Item.h by A8).
//
// - Rarity: TF's TFRarityEnumExtension adds the TWILIGHT rarity (dark green names: the boss
//   trophies and banner patterns). Bedrock's Rarity enum has no such entry, so names are coloured
//   by id. TF's own TWILIGHT items are built in; units may add more.
// - Bars: the durability / dose bar a unit gave with item::setBar, as the client mod's renderer
//   draws it (client/item).
// - Tooltips: the lines the server writes as lore, and a refresh after a unit changed a stack's
//   data outside the inventory tick (a chest, an item frame).
// - Display contexts: TFItemDisplayContextEnumExtension's JARRED context (items drawn inside a jar,
//   falling back to FIXED) as a flag next to client/render/Render.h's ItemContext values.

#include <optional>
#include <string>
#include <vector>

#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"

class ItemStack;
class Player;

namespace gf::item::ext {

/// Java's Rarity, plus TF's TWILIGHT.
enum class Rarity { Common, Uncommon, Rare, Epic, Twilight };
/// The § colour code of a rarity's names ("§f", "§e", "§b", "§d", "§2").
[[nodiscard]] char const* rarityColor(Rarity rarity);
/// Gives an item a rarity (its name colour with the client mod).
void setRarity(std::string const& itemId, Rarity rarity);
/// The rarity of an item (Common when none was set).
[[nodiscard]] Rarity rarityOf(std::string const& itemId);

/// The bar of a stack as setBar's function gives it (nullopt: hidden / none).
[[nodiscard]] inline std::optional<Bar> barOf(ItemStack const& stack) { return internal::bar(stack); }

/// The tooltip lines the server writes as lore for `viewer` (null: the server's language).
[[nodiscard]] inline std::vector<std::string> tooltipLines(ItemStack const& stack, Player const* viewer) {
    return internal::tooltipLines(stack, viewer);
}
/// Rewrites a stack's tooltip lore now (true when it changed).
inline bool refreshTooltip(ItemStack& stack, Player const* viewer) { return internal::refreshLore(stack, viewer); }

/// TFItemDisplayContextEnumExtension#JARRED as a client/render ItemContext flag (after Head = 32):
/// renderers registered for it draw items inside jars; without one, Fixed is used.
inline constexpr unsigned kJarredContext = 64;

} // namespace gf::item::ext
