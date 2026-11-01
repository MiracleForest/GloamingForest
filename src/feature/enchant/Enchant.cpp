// Neutral stub of the enchantment part of "enchant+effects" (F0). F9 replaces this file (rule R18).
#include "feature/enchant/Enchant.h"

#include "core/Stub.h"

namespace gf::enchant {

void registerType(std::string const&, Spec) { GF_STUB("enchant.registerType"); }
int  level(ItemStack const&, std::string const&) {
    GF_STUB("enchant.level");
    return 0;
}
void                       set(ItemStack&, std::string const&, int) { GF_STUB("enchant.set"); }
std::map<std::string, int> all(ItemStack const&) {
    GF_STUB("enchant.all");
    return {};
}

} // namespace gf::enchant
