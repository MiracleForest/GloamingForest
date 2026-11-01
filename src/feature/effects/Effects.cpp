// Neutral stub of the effect part of "enchant+effects" (F0). F9 replaces this file (rule R18).
#include "feature/effects/Effects.h"

#include "core/Stub.h"

namespace gf::effects {

void registerType(std::string const&, Spec) { GF_STUB("effects.registerType"); }
bool apply(Actor&, std::string const&, int, int) {
    GF_STUB("effects.apply");
    return false;
}
bool has(Actor const&, std::string const&) {
    GF_STUB("effects.has");
    return false;
}
int amplifier(Actor const&, std::string const&) {
    GF_STUB("effects.amplifier");
    return -1;
}
void remove(Actor&, std::string const&) { GF_STUB("effects.remove"); }

} // namespace gf::effects
