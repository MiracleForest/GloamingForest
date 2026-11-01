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
// Tooltips become lore written by the server (in the holder's language): vanilla clients and the
// client mod show the same lines. Bars (setBar) are drawn by the client mod (client/item).
#include "feature/item/Item.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/Container.h"
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

void cooldown(Player& player, std::string const& category, int ticks) {
    if (ticks <= 0) return;
    player.startItemCooldown(HashedString(fullId(category)), ticks, true);
}

int cooldownLeft(Player const& player, std::string const& category) {
    return std::max(0, player.getItemCooldownLeft(HashedString(fullId(category))));
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
    return used;
}

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

    auto& inv      = player.getInventory();
    int   size     = inv.getContainerSize();
    int   selected = player.getSelectedItemSlot();
    for (int slot = 0; slot < size; ++slot) {
        process(inv.getItem(slot), [&](ItemStack const& n) { inv.setItem(slot, n); }, slot, slot == selected, -1);
    }
    std::array<std::string, 4> now;
    for (int a = 0; a < 4; ++a) {
        auto slot = static_cast<SharedTypes::Legacy::ArmorSlot>(a);
        process(player.getArmor(slot), [&](ItemStack const& n) { player.setArmor(slot, n); }, a, false, a);
        now[static_cast<std::size_t>(a)] = game::items::idOf(player.getArmor(slot));
    }
    process(player.getOffhandSlot(), [&](ItemStack const& n) { player.setOffhandSlot(n); }, 0, false, -1);

    // equip / unequip: armour ids that changed since the last tick
    auto  uid  = player.getOrCreateUniqueID().rawID;
    auto& prev = worn()[uid];
    for (std::size_t a = 0; a < 4; ++a) {
        if (prev[a] == now[a]) continue;
        auto slot = static_cast<SharedTypes::Legacy::ArmorSlot>(a);
        if (!prev[a].empty())
            if (auto const* b = behaviourOf(prev[a]); b && b->onUnequip) {
                auto removed = game::items::make(prev[a]).value_or(ItemStack::EMPTY_ITEM());
                guarded("onUnequip", prev[a], b->onUnequip, player, removed, static_cast<int>(a));
            }
        if (!now[a].empty())
            if (auto const* b = behaviourOf(now[a]); b && b->onEquip) guarded("onEquip", now[a], b->onEquip, player, player.getArmor(slot), static_cast<int>(a));
        prev[a] = now[a];
    }

    // use ticks: the item being used (Java's onUseTick with the remaining use duration)
    auto const& inUse = player.mItemInUse->mItem.get();
    if (!inUse.isNull()) {
        Behaviour const* b = nullptr;
        std::string      id;
        {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            if (auto const* e = entryOf(inUse); e && e->hasBehaviour && e->behaviour.useTick) {
                b  = &e->behaviour;
                id = e->id;
            }
        }
        if (b) {
            auto fn = b->useTick;
            guarded("useTick", id, fn, player, const_cast<ItemStack&>(inUse), player.getItemUseDuration());
        }
    }
    if (sync) player.sendInventory(false);
}

} // namespace internal

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
    auto const* e = it::entryOf(this);
    if (!e || !e->hasBehaviour || !e->behaviour.release || !player || player->isClientSide()) return origin(item, player, durationLeft);
    auto fn   = e->behaviour.release;
    int  used = std::max(0, getMaxUseDuration(&item) - durationLeft);
    it::guarded("release", e->id, fn, *player, item, used);
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
    auto r = origin(item, level, player);
    auto const* e = it::entryOf(this);
    if (!e || !e->hasBehaviour || !e->behaviour.finishUsing || !player || player->isClientSide()) return r;
    auto fn = e->behaviour.finishUsing;
    it::guarded("finishUsing", e->id, fn, *player, item);
    return r;
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
    auto const* e = it::entryOf(this);
    if (!e || !e->hasBehaviour || !e->behaviour.hurtEnemy || actor.isClientSide()) return origin(item, actor, attacker);
    auto fn = e->behaviour.hurtEnemy;
    if (it::guarded("hurtEnemy", e->id, fn, item, actor, static_cast<Actor&>(attacker))) return; // the behaviour used the item
    origin(item, actor, attacker);
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
    auto const* e = it::entryOf(this);
    if (!e || !e->hasBehaviour || !e->behaviour.destroySpeed) return origin(stack, block);
    auto fn    = e->behaviour.destroySpeed;
    auto copy  = it::internal::stackOf(stack);
    auto speed = it::guarded("destroySpeed", e->id, fn, static_cast<ItemStack const&>(copy), block);
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
    auto const* e = it::entryOf(this);
    if (!e || !e->hasBehaviour || !e->behaviour.repairWith) return origin(stack, repairItem, version);
    auto fn = e->behaviour.repairWith;
    auto a  = it::internal::stackOf(stack);
    auto b  = it::internal::stackOf(repairItem);
    return it::guarded("repairWith", e->id, fn, static_cast<ItemStack const&>(a), static_cast<ItemStack const&>(b));
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
    auto const* e = it::entryOf(this);
    if (!e || !e->dispense) return origin(region, container, slot, pos, face);
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
    if (*used) container.setItem(slot, stack);
    return *used || origin(region, container, slot, pos, face);
}

// Extra melee damage of the held item (knightmetal against armoured targets, ...): where Bedrock adds
// the damage of Sharpness, Smite and Bane of Arthropods.
LL_STATIC_HOOK(ItemMeleeBonusHook, ll::memory::HookPriority::Normal, &EnchantUtils::getMeleeDamageBonus, float, Actor const& victim, Actor const& attacker) {
    float bonus = origin(victim, attacker);
    if (!attacker.isPlayer() || attacker.isClientSide()) return bonus;
    auto& player = const_cast<Player&>(static_cast<Player const&>(attacker));
    auto const& held   = player.getSelectedItem();
    auto const* e      = it::entryOf(held);
    if (!e || !e->hasBehaviour || !e->behaviour.attackBonus) return bonus;
    auto fn = e->behaviour.attackBonus;
    return bonus + it::guarded("attackBonus", e->id, fn, held, const_cast<Actor&>(victim), player);
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
                Behaviour const* b = internal::behaviourOf(ctx.itemId);
                if (!b || !b->use) return ia::Result::Pass;
                auto fn = b->use;
                auto r  = guarded("use", ctx.itemId, fn, ctx.player, ia::mutableItem(ctx.item));
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
            Behaviour const* b = internal::behaviourOf(ctx.itemId);
            if (!b || !b->mineBlock) return;
            auto  fn     = b->mineBlock;
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
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { worn().clear(); });
        }
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
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
    std::vector<ia::HandlerId> mHandlers;
};

} // namespace
} // namespace gf::item

GF_REGISTER_MODULE_ORDERED(gf::item::ItemModule, gf::ModuleOrder::Contracts);
