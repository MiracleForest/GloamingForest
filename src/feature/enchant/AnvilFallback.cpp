// The vanilla-client fallback of the anvil for custom enchantments (F9; ext/item/Enchant.h).
//
// A vanilla Bedrock client computes the anvil's result itself and offers none when the second item
// brings custom enchantments only (an enchanted book of TF's from loot: its enchantments live in the
// item's user data). The server's merge (Enchant.cpp's anvil hook) never runs for it, so a vanilla
// player who uses an anvil holding such a book also gets a form (the client opens its own anvil
// screen on the use whatever the server answers, and drops a form that arrives meanwhile: the form
// is sent once the anvil's container has closed, and a chat line says so): one button
// per inventory item the book makes a result for, with Java's AnvilMenu#createResult numbers (the
// merged enchantments, the level cost with the prior work penalty, "Too Expensive!" from 40 levels
// in survival; any item for a player who builds instantly); a choice is AnvilMenu#onTake (the
// levels, the book, the prior work of the result, and the anvil: 12 % chance of a damage step unless
// the player has infinite materials, the use or break sound), taken only while the menu would still
// be valid (the anvil in place, the player within reach of it) and the slots hold what the button
// showed. Players with the client mod (companions) and books that carry vanilla enchantments too go
// through the anvil screen.
#include <algorithm>
#include <climits>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/legacy/ActorUniqueID.h"
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

/// EnchantmentHelper#canStoreEnchantments: every 1.21 item carries the ENCHANTMENTS component
/// (DataComponents.COMMON_ITEM_COMPONENTS), so any stack can take the book. Whether an enchantment
/// joins it is AnvilMenu's flag1 (mergeAnvil): the item supports it, or the player builds instantly
/// (a creative player puts any book on any item), or the item is a book.
bool canStoreEnchantments(ItemStack const& stack) { return !items::isEmpty(stack); }

std::map<std::int64_t, int>& fallbackCounts() {
    static std::map<std::int64_t, int> m;
    return m;
}
std::map<std::int64_t, int>& formCounts() {
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

int anvilFormCount(Player& player) {
    std::lock_guard lock(countsMutex());
    auto            it = formCounts().find(player.getOrCreateUniqueID().rawID);
    return it == formCounts().end() ? 0 : it->second;
}

bool stillAtAnvil(Player const& player, BlockPos const& anvil) {
    // ItemCombinerMenu#stillValid -> Player#canInteractWithBlock(pos, 4.0):
    // new AABB(pos).distanceToSqr(getEyePosition()) < (blockInteractionRange() + 4.0)^2
    Vec3   eye   = player.getEyePos();
    double range = (instabuild(player) ? 5.0 : 4.5) + 4.0;
    auto   axis  = [](double v, int lo) { return v < lo ? lo - v : (v > lo + 1 ? v - (lo + 1) : 0.0); };
    double dx = axis(eye.x, anvil.x), dy = axis(eye.y, anvil.y), dz = axis(eye.z, anvil.z);
    return dx * dx + dy * dy + dz * dz < range * range;
}

AnvilChoiceResult takeAnvilChoice(Player& player, AnvilChoice const& choice) {
    auto& region = player.getDimensionBlockSource();
    auto  pos    = game::blockPos(choice.anvil);
    if (game::globalPos(region, pos) != choice.anvil || !isAnvil(game::blockIdAt(region, pos))) return AnvilChoiceResult::NoAnvil;
    if (!stillAtAnvil(player, pos)) return AnvilChoiceResult::TooFar;
    auto& inv = player.getInventory();
    if (choice.bookSlot < 0 || choice.targetSlot < 0 || choice.bookSlot >= inv.getContainerSize() || choice.targetSlot >= inv.getContainerSize())
        return AnvilChoiceResult::Changed;
    if (!items::sameItemSameData(inv.getItem(choice.bookSlot), choice.book) || !items::sameItemSameData(inv.getItem(choice.targetSlot), choice.target))
        return AnvilChoiceResult::Changed;
    auto now = anvilOffer(player, choice.bookSlot, choice.targetSlot);
    if (!now || now->cost != choice.cost) return AnvilChoiceResult::Changed;
    return applyAnvilOffer(player, choice.bookSlot, choice.targetSlot, &pos) ? AnvilChoiceResult::Applied : AnvilChoiceResult::NotApplied;
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
            internal::AnvilChoice choice{bookSlot, o.targetSlot, anvil, book, player.getInventory().getItem(o.targetSlot), o.cost};
            auto text = items::displayName(choice.target) + "\n" + trFor(player, "gf.item.anvil.cost", "Enchantment Cost: {}", o.cost);
            form.appendButton(text, [choice](Player& p) {
                // the form's answer comes later: AnvilMenu#stillValid and the slots as the button showed them
                auto result = internal::takeAnvilChoice(p, choice);
                if (result == internal::AnvilChoiceResult::TooFar) p.sendMessage(trFor(p, "gf.item.anvil.away", "You are too far from the anvil."));
                else if (result != internal::AnvilChoiceResult::Applied && result != internal::AnvilChoiceResult::NotApplied)
                    log().info("enchant: anvil form choice of {} dropped ({})", p.getRealName(), result == internal::AnvilChoiceResult::NoAnvil ? "no anvil" : "changed");
            });
        }
    }
    form.sendTo(player);
    {
        std::lock_guard lock(countsMutex());
        ++formCounts()[player.getOrCreateUniqueID().rawID];
    }
    log().info("enchant: anvil form for {} ({} offers)", player.getRealName(), offers.size());
}

/// A form sent while a client screen is open is dropped by the client, and a vanilla client opens its
/// anvil screen on the use whatever the server answers: the form goes out once the player's container
/// (the anvil's, server side) has closed again, or right away when none opened within a second.
constexpr int kOpenWait   = 20;           ///< ticks for the anvil screen to open
constexpr int kCloseWait  = 20 * 60 * 5;  ///< at most this long at the anvil
constexpr int kAfterClose = 10;           ///< the client's screen is gone

enum class Phase { WaitOpen, WaitClose, AfterClose };

/// Players with a form on the way (one at a time: repeated uses do not queue more).
std::set<std::int64_t>& pendingForms() {
    static std::set<std::int64_t> s;
    return s;
}

void formWhenClosed(ActorUniqueID uid, int slot, core::GlobalPos pos, Phase phase, int ticks) {
    ia::nextTick([=] {
        auto done  = [&] { pendingForms().erase(uid.rawID); };
        auto level = ll::service::getLevel();
        if (!level) return done();
        auto* p = level->getPlayer(uid);
        if (!p) return done();
        bool open = p->mContainerManager != nullptr;
        switch (phase) {
        case Phase::WaitOpen:
            if (open) formWhenClosed(uid, slot, pos, Phase::WaitClose, 0);
            else if (ticks >= kOpenWait) formWhenClosed(uid, slot, pos, Phase::AfterClose, 0); // no screen opened
            else formWhenClosed(uid, slot, pos, Phase::WaitOpen, ticks + 1);
            return;
        case Phase::WaitClose:
            if (!open) formWhenClosed(uid, slot, pos, Phase::AfterClose, 0);
            else if (ticks < kCloseWait) formWhenClosed(uid, slot, pos, Phase::WaitClose, ticks + 1);
            else {
                log().info("enchant: anvil form for {} given up (the anvil stayed open)", p->getRealName());
                done();
            }
            return;
        case Phase::AfterClose:
            if (open) formWhenClosed(uid, slot, pos, Phase::WaitClose, 0); // opened again
            else if (ticks < kAfterClose) formWhenClosed(uid, slot, pos, Phase::AfterClose, ticks + 1);
            else {
                done();
                showForm(*p, slot, pos);
            }
            return;
        }
    });
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
            // a vanilla client opens its own anvil screen on a use (sneaking or not) whatever the
            // server answers: the anvil works as usual, and the form follows when its screen is
            // closed (a chat line says so)
            if (!c.firstEvent) return ia::Result::Pass; // the held button's repeats
            auto const& uid = c.player.getOrCreateUniqueID();
            if (!pendingForms().insert(uid.rawID).second) return ia::Result::Pass; // one on the way
            c.player.sendMessage(trFor(c.player, "gf.item.anvil.hint", "Close the anvil to choose what this book goes on."));
            formWhenClosed(uid, slot, c.pos, Phase::WaitOpen, 0);
            return ia::Result::Pass;
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
