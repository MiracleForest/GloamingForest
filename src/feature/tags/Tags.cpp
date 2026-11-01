// The tag part of contract "data" (F9): TF's tags and the vanilla / c: tags they add to, loaded from
// the mod's data tree (data/<ns>/tags/<registry>/<path>.json, imported by F1 and unioned with the
// units' fragments by assemble, rule R6), nested tags resolved, the vanilla members of vanilla tags
// answered by the bridge (VanillaTags.h).
#include "feature/tags/Tags.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "feature/item/DataFiles.h"
#include "feature/tags/TagsInternal.h"
#include "feature/tags/VanillaTags.h"
#include "mod/Log.h"

namespace gf::tags {

namespace {

namespace data = feature::item::data;

struct RawTag {
    std::vector<std::string> values; ///< ids and "#tags", in file order
    std::vector<std::string> optional; ///< entries with "required": false
};

struct Resolved {
    std::unordered_set<std::string> set;     ///< data members (nested tags flattened)
    std::vector<std::string>        sorted;  ///< the same, sorted
    std::vector<std::string>        bridged; ///< vanilla tags reached (answered by the bridge)
};

struct Store {
    std::mutex                                                                 mutex;
    std::unordered_map<std::string, std::unordered_map<std::string, RawTag>>   raw;      ///< registry dir -> tag -> entries
    std::unordered_map<std::string, std::shared_ptr<Resolved const>>           resolved; ///< "dir|tag"
    std::unordered_map<std::string, std::shared_ptr<std::vector<std::string>>> members;  ///< "dir|tag", with the bridge
    std::size_t                                                                files = 0;
};

Store& store() {
    static Store s;
    return s;
}

/// Java 1.20 and older spelled some tag directories in the plural.
std::string normalizeDir(std::string dir) {
    static std::map<std::string, std::string> const plural{
        {"blocks", "block"}, {"items", "item"}, {"entity_types", "entity_type"}, {"fluids", "fluid"}, {"game_events", "game_event"},
    };
    if (auto it = plural.find(dir); it != plural.end()) return it->second;
    return dir;
}

/// "worldgen/biome/x/y" -> ("worldgen/biome", "x/y"); "block/x" -> ("block", "x").
std::pair<std::string, std::string> splitRegistry(std::string const& rel) {
    auto first = rel.find('/');
    if (first == std::string::npos) return {};
    auto head = rel.substr(0, first);
    if (head == "worldgen") {
        auto second = rel.find('/', first + 1);
        if (second == std::string::npos) return {};
        return {rel.substr(0, second), rel.substr(second + 1)};
    }
    return {normalizeDir(head), rel.substr(first + 1)};
}

std::string stripHash(std::string const& tag) {
    std::string t = tag.starts_with('#') ? tag.substr(1) : tag;
    return data::withNamespace(t);
}

void load() {
    auto& s = store();
    std::lock_guard lock(s.mutex);
    s.raw.clear();
    s.resolved.clear();
    s.members.clear();
    s.files = 0;
    for (auto const& f : data::list("tags")) {
        auto [dir, path] = splitRegistry(f.rel);
        if (dir.empty() || path.empty()) continue;
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        auto& tag = s.raw[dir][f.ns + ":" + path];
        if (doc.value("replace", false)) tag = {};
        if (auto it = doc.find("values"); it != doc.end() && it->is_array()) {
            for (auto const& v : *it) {
                if (v.is_string()) tag.values.push_back(v.get<std::string>());
                else if (v.is_object() && v.contains("id") && v["id"].is_string()) {
                    auto id = v["id"].get<std::string>();
                    tag.values.push_back(id);
                    if (!v.value("required", true)) tag.optional.push_back(id);
                }
            }
        }
        ++s.files;
    }
    log().info("tags: {} tag files loaded", s.files);
}

[[maybe_unused]] bool const kRegistered = (data::onReload(&load), true);

/// Resolves a tag (lock held). `stack` guards against cycles.
std::shared_ptr<Resolved const> resolveLocked(Store& s, Registry registry, std::string const& dir, std::string const& tag, std::vector<std::string>& stack) {
    auto key = dir + "|" + tag;
    if (auto it = s.resolved.find(key); it != s.resolved.end()) return it->second;
    auto out = std::make_shared<Resolved>();
    if (std::find(stack.begin(), stack.end(), tag) != stack.end()) {
        log().warn("tags: {} {} includes itself", dir, tag);
        return out;
    }
    stack.push_back(tag);
    if (vanilla::known(registry, tag)) out->bridged.push_back(tag);
    if (auto d = s.raw.find(dir); d != s.raw.end()) {
        if (auto t = d->second.find(tag); t != d->second.end()) {
            for (auto const& v : t->second.values) {
                if (v.starts_with('#')) {
                    auto sub = resolveLocked(s, registry, dir, stripHash(v), stack);
                    out->set.insert(sub->set.begin(), sub->set.end());
                    for (auto const& b : sub->bridged)
                        if (std::find(out->bridged.begin(), out->bridged.end(), b) == out->bridged.end()) out->bridged.push_back(b);
                } else {
                    out->set.insert(data::withNamespace(v));
                }
            }
        }
    }
    stack.pop_back();
    out->sorted.assign(out->set.begin(), out->set.end());
    std::sort(out->sorted.begin(), out->sorted.end());
    s.resolved[key] = out;
    return out;
}

std::shared_ptr<Resolved const> resolve(Registry registry, std::string const& dir, std::string const& tag) {
    auto&                    s = store();
    std::lock_guard          lock(s.mutex);
    std::vector<std::string> stack;
    return resolveLocked(s, registry, dir, tag, stack);
}

} // namespace

bool has(Registry registry, std::string const& tag, std::string const& id) {
    if (tag.empty() || id.empty()) return false;
    auto t = stripHash(tag);
    auto r = resolve(registry, registryName(registry), t);
    auto i = data::withNamespace(id);
    if (r->set.contains(i)) return true;
    for (auto const& b : r->bridged)
        if (vanilla::has(registry, b, i)) return true;
    return false;
}

std::vector<std::string> members(Registry registry, std::string const& tag) {
    auto t   = stripHash(tag);
    auto dir = std::string(registryName(registry));
    auto key = dir + "|" + t;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (auto it = s.members.find(key); it != s.members.end()) return *it->second;
    }
    auto                  r = resolve(registry, dir, t);
    std::set<std::string> all(r->sorted.begin(), r->sorted.end());
    for (auto const& b : r->bridged)
        for (auto& id : vanilla::members(registry, b)) all.insert(std::move(id));
    auto list = std::make_shared<std::vector<std::string>>(all.begin(), all.end());
    // the bridge enumerates the game's registries: cache only once a level exists
    if (!r->bridged.empty() && list->size() == r->sorted.size()) return *list;
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.members[key] = list;
    return *list;
}

char const* registryName(Registry registry) {
    switch (registry) {
    case Registry::Block:
        return "block";
    case Registry::Item:
        return "item";
    case Registry::EntityType:
        return "entity_type";
    case Registry::Biome:
        return "worldgen/biome";
    case Registry::Fluid:
        return "fluid";
    case Registry::DamageType:
        return "damage_type";
    case Registry::Enchantment:
        return "enchantment";
    case Registry::Structure:
        return "worldgen/structure";
    case Registry::BannerPattern:
        return "banner_pattern";
    case Registry::PaintingVariant:
        return "painting_variant";
    case Registry::GameEvent:
        return "game_event";
    case Registry::Instrument:
        return "instrument";
    }
    return "";
}

namespace internal {

std::vector<std::string> registryDirs() {
    auto&                    s = store();
    std::lock_guard          lock(s.mutex);
    std::vector<std::string> out;
    for (auto const& [dir, tags] : s.raw) out.push_back(dir);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> tagsOf(std::string const& dir) {
    auto&                    s = store();
    std::lock_guard          lock(s.mutex);
    std::vector<std::string> out;
    if (auto it = s.raw.find(dir); it != s.raw.end())
        for (auto const& [tag, raw] : it->second) out.push_back(tag);
    std::sort(out.begin(), out.end());
    return out;
}

bool hasIn(std::string const& dir, std::string const& tag, std::string const& id) {
    auto r = resolve(Registry::Instrument, dir, stripHash(tag)); // no bridge outside the enum's registries
    return r->set.contains(data::withNamespace(id));
}

std::vector<std::string> membersIn(std::string const& dir, std::string const& tag) {
    return resolve(Registry::Instrument, dir, stripHash(tag))->sorted;
}

std::size_t fileCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.files;
}

void reload() { load(); }

} // namespace internal

} // namespace gf::tags
