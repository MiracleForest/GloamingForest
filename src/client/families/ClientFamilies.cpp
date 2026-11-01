// Block families (F11), client mod: the client side of the families.
//
// - Climbable family blocks (TF's climbable hollow logs, rope, iron ladder: BlockProperty::Climbable,
//   like vanilla ladders) for the client's own movement: the client builds its block types per
//   world it joins (a remote server's blocks), so the flag is set again on every join. Local worlds
//   share the server's block types (src/feature/families/Module.cpp sets them too).
// - Sign text: the server sends every sign's text to companions ("signs.text"); the client keeps
//   it per position for the sign renderer (TF's SignRenderer / HangingSignRenderer equivalent),
//   which needs world-space text drawing from the client render contract (client.render, F12).

#ifdef GF_CLIENT

#include <map>
#include <mutex>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"

#include "core/GlobalPos.h"
#include "feature/families/Families.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::families {

namespace fam = gf::feature::families;

namespace {

/// Sign text per position, as the server sent it ({"front": {...}, "back": {...}, "waxed"}).
std::map<core::GlobalPos, nlohmann::json>& signTexts() {
    static std::map<core::GlobalPos, nlohmann::json> m;
    return m;
}

class ClientFamiliesModule {
public:
    void load() {
        net::client::on("signs.text", [](nlohmann::json const& p) {
            core::GlobalPos pos{p.value("dim", 0), p.value("x", 0), p.value("y", 0), p.value("z", 0)};
            if (p.contains("sign") && p["sign"].is_object()) signTexts()[pos] = p["sign"];
            else signTexts().erase(pos);
        });
    }

    void install() {
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](ll::event::ClientJoinLevelEvent&) {
            signTexts().clear();
            fam::applyClimbable();
        });
    }

    void uninstall() { ll::event::EventBus::getInstance().removeListener(mJoin); }

private:
    ll::event::ListenerPtr mJoin;
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::ClientFamiliesModule);

#endif // GF_CLIENT
