// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/CharmEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "death" (F10): the death pipeline, TF's CharmEvents host (rule R8: one die hook).
//
// When an entity dies (the outermost of Mob / Player / ServerPlayer::die):
//   1. `onLethal` handlers (higher priority first) may keep it alive (charm of life, TF's
//      LivingDeathEvent at HIGHEST): the handler sets the health; the death does not happen
//      (a health of 0 left by the handler becomes 1).
//   2. A player that really dies, not in creative / spectator and without keepInventory (TF's
//      applyKeepingAndCasket at HIGH): every keeper sees every stack of the inventory (0..35),
//      the armour (head, chest, legs, feet) and the off-hand, in that order, as the player's
//      real slot stacks (a keeper may compare addresses with the player's inventory to know the
//      slot); a stack a keeper keeps leaves the inventory and is stored in the world DB
//      ("gf:combat:kept:<uuid>", TF's TFCharmInventory persistent tag). Items in the
//      twilightforest:kept_on_death tag are always kept (the lowest keeper).
//   3. `onDropsRemaining` handlers get the stacks still in the inventory and may take any of them
//      (the keepsake casket); what they leave is dropped by the game.
//   4. The game's death runs; a player killer fires progression's onKilled; a mob's drops are
//      the loot contract's (feature/loot).
// On respawn the kept items go back to their slots (or anywhere free, or at the player's feet)
// and the `onRespawn` handlers run (TF's returnItemsOnRespawn).

#include "feature/death/Death.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/GameType.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/combat/CombatInternal.h"
#include "feature/common/Interactions.h"
#include "feature/death/DeathInternal.h"
#include "feature/loot/LootInternal.h"
#include "feature/progression/ProgressionInternal.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::death {

namespace {

template <class Fn>
struct Entry {
    int priority;
    Fn  fn;
};

struct Pipeline {
    std::mutex                    mutex;
    std::vector<Entry<LethalFn>>  lethal;
    std::vector<Entry<KeeperFn>>  keepers;
    std::vector<DropsFn>          drops;
    std::vector<RespawnFn>        respawn;
};

Pipeline& pipeline() {
    static Pipeline p;
    return p;
}

template <class Fn>
void addSorted(std::vector<Entry<Fn>>& list, int priority, Fn fn) {
    list.push_back({priority, std::move(fn)});
    std::stable_sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.priority > b.priority; });
}

thread_local std::vector<Actor const*> tDying;

std::string keptKey(Player const& p) { return "gf:combat:kept:" + p.getUuid().asString(); }

bool keepInventoryRule() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::KeepInventory);
    return level->getGameRules().getBool(id, false);
}

/// One slot of the dying player's inventory.
struct SlotRef {
    enum class Kind { Inventory, Armor, Offhand } kind;
    int index = 0;
};

std::string slotName(SlotRef const& s) {
    switch (s.kind) {
    case SlotRef::Kind::Inventory: return "inv:" + std::to_string(s.index);
    case SlotRef::Kind::Armor: return "armor:" + std::to_string(s.index);
    default: return "offhand";
    }
}

ItemStack& stackAt(Player& p, SlotRef const& s) {
    switch (s.kind) {
    case SlotRef::Kind::Inventory: return const_cast<ItemStack&>(p.getInventory().getItem(s.index));
    case SlotRef::Kind::Armor: return const_cast<ItemStack&>(p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(s.index)));
    default: return const_cast<ItemStack&>(p.getOffhandSlot());
    }
}

void setAt(Player& p, SlotRef const& s, ItemStack const& stack) {
    switch (s.kind) {
    case SlotRef::Kind::Inventory: p.getInventory().setItem(s.index, stack); break;
    case SlotRef::Kind::Armor: p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(s.index), stack); break;
    default: p.setOffhandSlot(stack); break;
    }
}

std::optional<SlotRef> refOf(std::string const& slot) {
    if (slot.rfind("inv:", 0) == 0) return SlotRef{SlotRef::Kind::Inventory, std::atoi(slot.c_str() + 4)};
    if (slot.rfind("armor:", 0) == 0) return SlotRef{SlotRef::Kind::Armor, std::atoi(slot.c_str() + 6)};
    if (slot == "offhand") return SlotRef{SlotRef::Kind::Offhand, 0};
    return std::nullopt;
}

/// A kept record whose stacks all still sit in their slots: the player's inventory as it was before
/// the death came back (the server stopped after the record was written and before the player was
/// saved without the stacks). Returning it would duplicate the stacks.
bool staleRecord(Player& player, nlohmann::json const& doc) {
    if (!doc.is_array() || doc.empty()) return false;
    for (auto const& e : doc) {
        auto ref   = refOf(e.value("slot", std::string{}));
        auto stack = game::items::fromJson(e.value("item", nlohmann::json{}));
        if (!ref || ref->index < 0 || game::items::isEmpty(stack)) return false; // can't tell: trust it
        if (ref->kind == SlotRef::Kind::Inventory && ref->index >= player.getInventory().getContainerSize()) return false;
        auto const& cur = stackAt(player, *ref);
        if (!game::items::sameItemSameData(cur, stack) || game::items::countOf(cur) != game::items::countOf(stack)) return false;
    }
    return true;
}

/// Forgets a stale kept record (staleRecord); true when it was one.
bool dropStaleRecord(Player& player) {
    auto doc = game::db::get(keptKey(player));
    if (!doc || !staleRecord(player, *doc)) return false;
    game::db::erase(keptKey(player));
    log().warn("death: ignored the kept items of {}: they are still in the inventory (restored from before the death)", player.getRealName());
    return true;
}

std::vector<SlotRef> allSlots(Player& p) {
    std::vector<SlotRef> out;
    int                  n = std::min(p.getInventory().getContainerSize(), 36);
    for (int i = 0; i < n; ++i) out.push_back({SlotRef::Kind::Inventory, i});
    for (int i = 0; i < 4; ++i) out.push_back({SlotRef::Kind::Armor, i});
    out.push_back({SlotRef::Kind::Offhand, 0});
    return out;
}

/// TF's keeping (keepers) and casket (drops remaining) on a real player death.
void keepAndOffer(Player& player) {
    if (player.isCreative() || player.isSpectator() || keepInventoryRule()) return;
    std::vector<Entry<KeeperFn>> keepers;
    std::vector<DropsFn>         drops;
    {
        std::lock_guard lock(pipeline().mutex);
        keepers = pipeline().keepers;
        drops   = pipeline().drops;
    }
    auto           slots = allSlots(player);
    nlohmann::json kept  = nlohmann::json::array();
    // a record not returned yet joins this one (unless it is a stale copy of what the player holds)
    if (auto previous = game::db::get(keptKey(player)); previous && previous->is_array() && !dropStaleRecord(player)) kept = *previous;
    int keptCount = 0;
    for (auto const& slot : slots) {
        ItemStack& stack = stackAt(player, slot);
        if (game::items::isEmpty(stack)) continue;
        for (auto const& k : keepers) {
            bool keep = false;
            try {
                keep = k.fn(player, stack);
            } catch (std::exception const& ex) {
                log().error("death: keeper failed: {}", ex.what());
            }
            if (!keep) continue;
            if (!game::items::isEmpty(stack)) {
                kept.push_back({{"slot", slotName(slot)}, {"item", game::items::toJson(stack)}});
                ++keptCount;
            }
            setAt(player, slot, game::items::empty());
            break;
        }
    }
    if (keptCount > 0) {
        game::db::put(keptKey(player), kept);
        log().info("death: {} keeps {} stack(s) through death", player.getRealName(), keptCount);
    }
    if (drops.empty()) return;
    // the drops left: copies the handlers may take (empty / shrink) or change
    std::vector<SlotRef>   refs;
    std::vector<ItemStack> list;
    for (auto const& slot : slots) {
        auto const& s = stackAt(player, slot);
        if (game::items::isEmpty(s)) continue;
        refs.push_back(slot);
        list.push_back(s);
    }
    if (list.empty()) return;
    auto before = list;
    for (auto const& fn : drops) {
        try {
            fn(player, list);
        } catch (std::exception const& ex) {
            log().error("death: drops handler failed: {}", ex.what());
        }
    }
    int taken = 0;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        ItemStack const& now = i < list.size() ? list[i] : game::items::empty();
        if (game::items::isEmpty(now)) {
            setAt(player, refs[i], game::items::empty());
            ++taken;
        } else if (!game::items::sameItemSameData(now, before[i]) || game::items::countOf(now) != game::items::countOf(before[i])) {
            setAt(player, refs[i], now);
        }
    }
    if (taken > 0) log().info("death: {} stack(s) of {} were taken before the drops", taken, player.getRealName());
}

/// The player credited with the kill (Java's lastHurtByPlayer), nullptr when none.
Player* killerPlayer(Actor& victim, Actor* attacker) {
    if (attacker && attacker->isPlayer() && attacker != &victim) return static_cast<Player*>(attacker);
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    // Java's lastHurtByPlayer only counts for 100 ticks after the hit (lastHurtByPlayerTime)
    if (victim.mLastHurtByPlayerTime <= 0) return nullptr;
    if (auto* p = level->getPlayer(*victim.mLastHurtByPlayerId); p && p != &victim) return p;
    return nullptr;
}

} // namespace

// ---- contract -------------------------------------------------------------------------------------

void onLethal(int priority, LethalFn fn) {
    std::lock_guard lock(pipeline().mutex);
    addSorted(pipeline().lethal, priority, std::move(fn));
}

void registerKeeper(int priority, KeeperFn fn) {
    std::lock_guard lock(pipeline().mutex);
    addSorted(pipeline().keepers, priority, std::move(fn));
}

void onDropsRemaining(DropsFn fn) {
    std::lock_guard lock(pipeline().mutex);
    pipeline().drops.push_back(std::move(fn));
}

void onRespawn(RespawnFn fn) {
    std::lock_guard lock(pipeline().mutex);
    pipeline().respawn.push_back(std::move(fn));
}

// ---- internal --------------------------------------------------------------------------------------

namespace {
std::uint64_t nowTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}

/// Victims kept alive this tick (ActorUniqueID) -> the health their handler left and the tick.
thread_local std::unordered_map<std::int64_t, std::pair<float, std::uint64_t>> tSurvived;

/// Entities whose death the pipeline handled: a second `die` for the same death (the game may call
/// it again) must not drop the loot or fire the kill again. A player's entry lasts its life (it
/// goes when the player respawns); a mob's goes once the dead mob is removed from the level (its
/// ActorUniqueID is never used again), checked by the sweep.
struct Handled {
    std::uint64_t tick   = 0;
    bool          player = false;
};
std::unordered_map<std::int64_t, Handled>& handledDeaths() {
    static std::unordered_map<std::int64_t, Handled> m;
    return m;
}

constexpr int kHandledSweepTicks = 20;

/// Forgets the deaths of mobs that are gone (every kHandledSweepTicks ticks).
void sweepHandledDeaths() {
    auto& handled = handledDeaths();
    if (handled.empty()) return;
    auto level = ll::service::getLevel();
    if (!level || level->getCurrentTick().tickID % kHandledSweepTicks != 0) return;
    std::erase_if(handled, [&](auto const& e) {
        if (e.second.player) return false;
        ActorUniqueID id;
        id.rawID = e.first;
        return level->fetchEntity(id, true) == nullptr;
    });
}
} // namespace

bool deathHandled(std::int64_t actorId) { return handledDeaths().contains(actorId); }

std::size_t handledDeathCount() { return handledDeaths().size(); }

void reapplySurvival(Actor& victim) {
    if (tSurvived.empty()) return;
    auto it = tSurvived.find(victim.getOrCreateUniqueID().rawID);
    if (it == tSurvived.end()) return;
    auto [health, tick] = it->second;
    tSurvived.erase(it);
    if (tick != nowTick()) return; // a survival of an earlier tick (a die outside a hurt)
    if (static_cast<float>(victim.getHealth()) != health) setHealth(victim, health);
}

// the server headers only expose the const attribute map; the map itself is the actor's own
void setHealth(Actor& a, float value) {
    auto* map = const_cast<BaseAttributeMap*>(static_cast<BaseAttributeMap const*>(a.getAttributes()));
    (void)map->setCurrentValue(SharedAttributes::HEALTH(), value);
}

bool runLethal(Actor& victim, combat::DamageInfo const& damage) {
    std::vector<Entry<LethalFn>> lethal;
    {
        std::lock_guard lock(pipeline().mutex);
        lethal = pipeline().lethal;
    }
    for (auto const& e : lethal) {
        bool survive = false;
        try {
            survive = e.fn(victim, damage);
        } catch (std::exception const& ex) {
            log().error("death: lethal handler failed: {}", ex.what());
        }
        if (survive) {
            if (victim.getHealth() <= 0) setHealth(victim, 1.f);
            if (tSurvived.size() > 64) std::erase_if(tSurvived, [now = nowTick()](auto const& e) { return e.second.second != now; });
            auto  id     = victim.getOrCreateUniqueID().rawID;
            float health = static_cast<float>(std::max(1, victim.getHealth()));
            tSurvived[id] = {health, nowTick()};
            // a die outside a hurt (a command, a game path that kills directly) gets no
            // reapplySurvival from the hurt hook: the next tick puts the survivor's health back
            // if the game left the lethal one (not when it really died since: a later death of the
            // same tick is handled and keeps its 0)
            interactions::nextTick([id, health] {
                auto level = ll::service::getLevel();
                if (!level || handledDeaths().contains(id)) return;
                ActorUniqueID uid;
                uid.rawID = id;
                if (auto* a = level->fetchEntity(uid, false); a && a->getHealth() <= 0) setHealth(*a, health);
            });
            log().info("death: {} survived lethal {} damage", victim.isPlayer() ? static_cast<Player&>(victim).getRealName() : victim.getTypeName(), damage.type);
            return true;
        }
    }
    return false;
}

bool returnKeptItems(Player& player) {
    auto doc = game::db::get(keptKey(player));
    if (!doc || !doc->is_array()) return false;
    if (dropStaleRecord(player)) return false;
    game::db::erase(keptKey(player));
    int returned = 0;
    for (auto const& e : *doc) {
        ItemStack stack = game::items::fromJson(e.value("item", nlohmann::json{}));
        if (game::items::isEmpty(stack)) continue;
        SlotRef ref    = refOf(e.value("slot", std::string{})).value_or(SlotRef{SlotRef::Kind::Inventory, -1});
        bool    placed = false;
        if (ref.index >= 0 && game::items::isEmpty(stackAt(player, ref))) {
            setAt(player, ref, stack);
            placed = true;
        }
        if (!placed && !player.add(stack) && !game::items::isEmpty(stack)) {
            auto& region = player.getDimensionBlockSource();
            player.getLevel().getSpawner().spawnItem(region, stack, nullptr, player.getPosition(), 0);
        }
        ++returned;
    }
    player.sendInventory(false);
    log().info("death: returned {} kept stack(s) to {}", returned, player.getRealName());
    return returned > 0;
}

void handleDeath(Actor& self, ActorDamageSource const& source, std::function<void()> const& die) {
    if (self.isClientSide() || std::find(tDying.begin(), tDying.end(), &self) != tDying.end()) return die();
    tDying.push_back(&self);
    struct Pop {
        ~Pop() { tDying.pop_back(); }
    } pop;
    auto selfId = self.getOrCreateUniqueID().rawID;
    // a second die for a death already handled (the game may call it again): one entry per life (a
    // player's goes when it respawns, a mob's when the dead mob is removed)
    if (handledDeaths().contains(selfId)) return die();
    auto [attacker, direct] = combat::entitiesOf(source);
    combat::DamageInfo info{self, combat::typeOf(self, source), attacker, direct, 0.f};
    if (runLethal(self, info)) return;
    handledDeaths()[selfId] = Handled{nowTick(), self.isPlayer()};
    Player*     killer   = killerPlayer(self, attacker);
    std::int64_t killerId = killer ? killer->getOrCreateUniqueID().rawID : 0;
    if (self.isPlayer() && !static_cast<Player&>(self).isClientSide()) keepAndOffer(static_cast<Player&>(self));
    bool wasPlayer = self.isPlayer();
    // a mob's own drops (Bedrock's loot, equipment) are collected for the onEntityDrops handlers
    std::vector<std::int64_t> gameDrops;
    if (wasPlayer) die();
    else {
        loot::DropCapture capture;
        die();
        gameDrops = capture.ids();
    }
    // the victim and the killer may be gone now: look the killer up again (R16)
    if (killerId) {
        ActorUniqueID id;
        id.rawID = killerId;
        auto level = ll::service::getLevel();
        if (auto* k = level ? level->getPlayer(id) : nullptr) progression::fireKilled(*k, self, info.type);
    }
    if (!wasPlayer) loot::onMobDied(self, info, killerId, gameDrops);
}

void forgetDeath(Player const& player) { handledDeaths().erase(const_cast<Player&>(player).getOrCreateUniqueID().rawID); }

} // namespace gf::death

// ---- hooks (the outermost one runs the pipeline) --------------------------------------------------

LL_TYPE_INSTANCE_HOOK(DeathMobDieHook, ll::memory::HookPriority::Normal, Mob, &Mob::$die, void, ActorDamageSource const& source) {
    gf::death::handleDeath(*this, source, [&] { origin(source); });
}

LL_TYPE_INSTANCE_HOOK(DeathPlayerDieHook, ll::memory::HookPriority::Normal, Player, &Player::$die, void, ActorDamageSource const& source) {
    gf::death::handleDeath(*this, source, [&] { origin(source); });
}

LL_TYPE_INSTANCE_HOOK(DeathServerPlayerDieHook, ll::memory::HookPriority::Normal, ServerPlayer, &ServerPlayer::$die, void, ActorDamageSource const& source) {
    gf::death::handleDeath(*this, source, [&] { origin(source); });
}

namespace gf::death {
namespace {

class DeathModule {
public:
    void install() {
        DeathMobDieHook::hook();
        DeathPlayerDieHook::hook();
        DeathServerPlayerDieHook::hook();
        mRespawn = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerRespawnEvent>([](ll::event::PlayerRespawnEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            forgetDeath(player); // a new life: its death is handled again
            returnKeptItems(player);
            std::vector<RespawnFn> respawn;
            {
                std::lock_guard lock(pipeline().mutex);
                respawn = pipeline().respawn;
            }
            for (auto const& fn : respawn) {
                try {
                    fn(player);
                } catch (std::exception const& ex) {
                    log().error("death: respawn handler failed: {}", ex.what());
                }
            }
        });
        // a player that comes back alive with the stacks of a kept record still in its slots: the
        // record is a stale copy (the server stopped before the player was saved after its death)
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            if (!ev.self().isClientSide()) dropStaleRecord(ev.self());
        });
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { sweepHandledDeaths(); });
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { handledDeaths().clear(); }); // ticks restart per world
            // TF: items in twilightforest:kept_on_death are always kept (lowest keeper; F9's tag data)
            registerKeeper(-100000, [](Player&, ItemStack& stack) {
                auto id = game::items::idOf(stack);
                return !id.empty() && tags::has(tags::Registry::Item, "twilightforest:kept_on_death", id);
            });
        }
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mRespawn);
        ll::event::EventBus::getInstance().removeListener(mJoin);
        ll::event::EventBus::getInstance().removeListener(mTick);
        DeathServerPlayerDieHook::unhook();
        DeathPlayerDieHook::unhook();
        DeathMobDieHook::unhook();
    }

private:
    ll::event::ListenerPtr mRespawn;
    ll::event::ListenerPtr mJoin;
    ll::event::ListenerPtr mTick;
};

} // namespace
} // namespace gf::death

GF_REGISTER_MODULE_ORDERED(gf::death::DeathModule, gf::ModuleOrder::Contracts);
