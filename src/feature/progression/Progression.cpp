// Neutral stub of the "progression" contract (F0). F10 replaces this file (rule R18).
#include "feature/progression/Progression.h"

#include "core/Stub.h"

namespace gf::progression {

bool has(Player const&, std::string const&) {
    GF_STUB("progression.has");
    return false;
}
void grant(Player&, std::string const&, std::string const&) { GF_STUB("progression.grant"); }
void revoke(Player&, std::string const&, std::string const&) { GF_STUB("progression.revoke"); }
bool done(Player const&, std::string const&, std::string const&) {
    GF_STUB("progression.done");
    return false;
}
void trigger(Player&, std::string const&, Json const&) { GF_STUB("progression.trigger"); }
void onTrigger(std::string const&, TriggerFn) { GF_STUB("progression.onTrigger"); }
void onGranted(GrantedFn) { GF_STUB("progression.onGranted"); }
void showMissing(Player&, std::string const&) { GF_STUB("progression.showMissing"); }

namespace events {
void onInventoryChanged(std::function<void(Player&)>) { GF_STUB("progression.events"); }
void onKilled(std::function<void(Player&, Actor&, std::string const&)>) { GF_STUB("progression.events"); }
void onConsumed(std::function<void(Player&, ItemStack const&)>) { GF_STUB("progression.events"); }
void onDimensionChanged(std::function<void(Player&, int, int)>) { GF_STUB("progression.events"); }
void onBiomeEntered(std::function<void(Player&, std::string const&)>) { GF_STUB("progression.events"); }
void onTick(std::function<void(Player&)>) { GF_STUB("progression.events"); }
} // namespace events

#ifdef GF_CLIENT
namespace client {
bool has(std::string const&) {
    GF_STUB("progression.client");
    return false;
}
bool enforced() {
    GF_STUB("progression.client");
    return false;
}
bool biomeLocked(std::string const&) {
    GF_STUB("progression.client");
    return false;
}
} // namespace client
#endif

} // namespace gf::progression
