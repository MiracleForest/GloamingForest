// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/events/RegistrationEvents.java
//   src/main/java/gloamingforest/world/components/feature/trees/growers/GloamingTreeGrowers.java
//   src/main/java/gloamingforest/world/components/feature/trees/growers/StructureTreeGrower.java
//   src/main/java/gloamingforest/init/GloamingBlocks.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11): the family data of every unit, read from <mod>/data/gf/families/**.json
// (assets/data/families/<u>.json and F1's imported data) and, in development builds,
// <mod>/data/gf_test/families/*.json (the fixture families tools/families writes).
//
// Entries are {"id", "family", "params"} (tools/families/README.md); one id may list one shape
// family and any modifiers (waterloggable, snowloggable, climbable, lightable). F1's entries name
// the Java class (params.class), which refines the family, and upstream's registrations fill the params
// units leave out: the wall variant of a sign, the plant of a potted plant, the strippables
// (RegistrationEvents STRIPPABLES / FlowerPotBlock.addPlant) and the tree growers (GloamingTreeGrowers).
//
// The server thread (block events, world ready) and the client's threads (collision of the local
// player, chunk tessellation, client join) read the registry at the same time. The entries are
// read once at mod load and stay (the Entry pointers handed out never dangle); only the table of
// the game's block types changes per world, published atomically, and an old table is freed once
// no thread reads it (each thread keeps the table it read until a newer one is published).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
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

/// F1's form: the modifiers of an entry listed in params.traits ({"family": "slab", "params":
/// {"traits": ["waterloggable"]}}). connected_texture there only repeats the family.
unsigned traitModifiers(Json const& params) {
    unsigned out = 0;
    auto     it  = params.find("traits");
    if (it == params.end() || !it->is_array()) return out;
    for (auto const& t : *it) {
        if (!t.is_string()) continue;
        if (auto m = modifierOf(t.get<std::string>())) out |= *m;
    }
    return out;
}

// ---- upstream's registrations ----------------------------------------------------------------------------

/// RegistrationEvents: AxeItem.STRIPPABLES.put(log, stripped log) for upstream's eight woods.
std::map<std::string, std::string, std::less<>> const& strippables() {
    static std::map<std::string, std::string, std::less<>> const m = [] {
        std::map<std::string, std::string, std::less<>> out;
        for (char const* wood : {"gloaming_oak", "canopy", "mangrove", "dark", "time", "transformation", "mining", "sorting"}) {
            for (char const* kind : {"log", "wood"}) {
                std::string id = std::string(wood) + "_" + kind;
                out["gloamingforest:" + id] = "gloamingforest:stripped_" + id;
            }
        }
        return out;
    }();
    return m;
}

/// RegistrationEvents: FlowerPotBlock.addPlant(plant, potted) where the names differ.
std::map<std::string, std::string, std::less<>> const& pottedPlants() {
    static std::map<std::string, std::string, std::less<>> const m{
        {"gloamingforest:potted_thorn", "gloamingforest:brown_thorns"},
        {"gloamingforest:potted_green_thorn", "gloamingforest:green_thorns"},
        {"gloamingforest:potted_dead_thorn", "gloamingforest:burnt_thorns"},
    };
    return m;
}

/// GloamingTreeGrowers (Java TreeGrower: secondaryChance, megaTree, secondaryMegaTree, tree,
/// secondaryTree) and StructureTreeGrower (the hollow tree structure), by sapling (GloamingBlocks).
Json growerOf(std::string_view saplingId) {
    auto f = [](char const* path) { return std::string("gloamingforest:tree/") + path; };
    if (saplingId == "gloamingforest:gloaming_oak_sapling")
        return {{"secondary_chance", 0.1}, {"mega_feature", f("forest_mega_oak_tree")}, {"secondary_mega_feature", f("savannah_mega_oak_tree")},
                {"feature", f("gloaming_oak_tree")}, {"secondary_feature", f("large_gloaming_oak_tree")}};
    if (saplingId == "gloamingforest:canopy_sapling") return {{"mega_feature", f("mega_canopy_tree")}, {"feature", f("canopy_tree")}};
    if (saplingId == "gloamingforest:mangrove_sapling") return {{"feature", f("mangrove_tree")}};
    if (saplingId == "gloamingforest:darkwood_sapling") return {{"feature", f("homegrown_darkwood_tree")}};
    if (saplingId == "gloamingforest:time_sapling") return {{"feature", f("time_tree")}};
    if (saplingId == "gloamingforest:transformation_sapling") return {{"feature", f("transformation_tree")}};
    if (saplingId == "gloamingforest:mining_sapling") return {{"feature", f("mining_tree")}};
    if (saplingId == "gloamingforest:sorting_sapling") return {{"feature", f("sorting_tree")}};
    if (saplingId == "gloamingforest:hollow_oak_sapling") return {{"structure", "gloamingforest:hollow_tree"}};
    if (saplingId == "gloamingforest:rainbow_oak_sapling")
        return {{"secondary_chance", 0.1}, {"feature", f("rainbow_oak")}, {"secondary_feature", f("large_rainbow_oak")}};
    return nullptr;
}

/// `id` with its last `from` replaced by `to` ("x_sign" -> "x_wall_sign"), "" when it has none.
std::string swapSuffix(std::string const& id, std::string_view from, std::string_view to) {
    if (!id.ends_with(from)) return {};
    return id.substr(0, id.size() - from.size()) + std::string(to);
}

// ---- the entries and the block type table ------------------------------------------------------------

/// The family entries by id: read once at mod load (a second load only if the first found none),
/// so the few sets ever loaded stay alive and the Entry pointers handed out never dangle.
struct Entries {
    std::map<std::string, Entry, std::less<>> byId;
};

std::atomic<Entries const*>& currentEntries() {
    static std::atomic<Entries const*> c{nullptr};
    return c;
}
std::mutex& entriesMutex() {
    static std::mutex m;
    return m;
}
std::vector<std::unique_ptr<Entries>>& loadedEntries() {
    static std::vector<std::unique_ptr<Entries>> k;
    return k;
}

Entries const& entriesNow() {
    static Entries const empty;
    auto const* s = currentEntries().load(std::memory_order_acquire);
    return s ? *s : empty;
}

/// The game's block types of the entries, per world (the client builds its block types again for
/// every world it joins). Readers on any thread (the collision hook, chunk tessellation) keep the
/// table they read in a per-thread cache, refreshed when a new one is published: an old table goes
/// once every thread that read it has moved on, so a client joining many worlds holds at most one
/// stale table per thread.
struct Types {
    std::unordered_map<BlockType const*, Entry const*> byType;
    std::vector<BlockType const*>                      tall; ///< sorted (tallCollisionType)
    BlockType const*                                   tallMin = nullptr;
    BlockType const*                                   tallMax = nullptr;
};

struct TypesSlot {
    std::mutex                   mutex;
    std::shared_ptr<Types const> current;
    std::atomic<std::uint64_t>   generation{0};
};
TypesSlot& typesSlot() {
    static TypesSlot s;
    return s;
}

Types const& types() {
    static Types const                       empty;
    thread_local std::shared_ptr<Types const> cached;
    thread_local std::uint64_t                cachedGeneration = ~std::uint64_t{0};
    auto&                                     slot             = typesSlot();
    auto                                      generation       = slot.generation.load(std::memory_order_acquire);
    if (generation != cachedGeneration) {
        std::lock_guard lock(slot.mutex);
        cached           = slot.current;
        cachedGeneration = slot.generation.load(std::memory_order_relaxed);
    }
    return cached ? *cached : empty;
}

void publishTypes(std::shared_ptr<Types const> t) {
    auto&           slot = typesSlot();
    std::lock_guard lock(slot.mutex);
    slot.current = std::move(t);
    slot.generation.fetch_add(1, std::memory_order_release);
}

/// Looks the block types of `entries` up (the game's block registry exists once a world loads).
std::shared_ptr<Types const> resolveTypes(Entries const& entries) {
    auto t = std::make_shared<Types>();
    for (auto const& [id, entry] : entries.byId) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (!type) continue;
        t->byType[type.as_ptr()] = &entry;
        if (entry.family == Family::Fence || entry.family == Family::Wall || entry.family == Family::FenceGate) t->tall.push_back(type.as_ptr());
    }
    std::sort(t->tall.begin(), t->tall.end());
    t->tallMin = t->tall.empty() ? nullptr : t->tall.front();
    t->tallMax = t->tall.empty() ? nullptr : t->tall.back();
    return t;
}

void readFile(fs::path const& file, std::map<std::string, Entry, std::less<>>& out) {
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
        auto        id     = e["id"].get<std::string>();
        auto        name   = e["family"].get<std::string>();
        Json const  params = e.contains("params") && e["params"].is_object() ? e["params"] : Json::object();
        std::string cls    = params.contains("class") && params["class"].is_string() ? params["class"].get<std::string>() : std::string();
        // the family or modifier first: an unknown name creates no entry
        auto shape = familyOf(name, cls);
        auto mod   = shape ? std::nullopt : modifierOf(name);
        if (!shape && !mod) {
            log().warn("families: {} has the unknown family '{}' ({})", id, name, file.filename().string());
            continue;
        }
        auto& entry = out[id];
        entry.id    = id;
        if (shape) {
            if (entry.shaped && entry.family != *shape) log().warn("families: {} has two shape families ({} and {})", id, familyName(entry.family), name);
            entry.family = *shape;
            entry.shaped = true;
        } else {
            entry.modifiers |= *mod;
        }
        for (auto const& [k, v] : params.items()) entry.params[k] = v;
        entry.modifiers |= traitModifiers(params);
        if (entry.params.value("waterlogged", false) || entry.params.value("waterloggable", false)) entry.modifiers |= Waterloggable;
    }
}

/// upstream's registrations for what the data leaves out (the data always wins).
void fillDefaults(std::map<std::string, Entry, std::less<>>& all) {
    auto exists = [&](std::string const& id) { return !id.empty() && all.contains(id); };
    for (auto& [id, e] : all) {
        auto& p = e.params;
        switch (e.family) {
        case Family::Sign:
            if (!p.contains("wall")) {
                if (auto w = swapSuffix(id, "_sign", "_wall_sign"); exists(w)) p["wall"] = w;
            }
            break;
        case Family::HangingSign:
            if (!p.contains("wall")) {
                if (auto w = swapSuffix(id, "_hanging_sign", "_wall_hanging_sign"); exists(w)) p["wall"] = w;
            }
            break;
        case Family::WallSign:
            if (!p.contains("standing")) {
                if (auto s = swapSuffix(id, "_wall_sign", "_sign"); exists(s)) p["standing"] = s;
            }
            break;
        case Family::WallHangingSign:
            if (!p.contains("standing")) {
                if (auto s = swapSuffix(id, "_wall_hanging_sign", "_hanging_sign"); exists(s)) p["standing"] = s;
            }
            break;
        case Family::Potted:
            if (!p.contains("plant")) {
                if (auto it = pottedPlants().find(id); it != pottedPlants().end()) {
                    p["plant"] = it->second;
                } else if (auto colon = id.find(':'); colon != std::string::npos && id.compare(colon + 1, 7, "potted_") == 0) {
                    p["plant"] = id.substr(0, colon + 1) + id.substr(colon + 8);
                }
            }
            if (!p.contains("special")) p["special"] = e.str("class") == "SpecialFlowerPotBlock";
            break;
        case Family::Log:
        case Family::Wood:
        case Family::Pillar:
            if (!p.contains("stripped")) {
                if (auto it = strippables().find(id); it != strippables().end()) p["stripped"] = it->second;
            }
            break;
        case Family::Sapling:
            if (!p.contains("grower") && !p.contains("feature")) {
                if (auto g = growerOf(id); !g.is_null()) p["grower"] = g;
            }
            break;
        case Family::Button:
            // upstream's buttons: wooden block set types, 30 ticks (GloamingBlocks: new ButtonBlock(set, 30, ...))
            if (!p.contains("ticks")) p["ticks"] = 30;
            break;
        default: break;
        }
    }
}

} // namespace

std::optional<Family> familyOf(std::string_view name) {
    for (auto const& f : kFamilies)
        if (name == f.name) return f.family;
    return std::nullopt;
}

std::optional<Family> familyOf(std::string_view name, std::string_view cls) {
    auto f = familyOf(name);
    if (!f) return f;
    // F1 names the Java class: the vanilla class picks the variant of a family
    if (*f == Family::Sign && cls == "WallSignBlock") return Family::WallSign;
    if (*f == Family::HangingSign && cls == "WallHangingSignBlock") return Family::WallHangingSign;
    if (*f == Family::Pillar) {
        if (cls == "WallPillarBlock") return Family::WallPillar;
        // upstream's ConnectableRotatedPillarBlock subclasses (thorns)
        if (cls == "ThornsBlock" || cls == "BurntThornsBlock" || cls == "ConnectableRotatedPillarBlock") return Family::ConnectablePillar;
    }
    return f;
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

double Entry::number(char const* key, double fallback) const {
    auto it = params.find(key);
    return it != params.end() && it->is_number() ? it->get<double>() : fallback;
}

bool Entry::flag(char const* key, bool fallback) const {
    auto it = params.find(key);
    return it != params.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void loadRegistry() {
    std::map<std::string, Entry, std::less<>> loaded;
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
    for (auto const& f : files) readFile(f, loaded);
    fillDefaults(loaded);
    // the block types are looked up when a world is ready (detail::refreshTypes: the client loads
    // mods before the game's block registry exists, and reading it then crashes the load)
    auto e   = std::make_unique<Entries>();
    e->byId  = std::move(loaded);
    auto n   = e->byId.size();
    {
        std::lock_guard lock(entriesMutex());
        currentEntries().store(e.get(), std::memory_order_release);
        loadedEntries().push_back(std::move(e));
    }
    publishTypes(nullptr); // types of an earlier set would point at its entries
    log().info("families: {} family blocks loaded", n);
}

Entry const* entryOf(std::string_view id) {
    auto const& s  = entriesNow();
    auto        it = s.byId.find(id);
    return it == s.byId.end() ? nullptr : &it->second;
}

Entry const* entryOf(BlockType const& type) {
    auto const& t = types();
    if (t.byType.empty()) return nullptr;
    auto it = t.byType.find(&type);
    return it == t.byType.end() ? nullptr : it->second;
}

Entry const* entryOf(Block const& block) { return entryOf(block.getBlockType()); }

bool tallCollisionType(BlockType const& type) {
    auto const& s = types();
    auto const* t = &type;
    if (!s.tallMin || t < s.tallMin || t > s.tallMax) return false;
    return std::binary_search(s.tall.begin(), s.tall.end(), t);
}

namespace detail {
/// The game's block types changed (a new world; the client rebuilds them per world joined): a
/// new table with the types looked up again (the old one goes when no thread reads it). Any thread.
void refreshTypes() {
    auto const& e     = entriesNow();
    auto        fresh = resolveTypes(e);
    auto        found = fresh->byType.size();
    publishTypes(std::move(fresh));
    log().info("families: {} family blocks ({} registered in the game)", e.byId.size(), found);
}
} // namespace detail

std::vector<Entry const*> entries() {
    std::vector<Entry const*> out;
    for (auto const& [id, e] : entriesNow().byId) out.push_back(&e);
    return out;
}

std::vector<Entry const*> entriesOf(Family family) {
    std::vector<Entry const*> out;
    for (auto const& [id, e] : entriesNow().byId)
        if (e.family == family) out.push_back(&e);
    return out;
}

} // namespace gf::feature::families
