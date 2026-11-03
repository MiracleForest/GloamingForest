// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFStats.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The statistics part of contract "data" (F9): the custom statistics (Java's Stats.CUSTOM, the
// registry minecraft:custom_stat): TF's 10 (TFStats: bugs_squished, uncrafting_table_interactions,
// trophy_pedestals_activated, e115_slices_eaten, torchberries_harvested, blocks_crumbled,
// life_charms_activated, keeping_charms_activated, skull_candles_made, tf_shields_broken;
// StatFormatter.DEFAULT) and Minecraft 1.21.1's 75 (minecraft:open_chest, which TF's chests award,
// minecraft:trigger_trapped_chest, ...), counted per player in the world DB
// ("gf:stats:<player uuid>" {stat: value}), written at every world save. Bedrock keeps no player
// statistics a server can award (its statistics screen shows the client's Xbox Live user
// statistics, fed by the client's own telemetry), so no vanilla statistic maps to a Bedrock
// counterpart: every one is counted here. Bedrock has no screen for them; companions get
// "stats.sync" with their values.
#include "feature/stats/Stats.h"

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include "core/Keys.h"
#include "feature/net/Net.h"
#include "feature/stats/StatsInternal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::stats {

namespace {

/// TFStats in registration order.
constexpr char const* kTfStats[] = {
    "twilightforest:bugs_squished",          "twilightforest:uncrafting_table_interactions", "twilightforest:trophy_pedestals_activated",
    "twilightforest:e115_slices_eaten",      "twilightforest:torchberries_harvested",        "twilightforest:blocks_crumbled",
    "twilightforest:life_charms_activated",  "twilightforest:keeping_charms_activated",      "twilightforest:skull_candles_made",
    "twilightforest:tf_shields_broken",
};

/// Minecraft 1.21.1's custom statistics (net.minecraft.stats.Stats, its makeCustomStat calls in
/// registration order).
constexpr char const* kVanillaStats[] = {
    "minecraft:leave_game",
    "minecraft:play_time",
    "minecraft:total_world_time",
    "minecraft:time_since_death",
    "minecraft:time_since_rest",
    "minecraft:sneak_time",
    "minecraft:walk_one_cm",
    "minecraft:crouch_one_cm",
    "minecraft:sprint_one_cm",
    "minecraft:walk_on_water_one_cm",
    "minecraft:fall_one_cm",
    "minecraft:climb_one_cm",
    "minecraft:fly_one_cm",
    "minecraft:walk_under_water_one_cm",
    "minecraft:minecart_one_cm",
    "minecraft:boat_one_cm",
    "minecraft:pig_one_cm",
    "minecraft:horse_one_cm",
    "minecraft:aviate_one_cm",
    "minecraft:swim_one_cm",
    "minecraft:strider_one_cm",
    "minecraft:jump",
    "minecraft:drop",
    "minecraft:damage_dealt",
    "minecraft:damage_dealt_absorbed",
    "minecraft:damage_dealt_resisted",
    "minecraft:damage_taken",
    "minecraft:damage_blocked_by_shield",
    "minecraft:damage_absorbed",
    "minecraft:damage_resisted",
    "minecraft:deaths",
    "minecraft:mob_kills",
    "minecraft:animals_bred",
    "minecraft:player_kills",
    "minecraft:fish_caught",
    "minecraft:talked_to_villager",
    "minecraft:traded_with_villager",
    "minecraft:eat_cake_slice",
    "minecraft:fill_cauldron",
    "minecraft:use_cauldron",
    "minecraft:clean_armor",
    "minecraft:clean_banner",
    "minecraft:clean_shulker_box",
    "minecraft:interact_with_brewingstand",
    "minecraft:interact_with_beacon",
    "minecraft:inspect_dropper",
    "minecraft:inspect_hopper",
    "minecraft:inspect_dispenser",
    "minecraft:play_noteblock",
    "minecraft:tune_noteblock",
    "minecraft:pot_flower",
    "minecraft:trigger_trapped_chest",
    "minecraft:open_enderchest",
    "minecraft:enchant_item",
    "minecraft:play_record",
    "minecraft:interact_with_furnace",
    "minecraft:interact_with_crafting_table",
    "minecraft:open_chest",
    "minecraft:sleep_in_bed",
    "minecraft:open_shulker_box",
    "minecraft:open_barrel",
    "minecraft:interact_with_blast_furnace",
    "minecraft:interact_with_smoker",
    "minecraft:interact_with_lectern",
    "minecraft:interact_with_campfire",
    "minecraft:interact_with_cartography_table",
    "minecraft:interact_with_loom",
    "minecraft:interact_with_stonecutter",
    "minecraft:bell_ring",
    "minecraft:raid_trigger",
    "minecraft:raid_win",
    "minecraft:interact_with_anvil",
    "minecraft:interact_with_grindstone",
    "minecraft:target_hit",
    "minecraft:interact_with_smithing_table",
};

/// The custom statistic registry: TF's statistics and Minecraft's.
std::set<std::string> const& registered() {
    static std::set<std::string> const s = [] {
        std::set<std::string> out(std::begin(kTfStats), std::end(kTfStats));
        out.insert(std::begin(kVanillaStats), std::end(kVanillaStats));
        return out;
    }();
    return s;
}

/// The ids award() warned about (once each): neither TF's nor Minecraft's.
struct Warned {
    std::mutex            mutex;
    std::set<std::string> ids;
};

Warned& warned() {
    static Warned w;
    return w;
}

struct PlayerStats {
    std::map<std::string, long long> values;
    bool                             loaded = false;
    bool                             dirty  = false;
};

struct Store {
    std::mutex                                   mutex;
    std::unordered_map<std::string, PlayerStats> players; ///< by uuid
};

Store& store() {
    static Store s;
    return s;
}

std::string keyOf(std::string const& uuid) { return core::worldKey("stats", uuid); }

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

PlayerStats& statsOf(Store& s, std::string const& uuid) {
    auto& p = s.players[uuid];
    if (!p.loaded) {
        p.loaded = true;
        if (auto doc = game::db::get(keyOf(uuid)); doc && doc->is_object())
            for (auto& [k, v] : doc->items())
                if (v.is_number_integer()) p.values[k] = v.get<long long>();
    }
    return p;
}

void saveAll() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    for (auto& [uuid, p] : s.players) {
        if (!p.dirty) continue;
        nlohmann::json j = nlohmann::json::object();
        for (auto const& [k, v] : p.values) j[k] = v;
        game::db::put(keyOf(uuid), j);
        p.dirty = false;
    }
}

void sync(Player& player, PlayerStats const& p) {
    if (!net::isCompanion(player)) return;
    nlohmann::json j = nlohmann::json::object();
    for (auto const& [k, v] : p.values) j[k] = v;
    net::send(player, "stats.sync", {{"stats", j}});
}

} // namespace

void award(Player& player, std::string const& statId, int amount) {
    if (amount == 0) return;
    auto id = fullId(statId);
    // Stats.CUSTOM.get(id): an id of the custom statistic registry (TF's or Minecraft's) counts
    // silently; any other id counts too, with one warning
    if (!registered().contains(id)) {
        auto&           w = warned();
        std::lock_guard lock(w.mutex);
        if (w.ids.insert(id).second) log().warn("stats: {} is not a custom statistic of TF or Minecraft", id);
    }
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           p = statsOf(s, player.getUuid().asString());
    p.values[id] += amount;
    p.dirty = true;
    sync(player, p);
}

long long value(Player const& player, std::string const& statId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           p  = statsOf(s, player.getUuid().asString());
    auto            it = p.values.find(fullId(statId));
    return it == p.values.end() ? 0 : it->second;
}

namespace internal {

bool isRegistered(std::string const& statId) { return registered().contains(fullId(statId)); }

std::size_t registeredCount() { return registered().size(); }

bool warnedAbout(std::string const& statId) {
    auto&           w = warned();
    std::lock_guard lock(w.mutex);
    return w.ids.contains(fullId(statId));
}

void forget(Player const& player) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            uuid = player.getUuid().asString();
    s.players.erase(uuid);
    game::db::erase(keyOf(uuid));
}

} // namespace internal

namespace {
class StatsModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        game::db::onWorldSave(&saveAll);
        game::db::onWorldClosed([] {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            s.players.clear();
        });
        net::onCompanionJoined([](Player& p) {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            sync(p, statsOf(s, p.getUuid().asString()));
        });
    }
    void uninstall() { saveAll(); }
};
} // namespace

} // namespace gf::stats

GF_REGISTER_MODULE_ORDERED(gf::stats::StatsModule, gf::ModuleOrder::Contracts);
