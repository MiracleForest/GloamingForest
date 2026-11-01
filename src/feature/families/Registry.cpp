// Block families (F11): the family data of every unit, read from <mod>/data/gf/families/**.json
// (assets/data/families/<u>.json and the import's generated family data) and, in development
// builds, <mod>/data/gf_test/families/*.json (the fixture families tools/families writes).
//
// Entries are {"id", "family", "params"}; one id may list one shape family and any modifiers
// (waterloggable, snowloggable, climbable, lightable). Lookups by block run on every block event,
// so the entries are also indexed by the game's block type once the block registry exists.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/families/Families.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::families {

namespace fs = std::filesystem;

namespace {

struct Names {
    char const* name;
    Family      family;
};
constexpr Names kFamilies[]{
    {"cube", Family::Cube},
    {"stairs", Family::Stairs},
    {"slab", Family::Slab},
    {"fence", Family::Fence},
    {"fence_gate", Family::FenceGate},
    {"wall", Family::Wall},
    {"door", Family::Door},
    {"trapdoor", Family::Trapdoor},
    {"button", Family::Button},
    {"pressure_plate", Family::PressurePlate},
    {"pane", Family::Pane},
    {"pillar", Family::Pillar},
    {"log", Family::Log},
    {"wood", Family::Wood},
    {"leaves", Family::Leaves},
    {"cross", Family::Cross},
    {"sapling", Family::Sapling},
    {"potted", Family::Potted},
    {"sign", Family::Sign},
    {"wall_sign", Family::WallSign},
    {"hanging_sign", Family::HangingSign},
    {"wall_hanging_sign", Family::WallHangingSign},
    {"chest", Family::Chest},
    {"trapped_chest", Family::TrappedChest},
    {"horizontal", Family::Horizontal},
    {"directional", Family::Directional},
    {"binary_rotated", Family::BinaryRotated},
    {"wall_pillar", Family::WallPillar},
    {"connectable_pillar", Family::ConnectablePillar},
    {"connected_texture", Family::ConnectedTexture},
};

std::optional<Modifier> modifierOf(std::string_view name) {
    if (name == "waterloggable") return Waterloggable;
    if (name == "snowloggable") return Snowloggable;
    if (name == "climbable") return Climbable;
    if (name == "lightable") return Lightable;
    return std::nullopt;
}

struct Registry {
    std::map<std::string, Entry, std::less<>>         byId;
    std::unordered_map<BlockType const*, Entry const*> byType;
    bool                                               typesResolved = false;
};
Registry& registry() {
    static Registry r;
    return r;
}

void readFile(fs::path const& file, std::map<std::string, Entry, std::less<>>& out, std::map<std::string, bool>& shaped) {
    std::ifstream in(file);
    if (!in) return;
    auto doc = Json::parse(in, nullptr, false, true);
    if (doc.is_discarded()) {
        log().warn("families: {} is not valid JSON", file.string());
        return;
    }
    Json const& list = doc.is_object() && doc.contains("entries") ? doc["entries"] : doc;
    if (!list.is_array()) return;
    for (auto const& e : list) {
        if (!e.is_object() || !e.contains("id") || !e.contains("family") || !e["id"].is_string() || !e["family"].is_string()) continue;
        auto  id     = e["id"].get<std::string>();
        auto  name   = e["family"].get<std::string>();
        auto& entry  = out[id];
        entry.id     = id;
        if (auto f = familyOf(name)) {
            if (shaped[id] && entry.family != *f) log().warn("families: {} has two shape families ({} and {})", id, familyName(entry.family), name);
            entry.family = *f;
            shaped[id]   = true;
        } else if (auto m = modifierOf(name)) {
            entry.modifiers |= *m;
        } else {
            log().warn("families: {} has the unknown family '{}' ({})", id, name, file.filename().string());
            continue;
        }
        if (e.contains("params") && e["params"].is_object()) {
            for (auto const& [k, v] : e["params"].items()) entry.params[k] = v;
        }
        if (entry.params.value("waterlogged", false) || entry.params.value("waterloggable", false)) entry.modifiers |= Waterloggable;
    }
}

void resolveTypes() {
    auto& r = registry();
    r.byType.clear();
    int found = 0;
    for (auto const& [id, entry] : r.byId) {
        if (auto type = BlockType::tryGetFromRegistry(HashedString(id))) {
            r.byType[type.as_ptr()] = &entry;
            ++found;
        }
    }
    r.typesResolved = true;
    log().info("families: {} family blocks ({} registered in the game)", r.byId.size(), found);
}

} // namespace

std::optional<Family> familyOf(std::string_view name) {
    for (auto const& f : kFamilies)
        if (name == f.name) return f.family;
    return std::nullopt;
}

char const* familyName(Family family) {
    for (auto const& f : kFamilies)
        if (f.family == family) return f.name;
    return "cube";
}

std::string Entry::str(char const* key, std::string fallback) const {
    auto it = params.find(key);
    return it != params.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
}

int Entry::integer(char const* key, int fallback) const {
    auto it = params.find(key);
    return it != params.end() && it->is_number() ? it->get<int>() : fallback;
}

bool Entry::flag(char const* key, bool fallback) const {
    auto it = params.find(key);
    return it != params.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void loadRegistry() {
    auto& r = registry();
    std::map<std::string, Entry, std::less<>> loaded;
    std::map<std::string, bool>               shaped;
    auto                                      dataDir = Mod::getInstance().getSelf().getModDir() / "data";
    std::vector<fs::path>                     files;
    std::error_code                           ec;
    for (auto const& dir : {dataDir / "gf" / "families", dataDir / "gf_test" / "families"}) {
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (auto const& f : files) readFile(f, loaded, shaped);
    r.byId          = std::move(loaded);
    r.typesResolved = false; // resolved on the first lookup (the block registry may not exist yet)
    r.byType.clear();
}

Entry const* entryOf(std::string_view id) {
    auto& r  = registry();
    auto  it = r.byId.find(id);
    return it == r.byId.end() ? nullptr : &it->second;
}

Entry const* entryOf(Block const& block) {
    auto& r = registry();
    if (r.byId.empty()) return nullptr;
    if (!r.typesResolved) resolveTypes();
    auto it = r.byType.find(&block.getBlockType());
    return it == r.byType.end() ? nullptr : it->second;
}

namespace detail {
/// The game's block types changed (a new world; the client rebuilds them per world).
void refreshTypes() {
    registry().typesResolved = false;
    registry().byType.clear();
}
} // namespace detail

std::vector<Entry const*> entries() {
    std::vector<Entry const*> out;
    for (auto const& [id, e] : registry().byId) out.push_back(&e);
    return out;
}

std::vector<Entry const*> entriesOf(Family family) {
    std::vector<Entry const*> out;
    for (auto const& [id, e] : registry().byId)
        if (e.family == family) out.push_back(&e);
    return out;
}

} // namespace gf::feature::families
