// Neutral stub of the gear part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/gear/Gear.h"

#include "core/Stub.h"

namespace gf::gear {

int level(Player const&, std::string const&) {
    GF_STUB("gear.level");
    return 0;
}
std::map<std::string, int> modifiers(ItemStack const&) {
    GF_STUB("gear.modifiers");
    return {};
}
bool add(ItemStack&, std::string const&, int) {
    GF_STUB("gear.add");
    return false;
}
bool remove(ItemStack&, std::string const&) {
    GF_STUB("gear.remove");
    return false;
}

} // namespace gf::gear
