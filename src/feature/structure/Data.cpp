// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/custom/WoodPalettes.java (bootstrap)
//   src/main/java/twilightforest/init/custom/StructureSpeleothemConfigs.java (bootstrap)
//   src/main/java/twilightforest/data/custom/stalactites/StalactiteGenerator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The twilight/* registries the structure kit consumes, read from the mod's data tree
// (data/<ns>/twilight/<registry>/<path>.json, imported from TF by F1's import tool). The wood
// palettes, stalactites and speleothem settings fall back to the values of TF's bootstrap /
// datagen code above when the imported data is not there, so the kit works on its own.
#include "feature/structure/Internal.h"

#include <filesystem>
#include <fstream>

#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::structure::data {

namespace {

namespace fs = std::filesystem;

// TF's datagen output for the three small registries (same values, compact).
constexpr char kFallbackPalettes[] = R"({
"twilightforest:canopy":{"banister":"twilightforest:canopy_banister","button":"twilightforest:canopy_button","fence":"twilightforest:canopy_fence","gate":"twilightforest:canopy_fence_gate","planks":"twilightforest:canopy_planks","plate":"twilightforest:canopy_pressure_plate","slab":"twilightforest:canopy_slab","stairs":"twilightforest:canopy_stairs"},
"twilightforest:darkwood":{"banister":"twilightforest:dark_banister","button":"twilightforest:dark_button","fence":"twilightforest:dark_fence","gate":"twilightforest:dark_fence_gate","planks":"twilightforest:dark_planks","plate":"twilightforest:dark_pressure_plate","slab":"twilightforest:dark_slab","stairs":"twilightforest:dark_stairs"},
"twilightforest:mangrove":{"banister":"twilightforest:mangrove_banister","button":"twilightforest:mangrove_button","fence":"twilightforest:mangrove_fence","gate":"twilightforest:mangrove_fence_gate","planks":"twilightforest:mangrove_planks","plate":"twilightforest:mangrove_pressure_plate","slab":"twilightforest:mangrove_slab","stairs":"twilightforest:mangrove_stairs"},
"twilightforest:minewood":{"banister":"twilightforest:mining_banister","button":"twilightforest:mining_button","fence":"twilightforest:mining_fence","gate":"twilightforest:mining_fence_gate","planks":"twilightforest:mining_planks","plate":"twilightforest:mining_pressure_plate","slab":"twilightforest:mining_slab","stairs":"twilightforest:mining_stairs"},
"twilightforest:sortwood":{"banister":"twilightforest:sorting_banister","button":"twilightforest:sorting_button","fence":"twilightforest:sorting_fence","gate":"twilightforest:sorting_fence_gate","planks":"twilightforest:sorting_planks","plate":"twilightforest:sorting_pressure_plate","slab":"twilightforest:sorting_slab","stairs":"twilightforest:sorting_stairs"},
"twilightforest:timewood":{"banister":"twilightforest:time_banister","button":"twilightforest:time_button","fence":"twilightforest:time_fence","gate":"twilightforest:time_fence_gate","planks":"twilightforest:time_planks","plate":"twilightforest:time_pressure_plate","slab":"twilightforest:time_slab","stairs":"twilightforest:time_stairs"},
"twilightforest:transwood":{"banister":"twilightforest:transformation_banister","button":"twilightforest:transformation_button","fence":"twilightforest:transformation_fence","gate":"twilightforest:transformation_fence_gate","planks":"twilightforest:transformation_planks","plate":"twilightforest:transformation_pressure_plate","slab":"twilightforest:transformation_slab","stairs":"twilightforest:transformation_stairs"},
"twilightforest:twilight_oak":{"banister":"twilightforest:twilight_oak_banister","button":"twilightforest:twilight_oak_button","fence":"twilightforest:twilight_oak_fence","gate":"twilightforest:twilight_oak_fence_gate","planks":"twilightforest:twilight_oak_planks","plate":"twilightforest:twilight_oak_pressure_plate","slab":"twilightforest:twilight_oak_slab","stairs":"twilightforest:twilight_oak_stairs"},
"minecraft:acacia":{"banister":"twilightforest:acacia_banister","button":"minecraft:acacia_button","fence":"minecraft:acacia_fence","gate":"minecraft:acacia_fence_gate","planks":"minecraft:acacia_planks","plate":"minecraft:acacia_pressure_plate","slab":"minecraft:acacia_slab","stairs":"minecraft:acacia_stairs"},
"minecraft:birch":{"banister":"twilightforest:birch_banister","button":"minecraft:birch_button","fence":"minecraft:birch_fence","gate":"minecraft:birch_fence_gate","planks":"minecraft:birch_planks","plate":"minecraft:birch_pressure_plate","slab":"minecraft:birch_slab","stairs":"minecraft:birch_stairs"},
"minecraft:crimson":{"banister":"twilightforest:crimson_banister","button":"minecraft:crimson_button","fence":"minecraft:crimson_fence","gate":"minecraft:crimson_fence_gate","planks":"minecraft:crimson_planks","plate":"minecraft:crimson_pressure_plate","slab":"minecraft:crimson_slab","stairs":"minecraft:crimson_stairs"},
"minecraft:dark_oak":{"banister":"twilightforest:dark_oak_banister","button":"minecraft:dark_oak_button","fence":"minecraft:dark_oak_fence","gate":"minecraft:dark_oak_fence_gate","planks":"minecraft:dark_oak_planks","plate":"minecraft:dark_oak_pressure_plate","slab":"minecraft:dark_oak_slab","stairs":"minecraft:dark_oak_stairs"},
"minecraft:jungle":{"banister":"twilightforest:jungle_banister","button":"minecraft:jungle_button","fence":"minecraft:jungle_fence","gate":"minecraft:jungle_fence_gate","planks":"minecraft:jungle_planks","plate":"minecraft:jungle_pressure_plate","slab":"minecraft:jungle_slab","stairs":"minecraft:jungle_stairs"},
"minecraft:mangrove":{"banister":"twilightforest:vangrove_banister","button":"minecraft:mangrove_button","fence":"minecraft:mangrove_fence","gate":"minecraft:mangrove_fence_gate","planks":"minecraft:mangrove_planks","plate":"minecraft:mangrove_pressure_plate","slab":"minecraft:mangrove_slab","stairs":"minecraft:mangrove_stairs"},
"minecraft:oak":{"banister":"twilightforest:oak_banister","button":"minecraft:oak_button","fence":"minecraft:oak_fence","gate":"minecraft:oak_fence_gate","planks":"minecraft:oak_planks","plate":"minecraft:oak_pressure_plate","slab":"minecraft:oak_slab","stairs":"minecraft:oak_stairs"},
"minecraft:spruce":{"banister":"twilightforest:spruce_banister","button":"minecraft:spruce_button","fence":"minecraft:spruce_fence","gate":"minecraft:spruce_fence_gate","planks":"minecraft:spruce_planks","plate":"minecraft:spruce_pressure_plate","slab":"minecraft:spruce_slab","stairs":"minecraft:spruce_stairs"},
"minecraft:warped":{"banister":"twilightforest:warped_banister","button":"minecraft:warped_button","fence":"minecraft:warped_fence","gate":"minecraft:warped_fence_gate","planks":"minecraft:warped_planks","plate":"minecraft:warped_pressure_plate","slab":"minecraft:warped_slab","stairs":"minecraft:warped_stairs"}
})";

constexpr char kFallbackStalactites[] = R"({
"twilightforest:hydra_lair":{"type":"hydra_lair","base_stalactites":[],"ore_chance":1.0,"ore_stalactites":["twilightforest:entries/iron_stalactite","twilightforest:entries/coal_stalactite","twilightforest:entries/copper_stalactite","twilightforest:entries/glowstone_stalactite","twilightforest:entries/gold_stalactite","twilightforest:entries/redstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.0625,"stalagmites":["twilightforest:entries/hill_stone_stalactite"]},
"twilightforest:large_hollow_hill":{"type":"large_hollow_hill","base_stalactites":["twilightforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["twilightforest:entries/iron_stalactite","twilightforest:entries/coal_stalactite","twilightforest:entries/copper_stalactite","twilightforest:entries/glowstone_stalactite","twilightforest:entries/gold_stalactite","twilightforest:entries/redstone_stalactite","twilightforest:entries/diamond_stalactite","twilightforest:entries/lapis_stalactite","twilightforest:entries/emerald_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["twilightforest:entries/hill_stone_stalactite"]},
"twilightforest:medium_hollow_hill":{"type":"medium_hollow_hill","base_stalactites":["twilightforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["twilightforest:entries/iron_stalactite","twilightforest:entries/coal_stalactite","twilightforest:entries/copper_stalactite","twilightforest:entries/glowstone_stalactite","twilightforest:entries/gold_stalactite","twilightforest:entries/redstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["twilightforest:entries/hill_stone_stalactite"]},
"twilightforest:small_hollow_hill":{"type":"small_hollow_hill","base_stalactites":["twilightforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["twilightforest:entries/iron_stalactite","twilightforest:entries/coal_stalactite","twilightforest:entries/copper_stalactite","twilightforest:entries/glowstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["twilightforest:entries/hill_stone_stalactite"]},
"twilightforest:troll_cave":{"type":"troll_cave","base_stalactites":["twilightforest:entries/hill_stone_stalactite"],"ore_chance":0.0,"ore_stalactites":[],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.25,"stalagmites":["twilightforest:entries/hill_stone_stalactite"]},
"twilightforest:yeti_cave":{"type":"yeti_cave","base_stalactites":["twilightforest:entries/ice_stalactite","twilightforest:entries/packed_ice_stalactite","twilightforest:entries/blue_ice_stalactite"],"ore_chance":0.0,"ore_stalactites":[],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.0,"stalagmites":[]},
"twilightforest:entries/blue_ice_stalactite":{"max_length":8,"ores":"minecraft:blue_ice","size_variation":1.0,"weight":1},
"twilightforest:entries/coal_stalactite":{"max_length":12,"ores":"minecraft:coal_ore","size_variation":0.8,"weight":30},
"twilightforest:entries/copper_stalactite":{"max_length":7,"ores":"minecraft:copper_ore","size_variation":0.6,"weight":25},
"twilightforest:entries/diamond_stalactite":{"max_length":4,"ores":"minecraft:diamond_ore","size_variation":0.5,"weight":8},
"twilightforest:entries/emerald_stalactite":{"max_length":3,"ores":"minecraft:emerald_ore","size_variation":0.5,"weight":8},
"twilightforest:entries/glowstone_stalactite":{"max_length":8,"ores":"minecraft:glowstone","size_variation":0.5,"weight":20},
"twilightforest:entries/gold_stalactite":{"max_length":6,"ores":"minecraft:gold_ore","size_variation":0.6,"weight":10},
"twilightforest:entries/hill_stone_stalactite":{"max_length":11,"ores":"minecraft:stone","size_variation":0.25,"weight":1},
"twilightforest:entries/ice_stalactite":{"max_length":10,"ores":"minecraft:ice","size_variation":0.6,"weight":1},
"twilightforest:entries/iron_stalactite":{"max_length":8,"ores":"minecraft:iron_ore","size_variation":0.7,"weight":40},
"twilightforest:entries/lapis_stalactite":{"max_length":8,"ores":"minecraft:lapis_ore","size_variation":0.8,"weight":12},
"twilightforest:entries/packed_ice_stalactite":{"max_length":9,"ores":"minecraft:packed_ice","size_variation":0.5,"weight":1},
"twilightforest:entries/redstone_stalactite":{"max_length":8,"ores":"minecraft:redstone_ore","size_variation":0.8,"weight":20}
})";

constexpr char kFallbackSpeleothemSettings[] = R"({
"twilightforest:hydra_lair":{"type":"hydra_lair","lattice":{"spacing":4.5,"x_offset":3.897186,"x_spacing":7.794372,"z_offset":2.2498753,"z_spacing":4.5}},
"twilightforest:large_hollow_hill":{"type":"large_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"twilightforest:medium_hollow_hill":{"type":"medium_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"twilightforest:small_hollow_hill":{"type":"small_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"twilightforest:troll_cave":{"type":"troll_cave","lattice":{"spacing":4.5,"x_offset":3.897186,"x_spacing":7.794372,"z_offset":2.2498753,"z_spacing":4.5}},
"twilightforest:yeti_cave":{"type":"yeti_cave","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}}
})";

std::map<std::string, Json> fallback(char const* text) {
    std::map<std::string, Json> out;
    for (auto const& [k, v] : Json::parse(text).items()) out[k] = v;
    return out;
}

std::size_t gDefinitionFiles = 0;

} // namespace

std::string root() { return (Mod::getInstance().getSelf().getModDir() / "data").string(); }

std::map<std::string, Json> loadDir(std::string const& dir) {
    std::map<std::string, Json> out;
    fs::path                    base(root());
    std::error_code             ec;
    if (!fs::is_directory(base, ec)) return out;
    for (auto const& ns : fs::directory_iterator(base, ec)) {
        if (!ns.is_directory()) continue;
        fs::path d = ns.path() / fs::path(dir);
        if (!fs::is_directory(d, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(d, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file() || it->path().extension() != ".json") continue;
            auto rel = fs::relative(it->path(), d, ec).generic_string();
            rel      = rel.substr(0, rel.size() - 5);
            try {
                std::ifstream in(it->path());
                out[ns.path().filename().string() + ":" + rel] = Json::parse(in);
            } catch (std::exception const& e) {
                warnOnce("data:" + it->path().string(), "structure: unreadable data " + it->path().string() + ": " + e.what());
            }
        }
    }
    return out;
}

std::optional<Json> load(std::string const& dir, std::string const& id) {
    auto        colon = id.find(':');
    std::string ns    = colon == std::string::npos ? "minecraft" : id.substr(0, colon);
    std::string path  = colon == std::string::npos ? id : id.substr(colon + 1);
    fs::path    file  = fs::path(root()) / ns / fs::path(dir) / (path + ".json");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    try {
        std::ifstream in(file);
        return Json::parse(in);
    } catch (std::exception const& e) {
        warnOnce("data:" + file.string(), "structure: unreadable data " + file.string() + ": " + e.what());
        return std::nullopt;
    }
}

void reload() {
    blocks::clearCaches();
    auto woods = loadDir("twilight/wood_palettes");
    if (woods.empty()) woods = fallback(kFallbackPalettes);
    palettes::load(woods);

    auto stal     = loadDir("twilight/stalactites");
    auto settings = loadDir("twilight/structure_speleothem_settings");
    if (stal.empty()) stal = fallback(kFallbackStalactites);
    if (settings.empty()) settings = fallback(kFallbackSpeleothemSettings);
    speleothems::load(stal, settings);

    auto defs         = loadDir("twilight/template_definition");
    gDefinitionFiles  = defs.size();
    pools::rebuild(defs);

    templates::scan();
    log().info("structure kit: {} wood palettes, {} speleothem configs, {} template definitions, {} templates", palettes::ids().size(),
               speleothems::ids().size(), gDefinitionFiles, templates::ids().size());
}

} // namespace gf::structure::data
