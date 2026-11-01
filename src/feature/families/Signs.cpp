// Neutral stub of the sign part of "families+blockevents" (F0). F11 replaces this file (rule R18).
#include "feature/families/Signs.h"

#include "core/Stub.h"

namespace gf::signs {

std::vector<std::string> text(core::GlobalPos const&, bool) {
    GF_STUB("signs.text");
    return {};
}

bool setText(core::GlobalPos const&, std::vector<std::string> const&, bool) {
    GF_STUB("signs.setText");
    return false;
}

} // namespace gf::signs
