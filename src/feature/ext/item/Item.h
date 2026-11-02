#pragma once

// Extension of contract "item" by F9 (rule R18; to be folded into feature/item/Item.h by A8).
//
// - Rarity: TF's TFRarityEnumExtension adds the TWILIGHT rarity (dark green names: the boss
//   trophies and banner patterns). Bedrock's Rarity enum has no such entry, so names are coloured
//   by id. TF's own TWILIGHT items are built in; units may add more. Enchanted stacks get Java's
//   bump (common / uncommon -> rare, rare -> epic; TWILIGHT and epic stay).
// - Bars: the durability / dose bar a unit gave with item::setBar, as the client mod's renderer
//   draws it (client/item: from the slot's real stack, its item data included).
// - Tooltips: the lines the server writes as lore, and a refresh after a unit changed a stack's
//   data outside the inventory tick (a chest, an item frame).
// - Display contexts: TFItemDisplayContextEnumExtension's JARRED context ("twilightforest:jarred",
//   items drawn inside a mason jar; Java falls back to FIXED): the display transforms of an item's
//   model by context with that fallback, and (client) a renderer registry the jar renderer
//   dispatches through.
//
// Semantics of contract callbacks the header leaves open (Java's):
// - Behaviour::hurtEnemy returns Java's hurtEnemy result: true runs the item's post-hit handling
//   (Java's postHurtEnemy: the durability loss of ComponentItem::hurtActor); false skips it.
// - DispenseFn: a registered item is always dispensed by its function (never Bedrock's default
//   ejection); false is TF's OptionalDispenseItemBehavior failing: the item stays and the
//   dispenser's fail sound (level event 1001) plays.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "feature/item/Item.h"

class ItemStack;
class ItemStackBase;
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
/// The rarity a stack shows (ItemStack#getRarity: the item's, bumped when enchanted).
[[nodiscard]] Rarity rarityOf(ItemStackBase const& stack);

/// The bar of a stack as setBar's function gives it (nullopt: hidden / none).
[[nodiscard]] std::optional<Bar> barOf(ItemStack const& stack);

/// The tooltip lines the server writes as lore for `viewer` (null: the server's language).
[[nodiscard]] std::vector<std::string> tooltipLines(ItemStack const& stack, Player const* viewer);
/// Rewrites a stack's tooltip lore now (true when it changed).
bool refreshTooltip(ItemStack& stack, Player const* viewer);

/// Java's ItemTransform of a model's "display" entry: degrees, 1/16 block, factors.
struct DisplayTransform {
    std::array<float, 3> rotation{0, 0, 0};
    std::array<float, 3> translation{0, 0, 0};
    std::array<float, 3> scale{1, 1, 1};
};
/// The JARRED context's name in item models ("display": {"twilightforest:jarred": {...}}).
inline constexpr char kJarred[] = "twilightforest:jarred";
/// TFItemDisplayContextEnumExtension#JARRED as a client/render ItemContext flag (after Head = 32).
inline constexpr unsigned kJarredContext = 64;
/// Sets an item's display transform for a context ("fixed", "gui", "ground", kJarred, ...): the
/// owner of the item gives its model's numbers.
void setDisplayTransform(std::string const& itemId, std::string const& context, DisplayTransform const& transform);
/// The transform an item is drawn with in `context`: its own, else the context's fallback (Java's
/// ItemDisplayContext fallback: JARRED -> FIXED), else nullopt (identity).
[[nodiscard]] std::optional<DisplayTransform> displayTransform(std::string const& itemId, std::string const& context);

/// An armour trim material (TF's: U7). Bedrock's level trim registry is the list the server sends
/// players in TrimDataPacket (at join, and again by anyone who changes it). A material is registered
/// at mod load and F9 appends it to every level's registry right after the registry initialises
/// (TrimMaterialRegistry::initializeServer; BDS, where the world exists before mod enable: at world
/// ready), so it is there for the smithing table, the tooltip's trim line (in the material's colour)
/// and every joining player. Spike S6: the inventory icon of armour trimmed with a material outside
/// Bedrock's own list is not drawn by the client (registered before the join or after, with a palette
/// texture of the vanilla pack or none); the client mod's icon generation for TF's materials is U7's.
struct TrimMaterialSpec {
    std::string id;     ///< the material id in item data ("Trim": {"Material": id})
    std::string color;  ///< the § colour code of its tooltip text ("§2")
    std::string itemId; ///< the item that applies it in the smithing table
};
void addTrimMaterial(TrimMaterialSpec const& spec);
/// Whether the current level's trim registry holds the material.
[[nodiscard]] bool hasTrimMaterial(std::string const& materialId);

} // namespace gf::item::ext
