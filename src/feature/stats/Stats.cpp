// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFStats.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The statistics part of contract "data" (F9): TF's 10 custom statistics (TFStats: bugs_squished,
// uncrafting_table_interactions, trophy_pedestals_activated, e115_slices_eaten,
// torchberries_harvested, blocks_crumbled, life_charms_activated, keeping_charms_activated,
// skull_candles_made, tf_shields_broken; StatFormatter.DEFAULT), counted per player in the world
// DB ("gf:stats:<player uuid>" {stat: value}), written at every world save. Bedrock has no
// statistics screen; companions get "stats.sync" with their values.
#include "feature/stats/Stats.h"

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include "core/Keys.h"
#include "feature/net/Net.h"
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
    {
        static std::set<std::string> const known(std::begin(kTfStats), std::end(kTfStats));
        static std::set<std::string>       warned;
        if (!known.contains(id) && warned.insert(id).second) log().warn("stats: {} is not one of TF's statistics", id);
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
