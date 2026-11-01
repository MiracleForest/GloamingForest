// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/bar/ClientTFBossBar.java
//   src/main/java/twilightforest/network/TFBossBarPacket.java
//   src/main/java/twilightforest/network/UpdateDeathTimePacket.java
//   src/main/java/twilightforest/network/MovePlayerPacket.java
//   src/main/java/twilightforest/network/UpdateThrownPacket.java
//   src/main/java/twilightforest/network/UpdateTFMultipartPacket.java
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's side of F8's channels:
//   boss.bar            ClientTFBossBar: the HUD's boss bar takes the boss's exact RGB (the vanilla
//                       packet only carries the nearest of 8 colours). The HUD asks the screen
//                       model for each bar's colour by index (ClientInstanceScreenModel::
//                       getBossBarColor); the hook answers with the RGB of the TF boss whose title
//                       (and, between equal titles, progress) that bar shows.
//   boss.death_time     UpdateDeathTimePacket (read with gf::client::entity::deathTime).
//   entity.move_player  MovePlayerPacket: the local player is pushed.
//   entity.hostile_mount  ClientGameEvents#removeHostileMountHealth: no mount hearts on a hostile
//                       mount (HudHorseHeartRenderer).
//   entity.multipart, entity.data, projectile.thrown: kept per entity for client renderers
//                       (feature/ext/entity/EntityClientExt.h).

#ifdef GF_CLIENT

#include "feature/ext/entity/EntityClientExt.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/renderers/HudHorseHeartRenderer.h"
#include "mc/client/gui/screens/models/ClientInstanceScreenModel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/safety/RedactableString.h"

#include "client/Client.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::entity {

namespace {

using Json  = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct DeathTime {
    int               value = 0;
    Clock::time_point at;
};

struct ClientState {
    std::mutex                                       mutex; ///< everything below: net handlers vs. HUD / renderers
    std::unordered_map<std::int64_t, BossBar>        bars;
    std::unordered_map<std::int64_t, DeathTime>      deathTimes;
    std::unordered_map<std::int64_t, std::vector<Part>> multipart;
    std::unordered_map<std::int64_t, std::int64_t>   partParents;
    std::unordered_map<std::int64_t, Json>           data;
    std::unordered_map<std::int64_t, Thrown>         thrown;
    std::set<std::int64_t>                           logged;   ///< bars whose colour was applied (one log line each)
    std::set<std::string>                            unmatched; ///< bar titles without a companion bar (one line each)
    std::atomic<bool>                                ridingUnfriendly{false};
};
ClientState& C() {
    static ClientState s;
    return s;
}

/// The text of a bar title without formatting codes.
std::string plain(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        // "§" is 0xC2 0xA7 in UTF-8, followed by the code character
        if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

} // namespace

/// ClientTFBossBar#renderBossBar's tint: the exact RGB of the TF boss the HUD's bar `index` shows.
std::optional<mce::Color> exactColour(ClientInstanceScreenModel const& model, int index) {
    auto& s = C();
    {
        std::scoped_lock lock(s.mutex);
        if (s.bars.empty()) return std::nullopt;
    }
    std::string title  = plain(model.getBossName(index).mUnredactedString);
    float       health = model.getBossHealthPercentage(index);
    std::scoped_lock lock(s.mutex);
    std::int64_t     best = 0;
    BossBar const*   bar  = nullptr;
    float            diff = 2.0f;
    for (auto const& [id, b] : s.bars) {
        if (plain(b.title) != title) continue;
        float d = std::abs(b.progress - health);
        if (!bar || d < diff) best = id, bar = &b, diff = d;
    }
    if (!bar) {
        if (s.unmatched.insert(title).second) log().info("client boss bar '{}' has no TF colour", title);
        return std::nullopt;
    }
    if (s.logged.insert(best).second) log().info("client boss bar {} coloured #{:06X}", best, bar->rgb);
    return mce::Color(static_cast<uint>(bar->rgb));
}

} // namespace gf::client::entity

// The HUD reads each bar's colour through the screen model: the TF bosses' bars get their RGB.
LL_TYPE_INSTANCE_HOOK(
    EntityClientBossColourHook,
    ll::memory::HookPriority::Normal,
    ClientInstanceScreenModel,
    &ClientInstanceScreenModel::getBossBarColor,
    mce::Color,
    int index
) {
    auto colour = origin(index);
    if (auto exact = gf::client::entity::exactColour(*this, index)) return *exact;
    return colour;
}

LL_TYPE_INSTANCE_HOOK(
    EntityClientMountHeartsHook,
    ll::memory::HookPriority::Normal,
    HudHorseHeartRenderer,
    &HudHorseHeartRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    if (gf::client::entity::ridingUnfriendly()) return; // removeHostileMountHealth
    origin(renderContext, client, owner, pass);
}

namespace gf::client::entity {

// ---- readers (EntityClientExt.h) ----------------------------------------------------------------

Json data(std::int64_t uid, std::string const& key) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.data.find(uid);
    if (it == s.data.end() || !it->second.is_object() || !it->second.contains(key)) return Json();
    return it->second[key];
}

std::vector<std::string> stringList(std::int64_t uid, std::string const& key) {
    std::vector<std::string> out;
    auto                     v = data(uid, key);
    if (v.is_array())
        for (auto const& e : v)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

std::vector<Part> parts(std::int64_t parentUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.multipart.find(parentUid);
    return it != s.multipart.end() ? it->second : std::vector<Part>{};
}

std::int64_t partParent(std::int64_t partUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.partParents.find(partUid);
    return it != s.partParents.end() ? it->second : 0;
}

int deathTime(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.deathTimes.find(uid);
    if (it == s.deathTimes.end()) return 0;
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - it->second.at).count() / 50;
    return it->second.value + static_cast<int>(ticks);
}

std::optional<Thrown> thrown(std::int64_t playerUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.thrown.find(playerUid);
    return it != s.thrown.end() ? std::optional<Thrown>(it->second) : std::nullopt;
}

bool ridingUnfriendly() { return C().ridingUnfriendly.load(); }

std::optional<BossBar> bossBar(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.bars.find(uid);
    return it != s.bars.end() ? std::optional<BossBar>(it->second) : std::nullopt;
}

namespace {

/// Forgets everything the previous server sent (a new world or server: its bosses and parts).
void clearState() {
    auto& s = C();
    {
        std::scoped_lock lock(s.mutex);
        s.bars.clear();
        s.logged.clear();
        s.unmatched.clear();
        s.deathTimes.clear();
        s.multipart.clear();
        s.partParents.clear();
        s.data.clear();
        s.thrown.clear();
    }
    s.ridingUnfriendly.store(false);
}

void registerHandlers() {
    namespace nc = gf::net::client;
    nc::onConnected(clearState);
    nc::on("boss.bar", [](Json const& m) {
        auto             id = m.value("id", std::int64_t{0});
        auto&            s  = C();
        std::scoped_lock lock(s.mutex);
        auto             op = m.value("op", std::string("add"));
        if (op == "remove") {
            s.bars.erase(id);
            s.logged.erase(id);
            s.deathTimes.erase(id);
            return;
        }
        if (!s.bars.contains(id) && op == "add") log().info("client boss bar {} received (#{:06X})", id, m.value("rgb", 0u));
        auto& bar = s.bars[id];
        if (op == "progress") {
            bar.progress = m.value("progress", bar.progress);
            return;
        }
        if (op == "name") {
            bar.title = m.value("title", bar.title);
            return;
        }
        bar.rgb      = m.value("rgb", bar.rgb);
        bar.overlay  = m.value("overlay", bar.overlay);
        bar.darken   = m.value("darken", bar.darken);
        bar.fog      = m.value("fog", bar.fog);
        bar.progress = m.value("progress", bar.progress);
        bar.title    = m.value("title", bar.title);
        if (op == "style") s.logged.erase(id);
    });
    nc::on("boss.death_time", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        s.deathTimes[m.value("id", std::int64_t{0})] = DeathTime{m.value("deathTime", 0), Clock::now()};
    });
    nc::on("entity.move_player", [](Json const& m) {
        auto* player = gf::client::currentPlayer();
        if (!player) return;
        auto v = player->getPosDelta();
        const_cast<Vec3&>(player->getPosDelta()) = Vec3{v.x + m.value("x", 0.0f), v.y + m.value("y", 0.0f), v.z + m.value("z", 0.0f)};
    });
    nc::on("entity.hostile_mount", [](Json const& m) { C().ridingUnfriendly.store(m.value("riding", false)); });
    nc::on("entity.multipart", [](Json const& m) {
        auto&             s      = C();
        std::scoped_lock  lock(s.mutex);
        auto              parent = m.value("parent", std::int64_t{0});
        std::vector<Part> list;
        if (auto ps = m.find("parts"); ps != m.end() && ps->is_array())
            for (auto const& p : *ps)
                list.push_back(Part{p.value("id", std::string()), p.value("uid", std::int64_t{0}), p.value("rid", std::int64_t{0}), p.value("index", 0), p.value("width", 1.0f), p.value("height", 1.0f)});
        std::erase_if(s.partParents, [&](auto const& kv) { return kv.second == parent; });
        for (auto const& p : list) s.partParents[p.uid] = parent;
        s.multipart[parent] = std::move(list);
    });
    nc::on("entity.data", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        auto&            d = s.data[m.value("id", std::int64_t{0})];
        if (!d.is_object()) d = Json::object();
        d[m.value("key", std::string())] = m.value("data", Json());
    });
    nc::on("projectile.thrown", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        s.thrown[m.value("id", std::int64_t{0})] = Thrown{m.value("thrown", false), m.value("thrower", std::int64_t{0}), m.value("cooldown", 0)};
    });
}

class EntityClientModule {
public:
    void install() {
        static bool handlers = false;
        if (!std::exchange(handlers, true)) registerHandlers();
        EntityClientBossColourHook::hook();
        EntityClientMountHeartsHook::hook();
    }
    void uninstall() {
        EntityClientMountHeartsHook::unhook();
        EntityClientBossColourHook::unhook();
        clearState();
    }
};

} // namespace
} // namespace gf::client::entity

GF_REGISTER_MODULE(gf::client::entity::EntityClientModule);

#endif
