// Neutral stub of the gamerule part of "rules+fx" (F0). F10 replaces this file (rule R18).
#include "feature/gamerules/GameRules.h"

#include "core/Stub.h"

namespace gf::gamerules {

void  registerBool(std::string const&, bool) { GF_STUB("gamerules.registerBool"); }
void  registerInt(std::string const&, int) { GF_STUB("gamerules.registerInt"); }
Value get(std::string const&) {
    GF_STUB("gamerules.get");
    return false;
}
bool getBool(std::string const&) {
    GF_STUB("gamerules.get");
    return false;
}
int getInt(std::string const&) {
    GF_STUB("gamerules.get");
    return 0;
}
bool set(std::string const&, Value) {
    GF_STUB("gamerules.set");
    return false;
}

} // namespace gf::gamerules
