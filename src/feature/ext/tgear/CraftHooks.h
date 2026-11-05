#pragma once

// U11 extension (rule R18: new files only; to be folded into F9's item / enchant contracts by A8 in W2b).
//
// The grindstone and the anvil (NeoForge's GrindstoneEvent.OnPlaceItem / OnTakeItem and
// AnvilUpdateEvent), which no W0 contract offers. F9 only hooks the enchantment helpers inside them
// (EnchantUtils::combineEnchantedItems / removeEnchants). Bedrock computes both results on the server
// (item stack requests):
//   - grindstone result: CraftHandlerGrindstone::getResultItem(input, additional). A listener returns
//     nullopt (the game's result), or the result to use: an empty stack refuses the craft;
//   - grindstone take: after CraftHandlerGrindstone::_handleCraftAction succeeded, the player and the two
//     inputs it consumed (the PlayerUIContainer's grindstone slots before the craft);
//   - anvil: AnvilContainerScreenValidator::getCraftResult; a listener that returns true for the player and
//     the anvil's input / material (the PlayerUIContainer's anvil slots) refuses the craft (no result, no
//     consumption: the request fails and the client's slots go back).
//
//     gf::ext::tgear::onGrindResult([](ItemStack const& in, ItemStack const& add) -> std::optional<ItemStack> { ... });
//     gf::ext::tgear::onGrindTaken([](Player& p, ItemStack const& in, ItemStack const& add) { ... });
//   - storage items: Item#canFitInsideContainerItems (Java: shulker box slots, a shulker box's hopper
//     faces, bundles). A listener that returns true for an item keeps it out of a shulker box's slots
//     (ShulkerBoxContainerValidation::isItemAllowedInSlot), out of a shulker box through a hopper
//     (ShulkerBoxBlockActor::canPushInItem) and out of a bundle (DynamicContainerValidation).
//
//     gf::ext::tgear::onAnvilRefuse([](Player& p, ItemStack const& in, ItemStack const& mat) { return false; });
//     gf::ext::tgear::onStorageRefuse([](ItemStackBase const& item) { return isBelt(item); });
//
// Server thread (the client mod's own integrated server in a local world). Installed by U11's module.

#include <functional>
#include <optional>

class ItemStack;
class ItemStackBase;
class Player;

namespace gf::ext::tgear {

using GrindResultFn = std::function<std::optional<ItemStack>(ItemStack const& input, ItemStack const& additional)>;
using GrindTakenFn  = std::function<void(Player& player, ItemStack const& input, ItemStack const& additional)>;
using AnvilRefuseFn = std::function<bool(Player& player, ItemStack const& input, ItemStack const& material)>;
void onGrindResult(GrindResultFn fn);
void onGrindTaken(GrindTakenFn fn);
void onAnvilRefuse(AnvilRefuseFn fn);
using StorageRefuseFn = std::function<bool(ItemStackBase const& item)>;
void onStorageRefuse(StorageRefuseFn fn);

void installCraftHooks();
void uninstallCraftHooks();

} // namespace gf::ext::tgear
