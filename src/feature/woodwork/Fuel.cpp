// Unit U4 (woodwork): the fuel values Java gives the wood set items through the vanilla item tags
// (net.minecraft AbstractFurnaceBlockEntity#buildFuels, NeoForge 21.1's neoforge:furnace_fuels of
// vanilla), read from the imported tags (TF's ItemTagGenerator puts its wood items into them). The
// banisters keep TF's own row (#twilightforest:banisters 300, F9's data map), the hollow log items
// HollowLogItem#getBurnTime (300). Chests and the canopy bookshelf burn in Java only as the vanilla
// blocks themselves (Blocks.CHEST, TRAPPED_CHEST, BOOKSHELF), not as tag members: TF's do not burn.
// Only the wood tags (the unit's items): #minecraft:saplings (100) is U3's to apply (rule R7).

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "feature/ext/woodwork/Fuel.h"
#include "feature/tags/Tags.h"
#include "feature/woodwork/Woodwork.h"

namespace gf::feature::woodwork {

namespace {

/// Java's vanilla fuel tags in buildFuels order (the first match wins as in Java's map, where no
/// TF item is in two of them with different values).
constexpr std::array<std::pair<char const*, int>, 14> kFuelTags{{
    {"minecraft:logs", 300},
    {"minecraft:planks", 300},
    {"minecraft:wooden_stairs", 300},
    {"minecraft:wooden_slabs", 150},
    {"minecraft:wooden_trapdoors", 300},
    {"minecraft:wooden_pressure_plates", 300},
    {"minecraft:wooden_fences", 300},
    {"minecraft:fence_gates", 300},
    {"minecraft:signs", 200},
    {"minecraft:hanging_signs", 800},
    {"minecraft:wooden_doors", 200},
    {"minecraft:wooden_buttons", 100},
    {"minecraft:boats", 1200},
    {"minecraft:chest_boats", 1200},
}};

} // namespace

std::optional<int> javaFuelTicks(std::string const& itemId) {
    if (hollowLogOf(itemId) && hollowLogOf(itemId)->item == itemId) return 300; // HollowLogItem#getBurnTime
    for (auto const& [tag, ticks] : kFuelTags) {
        if (tags::has(tags::Registry::Item, tag, itemId)) return ticks;
    }
    return std::nullopt;
}

void installFuel() {
    woodwork_ext::fuel::setProvider([](std::string const& itemId) { return javaFuelTicks(itemId); });
    woodwork_ext::fuel::install();
}

void uninstallFuel() {
    woodwork_ext::fuel::uninstall();
    woodwork_ext::fuel::setProvider({});
}

} // namespace gf::feature::woodwork
