// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFBlocks.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Java block states in Bedrock terms for the feature interpreter (F5): Java ids and properties of
// the imported worldgen JSON -> Bedrock permutations, and the Java block queries world
// generation uses (air, fluids, motion blocking, replaceable with TFBlocks' replaceable blocks,
// tags, survival).
#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

#include "feature/features/Internal.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

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

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// BlockStateBase#canBeReplaced: the blocks built with BlockBehaviour.Properties#replaceable, by
/// Java id. Vanilla 1.21.1's are the members of its minecraft:replaceable tag; TF's come from
/// TFBlocks (fiddlehead, fallen leaves, ominous fire, wispy cloud), which no tag lists.
bool replaceableId(std::string const& javaId) {
    static std::unordered_set<std::string> const ids{
        "minecraft:air",
        "minecraft:water",
        "minecraft:lava",
        "minecraft:short_grass",
        "minecraft:fern",
        "minecraft:dead_bush",
        "minecraft:seagrass",
        "minecraft:tall_seagrass",
        "minecraft:fire",
        "minecraft:soul_fire",
        "minecraft:snow",
        "minecraft:vine",
        "minecraft:glow_lichen",
        "minecraft:light",
        "minecraft:tall_grass",
        "minecraft:large_fern",
        "minecraft:structure_void",
        "minecraft:void_air",
        "minecraft:cave_air",
        "minecraft:bubble_column",
        "minecraft:warped_roots",
        "minecraft:nether_sprouts",
        "minecraft:crimson_roots",
        "minecraft:hanging_roots",
        "twilightforest:fiddlehead",
        "twilightforest:fallen_leaves",
        "twilightforest:ominous_fire",
        "twilightforest:wispy_cloud",
    };
    return ids.contains(javaId);
}

/// Heterogeneous lookup by string_view.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

/// The answers kept while a TagCacheScope is open on this thread.
struct TagCache {
    int depth = 0;
    /// tag (as asked) -> block -> member
    std::unordered_map<std::string, std::unordered_map<Block const*, bool>, StringHash, std::equal_to<>> blocks;
    /// a HolderSet's JSON (by address, its value kept to tell a reused address) -> its structure ids
    struct Set {
        Json                                            value;
        std::shared_ptr<std::vector<std::string> const> ids;
    };
    std::unordered_map<Json const*, Set> structureSets;
};
TagCache& tagCache() {
    thread_local TagCache c;
    return c;
}

/// kit::hasTag without the scope's cache.
bool hasTagUncached(Block const& block, std::string_view tagIn) {
    // gf::tags (F9): TF's tag data with the units' fragments, nested tags, and the vanilla members
    // of Minecraft 1.21.1's tags through its vanilla bridge (the data members its includes reach
    // too); it takes the game's (Bedrock) ids
    return tags::has(tags::Registry::Block, normalizeId(std::string(tagIn)), block.getTypeName());
}

std::shared_ptr<std::vector<std::string> const> structureIdsUncached(Json const& set) {
    auto out = std::make_shared<std::vector<std::string>>();
    auto add = [&](std::string const& entry) {
        if (entry.starts_with('#')) {
            for (auto& id : tags::members(tags::Registry::Structure, normalizeId(entry))) out->push_back(std::move(id));
        } else {
            out->push_back(normalizeId(entry));
        }
    };
    if (set.is_string()) add(set.get<std::string>());
    if (set.is_array())
        for (auto const& e : set)
            if (e.is_string()) add(e.get<std::string>());
    return out;
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
    return replaceableId(javaId(block));
}

bool hasTag(Block const& block, std::string_view tag) {
    auto& cache = tagCache();
    if (cache.depth == 0) return hasTagUncached(block, tag);
    auto byTag = cache.blocks.find(tag);
    if (byTag == cache.blocks.end()) byTag = cache.blocks.emplace(std::string(tag), std::unordered_map<Block const*, bool>{}).first;
    if (auto known = byTag->second.find(&block); known != byTag->second.end()) return known->second;
    bool member = hasTagUncached(block, tag); // gf::tags alone: it never re-enters hasTag
    byTag->second.emplace(&block, member);
    return member;
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

TagCacheScope::TagCacheScope() { ++tagCache().depth; }

TagCacheScope::~TagCacheScope() {
    auto& cache = tagCache();
    if (--cache.depth > 0) return;
    cache.blocks.clear();
    cache.structureSets.clear();
}

std::shared_ptr<std::vector<std::string> const> structureIds(Json const& set) {
    auto& cache = tagCache();
    if (cache.depth == 0) return structureIdsUncached(set);
    // keyed by the JSON's address (a placed feature's modifier, the same object at every call) and
    // checked by value: an inline JSON freed meanwhile may have left its address to another one
    auto& slot = cache.structureSets[&set];
    if (!slot.ids || slot.value != set) {
        slot.value = set;
        slot.ids   = structureIdsUncached(set);
    }
    return slot.ids;
}

} // namespace detail

} // namespace gf::features
