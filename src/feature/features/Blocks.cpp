// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFBlocks.java
//   src/main/java/twilightforest/block/TFPlantBlock.java
//   src/main/java/twilightforest/block/TFBushBlock.java
//   src/main/java/twilightforest/block/BerryBushBlock.java
//   src/main/java/twilightforest/block/DarkTowerBerryBushBlock.java
//   src/main/java/twilightforest/block/OreBerryBlock.java
//   src/main/java/twilightforest/block/MushgloomBlock.java
//   src/main/java/twilightforest/block/MossPatchBlock.java
//   src/main/java/twilightforest/block/FallenLeavesBlock.java
//   src/main/java/twilightforest/block/TorchberryPlantBlock.java
//   src/main/java/twilightforest/block/RootStrandBlock.java
//   src/main/java/twilightforest/block/TrollRootBlock.java
//   src/main/java/twilightforest/block/ThornRoseBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Java block states in Bedrock terms for the feature interpreter (F5): Java ids and properties of
// the imported worldgen JSON -> Bedrock permutations, and the Java block queries world
// generation uses (air, fluids, motion blocking, replaceable with TFBlocks' replaceable blocks,
// tags, survival).
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
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

/// Java ids whose Bedrock block has another name: the Bedrock candidates, the first that exists
/// wins (names that changed between Bedrock versions list both).
std::unordered_map<std::string, std::vector<std::string>> const& javaToBedrock() {
    static std::unordered_map<std::string, std::vector<std::string>> const m{
        {"minecraft:dead_bush", {"minecraft:deadbush"}},
        {"minecraft:sugar_cane", {"minecraft:reeds"}},
        {"minecraft:lily_pad", {"minecraft:waterlily"}},
        {"minecraft:snow", {"minecraft:snow_layer"}},
        {"minecraft:snow_block", {"minecraft:snow"}},
        {"minecraft:cobweb", {"minecraft:web"}},
        {"minecraft:dirt_path", {"minecraft:grass_path"}},
        {"minecraft:tall_seagrass", {"minecraft:seagrass"}},
        {"minecraft:rooted_dirt", {"minecraft:dirt_with_roots"}},
        {"minecraft:magma_block", {"minecraft:magma"}},
        {"minecraft:spawner", {"minecraft:mob_spawner"}},
        {"minecraft:note_block", {"minecraft:noteblock"}},
        {"minecraft:melon", {"minecraft:melon_block"}},
        {"minecraft:jack_o_lantern", {"minecraft:lit_pumpkin"}},
        {"minecraft:terracotta", {"minecraft:hardened_clay"}},
        {"minecraft:slime_block", {"minecraft:slime", "minecraft:slime_block"}},
        {"minecraft:nether_portal", {"minecraft:portal"}},
        {"minecraft:end_stone_bricks", {"minecraft:end_bricks"}},
        {"minecraft:cave_air", {"minecraft:air"}},
        {"minecraft:void_air", {"minecraft:air"}},
        {"minecraft:wall_torch", {"minecraft:torch"}},
        {"minecraft:soul_wall_torch", {"minecraft:soul_torch"}},
        {"minecraft:redstone_wall_torch", {"minecraft:redstone_torch"}},
        {"minecraft:moving_piston", {"minecraft:moving_block"}},
        {"minecraft:light", {"minecraft:light_block"}},
        {"minecraft:frogspawn", {"minecraft:frog_spawn"}},
        {"minecraft:grass_block", {"minecraft:grass_block", "minecraft:grass"}},
        {"minecraft:short_grass", {"minecraft:short_grass", "minecraft:tallgrass"}},
        {"minecraft:bricks", {"minecraft:brick_block"}},
        {"minecraft:nether_bricks", {"minecraft:nether_brick"}},
        {"minecraft:red_nether_bricks", {"minecraft:red_nether_brick"}},
        {"minecraft:powered_rail", {"minecraft:golden_rail"}},
        {"minecraft:tripwire", {"minecraft:trip_wire"}},
        {"minecraft:tripwire_hook", {"minecraft:tripwire_hook", "minecraft:trip_wire_hook"}},
        {"minecraft:chain", {"minecraft:iron_chain", "minecraft:chain"}},
        {"minecraft:stone_stairs", {"minecraft:normal_stone_stairs"}},
        {"minecraft:cobblestone_stairs", {"minecraft:stone_stairs"}},
        {"minecraft:stone_slab", {"minecraft:normal_stone_slab"}},
        {"minecraft:oak_door", {"minecraft:wooden_door"}},
        {"minecraft:oak_trapdoor", {"minecraft:trapdoor"}},
        {"minecraft:oak_fence_gate", {"minecraft:fence_gate"}},
        {"minecraft:oak_button", {"minecraft:wooden_button"}},
        {"minecraft:oak_pressure_plate", {"minecraft:wooden_pressure_plate"}},
        {"minecraft:oak_sign", {"minecraft:standing_sign"}},
        {"minecraft:oak_wall_sign", {"minecraft:wall_sign"}},
        {"minecraft:dark_oak_sign", {"minecraft:darkoak_standing_sign"}},
        {"minecraft:dark_oak_wall_sign", {"minecraft:darkoak_wall_sign"}},
        {"minecraft:skeleton_wall_skull", {"minecraft:skeleton_skull"}},
        {"minecraft:wither_skeleton_wall_skull", {"minecraft:wither_skeleton_skull"}},
        {"minecraft:zombie_wall_head", {"minecraft:zombie_head"}},
        {"minecraft:creeper_wall_head", {"minecraft:creeper_head"}},
        {"minecraft:player_wall_head", {"minecraft:player_head"}},
        {"minecraft:attached_melon_stem", {"minecraft:melon_stem"}},
        {"minecraft:attached_pumpkin_stem", {"minecraft:pumpkin_stem"}},
        {"minecraft:beetroots", {"minecraft:beetroot"}},
        {"minecraft:repeater", {"minecraft:unpowered_repeater"}},
        {"minecraft:comparator", {"minecraft:unpowered_comparator"}},
        {"minecraft:water_cauldron", {"minecraft:cauldron"}},
        {"minecraft:lava_cauldron", {"minecraft:cauldron"}},
        {"minecraft:powder_snow_cauldron", {"minecraft:cauldron"}},
        {"minecraft:stonecutter", {"minecraft:stonecutter_block"}},
        {"minecraft:shulker_box", {"minecraft:undyed_shulker_box"}},
        {"minecraft:piston_head", {"minecraft:piston_arm_collision"}},
        {"minecraft:sticky_piston_head", {"minecraft:sticky_piston_arm_collision"}},
    };
    return m;
}

/// Java ids whose Bedrock block depends on a property value (the Bedrock id replaces the
/// property: Bedrock keeps it in the block's name).
struct Variant {
    char const* javaId;
    char const* property;
    char const* value;
    char const* bedrockId;
};
constexpr Variant kVariants[] = {
    {"minecraft:furnace", "lit", "true", "minecraft:lit_furnace"},
    {"minecraft:blast_furnace", "lit", "true", "minecraft:lit_blast_furnace"},
    {"minecraft:smoker", "lit", "true", "minecraft:lit_smoker"},
    {"minecraft:redstone_lamp", "lit", "true", "minecraft:lit_redstone_lamp"},
    {"minecraft:redstone_ore", "lit", "true", "minecraft:lit_redstone_ore"},
    {"minecraft:deepslate_redstone_ore", "lit", "true", "minecraft:lit_deepslate_redstone_ore"},
    {"minecraft:redstone_torch", "lit", "false", "minecraft:unlit_redstone_torch"},
    {"minecraft:redstone_wall_torch", "lit", "false", "minecraft:unlit_redstone_torch"},
    {"minecraft:repeater", "powered", "true", "minecraft:powered_repeater"},
    {"minecraft:comparator", "powered", "true", "minecraft:powered_comparator"},
    {"minecraft:daylight_detector", "inverted", "true", "minecraft:daylight_detector_inverted"},
};

/// The colour names of Java's 16 <colour>_bed blocks (Bedrock: one minecraft:bed).
bool isColouredBed(std::string_view javaId) {
    static constexpr std::string_view kColours[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                                    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
    constexpr std::string_view ns = "minecraft:";
    if (!javaId.starts_with(ns) || !javaId.ends_with("_bed")) return false;
    auto colour = javaId.substr(ns.size(), javaId.size() - ns.size() - 4);
    return std::find(std::begin(kColours), std::end(kColours), colour) != std::end(kColours);
}

/// Java ids the reverse lookup (kit::javaId) must not answer: several Java ids share their Bedrock
/// block, or the Bedrock name is a Java id of its own.
bool ambiguousReverse(std::string const& javaId) {
    return javaId == "minecraft:cave_air" || javaId == "minecraft:void_air" || javaId == "minecraft:tall_seagrass" ||
           javaId.find("wall_") != std::string::npos || javaId.ends_with("_cauldron") || javaId.starts_with("minecraft:attached_");
}
std::unordered_map<std::string, std::string> const& bedrockToJava() {
    static std::unordered_map<std::string, std::string> const m = [] {
        std::unordered_map<std::string, std::string> r;
        std::unordered_set<std::string>              shared;
        for (auto const& [java, candidates] : javaToBedrock()) {
            if (ambiguousReverse(java)) continue;
            for (auto const& bedrock : candidates) {
                if (!r.emplace(bedrock, java).second) shared.insert(bedrock);
            }
        }
        for (auto const& s : shared) r.erase(s);
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

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Bedrock's horizontal direction encodings of a "direction" / "weirdo_direction" state.
enum class DirectionCodec { Legacy2D, Door, Weirdo };
DirectionCodec directionCodec(std::string const& bedrockId) {
    if (endsWith(bedrockId, "trapdoor")) return DirectionCodec::Weirdo;
    if (endsWith(bedrockId, "door")) return DirectionCodec::Door;
    return DirectionCodec::Legacy2D;
}
/// Java horizontal direction name -> the value of a Bedrock direction state (-1: not horizontal).
int encodeDirection(DirectionCodec codec, std::string const& v) {
    static std::map<std::string, int> const legacy{{"south", 0}, {"west", 1}, {"north", 2}, {"east", 3}}; // Direction#get2DDataValue
    static std::map<std::string, int> const weirdo{{"east", 0}, {"west", 1}, {"south", 2}, {"north", 3}};
    auto const& table = codec == DirectionCodec::Weirdo ? weirdo : legacy;
    auto        it    = table.find(v);
    if (it == table.end()) return -1;
    return codec == DirectionCodec::Door ? (it->second + 3) & 3 : it->second;
}

/// The huge_mushroom_bits of a huge mushroom block whose faces (up, down, north, south, west, east)
/// show the cap or stem side: the Bedrock (legacy) encoding names the outer faces of the cap
/// (1 north-west, 2 north, 3 north-east, 4 west, 5 top, 6 east, 7 south-west, 8 south, 9 south-east),
/// 0 the pores, 10 a stem's sides, 14 a cap and 15 a stem with every face out.
int mushroomBits(std::string const& bedrockId, std::map<std::string, bool> const& f) {
    bool up = f.at("up"), down = f.at("down"), n = f.at("north"), s = f.at("south"), w = f.at("west"), e = f.at("east");
    if (bedrockId == "minecraft:mushroom_stem") return up || down ? 15 : 10;
    if (up && down && n && e && s && w) return 14;
    // the nearest value (as U3's huge mushroom features choose it): the fewest differing faces,
    // then the fewest Java faces left without the cap skin; {up, down, north, east, south, west}
    struct Entry {
        int  bits;
        bool f[6];
    };
    static Entry const table[] = {
        {0, {false, false, false, false, false, false}}, {1, {true, false, true, false, false, true}}, {2, {true, false, true, false, false, false}},
        {3, {true, false, true, true, false, false}},    {4, {true, false, false, false, false, true}}, {5, {true, false, false, false, false, false}},
        {6, {true, false, false, true, false, false}},   {7, {true, false, false, false, true, true}},  {8, {true, false, false, false, true, false}},
        {9, {true, false, false, true, true, false}},    {14, {true, true, true, true, true, true}},
    };
    bool const java[6] = {up, down, n, e, s, w};
    int        best = 0, bestScore = 1 << 30;
    for (auto const& entry : table) {
        int miss = 0, extra = 0;
        for (int i = 0; i < 6; ++i) {
            miss += java[i] && !entry.f[i];
            extra += !java[i] && entry.f[i];
        }
        int score = (miss + extra) * 4 + miss;
        if (score < bestScore) {
            bestScore = score;
            best      = entry.bits;
        }
    }
    return best;
}
/// Maps the Java properties of `javaId` onto the Bedrock states `slots` of `bedrockId`: the vanilla
/// encodings of stairs (weirdo_direction, upside_down_bit), slabs (minecraft:vertical_half), walls
/// (wall_connection_type_*, wall_post_bit), doors, trapdoors, gates, buttons, torches, rails, ...;
/// TF's blocks by name ("<property>", "twilightforest:<property>"). Properties Bedrock recomputes
/// itself (stair shape, fence / pane connections, chest halves) or keeps elsewhere (waterlogged:
/// the liquid layer) are dropped without a warning.
Json mapProperties(std::string const& javaId, std::string const& bedrockId, Json const& props, std::map<std::string, SlotType> const& slots) {
    Json out = Json::object();
    auto has = [&](std::string const& s) { return slots.contains(s); };
    auto set = [&](std::string const& s, std::string const& v) {
        if (auto it = slots.find(s); it != slots.end()) out[s] = typed(it->second, v);
    };
    auto setBool = [&](std::string const& s, bool b) { set(s, b ? "true" : "false"); };
    auto num     = [](std::string const& v) {
        try {
            return std::stoi(v);
        } catch (...) {
            return 0;
        }
    };
    bool        vanilla = bedrockId.starts_with("minecraft:");
    std::string face    = props.is_object() && props.contains("face") ? text(props["face"]) : std::string{};
    int         vineBits = -1, faceBits = -1;
    // HugeMushroomBlock's six faces (Java: true by default) -> Bedrock huge_mushroom_bits
    std::map<std::string, bool> mushroomFaces{{"up", true}, {"down", true}, {"north", true}, {"south", true}, {"west", true}, {"east", true}};
    bool                        mushroom = has("huge_mushroom_bits");
    for (auto const& [name, value] : props.items()) {
        std::string v = text(value);
        if (mushroom && mushroomFaces.contains(name)) {
            mushroomFaces[name] = v == "true";
        } else if (name == "axis") {
            if (has("pillar_axis")) set("pillar_axis", v);
            else if (has("portal_axis")) set("portal_axis", v);
            else if (has("twilightforest:axis")) set("twilightforest:axis", v);
            else if (has("minecraft:axis")) set("minecraft:axis", v);
        } else if (name == "half" && javaId == "minecraft:tall_seagrass") {
            set("sea_grass_type", v == "upper" ? "double_top" : "double_bot");
        } else if (name == "half" && (has("upper_block_bit") || has("upside_down_bit") || has("minecraft:vertical_half"))) {
            bool upper = v == "upper" || v == "top";
            if (has("upper_block_bit")) setBool("upper_block_bit", upper);
            else if (has("upside_down_bit")) setBool("upside_down_bit", upper);
            else set("minecraft:vertical_half", upper ? "top" : "bottom");
        } else if (name == "type" && (v == "top" || v == "bottom") && (has("minecraft:vertical_half") || has("top_slot_bit"))) {
            if (has("minecraft:vertical_half")) set("minecraft:vertical_half", v);
            else setBool("top_slot_bit", v == "top");
        } else if (name == "type" && (v == "double" || v == "single" || v == "left" || v == "right") && vanilla) {
            // a double slab is its own Bedrock block (javaBlock); chests pair by themselves
        } else if (name == "persistent") {
            set("persistent_bit", v);
        } else if (name == "distance" || name == "waterlogged" || name == "snowy" || name == "stage" || name == "face") {
            // no Bedrock state (leaf distance, liquid layer, snowy grass, sapling stage); "face" goes with "facing"
        } else if (name == "shape" && has("rail_direction")) {
            static std::map<std::string, std::string> const rails{
                {"north_south", "0"}, {"east_west", "1"}, {"ascending_east", "2"}, {"ascending_west", "3"}, {"ascending_north", "4"},
                {"ascending_south", "5"}, {"south_east", "6"}, {"south_west", "7"}, {"north_west", "8"}, {"north_east", "9"}};
            if (auto it = rails.find(v); it != rails.end()) set("rail_direction", it->second);
        } else if (name == "shape" && vanilla) {
            // stairs: Bedrock shapes them from their neighbours
        } else if (name == "layers" && has("height")) {
            set("height", std::to_string(std::max(0, num(v) - 1)));
        } else if (name == "level" && has("liquid_depth")) {
            set("liquid_depth", v);
        } else if (name == "level" && has("fill_level")) {
            set("fill_level", std::to_string(num(v) * 2)); // LayeredCauldronBlock 1..3 -> Bedrock 0..6
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
        } else if ((name == "north" || name == "east" || name == "south" || name == "west") && has("wall_connection_type_" + name)) {
            // WallSide none / low / tall
            set("wall_connection_type_" + name, v == "low" ? "short" : v == "tall" ? "tall" : "none");
        } else if (name == "up" && has("wall_post_bit")) {
            set("wall_post_bit", v);
        } else if ((name == "north" || name == "east" || name == "south" || name == "west" || name == "up" || name == "down") && vanilla) {
            // fences, panes, iron bars, mushroom blocks' sides: Bedrock connects them by itself
        } else if (name == "facing") {
            static std::map<std::string, std::string> const d3{{"down", "0"}, {"up", "1"}, {"north", "2"}, {"south", "3"}, {"west", "4"}, {"east", "5"}};
            std::string dir3 = v;
            if (!face.empty() && face != "wall") dir3 = face == "floor" ? "up" : "down"; // buttons / levers on a floor or a ceiling
            if (has("weirdo_direction")) {
                if (int d = encodeDirection(DirectionCodec::Weirdo, v); d >= 0) set("weirdo_direction", std::to_string(d));
            } else if (has("minecraft:cardinal_direction")) set("minecraft:cardinal_direction", v);
            else if (has("torch_facing_direction")) set("torch_facing_direction", v == "up" ? "top" : v);
            else if (has("direction")) {
                if (int d = encodeDirection(directionCodec(bedrockId), v); d >= 0) set("direction", std::to_string(d));
            } else if (has("facing_direction")) {
                if (auto it = d3.find(dir3); it != d3.end()) set("facing_direction", it->second);
            } else if (has("minecraft:facing_direction")) set("minecraft:facing_direction", dir3);
            else if (has("minecraft:block_face")) set("minecraft:block_face", dir3);
            else if (has("twilightforest:facing")) set("twilightforest:facing", v);
            else if (has("facing")) set("facing", v);
            else warnOnce(javaId + "#facing", "no Bedrock state for property facing of " + javaId);
        } else if (name == "open" && has("open_bit")) {
            set("open_bit", v);
        } else if (name == "hinge" && has("door_hinge_bit")) {
            setBool("door_hinge_bit", v == "right");
        } else if (name == "in_wall" && has("in_wall_bit")) {
            set("in_wall_bit", v);
        } else if (name == "powered" && (has("button_pressed_bit") || has("powered_bit"))) {
            set(has("button_pressed_bit") ? "button_pressed_bit" : "powered_bit", v);
        } else if (name == "attached" && has("attached_bit")) {
            set("attached_bit", v);
        } else if (name == "disarmed" && has("disarmed_bit")) {
            set("disarmed_bit", v);
        } else if (name == "occupied" && has("occupied_bit")) {
            set("occupied_bit", v);
        } else if (name == "part" && has("head_piece_bit")) {
            setBool("head_piece_bit", v == "head");
        } else if (name == "lit" && has("extinguished")) {
            setBool("extinguished", v != "true"); // campfires
        } else if (name == "candles" && has("candles")) {
            set("candles", std::to_string(std::max(0, num(v) - 1)));
        } else if (name == "pickles" && has("cluster_count")) {
            set("cluster_count", std::to_string(std::max(0, num(v) - 1)));
        } else if (name == "delay" && has("repeater_delay")) {
            set("repeater_delay", std::to_string(std::max(0, num(v) - 1)));
        } else if (name == "bites" && has("bite_counter")) {
            set("bite_counter", v);
        } else if (name == "power" && has("redstone_signal")) {
            set("redstone_signal", v);
        } else if (name == "rotation" && has("ground_sign_direction")) {
            set("ground_sign_direction", v);
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
            // a vanilla block whose Bedrock form names the value instead (furnace / lit_furnace,
            // repeater / powered_repeater, ...: the variant id is chosen by bedrockName)
            bool variant = vanilla && (name == "lit" || name == "powered" || name == "inverted");
            if (!found && !variant) warnOnce(javaId + "#" + name, "no Bedrock state for property " + name + " of " + javaId);
        }
    }
    if (vineBits >= 0) set("vine_direction_bits", std::to_string(vineBits));
    if (faceBits >= 0) set("multi_face_direction_bits", std::to_string(faceBits));
    if (mushroom && props.is_object() && !props.empty()) set("huge_mushroom_bits", std::to_string(mushroomBits(bedrockId, mushroomFaces)));
    // LavaCauldronBlock / PowderSnowCauldronBlock: one Bedrock cauldron holding the liquid
    if (javaId == "minecraft:lava_cauldron" && has("cauldron_liquid")) {
        set("cauldron_liquid", "lava");
        set("fill_level", "6");
    } else if (javaId == "minecraft:powder_snow_cauldron" && has("cauldron_liquid")) {
        set("cauldron_liquid", "powder_snow");
    }
    return out;
}

/// The Bedrock id of a Java block state (renames, property variants, double slabs, beds, pots);
/// `consumed`: the Java property the id carries (left out of the states).
std::string bedrockName(std::string const& javaId, Json const& props, std::string* consumed = nullptr) {
    bool hasProps = props.is_object() && !props.empty();
    if (hasProps) {
        for (auto const& v : kVariants)
            if (javaId == v.javaId && props.contains(v.property) && text(props[v.property]) == v.value && game::blockWith(v.bedrockId)) {
                if (consumed) *consumed = v.property;
                return v.bedrockId;
            }
    }
    std::string name = javaId;
    if (auto it = javaToBedrock().find(javaId); it != javaToBedrock().end()) {
        // none of the candidates exists: the Java id itself (Bedrock may have adopted it)
        for (auto const& c : it->second)
            if (game::blockWith(c)) {
                name = c;
                break;
            }
    } else if (isColouredBed(javaId)) {
        name = "minecraft:bed";
    } else if (javaId.starts_with("minecraft:potted_")) {
        name = "minecraft:flower_pot";
    }
    // SlabType.DOUBLE: Bedrock's <x>_double_slab
    if (hasProps && props.contains("type") && text(props["type"]) == "double" && endsWith(name, "_slab")) {
        std::string dbl = name.substr(0, name.size() - 5) + "_double_slab";
        if (game::blockWith(dbl)) {
            if (consumed) *consumed = "type";
            return dbl;
        }
    }
    return name;
}
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, Block const*>& cache() {
    static std::unordered_map<std::string, Block const*> c;
    return c;
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
    std::string  consumed;
    std::string  name = bedrockName(javaId, properties, &consumed);
    Block const* base = game::blockWith(name);
    Block const* out  = base;
    if (!base) {
        warnOnce("block " + javaId, "unknown block " + javaId + " (skipped)");
    } else if (properties.is_object() && !properties.empty()) {
        Json props = properties;
        if (!consumed.empty()) props.erase(consumed);
        Json states = mapProperties(javaId, name, props, slotsOf(*base));
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
    // TF's blocks are JSON blocks, which answer no survival rule of their own: their Java classes'
    if (auto rule = detail::survivalRule(block); rule != detail::Survival::Engine) return detail::survives(rule, region, pos, block);
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

/// The first class of a block's Java class chain (data/gf/import/skeleton.json) with a survival rule.
Survival ruleOfClasses(std::vector<std::string> const& chain) {
    static std::unordered_map<std::string_view, Survival> const rules{
        {"MushgloomBlock", Survival::Mushgloom},
        {"MossPatchBlock", Survival::SturdyBelow},
        {"FallenLeavesBlock", Survival::FallenLeaves},
        {"TorchberryPlantBlock", Survival::HangingPlant},
        {"RootStrandBlock", Survival::RootStrand},
        {"TrollRootBlock", Survival::TrollRoot},
        {"ThornRoseBlock", Survival::ThornRose},
        {"WaterlilyBlock", Survival::Waterlily},
        {"DarkTowerBerryBushBlock", Survival::DarkTowerBerryBush},
        {"BerryBushBlock", Survival::BerryBush},
        {"OreBerryBlock", Survival::OreBerry},
        {"MushroomBlock", Survival::Mushroom},
        {"SaplingBlock", Survival::Bush},
        {"BushBlock", Survival::Bush},
    };
    for (auto const& cls : chain)
        if (auto it = rules.find(cls); it != rules.end()) return it->second;
    return Survival::Engine;
}

BlockPos offset(BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

/// Block#isFaceSturdy(FULL) of the face `face` (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
bool sturdyFace(Block const& block, int face) { return !block.isAir() && block.canProvideFullSupport(static_cast<uchar>(face)); }

/// Fluids.WATER (a water source) at a block.
bool waterSource(Block const& block) {
    if (block.getTypeName() != "minecraft:water" && block.getTypeName() != "minecraft:flowing_water") return false;
    auto depth = javaProperty(block, "liquid_depth");
    return !depth || *depth == "0";
}

/// BushBlock#mayPlaceOn: #minecraft:dirt or farmland.
bool bushSoil(Block const& below) { return kit::hasTag(below, "minecraft:dirt") || below.getTypeName() == "minecraft:farmland"; }

/// A TF block's facing (Bedrock "minecraft:facing_direction" / "twilightforest:facing"), "up" when none.
std::string facingOf(Block const& block) {
    auto states = game::statesOf(block);
    for (char const* k : {"minecraft:facing_direction", "twilightforest:facing", "facing"})
        if (auto it = states.find(k); it != states.end()) return it->second;
    return "up";
}

/// TFBushBlock#canSurvive's isSameMatureBush: the same bush below, at AGE >= MAX_AGE - 1 (2).
bool matureBushBelow(Block const& below, Block const& bush) {
    if (&below.getBlockType() != &bush.getBlockType()) return false;
    for (auto const& [k, v] : game::statesOf(below))
        if (k == "age" || k.ends_with(":age")) return std::atoi(v.c_str()) >= 2;
    return false;
}

} // namespace

Survival survivalRule(Block const& block) {
    auto const& id = block.getTypeName();
    if (id.starts_with("minecraft:")) return Survival::Engine; // vanilla blocks answer Block::canSurvive
    auto const& classes = data().blockClasses;
    auto        it      = classes.find(id);
    return it == classes.end() ? Survival::Engine : ruleOfClasses(it->second);
}

bool survives(Survival rule, Region& region, BlockPos const& pos, Block const& block) {
    auto const& below = region.getBlock(offset(pos, 0, -1, 0));
    auto const& above = region.getBlock(offset(pos, 0, 1, 0));
    switch (rule) {
    case Survival::Bush:
        return bushSoil(below);
    case Survival::SturdyBelow: // MossPatchBlock#mayPlaceOn
        return sturdyFace(below, 1);
    case Survival::FallenLeaves: // FallenLeavesBlock#canSurvive (Fluids.WATER: a source)
        return sturdyFace(below, 1) || waterSource(below);
    case Survival::Mushgloom: // MushgloomBlock#canSurvive
        return sturdyFace(below, 1) || below.getTypeName() == "twilightforest:uberous_soil";
    case Survival::Mushroom: // MushroomBlock#canSurvive (the brightness of a generation region is not known: below 13 assumed)
        return kit::hasTag(below, "minecraft:mushroom_grow_block") || kit::blocksMotion(below);
    case Survival::HangingPlant: // TFPlantBlock.canPlaceRootAt
        return kit::hasTag(above, "twilightforest:plants_hang_on");
    case Survival::RootStrand: // RootStrandBlock#canSurvive
        return kit::hasTag(above, "twilightforest:plants_hang_on") || &above.getBlockType() == &block.getBlockType();
    case Survival::TrollRoot: { // TrollRootBlock.canPlaceRootBelow(pos.above())
        auto const& a = above.getTypeName();
        return kit::hasTag(above, "minecraft:base_stone_overworld") || a == "twilightforest:trollvidr" || a == "twilightforest:trollber" ||
               a == "twilightforest:unripe_trollber";
    }
    case Survival::ThornRose: { // ThornRoseBlock#canSurvive: what it rests on (opposite its facing)
        static std::map<std::string, std::array<int, 4>> const dirs{{"down", {0, 1, 0, 0}},  {"up", {0, -1, 0, 1}},   {"north", {0, 0, 1, 2}},
                                                                    {"south", {0, 0, -1, 3}}, {"west", {1, 0, 0, 4}}, {"east", {-1, 0, 0, 5}}};
        auto it = dirs.find(facingOf(block));
        if (it == dirs.end()) it = dirs.find("up");
        auto const& d    = it->second;
        auto const& rest = region.getBlock(offset(pos, d[0], d[1], d[2]));
        auto const& r    = rest.getTypeName();
        return r == "twilightforest:brown_thorns" || r == "twilightforest:green_thorns" || sturdyFace(rest, d[3]);
    }
    case Survival::Waterlily: { // WaterlilyBlock#mayPlaceOn: water or ice below, no fluid here
        auto const& b   = below.getTypeName();
        bool        ice = b == "minecraft:ice" || b == "minecraft:frosted_ice";
        return (waterSource(below) || ice) && !kit::isFluid(region.getBlock(pos));
    }
    case Survival::BerryBush: // TFBushBlock#canSurvive with BerryBushBlock#canBePlacedAt
        return kit::hasTag(below, "twilightforest:tf_berry_bushes_survive") || matureBushBelow(below, block);
    case Survival::DarkTowerBerryBush:
        return kit::hasTag(below, "twilightforest:dark_tower_berry_bushes_survive") || matureBushBelow(below, block);
    case Survival::OreBerry: // OreBerryBlock (its brightness limit is not known in a generation region)
        return kit::hasTag(below, "twilightforest:oreberry_bushes_survive") || matureBushBelow(below, block);
    case Survival::Engine:
        break;
    }
    return true;
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
