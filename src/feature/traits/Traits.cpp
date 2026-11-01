// The traits part of contract "data" (F9): boolean per-player flags both sides need (TF's data
// attachments of that kind), stored in the world DB ("gf:traits:<player uuid>" {trait: true}) and
// sent to the player's client mod ("traits.sync" {traits: {id: bool}}) on every change and when
// it joins. The client side reads them through ext/item/Traits.h.
#include "feature/traits/Traits.h"

#include <map>
#include <mutex>
#include <unordered_map>

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include "core/Keys.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Module.h"

namespace gf::traits {

namespace {

struct PlayerTraits {
    std::map<std::string, bool> values;
    bool                        loaded = false;
};

struct Store {
    std::mutex                                    mutex;
    std::unordered_map<std::string, PlayerTraits> players; ///< by uuid
};

Store& store() {
    static Store s;
    return s;
}

std::string keyOf(std::string const& uuid) { return core::worldKey("traits", uuid); }

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

PlayerTraits& traitsOf(Store& s, std::string const& uuid) {
    auto& p = s.players[uuid];
    if (!p.loaded) {
        p.loaded = true;
        if (auto doc = game::db::get(keyOf(uuid)); doc && doc->is_object())
            for (auto& [k, v] : doc->items())
                if (v.is_boolean()) p.values[k] = v.get<bool>();
    }
    return p;
}

nlohmann::json toJson(PlayerTraits const& p) {
    nlohmann::json j = nlohmann::json::object();
    for (auto const& [k, v] : p.values)
        if (v) j[k] = true;
    return j;
}

void sync(Player& player, PlayerTraits const& p) {
    if (net::isCompanion(player)) net::send(player, "traits.sync", {{"traits", toJson(p)}});
}

} // namespace

void set(Player& player, std::string const& traitId, bool value) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            uuid = player.getUuid().asString();
    auto&           p    = traitsOf(s, uuid);
    auto            id   = fullId(traitId);
    bool            old  = p.values.contains(id) && p.values[id];
    if (old == value) return;
    if (value) p.values[id] = true;
    else p.values.erase(id);
    auto j = toJson(p);
    if (j.empty()) game::db::erase(keyOf(uuid));
    else game::db::put(keyOf(uuid), j);
    sync(player, p);
}

bool get(Player const& player, std::string const& traitId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           p  = traitsOf(s, player.getUuid().asString());
    auto            it = p.values.find(fullId(traitId));
    return it != p.values.end() && it->second;
}

namespace {
class TraitsModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        game::db::onWorldClosed([] {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            s.players.clear();
        });
        net::onCompanionJoined([](Player& p) {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            sync(p, traitsOf(s, p.getUuid().asString()));
        });
    }
    void uninstall() {}
};
} // namespace

} // namespace gf::traits

GF_REGISTER_MODULE_ORDERED(gf::traits::TraitsModule, gf::ModuleOrder::Contracts);
