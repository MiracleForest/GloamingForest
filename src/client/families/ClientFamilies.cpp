// Block families (F11), client mod: the client side of the families.
//
// - The family registry for the client's own block types: the client builds its block types per
//   world it joins (a remote server's blocks), so the registry publishes a new snapshot of them on
//   every join (the server thread of a local world reads the snapshot it has meanwhile).
// - Climbable family blocks (TF's climbable hollow logs, rope, iron ladder: BlockProperty::Climbable,
//   like vanilla ladders) for the client's own movement, set again on every join.
// - Sign text: the server sends the signs of the chunks around the player ("signs.text"); the
//   client keeps it per position (the text shows through the display entities every client sees;
//   the native sign editor shows it, src/client/families/Screens.cpp).

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

namespace gf::feature::families::detail {
void refreshTypes();
} // namespace gf::feature::families::detail

namespace gf::client::families {

namespace fam = gf::feature::families;

std::mutex& signMutex() {
    static std::mutex m;
    return m;
}
/// Sign text per position, as the server sent it ({"front": {...}, "back": {...}, "waxed"}).
std::map<core::GlobalPos, nlohmann::json>& signTexts() {
    static std::map<core::GlobalPos, nlohmann::json> m;
    return m;
}

namespace {

class ClientFamiliesModule {
public:
    void load() {
        net::client::on("signs.text", [](nlohmann::json const& p) {
            core::GlobalPos pos{p.value("dim", 0), p.value("x", 0), p.value("y", 0), p.value("z", 0)};
            std::lock_guard lock(signMutex());
            if (p.contains("sign") && p["sign"].is_object()) signTexts()[pos] = p["sign"];
            else signTexts().erase(pos);
        });
    }

    void install() {
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](ll::event::ClientJoinLevelEvent&) {
            {
                std::lock_guard lock(signMutex());
                signTexts().clear();
            }
            fam::detail::refreshTypes(); // the joined world's own block types (a new snapshot)
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
