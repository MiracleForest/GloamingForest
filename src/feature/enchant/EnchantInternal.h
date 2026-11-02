#pragma once

// Internals of the enchantment runtime shared with the item runtime (lore) and F9's selftests.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "core/GlobalPos.h"
#include "feature/enchant/Enchant.h"

class BlockPos;
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
/// anvil hook's path; `creative`: the player builds instantly (no supported-items check). `joined`:
/// how many of the second item's custom enchantments joined (AnvilMenu's "flag2").
int mergeAnvil(ItemStack const& first, ItemStack const& second, ItemStack& out, bool bookEnchant, bool creative = false, int* joined = nullptr);
/// The grindstone: removes every custom enchantment (none of TF's is a curse). True when any was.
bool grind(ItemStack& stack);
/// The experience the grindstone gives for the custom enchantments (Java: the min cost of each).
[[nodiscard]] int grindExperience(ItemStack const& stack);
/// The enchanting table: the custom enchantments Java's selectEnchantment would add for an option
/// of `cost` on `itemId` (seeded like the table), vanilla candidates of the item included.
[[nodiscard]] std::map<std::string, int> rollTable(std::string const& itemId, int enchantValue, int cost, int seed, int option);
/// Anvil merges with custom enchantments a player made through item stack requests (selftests).
void              countAnvilMerge(Player& player);
[[nodiscard]] int anvilMergeCount(Player& player);
/// Removes a registered enchantment spec and its ext::setFlags (selftests).
void unregisterType(std::string const& enchantmentId);
/// Whether the enchanting table may offer any custom enchantment (a discoverable, non-treasure spec).
[[nodiscard]] bool anyTableEnchantment();
/// The vanilla anvil fallback (ext/item/Enchant.h): what applying the book in the player's
/// inventory slot `bookSlot` to the stack in `targetSlot` gives (Java's AnvilMenu#createResult: the
/// merged copy and the level cost, prior work included); nullopt when it would give nothing.
struct AnvilOffer {
    int       targetSlot = -1;
    ItemStack result;
    int       cost = 0;
};
[[nodiscard]] std::optional<AnvilOffer> anvilOffer(Player& player, int bookSlot, int targetSlot);
/// Every inventory slot the book in `bookSlot` makes an offer for, in slot order.
[[nodiscard]] std::vector<AnvilOffer> anvilOffers(Player& player, int bookSlot);
/// Applies an offer as the anvil does (checked again; levels, the book, the prior work, the anvil at
/// `anvil` may be damaged). False when the inventory changed meanwhile or the player lacks the levels.
bool applyAnvilOffer(Player& player, int bookSlot, int targetSlot, BlockPos const* anvil);
/// Whether a stack is an enchanted book whose enchantments are all custom (the vanilla client's anvil
/// offers nothing for it).
[[nodiscard]] bool isCustomOnlyBook(ItemStack const& stack);
/// ItemCombinerMenu#stillValid's reach (Minecraft 1.21, Player#canInteractWithBlock(pos, 4.0)): the
/// anvil block's box within the block interaction range (4.5, creative 5) plus 4 of the eyes.
[[nodiscard]] bool stillAtAnvil(Player const& player, BlockPos const& anvil);
/// A button of the vanilla anvil form: what it showed and where.
struct AnvilChoice {
    int             bookSlot   = -1;
    int             targetSlot = -1;
    core::GlobalPos anvil;
    ItemStack       book;   ///< the book the form listed
    ItemStack       target; ///< the item the button showed
    int             cost = 0;
};
enum class AnvilChoiceResult { Applied, NoAnvil, TooFar, Changed, NotApplied };
/// The form's answer, which can come much later: the anvil still stands at its place, the player is
/// still at it (stillAtAnvil), the slots still hold the book and the item the button showed for the
/// same cost; then applyAnvilOffer (NotApplied: it refused, e.g. too few levels).
AnvilChoiceResult takeAnvilChoice(Player& player, AnvilChoice const& choice);
/// Vanilla anvil fallback uses of a player (selftests / e2e).
[[nodiscard]] int anvilFallbackCount(Player& player);
/// Anvil forms sent to a player (selftests).
[[nodiscard]] int anvilFormCount(Player& player);
/// Roman numerals as Java's enchantment.level.N ("I" .. "X", then digits).
[[nodiscard]] std::string levelName(int level);
/// The glint marker: an empty vanilla enchantment list makes clients draw the glint without a
/// vanilla enchantment line (spike S6). Set when the stack has custom enchantments and no vanilla
/// ones, removed otherwise.
void updateGlint(ItemStack& stack);

} // namespace gf::enchant::internal
