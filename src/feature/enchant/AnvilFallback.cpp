// The vanilla-client fallback of the anvil for custom enchantments (F9; ext/item/Enchant.h).
//
// A vanilla Bedrock client computes the anvil's result itself and offers none when the second item
// brings custom enchantments only (an enchanted book of TF's from loot: its enchantments live in the
// item's user data). The server's merge (Enchant.cpp's anvil hook) never runs for it, so a vanilla
// player who sneaks and uses an anvil holding such a book gets a form (a plain use opens the
// client's anvil screen whatever the server answers, so it gets a hint to sneak instead): one button
// per inventory item the book makes a result for, with Java's AnvilMenu#createResult numbers (the
// merged enchantments, the level cost with the prior work penalty, "Too Expensive!" from 40 levels
// in survival); a choice is AnvilMenu#onTake (the levels, the book, the prior work of the result,
// and the anvil: 12 % chance of a damage step unless the player has infinite materials, the use or
// break sound). Players with the client mod (companions) and books that carry vanilla enchantments
// too go through the anvil screen.
#include <algorithm>
#include <climits>
#include <map>
#include <mutex>

#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/util/Random.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::enchant {

namespace {

namespace ia    = feature::common::interactions;
namespace items = game::items;

constexpr char kBook[] = "minecraft:enchanted_book";

bool isAnvil(std::string const& blockId) {
    return blockId == "minecraft:anvil" || blockId == "minecraft:chipped_anvil" || blockId == "minecraft:damaged_anvil";
}

/// The next damage step of an anvil (AnvilBlock#damage), "" when it breaks.
std::string damaged(std::string const& blockId) {
    if (blockId == "minecraft:anvil") return "minecraft:chipped_anvil";
    if (blockId == "minecraft:chipped_anvil") return "minecraft:damaged_anvil";
    return {};
}

int levelsOf(Player const& player) {
    auto ref = player.getAttribute(Player::LEVEL());
    if (auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr)) return static_cast<int>(inst->mCurrentValue);
    return 0;
}

bool instabuild(Player const& player) { return player.canUseAbility(AbilitiesIndex::Instabuild); }

/// EnchantmentHelper#canStoreEnchantments: gear the enchanting system knows, and enchanted books.
bool canStoreEnchantments(ItemStack const& stack) {
    if (items::isEmpty(stack)) return false;
    if (items::idOf(stack) == kBook) return true;
    Item const* item = stack.mItem.get();
    return item && item->getEnchantSlot() != 0;
}

std::map<std::int64_t, int>& fallbackCounts() {
    static std::map<std::int64_t, int> m;
    return m;
}
std::mutex& countsMutex() {
    static std::mutex m;
    return m;
}

/// AnvilMenu#onTake's anvil: a damage step at 12 % (none with infinite materials), the use sound, or
/// the break sound when a damaged anvil breaks.
void useAnvil(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    auto  id     = game::blockIdAt(region, pos);
    if (!isAnvil(id)) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    auto event = SharedTypes::Legacy::LevelEvent::SoundAnvilUsed;
    if (!instabuild(player) && player.getRandom().nextFloat() < 0.12f) {
        auto next = damaged(id);
        if (next.empty()) {
            game::clearBlock(region, pos);
            event = SharedTypes::Legacy::LevelEvent::SoundAnvilBroken;
        } else {
            // the same facing on the next damage step
            nlohmann::json states = nlohmann::json::object();
            for (auto const& [k, v] : game::statesOf(region.getBlock(pos))) {
                if (v == "true" || v == "false") states[k] = v == "true";
                else if (!v.empty() && v.size() < 9 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; })) states[k] = std::stoi(v);
                else states[k] = v;
            }
            if (auto const* block = game::blockWithStates(next, states)) game::setBlock(region, pos, *block);
        }
    }
    region.getLevel().broadcastLevelEvent(event, at, 0, nullptr);
}

void showForm(Player& player, int bookSlot, core::GlobalPos anvil);

} // namespace

namespace internal {

bool isCustomOnlyBook(ItemStack const& stack) {
    if (items::isEmpty(stack) || items::idOf(stack) != kBook) return false;
    return !custom(stack).empty() && stack.constructItemEnchantsFromUserData().getAllEnchants().empty();
}

std::optional<AnvilOffer> anvilOffer(Player& player, int bookSlot, int targetSlot) {
    auto& inv = player.getInventory();
    if (bookSlot == targetSlot || bookSlot < 0 || targetSlot < 0 || bookSlot >= inv.getContainerSize() || targetSlot >= inv.getContainerSize()) return std::nullopt;
    ItemStack const& book   = inv.getItem(bookSlot);
    ItemStack const& target = inv.getItem(targetSlot);
    if (!isCustomOnlyBook(book) || !canStoreEnchantments(target)) return std::nullopt;
    bool      creative = instabuild(player);
    AnvilOffer offer;
    offer.targetSlot = targetSlot;
    offer.result     = target;
    int joined       = 0;
    // the enchantments of the book join the copy of the first item (EnchantUtils' vanilla part has
    // nothing to merge: the book has no vanilla enchantment)
    int i = mergeAnvil(target, book, offer.result, true, creative, &joined);
    if (joined == 0 || i <= 0) return std::nullopt; // no enchantment fits ("flag3 && !flag2"), no cost
    long long prior = static_cast<long long>(target.getBaseRepairCost()) + book.getBaseRepairCost();
    offer.cost      = static_cast<int>(std::clamp<long long>(prior + i, 0, INT_MAX));
    if (offer.cost >= 40 && !creative) return std::nullopt; // "Too Expensive!"
    // the result's prior work: the higher of the two, increased (no rename: k == 0)
    int repair = std::max(offer.result.getBaseRepairCost(), book.getBaseRepairCost());
    offer.result.setRepairCost(static_cast<int>(std::min<long long>(static_cast<long long>(repair) * 2 + 1, INT_MAX)));
    return offer;
}

std::vector<AnvilOffer> anvilOffers(Player& player, int bookSlot) {
    std::vector<AnvilOffer> out;
    auto&                   inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (auto offer = anvilOffer(player, bookSlot, i)) out.push_back(std::move(*offer));
    return out;
}

bool applyAnvilOffer(Player& player, int bookSlot, int targetSlot, BlockPos const* anvil) {
    auto offer = anvilOffer(player, bookSlot, targetSlot);
    if (!offer) return false;
    bool creative = instabuild(player);
    // AnvilMenu#mayPickup: the levels (or infinite materials)
    if (!creative && levelsOf(player) < offer->cost) {
        player.sendMessage(trFor(player, "gf.item.anvil.levels", "Enchantment Cost: {}", offer->cost));
        return false;
    }
    if (!creative) player.addLevels(-offer->cost);
    auto& inv = player.getInventory();
    inv.setItem(targetSlot, offer->result);
    inv.setItem(bookSlot, ItemStack::EMPTY_ITEM());
    player.sendInventory(false);
    if (anvil) useAnvil(player, *anvil);
    {
        std::lock_guard lock(countsMutex());
        ++fallbackCounts()[player.getOrCreateUniqueID().rawID];
    }
    log().info("enchant: anvil form merge for {}: {} ({} levels)", player.getRealName(), items::idOf(offer->result), offer->cost);
    return true;
}

int anvilFallbackCount(Player& player) {
    std::lock_guard lock(countsMutex());
    auto            it = fallbackCounts().find(player.getOrCreateUniqueID().rawID);
    return it == fallbackCounts().end() ? 0 : it->second;
}

} // namespace internal

namespace {

void showForm(Player& player, int bookSlot, core::GlobalPos anvil) {
    auto const& book   = player.getInventory().getItem(bookSlot);
    auto        offers = internal::anvilOffers(player, bookSlot);
    std::string enchants;
    for (auto const& line : internal::loreLines(book, &player)) enchants += "\n" + line;
    ll::form::SimpleForm form;
    form.setTitle(trFor(player, "gf.item.anvil.title", "Repair & Name"));
    if (offers.empty()) {
        form.setContent(trFor(player, "gf.item.anvil.none", "Nothing in your inventory can take this book:") + enchants);
    } else {
        form.setContent(trFor(player, "gf.item.anvil.pick", "Apply the book to:") + enchants);
        for (auto const& o : offers) {
            int  target = o.targetSlot;
            auto shown  = player.getInventory().getItem(target);
            auto text   = items::displayName(shown) + "\n" + trFor(player, "gf.item.anvil.cost", "Enchantment Cost: {}", o.cost);
            form.appendButton(text, [bookSlot, target, anvil, shown, cost = o.cost](Player& p) {
                // the form's answer comes later: the player is still at the anvil, and the slot
                // still holds the item and the offer the button showed
                auto& region = p.getDimensionBlockSource();
                auto  pos    = game::blockPos(anvil);
                if (game::globalPos(region, pos) != anvil || !isAnvil(game::blockIdAt(region, pos))) return;
                if (!items::sameItemSameData(p.getInventory().getItem(target), shown)) return;
                auto now = internal::anvilOffer(p, bookSlot, target);
                if (!now || now->cost != cost) return;
                internal::applyAnvilOffer(p, bookSlot, target, &pos);
            });
        }
    }
    form.sendTo(player);
    log().info("enchant: anvil form for {} ({} offers)", player.getRealName(), offers.size());
}

class AnvilFallbackModule {
public:
    void install() {
        if (mHandler >= 0) return;
        mHandler = ia::onUseOn({.item = kBook}, [](ia::UseOnContext const& c) {
            if (!isAnvil(c.blockId) || c.offhand) return ia::Result::Pass;
            if (net::isCompanion(c.player) || !internal::isCustomOnlyBook(c.item)) return ia::Result::Pass;
            int slot = c.player.getSelectedItemSlot();
            // nothing the book can go on: the anvil screen opens as usual (renaming, repairs)
            if (internal::anvilOffers(c.player, slot).empty()) return ia::Result::Pass;
            if (!c.sneaking) {
                // a vanilla client opens its anvil screen on a plain use whatever the server answers:
                // the screen opens, with a hint to the form (a sneaking use opens no screen)
                if (c.firstEvent)
                    c.player.sendMessage(trFor(c.player, "gf.item.anvil.hint", "Sneak and use the anvil with this book to apply it."));
                return ia::Result::Pass;
            }
            if (!c.firstEvent) return ia::Result::Consume; // the held button's repeats
            auto uid = c.player.getOrCreateUniqueID();
            auto pos  = c.pos;
            ia::nextTick([uid, slot, pos] {
                auto level = ll::service::getLevel();
                if (!level) return;
                if (auto* p = level->getPlayer(uid)) showForm(*p, slot, pos);
            });
            return ia::Result::Consume;
        });
    }
    void uninstall() {
        if (mHandler >= 0) ia::remove(mHandler);
        mHandler = -1;
    }

private:
    ia::HandlerId mHandler = -1;
};

} // namespace

} // namespace gf::enchant

GF_REGISTER_MODULE(gf::enchant::AnvilFallbackModule);
