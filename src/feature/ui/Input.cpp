// Server side of the key bindings (F12, contract extension feature/ext/ui/Server.h): the client
// mod forwards presses and releases of the keys registered with gf::client::keys::registerKey on
// channel "input.key" {id, down}; units react with gf::input::onKey (companions only, vanilla
// players use item-use fallbacks: rule "Keybinds get keyboard defaults only").

#include <map>
#include <string>
#include <vector>

#include "mc/world/actor/player/Player.h"

#include "feature/ext/ui/Server.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::input {

namespace {
std::map<std::string, std::vector<std::function<void(Player&, bool)>>>& handlers() {
    static std::map<std::string, std::vector<std::function<void(Player&, bool)>>> m;
    return m;
}
} // namespace

void onKey(std::string const& id, std::function<void(Player& player, bool down)> fn) {
    if (fn) handlers()[id].push_back(std::move(fn));
}

void dispatchKey(Player& player, std::string const& id, bool down) {
    auto it = handlers().find(id);
    if (it == handlers().end()) return;
    auto list = it->second; // a handler may register another one
    for (auto& fn : list) {
        try {
            fn(player, down);
        } catch (std::exception const& e) {
            log().error("input: key handler of {} failed: {}", id, e.what());
        }
    }
}

namespace {
class InputModule {
public:
    void install() {
        net::on("input.key", [](Player& p, nlohmann::json const& msg) {
            auto id = msg.value("id", std::string{});
            if (id.empty() || id.size() > 128) return;
            dispatchKey(p, id, msg.value("down", true));
        });
    }
    void uninstall() { net::off("input.key"); }
};
} // namespace

} // namespace gf::input

GF_REGISTER_MODULE_ORDERED(gf::input::InputModule, gf::ModuleOrder::Contracts);
