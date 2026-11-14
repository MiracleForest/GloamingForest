// The data map part of contract "data" (F9): NeoForge data maps (upstream's crumble_horn, ore_map_color,
// magic_map_color, transformation_powder, ominous_fire and NeoForge's compostables, furnace_fuels,
// parrot_imitations), loaded from data/<ns>/data_maps/<registry>/<name>.json as NeoForge reads
// them: {"replace": bool, "values": {"<id>": value, "#<tag>": value}, "remove": [...]}; a map id is
// "<ns>:<name>". Direct entries win over tag entries; tag entries are matched through gf::tags in
// the map's registry. The data's vanilla ids are Java's: the keys (by the map's registry) and the
// value fields known to hold ids (ext/item/DataMaps.h: crumble_to, transform_to, ...) become the
// game's Bedrock ids at load (tags/JavaIds.h); `get` takes Bedrock ids. A reload swaps in a new
// snapshot of every map and keeps the old one, so the values `get` returned stay valid while other
// threads (the client mod) read. The composter and furnace apply compostables / furnace_fuels
// (VanillaMaps.cpp).
#include "feature/datamaps/DataMaps.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

#include "feature/datamaps/DataMapsInternal.h"
#include "feature/ext/item/DataMaps.h"
#include "feature/item/DataFiles.h"
#include "feature/tags/JavaIds.h"
#include "feature/tags/Tags.h"
#include "mod/Log.h"

namespace gf::datamaps {

namespace {

namespace data = feature::item::data;

/// The value fields holding ids, by map: field -> registry directory.
using IdFields = std::map<std::string, std::map<std::string, std::string>>;

struct FieldStore {
    std::mutex mutex;
    IdFields   fields{
        {"gloamingforest:crumble_horn", {{"crumble_to", "block"}}},           // CrumbledBlock
        {"gloamingforest:transformation_powder", {{"transform_to", "entity_type"}}}, // EntityTransformation
        {"gloamingforest:ominous_fire", {{"transform_to", "entity_type"}}},    // EntityTransformation
        {"neoforge:oxidizables", {{"next_oxidation_stage", "block"}}},       // NeoForge's Oxidizable
        {"neoforge:waxables", {{"waxed", "block"}}},                         // NeoForge's Waxable
    };
    bool loaded = false; ///< the maps were loaded once (a registration loads them again)
};

FieldStore& fieldStore() {
    static FieldStore s;
    return s;
}

struct Map {
    std::string                                     registry; ///< "block", "item", "entity_type", "worldgen/biome"
    std::unordered_map<std::string, nlohmann::json> values;   ///< by id
    std::vector<std::pair<std::string, nlohmann::json>> tagged; ///< ("ns:tag", value) in file order
};

using Maps = std::map<std::string, Map>;

struct Store {
    std::mutex                              mutex;
    std::shared_ptr<Maps const>             maps = std::make_shared<Maps const>();
    /// Earlier generations: a reload swaps in a new snapshot, and the values `get` handed out (the
    /// client mod queries from its own thread) stay valid for the process.
    std::vector<std::shared_ptr<Maps const>> retired;
};

Store& store() {
    static Store s;
    return s;
}

std::optional<tags::Registry> registryOf(std::string const& dir) {
    using R = tags::Registry;
    static std::map<std::string, R> const m{
        {"block", R::Block},       {"item", R::Item},     {"entity_type", R::EntityType}, {"worldgen/biome", R::Biome},
        {"fluid", R::Fluid},       {"damage_type", R::DamageType}, {"enchantment", R::Enchantment},
        {"worldgen/structure", R::Structure}, {"game_event", R::GameEvent},
    };
    if (auto it = m.find(dir); it != m.end()) return it->second;
    return std::nullopt;
}

void load() {
    IdFields idFields;
    {
        auto&           fs = fieldStore();
        std::lock_guard lock(fs.mutex);
        idFields  = fs.fields;
        fs.loaded = true;
    }
    auto maps = std::make_shared<Maps>();
    for (auto const& f : data::list("data_maps")) {
        // rel = "<registry>/<name>" (registry may be "worldgen/biome")
        std::string registry, name;
        if (f.rel.starts_with("worldgen/")) {
            auto second = f.rel.find('/', 9);
            if (second == std::string::npos) continue;
            registry = f.rel.substr(0, second);
            name     = f.rel.substr(second + 1);
        } else {
            auto first = f.rel.find('/');
            if (first == std::string::npos) continue;
            registry = f.rel.substr(0, first);
            name     = f.rel.substr(first + 1);
        }
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        auto  mapId  = f.ns + ":" + name;
        auto& map    = (*maps)[mapId];
        map.registry = registry;
        if (doc.value("replace", false)) {
            map.values.clear();
            map.tagged.clear();
        }
        // the data's Java ids as the game's Bedrock ids (JavaIds.h): the keys by the map's registry,
        // and the value fields known to hold ids by theirs (only those: a sound id stays a sound id)
        auto const* fields = [&]() -> std::map<std::string, std::string> const* {
            auto it = idFields.find(mapId);
            return it == idFields.end() ? nullptr : &it->second;
        }();
        auto add = [&](std::string key, nlohmann::json value) {
            if (fields && value.is_object())
                for (auto const& [field, dir] : *fields)
                    if (auto v = value.find(field); v != value.end() && v->is_string())
                        *v = tags::javaids::primary(dir, data::withNamespace(v->get<std::string>()));
            if (key.starts_with('#')) {
                auto tag = data::withNamespace(key.substr(1));
                std::erase_if(map.tagged, [&](auto const& e) { return e.first == tag; });
                map.tagged.emplace_back(tag, std::move(value));
            } else {
                for (auto const& id : tags::javaids::bedrock(registry, data::withNamespace(key))) map.values[id] = value;
            }
        };
        if (auto it = doc.find("values"); it != doc.end()) {
            if (it->is_object()) {
                for (auto& [k, v] : it->items()) add(k, v);
            } else if (it->is_array()) {
                for (auto const& e : *it)
                    if (e.is_object() && e.contains("id") && e["id"].is_string()) add(e["id"].get<std::string>(), e.value("value", nlohmann::json()));
            }
        }
        if (auto it = doc.find("remove"); it != doc.end() && it->is_array()) {
            for (auto const& r : *it) {
                if (!r.is_string()) continue;
                auto key = r.get<std::string>();
                if (key.starts_with('#')) std::erase_if(map.tagged, [&](auto const& e) { return e.first == data::withNamespace(key.substr(1)); });
                else
                    for (auto const& id : tags::javaids::bedrock(registry, data::withNamespace(key))) map.values.erase(id);
            }
        }
    }
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.retired.push_back(s.maps);
    s.maps = std::move(maps);
    log().info("data maps: {} loaded", s.maps->size());
}

[[maybe_unused]] bool const kRegistered = (data::onReload(&load), true);

/// The current snapshot (a reload swaps it; the old one stays alive in `retired`).
std::shared_ptr<Maps const> snapshot() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.maps;
}

} // namespace

nlohmann::json const* get(std::string const& mapId, std::string const& key) {
    auto maps = snapshot();
    auto it   = maps->find(data::withNamespace(mapId));
    if (it == maps->end()) return nullptr;
    auto const& map = it->second;
    if (auto v = map.values.find(data::withNamespace(key)); v != map.values.end()) return &v->second;
    // tag entries through gf::tags in the map's registry
    auto registry = registryOf(map.registry);
    if (!registry) return nullptr;
    for (auto const& [tag, value] : map.tagged)
        if (tags::has(*registry, tag, key)) return &value;
    return nullptr;
}

std::vector<std::string> keys(std::string const& mapId) {
    auto maps = snapshot();
    auto it   = maps->find(data::withNamespace(mapId));
    if (it == maps->end()) return {};
    auto const*           map = &it->second;
    std::set<std::string> out;
    for (auto const& [id, v] : map->values) out.insert(id);
    if (auto registry = registryOf(map->registry))
        for (auto const& [tag, v] : map->tagged)
            for (auto& id : tags::members(*registry, tag)) out.insert(std::move(id));
    return {out.begin(), out.end()};
}

namespace ext {
void registerIdField(std::string const& mapId, std::string const& field, std::string const& registryDir) {
    bool reload = false;
    {
        auto&           fs = fieldStore();
        std::lock_guard lock(fs.mutex);
        auto&           slot = fs.fields[data::withNamespace(mapId)][field];
        if (slot == registryDir) return;
        slot   = registryDir;
        reload = fs.loaded;
    }
    if (reload) load(); // the values loaded before are renamed again
}
} // namespace ext

namespace internal {
std::vector<std::pair<std::string, std::string>> maps() {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto const& [id, map] : *snapshot()) out.emplace_back(id, map.registry);
    return out;
}

std::vector<std::string> directKeys(std::string const& mapId) {
    auto                     snap = snapshot();
    std::vector<std::string> out;
    if (auto it = snap->find(data::withNamespace(mapId)); it != snap->end())
        for (auto const& [id, v] : it->second.values) out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::map<std::string, std::string> idFields(std::string const& mapId) {
    auto&           fs = fieldStore();
    std::lock_guard lock(fs.mutex);
    auto            it = fs.fields.find(data::withNamespace(mapId));
    return it == fs.fields.end() ? std::map<std::string, std::string>{} : it->second;
}

void unregisterIdField(std::string const& mapId, std::string const& field) {
    {
        auto&           fs = fieldStore();
        std::lock_guard lock(fs.mutex);
        auto            it = fs.fields.find(data::withNamespace(mapId));
        if (it == fs.fields.end() || it->second.erase(field) == 0) return;
        if (it->second.empty()) fs.fields.erase(it);
    }
    load();
}
} // namespace internal

} // namespace gf::datamaps
