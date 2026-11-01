// Neutral stub of the traits part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/traits/Traits.h"

#include "core/Stub.h"

namespace gf::traits {

void set(Player&, std::string const&, bool) { GF_STUB("traits.set"); }
bool get(Player const&, std::string const&) {
    GF_STUB("traits.get");
    return false;
}

} // namespace gf::traits
