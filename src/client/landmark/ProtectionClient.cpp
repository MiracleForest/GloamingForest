// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/network/AreaProtectionPacket.java
//   src/main/java/gloamingforest/network/StructureProtectionPacket.java
//   src/main/java/gloamingforest/entity/ProtectionBox.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client side of the landmark protection packets (client mod). landmark.area_protection carries
// AreaProtectionPacket's boxes and position: the boxes live 60 ticks and are refreshed when the
// same box comes again (ProtectionBox#matches / resetLifetime). The visible boxes and the
// particles reach every client through the server (protection box entities and the protection
// particle), so this keeps the area boxes for client features. landmark.structure_protection
// carries StructureProtectionPacket's (box, protected) pairs or "all clear": the protection weather
// (TFWeatherRenderer#setProtectedBoxes, U15) reads them through the landmark extension.

#ifdef GF_CLIENT

#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "client/Client.h"
#include "core/GlobalPos.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::landmark {

namespace {

namespace net = gf::feature::net;
using Json    = nlohmann::json;
using core::BlockBox;

constexpr int kLifetime = 60; ///< ProtectionBox#lifeTime

struct State {
    std::mutex                             mutex;
    std::vector<std::pair<BlockBox, int>>  area;      ///< box, ticks left
    std::vector<std::pair<BlockBox, bool>> structure; ///< box, protected
};
State& state() {
    static State s;
    return s;
}

std::optional<BlockBox> boxOf(Json const& j) {
    if (!j.is_array() || j.size() != 6) return std::nullopt;
    for (auto const& v : j)
        if (!v.is_number_integer()) return std::nullopt;
    return BlockBox{j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>(), j[4].get<int>(), j[5].get<int>()};
}

void onAreaProtection(Json const& msg) {
    int n = 0;
    {
        std::lock_guard lock(state().mutex);
        auto&           area = state().area;
        for (auto const& b : msg.value("boxes", Json::array())) {
            auto box = boxOf(b);
            if (!box) continue;
            ++n;
            auto it = std::find_if(area.begin(), area.end(), [&](auto const& e) { return e.second > 0 && e.first == *box; });
            if (it != area.end()) it->second = kLifetime; // resetLifetime
            else area.emplace_back(*box, kLifetime);
        }
    }
    auto pos = msg.value("pos", Json::array());
    if (pos.is_array() && pos.size() == 3)
        log().info("client landmark: area protection {} box(es) at {} {} {}", n, pos[0].get<int>(), pos[1].get<int>(), pos[2].get<int>());
}

void onStructureProtection(Json const& msg) {
    std::vector<std::pair<BlockBox, bool>> boxes;
    auto const&                            list = msg.contains("boxes") ? msg["boxes"] : Json();
    if (list.is_array())
        for (auto const& e : list)
            if (auto box = boxOf(e.value("box", Json()))) boxes.emplace_back(*box, e.value("protected", true));
    {
        std::lock_guard lock(state().mutex);
        state().structure = boxes;
    }
    log().info("client landmark: structure protection {} box(es)", boxes.size());
}

void tick() {
    std::lock_guard lock(state().mutex);
    auto&           area = state().area;
    // ProtectionBox#tick: discarded when lifeTime <= 1
    for (auto& e : area) --e.second;
    std::erase_if(area, [](auto const& e) { return e.second <= 0; });
}

} // namespace

class ProtectionClientModule {
public:
    void install() {
        net::client::on("landmark.area_protection", onAreaProtection);
        net::client::on("landmark.structure_protection", onStructureProtection);
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            std::lock_guard lock(state().mutex);
            state().area.clear();
            state().structure.clear();
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace gf::client::landmark

namespace gf::landmark::ext::client {

std::vector<std::pair<BlockBox, bool>> structureProtection() {
    std::lock_guard lock(gf::client::landmark::state().mutex);
    return gf::client::landmark::state().structure;
}

std::vector<std::pair<BlockBox, int>> areaProtection() {
    std::lock_guard lock(gf::client::landmark::state().mutex);
    return gf::client::landmark::state().area;
}

} // namespace gf::landmark::ext::client

GF_REGISTER_MODULE(gf::client::landmark::ProtectionClientModule);

#endif // GF_CLIENT
