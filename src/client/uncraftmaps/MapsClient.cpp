// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/network/MagicMapPacket.java (client handler)
//   src/main/java/gloamingforest/network/MazeMapPacket.java (client handler)
//   src/main/java/gloamingforest/client/renderer/map/ConqueredMapIconRenderer.java
//   src/main/java/gloamingforest/client/renderer/map/MagicMapPlayerIconRenderer.java
//   src/main/java/gloamingforest/init/GloamingMapDecorations.java (the icons)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// upstream's map icons with the client mod: the 15 GloamingMapDecorations textures, the magic map's landmark
// icons from "uncraftmaps.magic_map" drawn over the map by F12 with upstream's two decoration renderers
// (a conquered landmark gets the red X; the player icons lie on top), and the vanilla stand-ins the
// server adds for vanilla clients removed from this client's copy of the map (they would double the
// upstream icons). The maze map's yCenter / ore flag (MazeMapPacket) are kept for the tooltip.
#ifdef GF_CLIENT

#include <map>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/saveddata/maps/MapDecoration.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/saveddata/maps/MapItemTrackedActor.h"

#include "client/Client.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::uncraftmaps {

namespace {

using Json = nlohmann::json;

constexpr char const* kDecorations[] = {
    "hedge_maze", "small_hollow_hill", "medium_hollow_hill", "large_hollow_hill", "quest_grove", "naga_courtyard", "lich_tower", "labyrinth",
    "hydra_lair", "knight_stronghold", "dark_tower", "yeti_lair", "aurora_palace", "troll_caves", "final_castle",
};

struct State {
    std::mutex                                           mutex;
    std::map<std::int64_t, std::vector<Json>>           magic; ///< map id -> upstream decorations
    std::map<std::int64_t, std::pair<int, bool>>        maze;  ///< map id -> (yCenter, ore)
};
State& st() {
    static State s;
    return s;
}

/// The client's map data of `id` (nullptr: not known yet).
MapItemSavedData* clientMap(std::int64_t id) {
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    return level ? level->getMapSavedData(ActorUniqueID{id}) : nullptr;
}

/// Every client tick: the upstream decorations of each magic map with the player icons on top, and the
/// vanilla stand-ins (Other-tracked decorations) taken off this client's copy.
void tick() {
    std::map<std::int64_t, std::vector<Json>> magic;
    {
        std::lock_guard lock(st().mutex);
        magic = st().magic;
    }
    for (auto const& [id, list] : magic) {
        if (auto* data = clientMap(id)) {
            std::erase_if(data->mDecorations.get(), [](auto const& e) { return e.first.type == MapItemTrackedActor::Type::Other; });
        }
        std::vector<maps::Decoration>      decos;
        std::vector<maps::DecorationStyle> styles;
        for (auto const& d : list) {
            decos.push_back({d.value("type", std::string()), d.value("x", 0), d.value("z", 0), d.value("rotation", 0), ""});
            styles.push_back({.conquered = d.value("conquered", false)});
        }
        // MagicMapPlayerIconRenderer: the player markers drawn above everything
        if (auto view = maps::view(id)) {
            for (auto const& v : view->decorations) {
                if (v.type != "minecraft:0" && v.type != "minecraft:6" && v.type != "minecraft:13") continue;
                decos.push_back(v);
                styles.push_back({.topmost = true});
            }
        }
        maps::setDecorations(id, std::move(decos), std::move(styles));
    }
}

class MapsClientModule {
public:
    void install() {
        for (auto const* name : kDecorations) maps::setIcon(std::string("gloamingforest:") + name, std::string("textures/gloamingforest/map/decorations/") + name);
        net::client::on("uncraftmaps.magic_map", [](Json const& msg) {
            auto id = msg.value("map", std::int64_t{0});
            std::vector<Json> list;
            for (auto const& d : msg.value("decorations", Json::array())) list.push_back(d);
            {
                std::lock_guard lock(st().mutex);
                st().magic[id] = std::move(list);
            }
            log().info("uncraftmaps: magic map {} has {} landmark icon(s)", id, msg.value("decorations", Json::array()).size());
        });
        net::client::on("uncraftmaps.maze_map", [](Json const& msg) {
            std::lock_guard lock(st().mutex);
            st().maze[msg.value("map", std::int64_t{0})] = {msg.value("y", 0), msg.value("ore", false)};
        });
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
        mExit     = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            std::map<std::int64_t, std::vector<Json>> old;
            {
                std::lock_guard lock(st().mutex);
                old.swap(st().magic);
                st().maze.clear();
            }
            for (auto const& [id, l] : old) maps::setDecorations(id, {});
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mExit);
    }

private:
    ll::event::ListenerPtr mTick, mExit;
};

} // namespace

} // namespace gf::client::uncraftmaps

GF_REGISTER_MODULE(gf::client::uncraftmaps::MapsClientModule);

#endif // GF_CLIENT
