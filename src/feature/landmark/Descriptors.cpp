// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java
//   src/main/java/twilightforest/world/components/structures/util/ProgressionStructure.java
//   src/main/java/twilightforest/world/components/structures/util/ControlledSpawningStructure.java
//   src/main/java/twilightforest/world/components/structures/util/StructureHints.java
//   src/main/java/twilightforest/world/components/structures/util/DecorationClearance.java
//   src/main/java/twilightforest/data/tags/StructureTagGenerator.java
//   src/main/java/twilightforest/init/TFStructureTypes.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Landmark descriptors: TF's structure records (codec fields of the Java structure classes) read
// from <mod>/data/gf/landmarks/*.json (assets/data/landmarks, made from TF's generated structure
// and structure set JSON). When the imported TF worldgen data (F1) is present, records missing
// here are read from data/twilightforest/worldgen/{structure,structure_set}.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <shared_mutex>

#include "core/landmark/Grid.h"
#include "feature/landmark/Internal.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::landmark {

namespace {

struct Store {
    std::shared_mutex                   mutex;
    std::map<std::string, Info>         infos;
    std::map<std::string, StructureSet> sets;
};
Store& store() {
    static Store s;
    return s;
}

/// The Java class family of each structure type (TFStructureTypes and the class hierarchy).
Traits traitsOf(std::string const& type) {
    Traits t;
    auto   controlled = [&] {
        t.landmark = t.conquerable = t.progression = t.controlledSpawns = true;
    };
    if (type == "twilightforest:aurora_palace" || type == "twilightforest:dark_tower" || type == "twilightforest:final_castle"
        || type == "twilightforest:knight_stronghold") {
        controlled(); // ControlledSpawningStructure
    } else if (type == "twilightforest:labyrinth" || type == "twilightforest:lich_tower" || type == "twilightforest:yeti_cave") {
        controlled();
        t.customDensity = true;
    } else if (type == "twilightforest:giant_house" || type == "twilightforest:troll_cave") {
        controlled(); // ProgressionStructure implements ConfigurableSpawns
    } else if (type == "twilightforest:hydra_lair") {
        t.landmark = t.conquerable = t.progression = t.customDensity = true;
    } else if (type == "twilightforest:naga_courtyard") {
        t.landmark = t.conquerable = t.customDensity = true;
    } else if (type == "twilightforest:quest_grove") {
        t.landmark = t.conquerable = true;
    } else if (type == "twilightforest:hedge_maze") {
        t.landmark = t.customDensity = true;
    } else if (type == "twilightforest:hollow_hill") {
        t.landmark = t.controlledSpawns = t.customDensity = true;
    } else if (type == "twilightforest:mushroom_tower" || type == "twilightforest:landmark") {
        t.landmark = true; // LandmarkStructure, LandmarkWrappedStructure
    } else if (type == "twilightforest:progression") {
        t.landmark = t.conquerable = t.progression = true; // ProgressionWrappedStructure
    } else if (type == "twilightforest:fallen_trunk") {
        t.customDensity = true;
    }
    return t;
}

/// StructureTagGenerator.LANDMARK.
bool inLandmarkTag(std::string const& key) {
    static char const* const members[] = {
        "twilightforest:hedge_maze",        "twilightforest:quest_grove",       "twilightforest:mushroom_tower",
        "twilightforest:small_hollow_hill", "twilightforest:medium_hollow_hill", "twilightforest:large_hollow_hill",
        "twilightforest:naga_courtyard",    "twilightforest:lich_tower",        "twilightforest:labyrinth",
        "twilightforest:hydra_lair",        "twilightforest:knight_stronghold", "twilightforest:dark_tower",
        "twilightforest:yeti_cave",         "twilightforest:aurora_palace",     "twilightforest:troll_cave",
        "twilightforest:final_castle",
    };
    return std::find(std::begin(members), std::end(members), key) != std::end(members);
}

std::optional<Json> readJson(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto j = Json::parse(in, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

StructureSet parseSet(std::string const& id, Json const& j) {
    StructureSet s;
    s.id        = id;
    s.placement = core::landmark::placement::Placement::fromJson(j.value("placement", Json::object()));
    if (auto it = j.find("structures"); it != j.end() && it->is_array())
        for (auto const& e : *it)
            if (e.is_object()) s.structures.emplace_back(e.value("structure", std::string{}), e.value("weight", 1));
    return s;
}

} // namespace

std::string normalizeKey(std::string const& id) {
    if (id.empty() || id.find(':') != std::string::npos) return id;
    return "twilightforest:" + id;
}

Info parseRecord(std::string const& key, Json const& record) {
    Info i;
    i.raw          = record;
    i.d.key        = key;
    i.type         = record.value("type", std::string{});
    i.traits       = traitsOf(i.type);
    i.traits.landmarkTag = inLandmarkTag(key);
    i.biomes       = record.value("biomes", std::string{});
    i.step         = record.value("step", std::string{"surface_structures"});
    i.centerInChunk = record.value("center_in_chunk", true);
    i.adjustment   = core::landmark::terrainAdjustmentOf(record.value("terrain_adaptation", std::string{"none"}));
    i.d.icon       = record.value("structure_icon", std::string{});
    if (auto it = record.find("advancements_required"); it != record.end() && it->is_array())
        for (auto const& a : *it)
            if (a.is_string()) i.d.lockAdvancements.push_back(a.get<std::string>());

    // DecorationClearance (LandmarkStructure's defaults when absent)
    auto clearance = record.contains("decoration_clearance") ? record["decoration_clearance"] : Json();
    i.decoration   = core::landmark::DecorationConfig::fromJson(clearance);
    i.d.surfaceDecorationsAllowed     = i.decoration.surfaceDecorations;
    i.d.undergroundDecorationsAllowed = i.decoration.undergroundDecorations;
    i.d.clearanceChunks               = static_cast<int>(std::ceil(i.decoration.chunkClearanceRadius));

    // ConquerableStructure: starts carry the conquered flag
    i.d.conquerable = i.traits.conquerable;

    // ControlledSpawns / spawn overrides
    if (record.contains("controlled_spawns")) i.d.controlledSpawns = record["controlled_spawns"];
    if (record.contains("spawn_overrides")) i.d.spawnOverrides = record["spawn_overrides"];

    // StructureHints: the hint item is a written book whose title / pages are book keys
    if (auto hint = record.find("hint_creature"); hint != record.end() && hint->is_object()) {
        i.hintMob = hint->value("hint_mob", std::string{});
        auto item = hint->value("hint_item", Json::object());
        auto book = item.value("components", Json::object()).value("minecraft:written_book_content", Json::object());
        auto title = book.value("title", Json::object()).value("raw", std::string{});
        static constexpr std::string_view prefix = "twilightforest.book.";
        if (title.starts_with(prefix)) i.hintBook = title.substr(prefix.size());
        if (auto pages = book.find("pages"); pages != book.end() && pages->is_array()) i.hintPages = static_cast<int>(pages->size());
        if (!i.hintBook.empty()) i.d.hintBookKey = std::string(prefix) + i.hintBook;
    }

    i.d.extra = Json{
        {"type", i.type},
        {"biomes", i.biomes},
        {"step", i.step},
        {"terrain_adaptation", record.value("terrain_adaptation", std::string{"none"})},
        {"center_in_chunk", i.centerInChunk},
        {"chunk_clearance_radius", i.decoration.chunkClearanceRadius},
        {"allow_biome_vegetation", i.decoration.vegetation},
        {"adjust_structure_elevation", i.decoration.adjustElevation},
        {"landmark", i.traits.landmark},
        {"in_landmark_tag", i.traits.landmarkTag},
        {"progression", i.traits.progression},
        {"controlled_spawns", i.traits.controlledSpawns},
        {"custom_density", i.traits.customDensity},
        {"hint_mob", i.hintMob},
        {"hint_book", Json{{"name", i.hintBook}, {"pages", i.hintPages}}},
    };
    return i;
}

void loadDescriptors() {
    std::map<std::string, Info>         infos;
    std::map<std::string, StructureSet> sets;
    auto const                          data = Mod::getInstance().getSelf().getModDir() / "data";
    std::error_code                     ec;

    // 1. the imported TF worldgen data (F1), when present
    auto tfSets = data / "twilightforest" / "worldgen" / "structure_set";
    if (std::filesystem::is_directory(tfSets, ec)) {
        for (auto const& e : std::filesystem::directory_iterator(tfSets, ec)) {
            if (e.path().extension() != ".json") continue;
            if (auto j = readJson(e.path())) {
                auto id  = "twilightforest:" + e.path().stem().string();
                sets[id] = parseSet(id, *j);
            }
        }
    }
    auto tfStructures = data / "twilightforest" / "worldgen" / "structure";
    if (std::filesystem::is_directory(tfStructures, ec)) {
        for (auto const& e : std::filesystem::directory_iterator(tfStructures, ec)) {
            if (e.path().extension() != ".json") continue;
            if (auto j = readJson(e.path())) {
                auto key   = "twilightforest:" + e.path().stem().string();
                infos[key] = parseRecord(key, *j);
            }
        }
    }

    // 2. the landmark descriptors (assets/data/landmarks/<namespace>/<path>.json): they win per key
    auto own = data / "gf" / "landmarks";
    int  n   = 0;
    if (std::filesystem::is_directory(own, ec)) {
        for (auto const& e : std::filesystem::recursive_directory_iterator(own, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".json") continue;
            auto j = readJson(e.path());
            if (!j || !j->is_object()) {
                log().warn("landmark: unreadable descriptor {}", e.path().filename().string());
                continue;
            }
            auto key    = normalizeKey(j->value("key", e.path().parent_path().filename().string() + ":" + e.path().stem().string()));
            auto record = j->value("structure", Json::object());
            if (auto fixed = j->find("fixed_decoration_clearance"); fixed != j->end() && !record.contains("decoration_clearance"))
                record["decoration_clearance"] = *fixed;
            infos[key] = parseRecord(key, record);
            if (auto set = j->find("structure_set"); set != j->end() && set->is_object()) {
                auto id  = set->value("id", key);
                sets[id] = parseSet(id, *set);
            }
            ++n;
        }
    }
    // structure set of each structure
    for (auto& [id, set] : sets)
        for (auto const& [key, weight] : set.structures)
            if (auto it = infos.find(key); it != infos.end()) it->second.structureSet = id;

    // the unimplemented TF keys never get a descriptor
    for (auto it = infos.begin(); it != infos.end();) {
        if (core::landmark::isUnimplementedKey(it->first)) it = infos.erase(it);
        else ++it;
    }

    {
        std::unique_lock lock(store().mutex);
        // keep run-time registrations (fixtures) that the data does not define
        for (auto& [key, i] : store().infos)
            if (!infos.contains(key) && i.raw.value("gf_runtime", false)) infos.emplace(key, i);
        store().infos = std::move(infos);
        store().sets  = std::move(sets);
    }
    log().info("landmark: {} descriptors, {} structure sets loaded", descriptorKeys().size(), structureSets().size());
    (void)n;
}

Info const* info(std::string const& key) {
    std::shared_lock lock(store().mutex);
    auto             it = store().infos.find(normalizeKey(key));
    return it == store().infos.end() ? nullptr : &it->second;
}

std::vector<std::string> descriptorKeys() {
    std::shared_lock         lock(store().mutex);
    std::vector<std::string> out;
    for (auto const& [k, v] : store().infos) out.push_back(k);
    return out;
}

StructureSet const* structureSet(std::string const& id) {
    std::shared_lock lock(store().mutex);
    auto             it = store().sets.find(id);
    return it == store().sets.end() ? nullptr : &it->second;
}

std::vector<StructureSet const*> structureSets() {
    std::shared_lock                 lock(store().mutex);
    std::vector<StructureSet const*> out;
    for (auto const& [k, v] : store().sets) out.push_back(&v);
    return out;
}

void registerInfo(Info i) {
    i.raw["gf_runtime"] = true;
    std::unique_lock lock(store().mutex);
    auto             key = i.d.key;
    // never overwrite or erase: info() hands out pointers into the map
    store().infos.try_emplace(key, std::move(i));
}

} // namespace gf::feature::landmark

namespace gf::landmark {

Descriptor const* descriptor(std::string const& key) {
    auto const* i = feature::landmark::info(key);
    return i ? &i->d : nullptr;
}

} // namespace gf::landmark
