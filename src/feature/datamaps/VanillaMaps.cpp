// The NeoForge data maps F9 applies to the game (owners.tsv: neoforge:compostables and
// neoforge:furnace_fuels follow their items' owner, the runtime is F9's):
// - neoforge:compostables {"chance": 0..1}: the composter's fill chance of an item
//   (ComposterBlock::getFillChance, a percentage: hoppers and players alike);
// - neoforge:furnace_fuels {"burn_time": ticks}: the fuel value of an item in furnaces, smokers and
//   blast furnaces (FurnaceBlockActor::getBurnDuration; on LeviLamina 26.60, which dropped it,
//   FurnaceBlockActor::getItemBurnDuration, what the game asks for an item without a minecraft:fuel
//   component), scaled from coal's (Java's 1600 ticks) so it follows whatever unit and furnace speed
//   the engine uses.
// Entries of TF's items (and tags of them) only: vanilla items keep Bedrock's values. Both hooks run
// on the server and in the client mod (its own composter / fuel slot checks); vanilla clients check
// the fuel slot themselves, which needs the item's minecraft:fuel component in the behaviour pack
// (F1's item import).
#include <algorithm>
#include <cmath>
#include <optional>

#include "ll/api/memory/Hook.h"

#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/datamaps/DataMaps.h"
#include "game/Compat.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::datamaps::vanilla {
namespace {

/// The data map value of a non-vanilla item (nullptr for vanilla ids and items without one).
nlohmann::json const* valueOf(char const* mapId, ItemStackBase const& item) {
    if (item.isNull() || !item.mItem) return nullptr;
    auto id = item.getTypeName();
    if (id.empty() || id.starts_with("minecraft:")) return nullptr;
    return get(mapId, id);
}

#if GF_LL_VERSION >= 2660
/// The data map value of a non-vanilla item type (nullptr for vanilla ids and items without one).
nlohmann::json const* valueOf(char const* mapId, Item const& item) {
    auto const& id = item.mFullName->getString();
    if (id.empty() || id.starts_with("minecraft:")) return nullptr;
    return get(mapId, id);
}
#endif

} // namespace
} // namespace gf::datamaps::vanilla

namespace dv = gf::datamaps::vanilla;

LL_STATIC_HOOK(DataMapsCompostHook, ll::memory::HookPriority::Normal, &ComposterBlock::getFillChance, schar, ItemStackBase const& item) {
    try {
        if (auto const* v = dv::valueOf("neoforge:compostables", item); v && v->is_object()) {
            double chance = v->value("chance", 0.0);
            if (chance > 0) return static_cast<schar>(std::clamp(std::lround(chance * 100.0), 1L, 100L));
        }
    } catch (...) {
    }
    return origin(item);
}

#if GF_LL_VERSION >= 2660
LL_STATIC_HOOK(
    DataMapsFuelHook,
    ll::memory::HookPriority::Normal,
    &FurnaceBlockActor::getItemBurnDuration,
    float,
    Item const& item,
    float       burnInterval
) {
#else
LL_STATIC_HOOK(
    DataMapsFuelHook,
    ll::memory::HookPriority::Normal,
    &FurnaceBlockActor::getBurnDuration,
    float,
    ItemStackBase const& item,
    float                burnInterval
) {
#endif
    try {
        if (auto const* v = dv::valueOf("neoforge:furnace_fuels", item); v && v->is_object()) {
            int burn = v->value("burn_time", 0);
            if (burn > 0) {
                // never destroyed: an ItemStack must not outlive the item registry at thread exit
                static thread_local ItemStack const* coal = [] {
                    auto s = gf::game::items::make("minecraft:coal");
                    return s ? new ItemStack(*s) : nullptr;
                }();
#if GF_LL_VERSION >= 2660
                // coal is a data-driven fuel there: its minecraft:fuel component, not this function
                if (coal) return gf::game::compat::burnDuration(*coal, burnInterval) * static_cast<float>(burn) / 1600.0f;
#else
                if (coal) return origin(*coal, burnInterval) * static_cast<float>(burn) / 1600.0f;
#endif
            }
        }
    } catch (...) {
    }
    return origin(item, burnInterval);
}

namespace gf::datamaps::vanilla {
namespace {
class VanillaMapsModule {
public:
    void install() {
        DataMapsCompostHook::hook();
        DataMapsFuelHook::hook();
    }
    void uninstall() {
        DataMapsFuelHook::unhook();
        DataMapsCompostHook::unhook();
    }
};
} // namespace
} // namespace gf::datamaps::vanilla

GF_REGISTER_MODULE_ORDERED(gf::datamaps::vanilla::VanillaMapsModule, gf::ModuleOrder::Contracts);
