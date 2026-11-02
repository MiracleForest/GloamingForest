#include "core/landmark/Decoration.h"

#include <utility>

#include "core/landmark/JavaMath.h"

namespace gf::core::landmark::decoration {

namespace {

constexpr std::string_view kStepNames[kSteps] = {
    "raw_generation",
    "lakes",
    "local_modifications",
    "underground_structures",
    "surface_structures",
    "strongholds",
    "underground_ores",
    "underground_decoration",
    "fluid_springs",
    "vegetal_decoration",
    "top_layer_modification",
};

/// (namespace, path) of a resource location ("minecraft" without a namespace, as ResourceLocation).
std::pair<std::string_view, std::string_view> split(std::string_view id) {
    auto colon = id.find(':');
    if (colon == std::string_view::npos) return {"minecraft", id};
    return {id.substr(0, colon), id.substr(colon + 1)};
}

} // namespace

int stepIndex(std::string_view name) {
    for (int i = 0; i < kSteps; ++i)
        if (name == kStepNames[i]) return i;
    return -1;
}

std::string_view stepName(int step) { return step >= 0 && step < kSteps ? kStepNames[step] : std::string_view{}; }

std::vector<Structure> const& vanillaStructures() {
    // net.minecraft 1.21.1: data/minecraft/worldgen/structure/<path>.json "step"
    constexpr int underground = 3, surface = 4, undergroundDecoration = 7;
    static std::vector<Structure> const list = {
        {"minecraft:ancient_city", undergroundDecoration},
        {"minecraft:bastion_remnant", surface},
        {"minecraft:buried_treasure", underground},
        {"minecraft:desert_pyramid", surface},
        {"minecraft:end_city", surface},
        {"minecraft:fortress", undergroundDecoration},
        {"minecraft:igloo", surface},
        {"minecraft:jungle_pyramid", surface},
        {"minecraft:mansion", surface},
        {"minecraft:mineshaft", underground},
        {"minecraft:mineshaft_mesa", underground},
        {"minecraft:monument", surface},
        {"minecraft:nether_fossil", undergroundDecoration},
        {"minecraft:ocean_ruin_cold", surface},
        {"minecraft:ocean_ruin_warm", surface},
        {"minecraft:pillager_outpost", surface},
        {"minecraft:ruined_portal", surface},
        {"minecraft:ruined_portal_desert", surface},
        {"minecraft:ruined_portal_jungle", surface},
        {"minecraft:ruined_portal_mountain", surface},
        {"minecraft:ruined_portal_nether", surface},
        {"minecraft:ruined_portal_ocean", surface},
        {"minecraft:ruined_portal_swamp", surface},
        {"minecraft:shipwreck", surface},
        {"minecraft:shipwreck_beached", surface},
        {"minecraft:stronghold", surface},
        {"minecraft:swamp_hut", surface},
        {"minecraft:trail_ruins", underground},
        {"minecraft:trial_chambers", underground},
        {"minecraft:village_desert", surface},
        {"minecraft:village_plains", surface},
        {"minecraft:village_savanna", surface},
        {"minecraft:village_snowy", surface},
        {"minecraft:village_taiga", surface},
    };
    return list;
}

bool registryBefore(std::string_view a, std::string_view b) {
    auto [aNs, aPath] = split(a);
    auto [bNs, bPath] = split(b);
    // the file paths "worldgen/structure/<path>.json" (String#compareTo: UTF-16 units, the byte order
    // of these ASCII ids; the ".json" suffix sorts "x" before "x_y"), then the namespaces
    std::string fa = std::string(aPath) + ".json", fb = std::string(bPath) + ".json";
    if (fa != fb) return fa < fb;
    return aNs < bNs;
}

Slot slotOf(std::string const& id, int step, std::vector<Structure> const& registry) {
    Slot slot{step, 0};
    for (auto const& s : registry)
        if (s.step == step && s.id != id && registryBefore(s.id, id)) ++slot.index;
    return slot;
}

std::int64_t decorationSeed(std::int64_t levelSeed, int chunkX, int chunkZ) {
    jrand::WorldgenRandom random(std::make_unique<jrand::Xoroshiro128pp>(0));
    // SectionPos#origin of the chunk: SectionPos#sectionToBlockCoord, an int shift
    return random.setDecorationSeed(levelSeed, java::ishl(chunkX, 4), java::ishl(chunkZ, 4));
}

std::unique_ptr<jrand::WorldgenRandom> structureRandom(std::int64_t decorationSeed, Slot slot) {
    auto random = std::make_unique<jrand::WorldgenRandom>(std::make_unique<jrand::Xoroshiro128pp>(0));
    random->setFeatureSeed(decorationSeed, slot.index, slot.step);
    return random;
}

} // namespace gf::core::landmark::decoration
