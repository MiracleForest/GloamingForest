// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/GloamingStructures.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Java's structure registry as ChunkGenerator#applyBiomeDecoration numbers it (F5): every
// structure of the registry, grouped by its GenerationStep.Decoration, in registry order. The
// registry is loaded from the data packs (RegistryDataLoader#loadContentsFromManager iterates
// FileToIdConverter#listMatchingResources, a TreeMap from MultiPackResourceManager#listResources,
// whose ResourceLocation#compareTo compares the file path first, then the namespace), so the
// order is the file paths "worldgen/structure/<path>.json" sorted, then the namespaces. Minecraft
// 1.21.1's 34 structures and upstream 4.8's 21 (src/generated/resources/data/gloamingforest/worldgen/
// structure, the registrations of GloamingStructures#bootstrap) are built in, with the steps of their
// JSON; any other structure JSON under <mod>/data/<ns>/worldgen/structure is added (and a JSON
// of a built-in id wins for its step).
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>

#include "feature/worldgen/Internal.h"

namespace gf::worldgen::ext {

namespace {

struct Builtin {
    char const*    id;
    DecorationStep step;
};

// net/minecraft/server 1.21.1: data/minecraft/worldgen/structure/*.json "step"
constexpr Builtin kVanilla[] = {
    {"minecraft:ancient_city", DecorationStep::UndergroundDecoration},
    {"minecraft:bastion_remnant", DecorationStep::SurfaceStructures},
    {"minecraft:buried_treasure", DecorationStep::UndergroundStructures},
    {"minecraft:desert_pyramid", DecorationStep::SurfaceStructures},
    {"minecraft:end_city", DecorationStep::SurfaceStructures},
    {"minecraft:fortress", DecorationStep::UndergroundDecoration},
    {"minecraft:igloo", DecorationStep::SurfaceStructures},
    {"minecraft:jungle_pyramid", DecorationStep::SurfaceStructures},
    {"minecraft:mansion", DecorationStep::SurfaceStructures},
    {"minecraft:mineshaft", DecorationStep::UndergroundStructures},
    {"minecraft:mineshaft_mesa", DecorationStep::UndergroundStructures},
    {"minecraft:monument", DecorationStep::SurfaceStructures},
    {"minecraft:nether_fossil", DecorationStep::UndergroundDecoration},
    {"minecraft:ocean_ruin_cold", DecorationStep::SurfaceStructures},
    {"minecraft:ocean_ruin_warm", DecorationStep::SurfaceStructures},
    {"minecraft:pillager_outpost", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_desert", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_jungle", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_mountain", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_nether", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_ocean", DecorationStep::SurfaceStructures},
    {"minecraft:ruined_portal_swamp", DecorationStep::SurfaceStructures},
    {"minecraft:shipwreck", DecorationStep::SurfaceStructures},
    {"minecraft:shipwreck_beached", DecorationStep::SurfaceStructures},
    {"minecraft:stronghold", DecorationStep::SurfaceStructures},
    {"minecraft:swamp_hut", DecorationStep::SurfaceStructures},
    {"minecraft:trail_ruins", DecorationStep::UndergroundStructures},
    {"minecraft:trial_chambers", DecorationStep::UndergroundStructures},
    {"minecraft:village_desert", DecorationStep::SurfaceStructures},
    {"minecraft:village_plains", DecorationStep::SurfaceStructures},
    {"minecraft:village_savanna", DecorationStep::SurfaceStructures},
    {"minecraft:village_snowy", DecorationStep::SurfaceStructures},
    {"minecraft:village_taiga", DecorationStep::SurfaceStructures},
};

// upstream 4.8: src/generated/resources/data/gloamingforest/worldgen/structure/*.json "step"
constexpr Builtin kGloaming[] = {
    {"gloamingforest:aurora_palace", DecorationStep::SurfaceStructures},
    {"gloamingforest:camp", DecorationStep::SurfaceStructures},
    {"gloamingforest:dark_tower", DecorationStep::SurfaceStructures},
    {"gloamingforest:fallen_trunk", DecorationStep::SurfaceStructures},
    {"gloamingforest:final_castle", DecorationStep::SurfaceStructures},
    {"gloamingforest:giant_house", DecorationStep::TopLayerModification},
    {"gloamingforest:hedge_maze", DecorationStep::SurfaceStructures},
    {"gloamingforest:hollow_tree", DecorationStep::SurfaceStructures},
    {"gloamingforest:hydra_lair", DecorationStep::SurfaceStructures},
    {"gloamingforest:knight_stronghold", DecorationStep::UndergroundStructures},
    {"gloamingforest:labyrinth", DecorationStep::UndergroundStructures},
    {"gloamingforest:large_hollow_hill", DecorationStep::SurfaceStructures},
    {"gloamingforest:lich_tower", DecorationStep::SurfaceStructures},
    {"gloamingforest:medium_hollow_hill", DecorationStep::SurfaceStructures},
    {"gloamingforest:mushroom_tower", DecorationStep::SurfaceStructures},
    {"gloamingforest:naga_courtyard", DecorationStep::SurfaceStructures},
    {"gloamingforest:quest_grove", DecorationStep::SurfaceStructures},
    {"gloamingforest:small_hollow_hill", DecorationStep::SurfaceStructures},
    {"gloamingforest:swamp_hollow_tree", DecorationStep::SurfaceStructures},
    {"gloamingforest:troll_cave", DecorationStep::UndergroundStructures},
    {"gloamingforest:yeti_cave", DecorationStep::SurfaceStructures},
};

/// GenerationStep.Decoration#getSerializedName -> ordinal (-1: unknown).
int stepOf(std::string const& name) {
    static char const* const names[] = {
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
    for (int i = 0; i < static_cast<int>(std::size(names)); ++i)
        if (name == names[i]) return i;
    return -1;
}

struct Registry {
    std::uint64_t                         dataGeneration = 0;
    std::vector<std::vector<std::string>> steps;
    std::map<std::string, StructureSlot>  slots;
};

std::shared_ptr<Registry const> registry() {
    static std::mutex                      m;
    static std::shared_ptr<Registry const> cached;
    auto const&                            data = features::detail::data();
    std::lock_guard                        lock(m);
    if (cached && cached->dataGeneration == data.generation) return cached;
    std::map<std::string, int> all; // id -> step
    for (auto const& b : kVanilla) all[b.id] = static_cast<int>(b.step);
    for (auto const& b : kGloaming) all[b.id] = static_cast<int>(b.step);
    for (auto const& [id, json] : data.structures) {
        int step = stepOf(json.value("step", std::string()));
        if (step < 0) {
            features::detail::warnOnce("structure step " + id, "structure " + id + " has no valid step (Java refuses it)");
            continue;
        }
        all[features::detail::normalizeId(id)] = step;
    }
    // TreeMap<ResourceLocation> over "<ns>:worldgen/structure/<path>.json": the path, then the namespace
    std::vector<std::tuple<std::string, std::string, std::string, int>> sorted; // file path, namespace, id, step
    for (auto const& [id, step] : all) {
        auto colon = id.find(':');
        sorted.emplace_back("worldgen/structure/" + id.substr(colon + 1) + ".json", id.substr(0, colon), id, step);
    }
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        // Java's String#compareTo on these ASCII paths is the byte order
        if (int c = std::get<0>(a).compare(std::get<0>(b)); c != 0) return c < 0;
        return std::get<1>(a) < std::get<1>(b);
    });
    auto r            = std::make_shared<Registry>();
    r->dataGeneration = data.generation;
    r->steps.resize(11);
    for (auto const& [file, ns, id, step] : sorted) {
        auto& list = r->steps[static_cast<std::size_t>(step)];
        r->slots[id] = {step, static_cast<int>(list.size())};
        list.push_back(id);
    }
    cached = r;
    return cached;
}

} // namespace

std::vector<std::vector<std::string>> structuresByStep() { return registry()->steps; }

std::optional<StructureSlot> structureSlot(std::string const& structureId) {
    auto r  = registry();
    auto it = r->slots.find(features::detail::normalizeId(structureId));
    if (it == r->slots.end()) return std::nullopt;
    return it->second;
}

std::unique_ptr<core::jrand::WorldgenRandom> DecorationContext::structureRandomOf(std::string const& structureId) const {
    auto slot = structureSlot(structureId);
    if (!slot || slot->step != step) return nullptr;
    return structureRandom(slot->index);
}

} // namespace gf::worldgen::ext
