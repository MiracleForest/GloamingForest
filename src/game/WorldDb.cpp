// Adapted from LiteLDev/Levitate v0.1.0 (src/game/WorldDb.cpp), LGPL-3.0-or-later.
#include "game/WorldDb.h"

#include <exception>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/Keys.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::game::db {

namespace {
constexpr int kAutosaveTicks = 20 * 30;

LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return &level->getLevelStorage();
}

std::vector<std::function<void()>>& readyListeners() {
    static std::vector<std::function<void()>> v;
    return v;
}
std::vector<std::function<void()>>& saveListeners() {
    static std::vector<std::function<void()>> v;
    return v;
}
std::vector<std::function<void()>>& closedListeners() {
    static std::vector<std::function<void()>> v;
    return v;
}

void fire(std::vector<std::function<void()>> const& list, char const* what) {
    for (auto const& fn : list) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("world {} listener failed: {}", what, e.what());
        }
    }
}
} // namespace

bool available() { return levelStorage() != nullptr; }

namespace {
/// Rule R16: units only touch their own "gf:<unit>:..." records, never vanilla LevelDB records
/// such as "~local_player" or chunk keys.
bool checkKey(std::string_view key, char const* what) {
    if (core::validWorldKey(key)) return true;
    log().error("world data key '{}' is not of the form gf:<unit>:<...>; not {}", key, what);
    return false;
}
} // namespace

std::optional<nlohmann::json> get(std::string const& key) {
    if (!checkKey(key, "read")) return std::nullopt;
    auto* db = levelStorage();
    if (!db) return std::nullopt;
    std::string buffer;
    if (!db->loadData(key, buffer, DBHelpers::Category::Uncategorized)) return std::nullopt;
    auto j = nlohmann::json::parse(buffer, nullptr, false);
    if (j.is_discarded()) {
        log().error("world data {} is unreadable (kept untouched)", key);
        return std::nullopt;
    }
    return j;
}

void put(std::string const& key, nlohmann::json const& value) {
    if (!checkKey(key, "saved")) return;
    auto* db = levelStorage();
    if (!db) return;
    db->saveData(key, value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), DBHelpers::Category::Uncategorized);
}

void erase(std::string const& key) {
    if (!checkKey(key, "erased")) return;
    if (auto* db = levelStorage()) db->deleteData(key, DBHelpers::Category::Uncategorized);
}

void forEachKey(std::string const& prefix, std::function<void(std::string_view)> const& fn) {
    if (!std::string_view(prefix).starts_with(core::kWorldDbPrefix)) {
        log().error("world data prefix '{}' does not start with gf:; nothing listed", prefix);
        return;
    }
    auto* db = levelStorage();
    if (!db) return;
    db->forEachKeyWithPrefix(prefix, DBHelpers::Category::Uncategorized, [&](std::string_view key, std::string_view) {
        if (key.starts_with(prefix)) key.remove_prefix(prefix.size()); // some DB versions pass the full key
        fn(key);
    });
}

void onWorldReady(std::function<void()> fn) { readyListeners().push_back(std::move(fn)); }
void onWorldSave(std::function<void()> fn) { saveListeners().push_back(std::move(fn)); }
void onWorldClosed(std::function<void()> fn) { closedListeners().push_back(std::move(fn)); }

class WorldDbModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        // Client: every local world load. (BDS: already started when mods are enabled.)
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([](auto&) { ready(); }));
        if (available()) ready();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { close(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks < kAutosaveTicks) return;
            mTicks = 0;
            fire(saveListeners(), "save");
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        close();
    }

private:
    static void ready() {
        gOpen = true;
        fire(readyListeners(), "ready");
        log().info("GloamingForest world ready");
    }
    static void close() {
        if (!gOpen) return; // BDS: ServerStoppingEvent, then the mod is disabled with the level still alive
        gOpen = false;
        fire(saveListeners(), "save");
        fire(closedListeners(), "closed");
    }

    static inline bool gOpen = false;

    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mTicks = 0;
};

} // namespace gf::game::db

GF_REGISTER_MODULE_ORDERED(gf::game::db::WorldDbModule, gf::ModuleOrder::Storage);
