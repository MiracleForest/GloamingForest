// Java block states in Bedrock terms for the feature interpreter (F5): Java ids and properties of
// the imported worldgen JSON -> Bedrock permutations, and the Java block queries world
// generation uses (air, fluids, motion blocking, replaceable, tags, survival).
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <shared_mutex>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

#include "feature/features/Internal.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::features {

namespace detail {

void warnOnce(std::string const& key, std::string const& message) {
    static std::mutex                      m;
    static std::unordered_set<std::string> seen;
    {
        std::lock_guard lock(m);
        if (!seen.insert(key).second) return;
    }
    gf::log().warn("worldgen: {}", message);
}

std::string normalizeId(std::string id) {
    if (!id.empty() && id[0] == '#') id.erase(0, 1);
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

Block const& air() {
    static Block const* a = game::blockWith("minecraft:air");
    return *a;
}

} // namespace detail

namespace {

using detail::normalizeId;
using detail::warnOnce;
using Json = nlohmann::json;

/// Java ids whose Bedrock block has another name.
std::unordered_map<std::string, std::string> const& javaToBedrock() {
    static std::unordered_map<std::string, std::string> const m{
        {"minecraft:dead_bush", "minecraft:deadbush"},
        {"minecraft:sugar_cane", "minecraft:reeds"},
        {"minecraft:lily_pad", "minecraft:waterlily"},
        {"minecraft:snow", "minecraft:snow_layer"},
        {"minecraft:snow_block", "minecraft:snow"},
        {"minecraft:cobweb", "minecraft:web"},
        {"minecraft:dirt_path", "minecraft:grass_path"},
        {"minecraft:tall_seagrass", "minecraft:seagrass"},
        {"minecraft:rooted_dirt", "minecraft:dirt_with_roots"},
        {"minecraft:magma_block", "minecraft:magma"},
        {"minecraft:spawner", "minecraft:mob_spawner"},
        {"minecraft:note_block", "minecraft:noteblock"},
        {"minecraft:melon", "minecraft:melon_block"},
        {"minecraft:jack_o_lantern", "minecraft:lit_pumpkin"},
        {"minecraft:terracotta", "minecraft:hardened_clay"},
        {"minecraft:slime_block", "minecraft:slime"},
        {"minecraft:nether_portal", "minecraft:portal"},
        {"minecraft:end_stone_bricks", "minecraft:end_bricks"},
        {"minecraft:cave_air", "minecraft:air"},
        {"minecraft:void_air", "minecraft:air"},
        {"minecraft:wall_torch", "minecraft:torch"},
        {"minecraft:redstone_wall_torch", "minecraft:redstone_torch"},
        {"minecraft:moving_piston", "minecraft:moving_block"},
        {"minecraft:light", "minecraft:light_block"},
        {"minecraft:frogspawn", "minecraft:frog_spawn"},
        {"minecraft:bubble_column", "minecraft:bubble_column"},
        {"minecraft:sweet_berry_bush", "minecraft:sweet_berry_bush"},
        {"minecraft:powder_snow", "minecraft:powder_snow"},
    };
    return m;
}

std::unordered_map<std::string, std::string> const& bedrockToJava() {
    static std::unordered_map<std::string, std::string> const m = [] {
        std::unordered_map<std::string, std::string> r;
        for (auto const& [java, bedrock] : javaToBedrock())
            if (java != "minecraft:cave_air" && java != "minecraft:void_air" && java != "minecraft:wall_torch"
                && java != "minecraft:redstone_wall_torch" && java != "minecraft:tall_seagrass")
                r.emplace(bedrock, java);
        return r;
    }();
    return m;
}

enum class SlotType { Bool, Int, String };

/// The Bedrock states of a block type, by name, with their tag type (from the default permutation).
std::map<std::string, SlotType> slotsOf(Block const& block) {
    std::map<std::string, SlotType> out;
    for (auto const& [name, text] : game::statesOf(block)) {
        if (text == "true" || text == "false") out[name] = SlotType::Bool;
        else if (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-')
                 && std::all_of(text.begin() + 1, text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            out[name] = SlotType::Int;
        else out[name] = SlotType::String;
    }
    return out;
}

Json typed(SlotType t, std::string const& v) {
    switch (t) {
    case SlotType::Bool:
        return v == "true" || v == "1";
    case SlotType::Int:
        try {
            return std::stoi(v);
        } catch (...) {
            return 0;
        }
    case SlotType::String:
        return v;
    }
    return v;
}

std::string text(Json const& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return v.dump();
}

/// Maps the Java properties of `javaId` onto the Bedrock states `slots` of `bedrockId`.
Json mapProperties(std::string const& javaId, std::string const& bedrockId, Json const& props, std::map<std::string, SlotType> const& slots) {
    Json out = Json::object();
    auto has = [&](std::string const& s) { return slots.contains(s); };
    auto set = [&](std::string const& s, std::string const& v) {
        if (auto it = slots.find(s); it != slots.end()) out[s] = typed(it->second, v);
    };
    int  vineBits = -1, faceBits = -1;
    for (auto const& [name, value] : props.items()) {
        std::string v = text(value);
        if (name == "axis") {
            set("pillar_axis", v);
        } else if (name == "half" && javaId == "minecraft:tall_seagrass") {
            set("sea_grass_type", v == "upper" ? "double_top" : "double_bot");
        } else if (name == "half" && has("upper_block_bit")) {
            set("upper_block_bit", v == "upper" ? "true" : "false");
        } else if (name == "half" && has("upside_down_bit")) {
            set("upside_down_bit", v == "top" ? "true" : "false");
        } else if (name == "persistent") {
            set("persistent_bit", v);
        } else if (name == "distance" || name == "waterlogged" || name == "snowy" || name == "stage") {
            // no Bedrock state (leaf distance, liquid layer, snowy grass, sapling stage)
        } else if (name == "layers" && has("height")) {
            set("height", std::to_string(std::max(0, std::stoi(v) - 1)));
        } else if (name == "level" && has("liquid_depth")) {
            set("liquid_depth", v);
        } else if (name == "moisture" && has("moisturized_amount")) {
            set("moisturized_amount", v);
        } else if ((name == "north" || name == "east" || name == "south" || name == "west" || name == "up" || name == "down")
                   && (has("vine_direction_bits") || has("multi_face_direction_bits"))) {
            bool on = v == "true";
            if (has("vine_direction_bits")) {
                static std::map<std::string, int> const bits{{"south", 1}, {"west", 2}, {"north", 4}, {"east", 8}};
                if (vineBits < 0) vineBits = 0;
                if (auto it = bits.find(name); on && it != bits.end()) vineBits |= it->second;
            } else {
                static std::map<std::string, int> const bits{{"down", 1}, {"up", 2}, {"south", 4}, {"west", 8}, {"north", 16}, {"east", 32}};
                if (faceBits < 0) faceBits = 0;
                if (on) faceBits |= bits.at(name);
            }
        } else if (name == "facing") {
            if (has("minecraft:cardinal_direction")) set("minecraft:cardinal_direction", v);
            else if (has("direction")) {
                static std::map<std::string, std::string> const d{{"south", "0"}, {"west", "1"}, {"north", "2"}, {"east", "3"}};
                if (auto it = d.find(v); it != d.end()) set("direction", it->second);
            } else if (has("facing_direction")) {
                static std::map<std::string, std::string> const d{{"down", "0"}, {"up", "1"}, {"north", "2"}, {"south", "3"}, {"west", "4"}, {"east", "5"}};
                if (auto it = d.find(v); it != d.end()) set("facing_direction", it->second);
            } else if (has("minecraft:facing_direction")) set("minecraft:facing_direction", v);
            else if (has("twilightforest:facing")) set("twilightforest:facing", v);
            else warnOnce(javaId + "#facing", "no Bedrock state for property facing of " + javaId);
        } else if (name == "age" && !has("age") && has("growth")) {
            set("growth", v);
        } else {
            // the same name, or the name in the block's / the mod's namespace
            auto ns    = bedrockId.substr(0, bedrockId.find(':'));
            bool found = false;
            for (auto const& candidate : {name, ns + ":" + name, std::string("twilightforest:") + name, std::string("minecraft:") + name}) {
                if (!found && has(candidate)) {
                    set(candidate, v);
                    found = true;
                }
            }
            if (!found) warnOnce(javaId + "#" + name, "no Bedrock state for property " + name + " of " + javaId);
        }
    }
    if (vineBits >= 0) set("vine_direction_bits", std::to_string(vineBits));
    if (faceBits >= 0) set("multi_face_direction_bits", std::to_string(faceBits));
    return out;
}

std::string bedrockName(std::string const& javaId) {
    if (auto it = javaToBedrock().find(javaId); it != javaToBedrock().end()) return it->second;
    return javaId;
}

std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, Block const*>& cache() {
    static std::unordered_map<std::string, Block const*> c;
    return c;
}

bool nameIn(std::string const& id, std::initializer_list<char const*> names) {
    return std::any_of(names.begin(), names.end(), [&](char const* n) { return id == n; });
}

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// The vanilla tags world generation reads, by Java id (TF members join through gf::tags).
std::optional<bool> builtinTag(std::string const& tag, std::string const& id) {
    if (tag == "minecraft:dirt")
        return nameIn(id, {"minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium",
                           "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots"});
    if (tag == "minecraft:logs" || tag == "minecraft:logs_that_burn" || tag == "minecraft:overworld_natural_logs")
        return id.starts_with("minecraft:") && (endsWith(id, "_log") || endsWith(id, "_wood") || endsWith(id, "_stem") || endsWith(id, "_hyphae"));
    if (tag == "minecraft:leaves") return id.starts_with("minecraft:") && endsWith(id, "_leaves");
    if (tag == "minecraft:sand") return nameIn(id, {"minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand"});
    if (tag == "minecraft:base_stone_overworld")
        return nameIn(id, {"minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate"});
    if (tag == "minecraft:stone_ore_replaceables")
        return nameIn(id, {"minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite"});
    if (tag == "minecraft:deepslate_ore_replaceables") return nameIn(id, {"minecraft:deepslate", "minecraft:tuff"});
    if (tag == "minecraft:replaceable_by_trees" || tag == "minecraft:replaceable_by_mushrooms")
        return (id.starts_with("minecraft:") && endsWith(id, "_leaves"))
            || nameIn(id, {"minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:vine", "minecraft:glow_lichen",
                           "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush", "minecraft:peony", "minecraft:tall_grass",
                           "minecraft:large_fern", "minecraft:hanging_roots", "minecraft:pitcher_plant", "minecraft:water",
                           "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:warped_roots", "minecraft:nether_sprouts",
                           "minecraft:crimson_roots"});
    if (tag == "minecraft:replaceable")
        return nameIn(id, {"minecraft:air", "minecraft:water", "minecraft:lava", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush",
                           "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:fire", "minecraft:soul_fire", "minecraft:snow",
                           "minecraft:vine", "minecraft:glow_lichen", "minecraft:light", "minecraft:tall_grass", "minecraft:large_fern",
                           "minecraft:structure_void", "minecraft:bubble_column", "minecraft:warped_roots", "minecraft:nether_sprouts",
                           "minecraft:crimson_roots", "minecraft:hanging_roots"});
    if (tag == "minecraft:features_cannot_replace")
        return nameIn(id, {"minecraft:bedrock", "minecraft:spawner", "minecraft:chest", "minecraft:end_portal_frame", "minecraft:reinforced_deepslate"});
    if (tag == "minecraft:ice") return nameIn(id, {"minecraft:ice", "minecraft:packed_ice", "minecraft:blue_ice", "minecraft:frosted_ice"});
    if (tag == "minecraft:snow") return nameIn(id, {"minecraft:snow", "minecraft:snow_block", "minecraft:powder_snow"});
    if (tag == "minecraft:flowers" || tag == "minecraft:small_flowers")
        return nameIn(id, {"minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet",
                           "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip",
                           "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose"});
    return std::nullopt;
}

} // namespace

namespace kit {

Block const* javaBlock(std::string_view javaIdIn, Json const& properties) {
    std::string javaId = normalizeId(std::string(javaIdIn));
    std::string key    = javaId;
    if (properties.is_object() && !properties.empty()) key += properties.dump();
    {
        std::lock_guard lock(cacheMutex());
        if (auto it = cache().find(key); it != cache().end()) return it->second;
    }
    std::string  name = bedrockName(javaId);
    Block const* base = game::blockWith(name);
    Block const* out  = base;
    if (!base) {
        warnOnce("block " + javaId, "unknown block " + javaId + " (skipped)");
    } else if (properties.is_object() && !properties.empty()) {
        Json states = mapProperties(javaId, name, properties, slotsOf(*base));
        if (!states.empty()) {
            out = game::blockWithStates(name, states);
            if (!out) {
                warnOnce("states " + key, "no Bedrock permutation of " + javaId + " for " + properties.dump() + " (default used)");
                out = base;
            }
        }
    }
    std::lock_guard lock(cacheMutex());
    cache()[key] = out;
    return out;
}

Block const* javaState(Json const& state) {
    if (state.is_string()) return javaBlock(state.get<std::string>());
    if (!state.is_object()) return nullptr;
    auto name = state.value("Name", std::string());
    if (name.empty()) return nullptr;
    return javaBlock(name, state.contains("Properties") ? state["Properties"] : Json::object());
}

std::string javaId(Block const& block) {
    std::string const& name = block.getTypeName();
    if (auto it = bedrockToJava().find(name); it != bedrockToJava().end()) return it->second;
    return name;
}

Block const& withProperty(Block const& block, std::string_view name, std::string_view value) {
    auto slots = slotsOf(block);
    Json props = Json::object();
    props[std::string(name)] = std::string(value);
    Json states = mapProperties(javaId(block), block.getTypeName(), props, slots);
    if (states.empty()) return block;
    // keep the block's other states: start from its own serialized states
    Json all = Json::object();
    for (auto const& [k, v] : game::statesOf(block)) {
        if (auto it = slots.find(k); it != slots.end()) all[k] = typed(it->second, v);
    }
    for (auto const& [k, v] : states.items()) all[k] = v;
    auto const* b = game::blockWithStates(block.getTypeName(), all);
    return b ? *b : block;
}

bool isAir(Block const& block) { return block.isAir(); }

bool isFluid(Block const& block) { return block.getMaterial().mLiquid; }

bool isWater(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water";
}

bool isLava(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

bool blocksMotion(Block const& block) {
    auto const& n = block.getTypeName();
    if (n == "minecraft:web" || n == "minecraft:bamboo_sapling") return false;
    return block.getMaterial().mBlocksMotion;
}

bool isLeaves(Block const& block) {
    auto const& n = block.getTypeName();
    return endsWith(n, "leaves") || endsWith(n, "_leaves");
}

bool isReplaceable(Block const& block) {
    if (block.isAir() || isFluid(block)) return true;
    return builtinTag("minecraft:replaceable", javaId(block)).value_or(false);
}

bool hasTag(Block const& block, std::string_view tagIn) {
    std::string tag = normalizeId(std::string(tagIn));
    std::string id  = javaId(block);
    if (auto b = builtinTag(tag, id); b && *b) return true;
    if (tags::has(tags::Registry::Block, tag, id)) return true;
    // the imported tag JSON itself (TF's members of its own and of vanilla tags), so world
    // generation sees them whatever gf::tags answers
    auto members = detail::dataBlockTag(tag);
    return members && members->contains(id);
}

bool canSurvive(Region& region, BlockPos const& pos, Block const& block) {
    if (auto* src = blockSource(region)) return block.canSurvive(*src, pos);
    // in-memory regions: plants need a solid block below
    BlockPos below{pos.x, pos.y - 1, pos.z};
    auto const& b = region.getBlock(below);
    if (isLeaves(block) || blocksMotion(block)) return true;
    return blocksMotion(b);
}

} // namespace kit

namespace detail {

namespace {

struct DataTags {
    std::shared_mutex                                                                  mutex; ///< shared: lookups, unique: loads
    std::uint64_t                                                                      generation = 0;
    std::unordered_map<std::string, std::shared_ptr<std::unordered_set<std::string> const>> sets;
};
DataTags& dataTags() {
    static DataTags t;
    return t;
}

/// <mod>/data/<ns>/tags/block/<path>.json of `tag` ("values": ids, {"id", "required"} objects and
/// "#tag" references, expanded recursively).
void readTag(std::filesystem::path const& root, std::string const& tag, std::unordered_set<std::string>& out, std::set<std::string>& visiting) {
    if (!visiting.insert(tag).second) return; // a reference cycle
    auto colon = tag.find(':');
    auto file  = root / tag.substr(0, colon) / "tags" / "block" / (tag.substr(colon + 1) + ".json");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return;
    Json doc;
    try {
        std::ifstream in(file);
        doc = Json::parse(in);
    } catch (std::exception const& e) {
        warnOnce("tag file " + tag, "cannot read block tag " + tag + ": " + e.what());
        return;
    }
    for (auto const& v : fieldArr(doc, "values")) {
        std::string entry = v.is_string() ? v.get<std::string>() : (v.is_object() ? v.value("id", std::string()) : std::string());
        if (entry.empty()) continue;
        if (entry[0] == '#') readTag(root, normalizeId(entry), out, visiting);
        else out.insert(normalizeId(entry));
    }
}

} // namespace

std::shared_ptr<std::unordered_set<std::string> const> dataBlockTag(std::string const& tag) {
    auto& t          = dataTags();
    auto  generation = data().generation;
    {
        // the hot path (every tag query of every worker thread): a shared lock
        std::shared_lock lock(t.mutex);
        if (t.generation == generation)
            if (auto it = t.sets.find(tag); it != t.sets.end()) return it->second;
    }
    std::unique_lock lock(t.mutex);
    if (t.generation != generation) {
        t.sets.clear();
        t.generation = generation;
    }
    if (auto it = t.sets.find(tag); it != t.sets.end()) return it->second;
    auto set = std::make_shared<std::unordered_set<std::string>>();
    try {
        auto                  root = Mod::getInstance().getSelf().getModDir() / "data";
        std::set<std::string> visiting;
        readTag(root, tag, *set, visiting);
    } catch (std::exception const&) {
        // no mod directory (tests): no data tags
    }
    return t.sets.emplace(tag, std::move(set)).first->second;
}

void clearBlockCache() {
    std::lock_guard lock(cacheMutex());
    cache().clear();
}

Json blockToJson(Block const& block) {
    auto slots  = slotsOf(block);
    Json states = Json::object();
    for (auto const& [k, v] : game::statesOf(block))
        if (auto it = slots.find(k); it != slots.end()) states[k] = typed(it->second, v);
    return Json{{"name", block.getTypeName()}, {"states", states}};
}

Block const* blockFromJson(Json const& json) {
    auto name = json.value("name", std::string());
    if (name.empty()) return nullptr;
    return game::blockWithStates(name, json.value("states", Json::object()));
}

std::optional<std::string> javaProperty(Block const& block, std::string_view name) {
    auto states = game::statesOf(block);
    if (name == "axis") {
        if (auto it = states.find("pillar_axis"); it != states.end()) return it->second;
        return std::nullopt;
    }
    if (name == "persistent") {
        if (auto it = states.find("persistent_bit"); it != states.end()) return it->second;
        return std::nullopt;
    }
    if (auto it = states.find(std::string(name)); it != states.end()) return it->second;
    return std::nullopt;
}

} // namespace detail

} // namespace gf::features
