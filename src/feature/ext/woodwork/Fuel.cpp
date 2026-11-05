// Contract extension of U4 woodwork (Fuel.h): Java's vanilla fuel tags for the TF items without a
// neoforge:furnace_fuels row.

#include "feature/ext/woodwork/Fuel.h"

#include <mutex>
#include <utility>

#include "ll/api/memory/Hook.h"

#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/datamaps/DataMaps.h"
#include "game/Items.h"

namespace gf::woodwork_ext::fuel {

namespace {
struct Store {
    std::mutex mutex;
    ProviderFn fn;
};
Store& store() {
    static Store s;
    return s;
}
} // namespace

void setProvider(ProviderFn fn) {
    std::lock_guard lock(store().mutex);
    store().fn = std::move(fn);
}

std::optional<int> burnTicks(std::string const& itemId) {
    ProviderFn fn;
    {
        std::lock_guard lock(store().mutex);
        fn = store().fn;
    }
    if (!fn || itemId.empty() || itemId.starts_with("minecraft:")) return std::nullopt;
    if (datamaps::get("neoforge:furnace_fuels", itemId)) return std::nullopt; // F9 applies the row
    return fn(itemId);
}

} // namespace gf::woodwork_ext::fuel

LL_STATIC_HOOK(
    WoodworkExtFuelHook,
    ll::memory::HookPriority::Normal,
    &FurnaceBlockActor::getBurnDuration,
    float,
    ItemStackBase const& item,
    float                burnInterval
) {
    try {
        if (!item.isNull() && item.mItem) {
            if (auto ticks = gf::woodwork_ext::fuel::burnTicks(item.getTypeName()); ticks && *ticks > 0) {
                // never destroyed: an ItemStack must not outlive the item registry at thread exit
                static thread_local ItemStack const* coal = [] {
                    auto s = gf::game::items::make("minecraft:coal");
                    return s ? new ItemStack(*s) : nullptr;
                }();
                if (coal) return origin(*coal, burnInterval) * static_cast<float>(*ticks) / 1600.0f;
            }
        }
    } catch (...) {
    }
    return origin(item, burnInterval);
}

namespace gf::woodwork_ext::fuel {

void install() { WoodworkExtFuelHook::hook(); }
void uninstall() { WoodworkExtFuelHook::unhook(); }

} // namespace gf::woodwork_ext::fuel
