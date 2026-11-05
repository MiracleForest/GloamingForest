// Contract extension of U4 woodwork (EnchantPower.h): extra enchantment power providers on the game's
// enchanting table (EnchantUtils::getBookCasePositions).

#include "feature/ext/woodwork/EnchantPower.h"

#include <cmath>
#include <utility>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

namespace gf::woodwork_ext::enchantpower {

namespace {
ProviderFn& provider() {
    static ProviderFn fn;
    return fn;
}
} // namespace

void setProvider(ProviderFn fn) { provider() = std::move(fn); }

} // namespace gf::woodwork_ext::enchantpower

LL_STATIC_HOOK(
    WoodworkExtBookcaseHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getBookCasePositions,
    std::vector<Vec3>,
    BlockSource& source,
    Vec3 const&  pos
) {
    auto        list = origin(source, pos);
    auto const& fn   = gf::woodwork_ext::enchantpower::provider();
    if (!fn) return list;
    BlockPos table{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    for (auto const& shelf : fn(source, table)) list.push_back(Vec3{static_cast<float>(shelf.x), static_cast<float>(shelf.y), static_cast<float>(shelf.z)});
    return list;
}

namespace gf::woodwork_ext::enchantpower {

void install() { WoodworkExtBookcaseHook::hook(); }
void uninstall() { WoodworkExtBookcaseHook::unhook(); }

} // namespace gf::woodwork_ext::enchantpower
