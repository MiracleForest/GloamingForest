// Contract "item" (F9; spike S6): TF's item behaviours on Bedrock items.
//
// TF's items are data-driven BP items (ComponentItem, F1's skeleton) whose behaviour is native and
// registered by id. One hook per engine event (rule R8):
// - right-click: the shared interaction dispatch (onUse, low priority, after protection and units);
// - releasing a charged use, the use finishing, hitting an entity, mining speed, anvil repair
//   material and dispensers: ComponentItem's virtuals, looked up by the item's name hash;
// - a mined block: the interaction dispatch's onDestroyed;
// - extra melee damage: EnchantUtils::getMeleeDamageBonus (where Bedrock adds Sharpness & co.);
// - inventory / armour ticks, equip changes, use ticks and the lore refresh: one server tick pass
//   over the players.
// hurtEnemy returning true lets the item's own post-hit handling run (Java's postHurtEnemy: the
// durability loss); a registered dispense function is the item's only dispense behaviour (false:
// the item stays and the fail sound plays, TF's OptionalDispenseItemBehavior). See ext/item/Item.h.
// Tooltips become lore written by the server (in the holder's language): vanilla clients and the
// client mod show the same lines. Bars (setBar) are drawn by the client mod (client/item).
#include "feature/item/Item.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/server/ServerPlayer.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/Container.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/ItemUseMethod.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::item {

namespace {

struct Entry {
    std::string            id;
    Behaviour              behaviour;
    bool                   hasBehaviour = false;
    std::vector<TooltipFn> tooltips;
    DispenseFn             dispense;
    BarFn                  bar;
};

struct Store {
    std::recursive_mutex                   mutex;
    std::unordered_map<std::uint64_t, Entry> byHash; ///< by HashedString(id).getHash()
};

Store& store() {
    static Store s;
    return s;
}

std::uint64_t hashOf(std::string const& id) { return HashedString(id).getHash(); }

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

Entry& entryFor(std::string const& id) {
    auto  full = fullId(id);
    auto& e    = store().byHash[hashOf(full)];
    e.id       = full;
    return e;
}

/// The entry of an item (nullptr when nothing is registered for it). Hooks call this a lot: the
/// item's name hash, no string.
Entry const* entryOf(Item const* item) {
    if (!item) return nullptr;
    auto& s  = store();
    auto  it = s.byHash.find(item->mFullName->getHash());
    return it == s.byHash.end() ? nullptr : &it->second;
}

Entry const* entryOf(ItemStackBase const& stack) { return stack.isNull() ? nullptr : entryOf(stack.mItem.get()); }

Entry const* entryOf(std::string const& id) {
    auto& s  = store();
    auto  it = s.byHash.find(hashOf(fullId(id)));
    return it == s.byHash.end() ? nullptr : &it->second;
}

/// A copy of one behaviour function of an item (empty when none), taken under the lock: the hooks
/// also run on the client thread (prediction) while units and selftests register on the server
/// thread, and a callback must never run from inside the registry (it may unregister itself).
template <class Fn>
Fn behaviourFn(Entry const* e, Fn Behaviour::*member, std::string* id) {
    if (!e || !e->hasBehaviour) return {};
    if (id) *id = e->id;
    return e->behaviour.*member;
}
template <class Fn>
Fn behaviourFn(Item const* item, Fn Behaviour::*member, std::string* id = nullptr) {
    if (!item) return {};
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return behaviourFn(entryOf(item), member, id);
}
template <class Fn>
Fn behaviourFn(std::string const& itemId, Fn Behaviour::*member, std::string* id = nullptr) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return behaviourFn(entryOf(itemId), member, id);
}

bool changedStack(ItemStack const& before, ItemStack const& after) {
    return game::items::countOf(before) != game::items::countOf(after) || !game::items::sameItemSameData(before, after)
        || before.getDamageValue() != after.getDamageValue();
}

template <class Fn, class... Args>
auto guarded(char const* what, std::string const& id, Fn const& fn, Args&&... args) -> decltype(fn(std::forward<Args>(args)...)) {
    try {
        return fn(std::forward<Args>(args)...);
    } catch (std::exception const& e) {
        log().error("item: {} of {} failed: {}", what, id, e.what());
        if constexpr (!std::is_void_v<decltype(fn(std::forward<Args>(args)...))>) return {};
    }
}

/// The armour ids each player wore at the last tick (equip / unequip), by unique id.
std::unordered_map<std::int64_t, std::array<std::string, 4>>& worn() {
    static std::unordered_map<std::int64_t, std::array<std::string, 4>> m;
    return m;
}

std::uint64_t& tickCount() {
    static std::uint64_t t = 0;
    return t;
}

} // namespace

// ---- registration --------------------------------------------------------------------------------

void registerItem(std::string const& itemId, Behaviour behaviour) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           e = entryFor(itemId);
    if (e.hasBehaviour) log().warn("item: {} registered twice; the last behaviour wins", e.id);
    e.behaviour    = std::move(behaviour);
    e.hasBehaviour = true;
}

Json getData(ItemStack const& stack, std::string const& key) { return game::items::getData(stack, key); }
void setData(ItemStack& stack, std::string const& key, Json const& value) { game::items::setData(stack, key, value); }

// The category as given: Bedrock's categories (an item's minecraft:cooldown category, vanilla's
// "ender_pearl") have no namespace, and the client's overlay matches the item's category exactly.
void cooldown(Player& player, std::string const& category, int ticks) {
    if (ticks <= 0 || category.empty()) return;
    player.startItemCooldown(HashedString(category), ticks, true);
}

int cooldownLeft(Player const& player, std::string const& category) {
    if (category.empty()) return 0;
    return std::max(0, player.getItemCooldownLeft(HashedString(category)));
}

void addTooltip(std::string const& itemId, TooltipFn fn) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    entryFor(itemId).tooltips.push_back(std::move(fn));
}

void registerDispense(std::string const& itemId, DispenseFn fn) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    entryFor(itemId).dispense = std::move(fn);
}

void setBar(std::string const& itemId, BarFn fn) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    entryFor(itemId).bar = std::move(fn);
}

// ---- internals -------------------------------------------------------------------------------------

namespace internal {

ItemStack stackOf(ItemStackBase const& base) {
    if (base.isNull()) return ItemStack::EMPTY_ITEM();
    auto ctx = SaveContextFactory::createCloneSaveContext();
    if (auto tag = base.save(*ctx)) {
        auto s = ItemStack::fromTag(*tag);
        if (!s.isNull()) return s;
    }
    auto s = game::items::make(base.getTypeName(), base.mCount, base.getAuxValue()).value_or(ItemStack::EMPTY_ITEM());
    if (!s.isNull() && base.mUserData) s.setUserData(base.mUserData->clone());
    return s;
}

std::vector<std::string> tooltipLines(ItemStack const& stack, Player const* viewer) {
    std::vector<std::string> lines;
    if (game::items::isEmpty(stack)) return lines;
    std::vector<TooltipFn> fns;
    std::string            id;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (auto const* e = entryOf(stack)) {
            fns = e->tooltips;
            id  = e->id;
        }
    }
    for (auto const& fn : fns) guarded("tooltip", id, fn, stack, lines, viewer);
    for (auto& l : enchant::internal::loreLines(stack, viewer)) lines.push_back(std::move(l));
    return lines;
}

bool hasTooltip(ItemStack const& stack) {
    if (game::items::isEmpty(stack)) return false;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (auto const* e = entryOf(stack); e && !e->tooltips.empty()) return true;
    }
    auto const* tag = game::items::userData(stack);
    return tag && (tag->contains("gf:lore") || tag->contains("gf:enchant"));
}

bool refreshLore(ItemStack& stack, Player const* viewer) {
    if (game::items::isEmpty(stack)) return false;
    std::vector<std::string> mine;
    for (auto& line : tooltipLines(stack, viewer)) mine.push_back((line.starts_with("\xC2\xA7") ? "\xC2\xA7r" : "\xC2\xA7r\xC2\xA7" "7") + line);
    auto old = game::items::getData(stack, kLoreKey);
    std::vector<std::string> previous;
    if (old.is_array())
        for (auto const& o : old)
            if (o.is_string()) previous.push_back(o.get<std::string>());
    if (previous == mine) return false;
    auto lore = stack.getCustomLore();
    for (auto const& p : previous)
        if (auto it = std::find(lore.begin(), lore.end(), p); it != lore.end()) lore.erase(it);
    lore.insert(lore.begin(), mine.begin(), mine.end());
    if (lore.empty()) stack.clearCustomLore();
    else stack.setCustomLore(lore);
    game::items::setData(stack, kLoreKey, mine.empty() ? Json() : Json(mine));
    return true;
}

std::optional<Bar> bar(ItemStack const& stack) {
    BarFn fn;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto const*     e = entryOf(stack);
        if (!e || !e->bar) return std::nullopt;
        fn = e->bar;
    }
    try {
        auto b = fn(stack);
        if (b) b->fraction = std::clamp(b->fraction, 0.0f, 1.0f);
        return b;
    } catch (...) {
        return std::nullopt;
    }
}

bool hasBar(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto const*     e = entryOf(itemId);
    return e && e->bar;
}

bool hasBar(Item const* item) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto const*     e = entryOf(item);
    return e && e->bar;
}

Behaviour const* behaviourOf(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto const*     e = entryOf(itemId);
    return e && e->hasBehaviour ? &e->behaviour : nullptr;
}

std::optional<bool> dispense(BlockSource& region, BlockPos const& dispenser, int facing, ItemStack& stack) {
    DispenseFn  fn;
    std::string id;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto const*     e = entryOf(stack);
        if (!e || !e->dispense) return std::nullopt;
        fn = e->dispense;
        id = e->id;
    }
    bool used = guarded("dispense", id, fn, region, dispenser, facing, stack);
    if (used) log().info("item: dispensed {} at {} {} {}", id, dispenser.x, dispenser.y, dispenser.z);
    else log().info("item: dispensing {} at {} {} {} failed (kept)", id, dispenser.x, dispenser.y, dispenser.z);
    return used;
}

void forgetWorn(Player const& player) { worn().erase(const_cast<Player&>(player).getOrCreateUniqueID().rawID); }

void unregister(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.byHash.erase(hashOf(fullId(itemId)));
}

void tickPlayer(Player& player, bool lore) {
    bool sync = false;
    // one stack: its behaviour's ticks, then the lore; `set` writes a changed copy back
    auto process = [&](ItemStack const& current, auto&& set, int slot, bool selected, int armorSlot) {
        if (current.isNull()) return;
        Behaviour behaviour;
        bool      hasInv = false, hasArmor = false;
        std::string id;
        {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            if (auto const* e = entryOf(current); e && e->hasBehaviour) {
                hasInv   = static_cast<bool>(e->behaviour.inventoryTick);
                hasArmor = armorSlot >= 0 && static_cast<bool>(e->behaviour.armorTick);
                if (hasInv || hasArmor) behaviour = e->behaviour;
                id = e->id;
            }
        }
        bool doLore = lore && hasTooltip(current);
        if (!hasInv && !hasArmor && !doLore) return;
        ItemStack copy = current;
        if (hasInv) guarded("inventoryTick", id, behaviour.inventoryTick, copy, player, slot, selected);
        if (hasArmor && !copy.isNull()) guarded("armorTick", id, behaviour.armorTick, copy, player, armorSlot);
        if (doLore && !copy.isNull()) refreshLore(copy, &player);
        if (changedStack(current, copy)) {
            set(copy);
            sync = true;
        }
    };

    // Java's Inventory#tick: every compartment (items, armor, offhand) with the stack's index in it
    // and `selected == index` (so armour slot i and the offhand count as selected when the hotbar
    // selection is i / 0); Java's armour list runs feet (0) .. head (3)
    auto& inv      = player.getInventory();
    int   size     = inv.getContainerSize();
    int   selected = player.getSelectedItemSlot();
    for (int slot = 0; slot < size; ++slot) {
        process(inv.getItem(slot), [&](ItemStack const& n) { inv.setItem(slot, n); }, slot, slot == selected, -1);
    }
    std::array<std::string, 4> now;
    for (int a = 0; a < 4; ++a) {
        auto slot      = static_cast<SharedTypes::Legacy::ArmorSlot>(a);
        int  javaIndex = 3 - a;
        process(player.getArmor(slot), [&](ItemStack const& n) { player.setArmor(slot, n); }, javaIndex, selected == javaIndex, a);
        now[static_cast<std::size_t>(a)] = game::items::idOf(player.getArmor(slot));
    }
    process(player.getOffhandSlot(), [&](ItemStack const& n) { player.setOffhandSlot(n); }, 0, selected == 0, -1);

    // equip / unequip: armour ids that changed since the last tick
    auto  uid  = player.getOrCreateUniqueID().rawID;
    auto& prev = worn()[uid];
    for (std::size_t a = 0; a < 4; ++a) {
        if (prev[a] == now[a]) continue;
        auto slot = static_cast<SharedTypes::Legacy::ArmorSlot>(a);
        if (!prev[a].empty())
            if (auto fn = behaviourFn(prev[a], &Behaviour::onUnequip)) {
                auto removed = game::items::make(prev[a]).value_or(ItemStack::EMPTY_ITEM());
                guarded("onUnequip", prev[a], fn, player, removed, static_cast<int>(a));
            }
        if (!now[a].empty())
            if (auto fn = behaviourFn(now[a], &Behaviour::onEquip)) guarded("onEquip", now[a], fn, player, player.getArmor(slot), static_cast<int>(a));
        prev[a] = now[a];
    }

    // use ticks: the item being used (Java's onUseTick with the remaining use duration), on the
    // stack in the hand it is used from: a change goes back to that slot and to the item in use
    auto& itemInUse = player.mItemInUse.get();
    if (!itemInUse.mItem->isNull()) {
        std::string id;
        // getItemUseDuration counts down: Bedrock passes it to releaseUsing as durationLeft
        if (auto fn = behaviourFn(itemInUse.mItem->mItem.get(), &Behaviour::useTick, &id)) {
            ItemStack before = itemInUse.mItem.get();
            ItemStack copy   = before;
            guarded("useTick", id, fn, player, copy, player.getItemUseDuration());
            if (changedStack(before, copy)) {
                itemInUse.mItem.get() = copy;
                if (itemInUse.mSlot->mContainerId == ContainerID::Offhand) {
                    player.setOffhandSlot(copy);
                } else {
                    int at = itemInUse.mSlot->mSlot;
                    if (at < 0 || at >= size || game::items::idOf(inv.getItem(at)) != game::items::idOf(before)) at = selected;
                    if (game::items::idOf(inv.getItem(at)) == game::items::idOf(before)) inv.setItem(at, copy);
                }
                sync = true;
            }
        }
    }
    if (sync) player.sendInventory(false);
}

} // namespace internal

// ---- extension (ext/item/Item.h) ---------------------------------------------------------------------

namespace ext {

std::optional<Bar>       barOf(ItemStack const& stack) { return internal::bar(stack); }
std::vector<std::string> tooltipLines(ItemStack const& stack, Player const* viewer) { return internal::tooltipLines(stack, viewer); }
bool                     refreshTooltip(ItemStack& stack, Player const* viewer) { return internal::refreshLore(stack, viewer); }

namespace {
struct Transforms {
    std::mutex                                                     mutex;
    std::map<std::string, std::map<std::string, DisplayTransform>> byItem; ///< item -> context -> transform
};
Transforms& transforms() {
    static Transforms t;
    return t;
}
} // namespace

void setDisplayTransform(std::string const& itemId, std::string const& context, DisplayTransform const& transform) {
    auto&           t = transforms();
    std::lock_guard lock(t.mutex);
    t.byItem[fullId(itemId)][context] = transform;
}

void clearTransforms(std::string const& itemId) {
    auto&           t = transforms();
    std::lock_guard lock(t.mutex);
    t.byItem.erase(fullId(itemId));
}

std::optional<DisplayTransform> displayTransform(std::string const& itemId, std::string const& context) {
    auto&           t = transforms();
    std::lock_guard lock(t.mutex);
    auto            it = t.byItem.find(fullId(itemId));
    if (it == t.byItem.end()) return std::nullopt;
    // ItemDisplayContext's fallback chain: JARRED (TFEnumExtensions#ItemDisplayContext_JARRED) -> FIXED
    for (std::string c = context; !c.empty(); c = c == kJarred ? "fixed" : "") {
        if (auto d = it->second.find(c); d != it->second.end()) return d->second;
    }
    return std::nullopt;
}

} // namespace ext

void internal::clearDisplayTransforms(std::string const& itemId) { ext::clearTransforms(itemId); }

} // namespace gf::item

// ---- hooks ---------------------------------------------------------------------------------------------

namespace it = gf::item;

LL_TYPE_INSTANCE_HOOK(
    ItemReleaseUsingHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$releaseUsing,
    void,
    ItemStack& item,
    Player*    player,
    int        durationLeft
) {
    std::string id;
    auto        fn = it::behaviourFn(this, &it::Behaviour::release, &id);
    if (!fn || !player || player->isClientSide()) return origin(item, player, durationLeft);
    int used = std::max(0, getMaxUseDuration(&item) - durationLeft);
    it::guarded("release", id, fn, *player, item, used);
    origin(item, player, durationLeft);
}

LL_TYPE_INSTANCE_HOOK(
    ItemUseTimeDepletedHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$useTimeDepleted,
    ItemUseMethod,
    ItemStack& item,
    Level*     level,
    Player*    player
) {
    // the behaviour first, on the stack as it was used (Java's finishUsingItem; TF's overrides call
    // super for the default part), then the item's own finish (eating, ...)
    std::string id;
    auto        fn = it::behaviourFn(this, &it::Behaviour::finishUsing, &id);
    if (fn && player && !player->isClientSide()) it::guarded("finishUsing", id, fn, *player, item);
    return origin(item, level, player);
}

LL_TYPE_INSTANCE_HOOK(
    ItemHurtActorHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$hurtActor,
    void,
    ItemStack& item,
    Actor&     actor,
    Mob&       attacker
) {
    // Java's Player#attack: hurtEnemy, and when it returns true postHurtEnemy, where the item loses
    // its durability (Bedrock: ComponentItem's own hurtActor)
    std::string id;
    auto        fn = it::behaviourFn(this, &it::Behaviour::hurtEnemy, &id);
    if (!fn || actor.isClientSide()) return origin(item, actor, attacker);
    if (it::guarded("hurtEnemy", id, fn, item, actor, static_cast<Actor&>(attacker))) origin(item, actor, attacker);
}

LL_TYPE_INSTANCE_HOOK(
    ItemDestroySpeedHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$getDestroySpeed,
    float,
    ItemStackBase const& stack,
    Block const&         block
) {
    std::string id;
    auto        fn = it::behaviourFn(this, &it::Behaviour::destroySpeed, &id);
    if (!fn) return origin(stack, block);
    auto copy  = it::internal::stackOf(stack);
    auto speed = it::guarded("destroySpeed", id, fn, static_cast<ItemStack const&>(copy), block);
    return speed ? *speed : origin(stack, block);
}

LL_TYPE_INSTANCE_HOOK(
    ItemRepairHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$isValidRepairItem,
    bool,
    ItemStackBase const&   stack,
    ItemStackBase const&   repairItem,
    BaseGameVersion const& version
) {
    std::string id;
    auto        fn = it::behaviourFn(this, &it::Behaviour::repairWith, &id);
    if (!fn) return origin(stack, repairItem, version);
    auto a = it::internal::stackOf(stack);
    auto b = it::internal::stackOf(repairItem);
    return it::guarded("repairWith", id, fn, static_cast<ItemStack const&>(a), static_cast<ItemStack const&>(b));
}

LL_TYPE_INSTANCE_HOOK(
    ItemDispenseHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    // DispenserBlock::dispenseFrom passes the dispense position: the block centre + 0.7 * facing
    static constexpr int dx[] = {0, 0, 0, 0, -1, 1}, dy[] = {-1, 1, 0, 0, 0, 0}, dz[] = {0, 0, -1, 1, 0, 0};
    int      f = face < 6 ? face : 0;
    BlockPos dispenser{
        static_cast<int>(std::floor(pos.x - 0.7 * dx[f])),
        static_cast<int>(std::floor(pos.y - 0.7 * dy[f])),
        static_cast<int>(std::floor(pos.z - 0.7 * dz[f])),
    };
    ItemStack stack = container.getItem(slot);
    auto      used  = it::internal::dispense(region, dispenser, f, stack);
    if (!used) return origin(region, container, slot, pos, face);
    container.setItem(slot, stack); // the behaviour's stack either way (Java returns it from execute)
    if (*used) return true;
    // TF's OptionalDispenseItemBehavior: an unsuccessful dispense keeps the item and plays the
    // dispenser's fail sound (level event 1001) instead of the default ejection
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::SoundClickFail,
        Vec3{static_cast<float>(dispenser.x) + 0.5f, static_cast<float>(dispenser.y) + 0.5f, static_cast<float>(dispenser.z) + 0.5f},
        0,
        nullptr
    );
    return true;
}

// Extra melee damage of the held item (knightmetal against armoured targets, ...): where Bedrock adds
// the damage of Sharpness, Smite and Bane of Arthropods.
LL_STATIC_HOOK(ItemMeleeBonusHook, ll::memory::HookPriority::Normal, &EnchantUtils::getMeleeDamageBonus, float, Actor const& victim, Actor const& attacker) {
    float bonus = origin(victim, attacker);
    if (!attacker.isPlayer() || attacker.isClientSide()) return bonus;
    auto&       player = const_cast<Player&>(static_cast<Player const&>(attacker));
    auto const& held   = player.getSelectedItem();
    if (held.isNull()) return bonus;
    std::string id;
    auto        fn = it::behaviourFn(held.mItem.get(), &it::Behaviour::attackBonus, &id);
    if (!fn) return bonus;
    return bonus + it::guarded("attackBonus", id, fn, held, const_cast<Actor&>(victim), player);
}

namespace gf::item {
namespace {

namespace ia = gf::interactions;

class ItemModule {
public:
    void install() {
        ItemReleaseUsingHook::hook();
        ItemUseTimeDepletedHook::hook();
        ItemHurtActorHook::hook();
        ItemDestroySpeedHook::hook();
        ItemRepairHook::hook();
        ItemDispenseHook::hook();
        ItemMeleeBonusHook::hook();

        // right-click with an item (Java's Item#use): after landmark protection and the units'
        // own handlers, before vanilla
        mHandlers.push_back(ia::onUse(
            ia::Match{},
            [](ia::UseContext const& ctx) {
                if (ctx.itemId.empty()) return ia::Result::Pass;
                auto fn = behaviourFn(ctx.itemId, &Behaviour::use);
                if (!fn) return ia::Result::Pass;
                auto r = guarded("use", ctx.itemId, fn, ctx.player, ia::mutableItem(ctx.item));
                switch (r) {
                case Result::Pass:
                    return ia::Result::Pass;
                case Result::Fail:
                    return ia::Result::Deny;
                default:
                    return ia::Result::Consume;
                }
            },
            ia::priority::Low
        ));
        // a mined block (Java's Item#mineBlock): after vanilla broke it
        mHandlers.push_back(ia::onDestroyed(ia::Match{}, [](ia::DestroyContext const& ctx, bool destroyed) {
            if (!destroyed || ctx.itemId.empty()) return;
            auto fn = behaviourFn(ctx.itemId, &Behaviour::mineBlock);
            if (!fn) return;
            auto& region = ctx.player.getDimensionBlockSource();
            guarded("mineBlock", ctx.itemId, fn, ia::mutableItem(ctx.item), region, BlockPos{ctx.pos.x, ctx.pos.y, ctx.pos.z}, ctx.block, ctx.player);
        }));

        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            auto level = ll::service::getLevel();
            if (!level) return;
            auto t = ++tickCount();
            level->forEachPlayer([t](Player& p) {
                if (!ia::isServerPlayer(p)) return true;
                try {
                    // the lore once a second per player (staggered)
                    bool lore = (t + static_cast<std::uint64_t>(p.getOrCreateUniqueID().rawID)) % 20 == 0;
                    internal::tickPlayer(p, lore);
                } catch (std::exception const& e) {
                    log().error("item: player tick failed: {}", e.what());
                }
                return true;
            });
        });
        // a player who leaves forgets the armour it wore: when it comes back, its worn armour is
        // equipped again (Java compares with the empty equipment of a new player entity)
        mLeave = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            internal::forgetWorn(ev.self());
        });
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { worn().clear(); });
        }
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        ll::event::EventBus::getInstance().removeListener(mLeave);
        for (auto id : mHandlers) ia::remove(id);
        mHandlers.clear();
        ItemMeleeBonusHook::unhook();
        ItemDispenseHook::unhook();
        ItemRepairHook::unhook();
        ItemDestroySpeedHook::unhook();
        ItemHurtActorHook::unhook();
        ItemUseTimeDepletedHook::unhook();
        ItemReleaseUsingHook::unhook();
        worn().clear();
    }

private:
    ll::event::ListenerPtr     mTick;
    ll::event::ListenerPtr     mLeave;
    std::vector<ia::HandlerId> mHandlers;
};

} // namespace
} // namespace gf::item

GF_REGISTER_MODULE_ORDERED(gf::item::ItemModule, gf::ModuleOrder::Contracts);
