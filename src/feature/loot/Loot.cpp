// Neutral stub of the "loot" contract (F0). F10 replaces this file (rule R18).
#include "feature/loot/Loot.h"

#include "mc/world/item/ItemStack.h"

#include "core/Stub.h"

namespace gf::loot {

std::vector<ItemStack> roll(std::string const&, LootContext const&) {
    GF_STUB("loot.roll");
    return {};
}
void registerFunction(std::string const&, FunctionFn) { GF_STUB("loot.registerFunction"); }
void registerCondition(std::string const&, ConditionFn) { GF_STUB("loot.registerCondition"); }
void addGlobalModifier(std::string const&, ModifierFn) { GF_STUB("loot.addGlobalModifier"); }
bool placeContainer(BlockSource&, BlockPos const&, std::string const&, int, std::string const&, std::int64_t) {
    GF_STUB("loot.placeContainer");
    return false;
}
void onEntityDrops(EntityDropsFn) { GF_STUB("loot.onEntityDrops"); }

} // namespace gf::loot
