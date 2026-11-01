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
//                       packet only carries the nearest of 8 colours). The HUD reads each bar's
//                       colour from ClientInstanceScreenModel's boss table; the hook writes the RGB
//                       into the entries of TF bosses before the HUD reads them.
//   boss.death_time     UpdateDeathTimePacket (kept for renderers of TF's death animations).
//   entity.move_player  MovePlayerPacket: the local player is pushed.
//   entity.hostile_mount  ClientGameEvents#removeHostileMountHealth: no mount hearts on a hostile
//                       mount (HudHorseHeartRenderer).
//   entity.multipart, entity.data, projectile.thrown: kept per entity for client renderers.

#ifdef GF_CLIENT

#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/renderers/HudHorseHeartRenderer.h"
#include "mc/client/gui/screens/models/BossInfo.h"
#include "mc/client/gui/screens/models/ClientInstanceScreenModel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/legacy/ActorUniqueID.h"

#include "client/Client.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::entity {

namespace {

using Json = nlohmann::json;

struct ClientBar {
    unsigned rgb     = 0xFFFFFF;
    int      overlay = 0;
    bool     darken  = false;
    bool     fog     = false;
};

struct ClientState {
    std::unordered_map<std::int64_t, ClientBar> bars;
    std::unordered_map<std::int64_t, int>       deathTimes;
    std::unordered_map<std::int64_t, Json>      multipart;
    std::unordered_map<std::int64_t, Json>      data;
    std::unordered_map<std::int64_t, Json>      thrown;
    std::set<std::int64_t>                      logged; ///< bars whose colour was applied (one log line each)
    bool                                        ridingUnfriendly = false;
};
ClientState& C() {
    static ClientState s;
    return s;
}

/// Writes the exact colours into the HUD's boss table (ClientTFBossBar#renderBossBar's tint).
void applyColours(ClientInstanceScreenModel const& model) {
    auto& s = C();
    if (s.bars.empty()) return;
    auto& infos = const_cast<std::unordered_map<ActorUniqueID, BossInfo>&>(model.mBossInfo.get());
    for (auto& [id, info] : infos) {
        auto it = s.bars.find(id.rawID);
        if (it == s.bars.end()) continue;
        info.mBarColor = mce::Color(static_cast<uint>(it->second.rgb));
        if (s.logged.insert(id.rawID).second) log().info("client boss bar {} coloured #{:06X}", id.rawID, it->second.rgb);
    }
}

} // namespace

} // namespace gf::client::entity

LL_TYPE_INSTANCE_HOOK(
    EntityClientBossColourHook,
    ll::memory::HookPriority::Normal,
    ClientInstanceScreenModel,
    &ClientInstanceScreenModel::getBossBarColor,
    mce::Color,
    int index
) {
    gf::client::entity::applyColours(*this);
    return origin(index);
}

LL_TYPE_INSTANCE_HOOK(
    EntityClientBossHealthHook,
    ll::memory::HookPriority::Normal,
    ClientInstanceScreenModel,
    &ClientInstanceScreenModel::getBossHealthPercentage,
    float,
    int index
) {
    gf::client::entity::applyColours(*this);
    return origin(index);
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
    if (gf::client::entity::C().ridingUnfriendly) return; // removeHostileMountHealth
    origin(renderContext, client, owner, pass);
}

namespace gf::client::entity {
namespace {

void registerHandlers() {
    namespace nc = gf::net::client;
    nc::on("boss.bar", [](Json const& m) {
        auto  id = m.value("id", std::int64_t{0});
        auto& s  = C();
        auto  op = m.value("op", std::string("add"));
        if (op == "remove") {
            s.bars.erase(id);
            s.logged.erase(id);
            s.deathTimes.erase(id);
            return;
        }
        auto& bar   = s.bars[id];
        bar.rgb     = m.value("rgb", bar.rgb);
        bar.overlay = m.value("overlay", bar.overlay);
        bar.darken  = m.value("darken", bar.darken);
        bar.fog     = m.value("fog", bar.fog);
        if (op == "style") s.logged.erase(id);
    });
    nc::on("boss.death_time", [](Json const& m) { C().deathTimes[m.value("id", std::int64_t{0})] = m.value("deathTime", 0); });
    nc::on("entity.move_player", [](Json const& m) {
        auto* player = gf::client::currentPlayer();
        if (!player) return;
        auto v = player->getPosDelta();
        const_cast<Vec3&>(player->getPosDelta()) = Vec3{v.x + m.value("x", 0.0f), v.y + m.value("y", 0.0f), v.z + m.value("z", 0.0f)};
    });
    nc::on("entity.hostile_mount", [](Json const& m) { C().ridingUnfriendly = m.value("riding", false); });
    nc::on("entity.multipart", [](Json const& m) { C().multipart[m.value("parent", std::int64_t{0})] = m; });
    nc::on("entity.data", [](Json const& m) {
        auto& d = C().data[m.value("id", std::int64_t{0})];
        if (!d.is_object()) d = Json::object();
        d[m.value("key", std::string())] = m.value("data", Json());
    });
    nc::on("projectile.thrown", [](Json const& m) { C().thrown[m.value("id", std::int64_t{0})] = m; });
}

class EntityClientModule {
public:
    void install() {
        static bool handlers = false;
        if (!std::exchange(handlers, true)) registerHandlers();
        EntityClientBossColourHook::hook();
        EntityClientBossHealthHook::hook();
        EntityClientMountHeartsHook::hook();
    }
    void uninstall() {
        EntityClientMountHeartsHook::unhook();
        EntityClientBossHealthHook::unhook();
        EntityClientBossColourHook::unhook();
        C() = {};
    }
};

} // namespace
} // namespace gf::client::entity

GF_REGISTER_MODULE(gf::client::entity::EntityClientModule);

#endif
