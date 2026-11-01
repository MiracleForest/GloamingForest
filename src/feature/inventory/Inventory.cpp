// Neutral stub of the "inventory" contract (F0). F10 replaces this file (rule R18).
#include "feature/inventory/Inventory.h"

#include "core/Stub.h"

namespace gf::inventory {

void                   registerBlock(std::string const&, Handler) { GF_STUB("inventory.registerBlock"); }
std::optional<Handler> find(BlockSource&, BlockPos const&) {
    GF_STUB("inventory.find");
    return std::nullopt;
}
bool openChestLike(Player&, GlobalPos const&, int, std::string const&) {
    GF_STUB("inventory.openChestLike");
    return false;
}

} // namespace gf::inventory
