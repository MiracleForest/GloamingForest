// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/MissingAdvancementToast.java
//   src/main/java/gloamingforest/network/MissingAdvancementToastPacket.java
//   src/main/java/gloamingforest/util/PlayerHelper.java
//   src/main/java/gloamingforest/util/Restriction.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "progression" (F10): the progression store, the trigger bus and the central events.
//
// - Store: per player (UUID) the criteria done per advancement, in the world DB under
//   "gf:combat:progress:<uuid>". Criteria and requirements come from the advancement JSON of the
//   data packs (<mod>/data/<ns>/advancement/**.json, Java format; U14 owns them); an advancement
//   without a known definition completes with its first grant. onGranted fires once when the
//   last requirement is met.
// - Trigger bus: trigger(player, id, data) -> the onTrigger listeners of `id` (U14's criteria
//   engine, fixtures), in registration order.
// - Events (one engine hook each): inventory changed (Player::inventoryChanged, coalesced to once
//   per player and tick, as "minecraft:inventory_changed"), killed (the death pipeline, as
//   "minecraft:player_killed_entity"), consumed (Player::completeUsingItem, as
//   "minecraft:consume_item"), dimension changed (polled every tick, as
//   "minecraft:changed_dimension"), biome entered (polled every second), and the per-second tick.
// - Companions get their completed advancements over "combat.progress" (the client view of
//   progression::client) and the missing advancement toast over "combat.missing_advancement";
//   vanilla clients get the toast as a ToastRequestPacket.

#include "feature/progression/Progression.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ToastRequestPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"

#include "feature/dimension/World.h"
#include "feature/gamerules/GameRules.h"
#include "feature/net/Net.h"
#include "feature/progression/ProgressionInternal.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::progression {

namespace {

/// An advancement's criteria and requirements (AND of ORs) and its display.
struct Definition {
    std::vector<std::string>              criteria;
    std::vector<std::vector<std::string>> requirements;
    bool                                  hasDisplay = false; ///< the JSON has a "display" (Java's DisplayInfo)
    std::string                           title;              ///< translation key or text
    bool                                  titleIsKey = false;
    std::string                           icon;               ///< item id
};

struct PlayerProgress {
    std::map<std::string, std::set<std::string>> criteria; ///< advancement -> done criteria
    std::set<std::string>                        completed;
};

struct State {
    std::recursive_mutex                                  mutex;
    std::unordered_map<std::string, Definition>           definitions;
    bool                                                  definitionsLoaded = false;
    std::unordered_map<std::string, PlayerProgress>       players; ///< uuid -> progress (loaded lazily)
    std::unordered_map<std::string, std::vector<TriggerFn>> triggers;
    std::vector<GrantedFn>                                granted;
    // events
    std::vector<std::function<void(Player&)>>                                     inventoryChanged;
    std::vector<std::function<void(Player&, Actor&, std::string const&)>>          killed;
    std::vector<std::function<void(Player&, ItemStack const&)>>                    consumed;
    std::vector<std::function<void(Player&, int, int)>>                            dimensionChanged;
    std::vector<std::function<void(Player&, std::string const&)>>                  biomeEntered;
    std::vector<std::function<void(Player&)>>                                      tick;
    // polling
    std::unordered_map<std::int64_t, int>         lastDim;
    std::unordered_map<std::int64_t, std::string> lastBiome;
    std::set<std::int64_t>                        inventoryDirty;
    std::uint64_t                                 tickCount = 0;
};

State& state() {
    static State s;
    return s;
}

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

std::string dbKey(std::string const& uuid) { return "gf:combat:progress:" + uuid; }

// ---- definitions ---------------------------------------------------------------------------------

std::string textOf(nlohmann::json const& component, bool& isKey) {
    isKey = false;
    if (component.is_string()) return component.get<std::string>();
    if (component.is_object()) {
        if (component.contains("translate")) {
            isKey = true;
            return component.value("translate", std::string{});
        }
        return component.value("text", std::string{});
    }
    return {};
}

void loadDefinitionFile(std::filesystem::path const& file, std::string const& id, State& s) {
    try {
        std::ifstream in(file);
        auto          j = nlohmann::json::parse(in, nullptr, true, true);
        Definition    d;
        if (auto c = j.find("criteria"); c != j.end() && c->is_object())
            for (auto const& [name, _] : c->items()) d.criteria.push_back(name);
        if (auto r = j.find("requirements"); r != j.end() && r->is_array()) {
            for (auto const& group : *r) {
                std::vector<std::string> g;
                if (group.is_array())
                    for (auto const& n : group)
                        if (n.is_string()) g.push_back(n.get<std::string>());
                if (!g.empty()) d.requirements.push_back(std::move(g));
            }
        }
        if (d.requirements.empty())
            for (auto const& c : d.criteria) d.requirements.push_back({c}); // Java: AND of every criterion
        if (auto disp = j.find("display"); disp != j.end() && disp->is_object()) {
            d.hasDisplay = true;
            d.title      = textOf(disp->value("title", nlohmann::json{}), d.titleIsKey);
            auto icon    = disp->value("icon", nlohmann::json::object());
            d.icon       = icon.is_object() ? icon.value("id", icon.value("item", std::string{})) : std::string{};
        }
        s.definitions[id] = std::move(d);
    } catch (std::exception const& ex) {
        log().warn("progression: bad advancement {}: {}", file.string(), ex.what());
    }
}

void loadDefinitions(State& s) {
    s.definitionsLoaded = true;
    s.definitions.clear();
    std::filesystem::path data;
    try {
        data = Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(data, ec)) return;
    for (auto const& ns : std::filesystem::directory_iterator(data, ec)) {
        if (!ns.is_directory()) continue;
        for (char const* dir : {"advancement", "advancements"}) {
            auto root = ns.path() / dir;
            if (!std::filesystem::is_directory(root, ec)) continue;
            for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
                if (f.path().extension() != ".json") continue;
                auto rel = std::filesystem::relative(f.path(), root, ec).replace_extension().generic_string();
                loadDefinitionFile(f.path(), ns.path().filename().string() + ":" + rel, s);
            }
        }
    }
}

Definition const* definition(std::string const& id) {
    auto& s = state();
    if (!s.definitionsLoaded) loadDefinitions(s);
    auto it = s.definitions.find(id);
    return it == s.definitions.end() ? nullptr : &it->second;
}

// ---- store ---------------------------------------------------------------------------------------

PlayerProgress& progressOf(std::string const& uuid) {
    auto& s  = state();
    auto  it = s.players.find(uuid);
    if (it != s.players.end()) return it->second;
    PlayerProgress p;
    if (auto doc = game::db::get(dbKey(uuid)); doc && doc->is_object()) {
        for (auto const& [adv, list] : doc->value("criteria", nlohmann::json::object()).items())
            for (auto const& c : list)
                if (c.is_string()) p.criteria[adv].insert(c.get<std::string>());
        for (auto const& a : doc->value("completed", nlohmann::json::array()))
            if (a.is_string()) p.completed.insert(a.get<std::string>());
    }
    return s.players.emplace(uuid, std::move(p)).first->second;
}

void save(std::string const& uuid, PlayerProgress const& p) {
    nlohmann::json crit = nlohmann::json::object();
    for (auto const& [adv, set] : p.criteria) crit[adv] = std::vector<std::string>(set.begin(), set.end());
    game::db::put(dbKey(uuid), {{"criteria", crit}, {"completed", std::vector<std::string>(p.completed.begin(), p.completed.end())}});
}

bool requirementsMet(Definition const& d, std::set<std::string> const& done) {
    if (d.requirements.empty()) return !done.empty();
    for (auto const& group : d.requirements) {
        bool any = std::any_of(group.begin(), group.end(), [&](std::string const& c) { return done.contains(c); });
        if (!any) return false;
    }
    return true;
}

void syncTo(Player& player) {
    if (!net::isCompanion(player)) return;
    std::vector<std::string> done;
    {
        std::lock_guard lock(state().mutex);
        auto const&     p = progressOf(uuidOf(player));
        done.assign(p.completed.begin(), p.completed.end());
    }
    net::send(player, "combat.progress", {{"done", done}});
}

template <class List, class... Args>
void fire(List const& list, char const* what, Args&&... args) {
    for (auto const& fn : list) {
        try {
            fn(args...);
        } catch (std::exception const& ex) {
            log().error("progression: {} listener failed: {}", what, ex.what());
        }
    }
}

} // namespace

bool has(Player const& player, std::string const& advancementId) {
    std::lock_guard lock(state().mutex);
    return progressOf(uuidOf(player)).completed.contains(advancementId);
}

bool done(Player const& player, std::string const& advancementId, std::string const& criterion) {
    std::lock_guard lock(state().mutex);
    auto const&     p = progressOf(uuidOf(player));
    if (p.completed.contains(advancementId) && criterion.empty()) return true;
    auto it = p.criteria.find(advancementId);
    return it != p.criteria.end() && it->second.contains(criterion);
}

void grant(Player& player, std::string const& advancementId, std::string const& criterion) {
    bool completedNow = false;
    {
        std::lock_guard lock(state().mutex);
        auto            uuid = uuidOf(player);
        auto&           p    = progressOf(uuid);
        if (p.completed.contains(advancementId)) return;
        auto const* def  = definition(advancementId);
        auto&       done = p.criteria[advancementId];
        if (criterion.empty()) {
            if (def)
                for (auto const& c : def->criteria) done.insert(c);
            completedNow = true;
        } else {
            done.insert(criterion);
            completedNow = !def || requirementsMet(*def, done);
        }
        if (completedNow) p.completed.insert(advancementId);
        save(uuid, p);
    }
    if (!completedNow) return;
    log().info("progression: {} completed {}", player.getRealName(), advancementId);
    std::vector<GrantedFn> listeners;
    {
        std::lock_guard lock(state().mutex);
        listeners = state().granted;
    }
    fire(listeners, "granted", player, advancementId);
    syncTo(player);
}

void revoke(Player& player, std::string const& advancementId, std::string const& criterion) {
    {
        std::lock_guard lock(state().mutex);
        auto            uuid = uuidOf(player);
        auto&           p    = progressOf(uuid);
        if (criterion.empty()) p.criteria.erase(advancementId);
        else if (auto it = p.criteria.find(advancementId); it != p.criteria.end()) it->second.erase(criterion);
        auto const* def  = definition(advancementId);
        auto        it   = p.criteria.find(advancementId);
        bool        keep = it != p.criteria.end() && def && requirementsMet(*def, it->second);
        if (!keep) p.completed.erase(advancementId);
        save(uuid, p);
    }
    syncTo(player);
}

void trigger(Player& player, std::string const& triggerId, Json const& data) {
    std::vector<TriggerFn> listeners;
    {
        std::lock_guard lock(state().mutex);
        auto            it = state().triggers.find(triggerId);
        if (it == state().triggers.end()) return;
        listeners = it->second;
    }
    fire(listeners, triggerId.c_str(), player, data);
}

void onTrigger(std::string const& triggerId, TriggerFn fn) {
    std::lock_guard lock(state().mutex);
    state().triggers[triggerId].push_back(std::move(fn));
}

void onGranted(GrantedFn fn) {
    std::lock_guard lock(state().mutex);
    state().granted.push_back(std::move(fn));
}

MissingToast missingToastFor(std::string const& advancementId) {
    // upstream: info == null ? (no_title, the portal miniature) : (info.getTitle(), info.getIcon())
    MissingToast t{kMissingNoTitle, true, kMissingIcon};
    std::lock_guard lock(state().mutex);
    if (auto const* d = definition(advancementId); d && d->hasDisplay) {
        t.title      = d->title;
        t.titleIsKey = d->titleIsKey;
        t.icon       = d->icon;
    }
    return t;
}

void showMissing(Player& player, std::string const& advancementId) {
    auto t = missingToastFor(advancementId);
    log().info("progression: missing advancement toast {} for {}", advancementId, player.getRealName());
    if (net::isCompanion(player)) {
        net::send(player, "combat.missing_advancement", {{"advancement", advancementId}, {"title", t.title}, {"titleIsKey", t.titleIsKey}, {"icon", t.icon}});
        return;
    }
    // vanilla clients: the game's toast with the same two lines (no icon: the packet has none)
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::ToastRequest);
    if (!packet) return;
    auto& pkt    = static_cast<ToastRequestPacket&>(*packet);
    pkt.mTitle   = trFor(player, kMissingUpperText, "Advancement Required:");
    // an untranslated key reads as the key, as Java's Component.translatable (upstream has no text for
    // gloamingforest.ui.advancement.no_title)
    pkt.mContent = t.titleIsKey ? trFor(player, t.title, t.title) : t.title;
    pkt.sendTo(player);
}

namespace events {
void onInventoryChanged(std::function<void(Player&)> fn) {
    std::lock_guard lock(state().mutex);
    state().inventoryChanged.push_back(std::move(fn));
}
void onKilled(std::function<void(Player&, Actor&, std::string const&)> fn) {
    std::lock_guard lock(state().mutex);
    state().killed.push_back(std::move(fn));
}
void onConsumed(std::function<void(Player&, ItemStack const&)> fn) {
    std::lock_guard lock(state().mutex);
    state().consumed.push_back(std::move(fn));
}
void onDimensionChanged(std::function<void(Player&, int, int)> fn) {
    std::lock_guard lock(state().mutex);
    state().dimensionChanged.push_back(std::move(fn));
}
void onBiomeEntered(std::function<void(Player&, std::string const&)> fn) {
    std::lock_guard lock(state().mutex);
    state().biomeEntered.push_back(std::move(fn));
}
void onTick(std::function<void(Player&)> fn) {
    std::lock_guard lock(state().mutex);
    state().tick.push_back(std::move(fn));
}
} // namespace events

// ---- F10-internal ---------------------------------------------------------------------------------

void fireKilled(Player& killer, Actor& victim, std::string const& damageType) {
    decltype(State::killed) listeners;
    {
        std::lock_guard lock(state().mutex);
        listeners = state().killed;
    }
    fire(listeners, "killed", killer, victim, damageType);
    trigger(killer, "minecraft:player_killed_entity", {{"entity", victim.getTypeName()}, {"damage_type", damageType}});
}

void forgetPlayer(Player const& player) {
    auto            id = const_cast<Player&>(player).getOrCreateUniqueID().rawID;
    std::lock_guard lock(state().mutex);
    auto&           s = state();
    s.players.erase(uuidOf(player)); // saved on every change: read back on the next use
    s.lastDim.erase(id);
    s.lastBiome.erase(id);
    s.inventoryDirty.erase(id);
}

std::string biomeIdAt(Player& player) {
    auto pos = player.getPosition();
    BlockPos bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    if (world::isGloaming(player.getDimension())) {
        auto id = worldgen::biomeAt(bp);
        if (!id.empty()) return id;
    }
    auto& region = player.getDimensionBlockSource();
    if (!region.hasChunksAt(bp, 0, false)) return {};
    std::string name = region.getBiome(bp).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

namespace {

void onLevelTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& s      = state();
    bool  second = ++s.tickCount % 20 == 0;
    std::vector<std::pair<std::int64_t, std::pair<int, int>>> dimChanges;
    std::vector<std::pair<std::int64_t, std::string>>         biomeChanges;
    std::set<std::int64_t>                                    dirty;
    {
        std::lock_guard lock(s.mutex);
        dirty.swap(s.inventoryDirty);
    }
    level->forEachPlayer([&](Player& p) {
        if (p.isClientSide()) return true;
        auto id  = p.getOrCreateUniqueID().rawID;
        int  dim = static_cast<int>(p.getDimensionId());
        {
            std::lock_guard lock(s.mutex);
            auto            it = s.lastDim.find(id);
            if (it == s.lastDim.end()) s.lastDim[id] = dim;
            else if (it->second != dim) {
                dimChanges.push_back({id, {it->second, dim}});
                it->second = dim;
            }
        }
        if (second) {
            auto biome = biomeIdAt(p);
            std::lock_guard lock(s.mutex);
            auto&           last = s.lastBiome[id];
            if (!biome.empty() && biome != last) {
                last = biome;
                biomeChanges.push_back({id, biome});
            }
        }
        return true;
    });
    auto playerOf = [&](std::int64_t raw) -> Player* {
        ActorUniqueID uid;
        uid.rawID = raw;
        return level->getPlayer(uid);
    };
    decltype(State::inventoryChanged) inv;
    decltype(State::dimensionChanged) dims;
    decltype(State::biomeEntered)     biomes;
    decltype(State::tick)             ticks;
    {
        std::lock_guard lock(s.mutex);
        inv    = s.inventoryChanged;
        dims   = s.dimensionChanged;
        biomes = s.biomeEntered;
        ticks  = s.tick;
    }
    for (auto raw : dirty)
        if (auto* p = playerOf(raw)) {
            fire(inv, "inventory changed", *p);
            trigger(*p, "minecraft:inventory_changed", {});
        }
    for (auto const& [raw, change] : dimChanges)
        if (auto* p = playerOf(raw)) {
            fire(dims, "dimension changed", *p, change.first, change.second);
            trigger(*p, "minecraft:changed_dimension", {{"from", change.first}, {"to", change.second}});
        }
    for (auto const& [raw, biome] : biomeChanges)
        if (auto* p = playerOf(raw)) {
            fire(biomes, "biome entered", *p, biome);
            trigger(*p, "minecraft:location", {{"biome", biome}});
        }
    if (second && !ticks.empty()) {
        level->forEachPlayer([&](Player& p) {
            if (!p.isClientSide()) fire(ticks, "tick", p);
            return true;
        });
    }
}

} // namespace

} // namespace gf::progression

LL_TYPE_INSTANCE_HOOK(
    ProgressionInventoryChangedHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::inventoryChanged,
    void,
    Container&         container,
    int                slot,
    ItemStack const&   oldItem,
    ItemStack const&   newItem,
    bool               forceBalanced
) {
    origin(container, slot, oldItem, newItem, forceBalanced);
    if (isClientSide()) return;
    auto&           s = gf::progression::state();
    std::lock_guard lock(s.mutex);
    s.inventoryDirty.insert(getOrCreateUniqueID().rawID);
}

LL_TYPE_INSTANCE_HOOK(ProgressionCompleteUsingHook, ll::memory::HookPriority::Normal, Player, &Player::completeUsingItem, void) {
    if (isClientSide()) return origin();
    ItemStack used = mItemInUse->mItem;
    origin();
    if (gf::game::items::isEmpty(used)) return;
    decltype(gf::progression::State::consumed) listeners;
    {
        auto&           s = gf::progression::state();
        std::lock_guard lock(s.mutex);
        listeners = s.consumed;
    }
    gf::progression::fire(listeners, "consumed", *this, used);
    gf::progression::trigger(*this, "minecraft:consume_item", {{"item", gf::game::items::idOf(used)}});
}

namespace gf::progression {

namespace {

class ProgressionModule {
public:
    void install() {
        ProgressionInventoryChangedHook::hook();
        ProgressionCompleteUsingHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onLevelTick(); });
        // a player who leaves: its cached progress and polling state go
        mDisconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            if (!ev.self().isClientSide()) forgetPlayer(ev.self());
        });
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                std::lock_guard lock(state().mutex);
                loadDefinitions(state());
            });
            game::db::onWorldClosed([] {
                std::lock_guard lock(state().mutex);
                auto&           s = state();
                s.players.clear();
                s.lastDim.clear();
                s.lastBiome.clear();
                s.inventoryDirty.clear();
            });
            net::onCompanionJoined([](Player& p) { syncTo(p); });
        }
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        ll::event::EventBus::getInstance().removeListener(mDisconnect);
        ProgressionCompleteUsingHook::unhook();
        ProgressionInventoryChangedHook::unhook();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mDisconnect;
};

} // namespace

} // namespace gf::progression

GF_REGISTER_MODULE_ORDERED(gf::progression::ProgressionModule, gf::ModuleOrder::Contracts);
