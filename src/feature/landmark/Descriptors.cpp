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
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "core/landmark/Grid.h"
#include "feature/landmark/Internal.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::landmark {

namespace {

/// The descriptors and structure sets. Entries are added, never erased or replaced: info(),
/// structureSet(), structureSets() and the contract's descriptor() hand out pointers into the maps
/// (std::map nodes stay where they are), which W1 units and generation threads may keep.
struct Store {
    std::shared_mutex                   mutex;
    std::map<std::string, Info>         infos;
    std::map<std::string, StructureSet> sets;
    std::mutex                          loadMutex; ///< one load at a time
    std::atomic<bool>                   loaded{false};
    /// bit `step`: some descriptor decorates at that GenerationStep.Decoration (decorationStepMask)
    std::atomic<std::uint32_t>          stepMask{0};
};
Store& store() {
    static Store s;
    return s;
}
/// The thread loading the descriptors (its own lookups during the load must not wait for it).
thread_local bool tLoading = false;

void loadDescriptorsLocked();

/// Loads the descriptors on first use: a module installed before this unit's, or a generation
/// thread, gets them too.
void ensureLoaded() {
    if (store().loaded.load(std::memory_order_acquire) || tLoading) return;
    std::lock_guard load(store().loadMutex);
    if (!store().loaded.load(std::memory_order_acquire)) loadDescriptorsLocked();
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

/// The structure classes overriding LandmarkStructure#adjustForTerrain with WorldUtil
/// #adjustForTerrain over the center chunk widened by a radius (no clamp, adjust_structure_elevation
/// not consulted); every other one uses DecorationClearance#adjustForTerrain.
std::optional<TerrainOverride> terrainOverrideOf(std::string const& type) {
    if (type == "twilightforest:lich_tower") return TerrainOverride{32, 4, 0};     // LichTowerStructure#adjustForTerrain
    if (type == "twilightforest:hedge_maze") return TerrainOverride{24, 4, 0};     // HedgeMazeStructure#adjustForTerrain
    if (type == "twilightforest:naga_courtyard") return TerrainOverride{40, 4, 2}; // NagaCourtyardStructure#adjustForTerrain: + 2
    return std::nullopt;
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

/// Descriptor::extra["gf"]: what GloamingForest derives from the record (type traits, defaults).
constexpr char kDerivedKey[] = "gf";
/// The TF record keys parsed into Descriptor's own fields (left out of Descriptor::extra).
constexpr char const* kDescriptorFields[] = {"structure_icon", "advancements_required", "controlled_spawns", "spawn_overrides"};

/// Merges a landmark descriptor's record over the imported TF record of the same key (top-level
/// keys of the descriptor win), so no TF field is lost when both exist.
Json mergeRecord(Json const* imported, Json const& own) {
    Json merged = imported && imported->is_object() ? *imported : Json::object();
    if (own.is_object())
        for (auto const& [k, v] : own.items()) merged[k] = v;
    return merged;
}

/// Runs one file's parsing; a malformed file (a field of the wrong type) is skipped with a warning
/// instead of failing the whole load (which every lookup would then retry).
template <class Fn>
void guarded(std::filesystem::path const& file, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& ex) {
        log().warn("landmark: skipped malformed descriptor {}: {}", file.filename().string(), ex.what());
    }
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

Info parseRecord(std::string const& key, Json const& record, Json const* fixedClearance) {
    Info i;
    i.raw          = record;
    i.d.key        = key;
    i.type         = record.value("type", std::string{});
    i.traits       = traitsOf(i.type);
    i.traits.landmarkTag = inLandmarkTag(key);
    i.terrainOverride    = terrainOverrideOf(i.type);
    i.biomes       = record.value("biomes", std::string{});
    i.step         = record.value("step", std::string{"surface_structures"});
    i.centerInChunk = record.value("center_in_chunk", true);
    i.adjustment   = core::landmark::terrainAdjustmentOf(record.value("terrain_adaptation", std::string{"none"}));
    i.d.icon       = record.value("structure_icon", std::string{});
    if (auto it = record.find("advancements_required"); it != record.end() && it->is_array())
        for (auto const& a : *it)
            if (a.is_string()) i.d.lockAdvancements.push_back(a.get<std::string>());

    // DecorationClearance (LandmarkStructure's defaults when absent; CampStructure and
    // FallenTrunkStructure hard-code theirs in Java, which the descriptor passes as fixedClearance)
    bool useFixed  = !record.contains("decoration_clearance") && fixedClearance && fixedClearance->is_object();
    auto clearance = record.contains("decoration_clearance") ? record["decoration_clearance"] : useFixed ? *fixedClearance : Json();
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

    // Descriptor::extra: everything of the TF record the descriptor fields do not carry, verbatim
    // (type-specific codec fields: hill_size, speleothem_config, the fallen trunk and hollow tree
    // parameters, decoration_clearance, hint_creature, biomes, step, terrain_adaptation, ...), plus
    // what GloamingForest derives from the record under the separate key "gf" (a Java-side fixed
    // clearance included: it is no field of the TF record)
    i.d.extra = record.is_object() ? record : Json::object();
    for (auto const* parsed : kDescriptorFields) i.d.extra.erase(parsed);
    i.d.extra.erase("gf_runtime");
    i.d.extra[kDerivedKey] = Json{
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
    if (useFixed) i.d.extra[kDerivedKey]["fixed_decoration_clearance"] = *fixedClearance;
    return i;
}

void loadDescriptors() {
    // once per process: a second install (the mod disabled and enabled again) keeps the loaded
    // descriptors, so the pointers info() and descriptor() handed out stay valid
    if (store().loaded.load(std::memory_order_acquire)) return;
    std::lock_guard load(store().loadMutex);
    if (!store().loaded.load(std::memory_order_acquire)) loadDescriptorsLocked();
}

namespace {

void loadDescriptorsLocked() {
    struct Loading {
        Loading() { tLoading = true; }
        ~Loading() { tLoading = false; }
    } loading;
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
                auto id = "twilightforest:" + e.path().stem().string();
                guarded(e.path(), [&] { sets[id] = parseSet(id, *j); });
            }
        }
    }
    auto tfStructures = data / "twilightforest" / "worldgen" / "structure";
    if (std::filesystem::is_directory(tfStructures, ec)) {
        for (auto const& e : std::filesystem::directory_iterator(tfStructures, ec)) {
            if (e.path().extension() != ".json") continue;
            if (auto j = readJson(e.path())) {
                auto key = "twilightforest:" + e.path().stem().string();
                guarded(e.path(), [&] { infos[key] = parseRecord(key, *j); });
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
            guarded(e.path(), [&] {
                auto key      = normalizeKey(j->value("key", e.path().parent_path().filename().string() + ":" + e.path().stem().string()));
                auto imported = infos.find(key);
                auto record   = mergeRecord(imported != infos.end() ? &imported->second.raw : nullptr, j->value("structure", Json::object()));
                auto fixed    = j->find("fixed_decoration_clearance");
                auto parsed   = parseRecord(key, record, fixed != j->end() ? &*fixed : nullptr);
                std::optional<StructureSet> set;
                if (auto s = j->find("structure_set"); s != j->end() && s->is_object()) set = parseSet(s->value("id", key), *s);
                infos[key] = std::move(parsed);
                if (set) sets[set->id] = std::move(*set);
                ++n;
            });
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

    // the decoration slots (ChunkGenerator#applyBiomeDecoration, assignSlot), then into the store.
    // Other threads wait in ensureLoaded until `loaded` is set
    std::map<int, int> unregistered; // step -> unregistered descriptors given a slot past its end
    {
        std::shared_lock lock(store().mutex);
        for (auto const& [key, i] : store().infos)
            if (!i.inRegistry) ++unregistered[i.slot.step];
    }
    for (auto& [key, i] : infos) {
        assignSlot(i, unregistered[stepOf(i)]);
        if (!i.inRegistry) ++unregistered[i.slot.step];
    }
    {
        std::unique_lock lock(store().mutex);
        // add only: entries already there (run-time registrations) keep their address
        for (auto& [key, i] : infos) {
            auto [it, added] = store().infos.try_emplace(key, std::move(i));
            if (added) store().stepMask.fetch_or(1u << it->second.slot.step, std::memory_order_relaxed);
        }
        for (auto& [id, s] : sets) store().sets.try_emplace(id, std::move(s));
    }
    store().loaded.store(true, std::memory_order_release);
    log().info("landmark: {} descriptors, {} structure sets loaded", descriptorKeys().size(), structureSets().size());
    (void)n;
}

} // namespace

Info const* info(std::string const& key) {
    ensureLoaded();
    std::shared_lock lock(store().mutex);
    auto             it = store().infos.find(normalizeKey(key));
    return it == store().infos.end() ? nullptr : &it->second;
}

std::vector<std::string> descriptorKeys() {
    ensureLoaded();
    std::shared_lock         lock(store().mutex);
    std::vector<std::string> out;
    for (auto const& [k, v] : store().infos) out.push_back(k);
    return out;
}

StructureSet const* structureSet(std::string const& id) {
    ensureLoaded();
    std::shared_lock lock(store().mutex);
    auto             it = store().sets.find(id);
    return it == store().sets.end() ? nullptr : &it->second;
}

std::vector<StructureSet const*> structureSets() {
    ensureLoaded();
    std::shared_lock                 lock(store().mutex);
    std::vector<StructureSet const*> out;
    for (auto const& [k, v] : store().sets) out.push_back(&v);
    return out;
}

void registerInfo(Info i) {
    ensureLoaded(); // the data's descriptors first: a fixture never takes a TF key's place
    i.raw["gf_runtime"] = true;
    assignSlot(i, 0); // the worldgen's registry first (its own lock), outside the store lock
    std::unique_lock lock(store().mutex);
    // past the end of the step: after the unregistered descriptors already there, counted under the
    // same lock as the insertion so two registrations never share an index
    if (!i.inRegistry)
        for (auto const& [k, o] : store().infos)
            if (!o.inRegistry && o.slot.step == i.slot.step) ++i.slot.index;
    auto key = i.d.key;
    // never overwrite or erase: info() hands out pointers into the map
    auto [it, added] = store().infos.try_emplace(key, std::move(i));
    if (added) store().stepMask.fetch_or(1u << it->second.slot.step, std::memory_order_relaxed);
}

std::uint32_t decorationStepMask() {
    ensureLoaded();
    return store().stepMask.load(std::memory_order_relaxed);
}

} // namespace gf::feature::landmark

namespace gf::landmark {

Descriptor const* descriptor(std::string const& key) {
    auto const* i = feature::landmark::info(key);
    return i ? &i->d : nullptr;
}

} // namespace gf::landmark
