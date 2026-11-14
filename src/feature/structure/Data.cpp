// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/custom/WoodPalettes.java (bootstrap)
//   src/main/java/gloamingforest/init/custom/StructureSpeleothemConfigs.java (bootstrap)
//   src/main/java/gloamingforest/data/custom/stalactites/StalactiteGenerator.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The gloaming/* registries the structure kit consumes, read from the mod's data tree
// (data/<ns>/gloaming/<registry>/<path>.json, imported from upstream by F1's import tool). The wood
// palettes, stalactites and speleothem settings fall back to the values of upstream's bootstrap /
// datagen code above when the imported data is not there, so the kit works on its own.
#include "feature/structure/Internal.h"

#include <filesystem>
#include <fstream>

#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::structure::data {

namespace {

namespace fs = std::filesystem;

// upstream's datagen output for the three small registries (same values, compact).
constexpr char kFallbackPalettes[] = R"({
"gloamingforest:canopy":{"banister":"gloamingforest:canopy_banister","button":"gloamingforest:canopy_button","fence":"gloamingforest:canopy_fence","gate":"gloamingforest:canopy_fence_gate","planks":"gloamingforest:canopy_planks","plate":"gloamingforest:canopy_pressure_plate","slab":"gloamingforest:canopy_slab","stairs":"gloamingforest:canopy_stairs"},
"gloamingforest:darkwood":{"banister":"gloamingforest:dark_banister","button":"gloamingforest:dark_button","fence":"gloamingforest:dark_fence","gate":"gloamingforest:dark_fence_gate","planks":"gloamingforest:dark_planks","plate":"gloamingforest:dark_pressure_plate","slab":"gloamingforest:dark_slab","stairs":"gloamingforest:dark_stairs"},
"gloamingforest:mangrove":{"banister":"gloamingforest:mangrove_banister","button":"gloamingforest:mangrove_button","fence":"gloamingforest:mangrove_fence","gate":"gloamingforest:mangrove_fence_gate","planks":"gloamingforest:mangrove_planks","plate":"gloamingforest:mangrove_pressure_plate","slab":"gloamingforest:mangrove_slab","stairs":"gloamingforest:mangrove_stairs"},
"gloamingforest:minewood":{"banister":"gloamingforest:mining_banister","button":"gloamingforest:mining_button","fence":"gloamingforest:mining_fence","gate":"gloamingforest:mining_fence_gate","planks":"gloamingforest:mining_planks","plate":"gloamingforest:mining_pressure_plate","slab":"gloamingforest:mining_slab","stairs":"gloamingforest:mining_stairs"},
"gloamingforest:sortwood":{"banister":"gloamingforest:sorting_banister","button":"gloamingforest:sorting_button","fence":"gloamingforest:sorting_fence","gate":"gloamingforest:sorting_fence_gate","planks":"gloamingforest:sorting_planks","plate":"gloamingforest:sorting_pressure_plate","slab":"gloamingforest:sorting_slab","stairs":"gloamingforest:sorting_stairs"},
"gloamingforest:timewood":{"banister":"gloamingforest:time_banister","button":"gloamingforest:time_button","fence":"gloamingforest:time_fence","gate":"gloamingforest:time_fence_gate","planks":"gloamingforest:time_planks","plate":"gloamingforest:time_pressure_plate","slab":"gloamingforest:time_slab","stairs":"gloamingforest:time_stairs"},
"gloamingforest:transwood":{"banister":"gloamingforest:transformation_banister","button":"gloamingforest:transformation_button","fence":"gloamingforest:transformation_fence","gate":"gloamingforest:transformation_fence_gate","planks":"gloamingforest:transformation_planks","plate":"gloamingforest:transformation_pressure_plate","slab":"gloamingforest:transformation_slab","stairs":"gloamingforest:transformation_stairs"},
"gloamingforest:gloaming_oak":{"banister":"gloamingforest:gloaming_oak_banister","button":"gloamingforest:gloaming_oak_button","fence":"gloamingforest:gloaming_oak_fence","gate":"gloamingforest:gloaming_oak_fence_gate","planks":"gloamingforest:gloaming_oak_planks","plate":"gloamingforest:gloaming_oak_pressure_plate","slab":"gloamingforest:gloaming_oak_slab","stairs":"gloamingforest:gloaming_oak_stairs"},
"minecraft:acacia":{"banister":"gloamingforest:acacia_banister","button":"minecraft:acacia_button","fence":"minecraft:acacia_fence","gate":"minecraft:acacia_fence_gate","planks":"minecraft:acacia_planks","plate":"minecraft:acacia_pressure_plate","slab":"minecraft:acacia_slab","stairs":"minecraft:acacia_stairs"},
"minecraft:birch":{"banister":"gloamingforest:birch_banister","button":"minecraft:birch_button","fence":"minecraft:birch_fence","gate":"minecraft:birch_fence_gate","planks":"minecraft:birch_planks","plate":"minecraft:birch_pressure_plate","slab":"minecraft:birch_slab","stairs":"minecraft:birch_stairs"},
"minecraft:crimson":{"banister":"gloamingforest:crimson_banister","button":"minecraft:crimson_button","fence":"minecraft:crimson_fence","gate":"minecraft:crimson_fence_gate","planks":"minecraft:crimson_planks","plate":"minecraft:crimson_pressure_plate","slab":"minecraft:crimson_slab","stairs":"minecraft:crimson_stairs"},
"minecraft:dark_oak":{"banister":"gloamingforest:dark_oak_banister","button":"minecraft:dark_oak_button","fence":"minecraft:dark_oak_fence","gate":"minecraft:dark_oak_fence_gate","planks":"minecraft:dark_oak_planks","plate":"minecraft:dark_oak_pressure_plate","slab":"minecraft:dark_oak_slab","stairs":"minecraft:dark_oak_stairs"},
"minecraft:jungle":{"banister":"gloamingforest:jungle_banister","button":"minecraft:jungle_button","fence":"minecraft:jungle_fence","gate":"minecraft:jungle_fence_gate","planks":"minecraft:jungle_planks","plate":"minecraft:jungle_pressure_plate","slab":"minecraft:jungle_slab","stairs":"minecraft:jungle_stairs"},
"minecraft:mangrove":{"banister":"gloamingforest:vangrove_banister","button":"minecraft:mangrove_button","fence":"minecraft:mangrove_fence","gate":"minecraft:mangrove_fence_gate","planks":"minecraft:mangrove_planks","plate":"minecraft:mangrove_pressure_plate","slab":"minecraft:mangrove_slab","stairs":"minecraft:mangrove_stairs"},
"minecraft:oak":{"banister":"gloamingforest:oak_banister","button":"minecraft:oak_button","fence":"minecraft:oak_fence","gate":"minecraft:oak_fence_gate","planks":"minecraft:oak_planks","plate":"minecraft:oak_pressure_plate","slab":"minecraft:oak_slab","stairs":"minecraft:oak_stairs"},
"minecraft:spruce":{"banister":"gloamingforest:spruce_banister","button":"minecraft:spruce_button","fence":"minecraft:spruce_fence","gate":"minecraft:spruce_fence_gate","planks":"minecraft:spruce_planks","plate":"minecraft:spruce_pressure_plate","slab":"minecraft:spruce_slab","stairs":"minecraft:spruce_stairs"},
"minecraft:warped":{"banister":"gloamingforest:warped_banister","button":"minecraft:warped_button","fence":"minecraft:warped_fence","gate":"minecraft:warped_fence_gate","planks":"minecraft:warped_planks","plate":"minecraft:warped_pressure_plate","slab":"minecraft:warped_slab","stairs":"minecraft:warped_stairs"}
})";

constexpr char kFallbackStalactites[] = R"({
"gloamingforest:hydra_lair":{"type":"hydra_lair","base_stalactites":[],"ore_chance":1.0,"ore_stalactites":["gloamingforest:entries/iron_stalactite","gloamingforest:entries/coal_stalactite","gloamingforest:entries/copper_stalactite","gloamingforest:entries/glowstone_stalactite","gloamingforest:entries/gold_stalactite","gloamingforest:entries/redstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.0625,"stalagmites":["gloamingforest:entries/hill_stone_stalactite"]},
"gloamingforest:large_hollow_hill":{"type":"large_hollow_hill","base_stalactites":["gloamingforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["gloamingforest:entries/iron_stalactite","gloamingforest:entries/coal_stalactite","gloamingforest:entries/copper_stalactite","gloamingforest:entries/glowstone_stalactite","gloamingforest:entries/gold_stalactite","gloamingforest:entries/redstone_stalactite","gloamingforest:entries/diamond_stalactite","gloamingforest:entries/lapis_stalactite","gloamingforest:entries/emerald_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["gloamingforest:entries/hill_stone_stalactite"]},
"gloamingforest:medium_hollow_hill":{"type":"medium_hollow_hill","base_stalactites":["gloamingforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["gloamingforest:entries/iron_stalactite","gloamingforest:entries/coal_stalactite","gloamingforest:entries/copper_stalactite","gloamingforest:entries/glowstone_stalactite","gloamingforest:entries/gold_stalactite","gloamingforest:entries/redstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["gloamingforest:entries/hill_stone_stalactite"]},
"gloamingforest:small_hollow_hill":{"type":"small_hollow_hill","base_stalactites":["gloamingforest:entries/hill_stone_stalactite"],"ore_chance":0.2,"ore_stalactites":["gloamingforest:entries/iron_stalactite","gloamingforest:entries/coal_stalactite","gloamingforest:entries/copper_stalactite","gloamingforest:entries/glowstone_stalactite"],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.8,"stalagmites":["gloamingforest:entries/hill_stone_stalactite"]},
"gloamingforest:troll_cave":{"type":"troll_cave","base_stalactites":["gloamingforest:entries/hill_stone_stalactite"],"ore_chance":0.0,"ore_stalactites":[],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.25,"stalagmites":["gloamingforest:entries/hill_stone_stalactite"]},
"gloamingforest:yeti_cave":{"type":"yeti_cave","base_stalactites":["gloamingforest:entries/ice_stalactite","gloamingforest:entries/packed_ice_stalactite","gloamingforest:entries/blue_ice_stalactite"],"ore_chance":0.0,"ore_stalactites":[],"replace":false,"stalactite_chance":1.0,"stalagmite_chance":0.0,"stalagmites":[]},
"gloamingforest:entries/blue_ice_stalactite":{"max_length":8,"ores":"minecraft:blue_ice","size_variation":1.0,"weight":1},
"gloamingforest:entries/coal_stalactite":{"max_length":12,"ores":"minecraft:coal_ore","size_variation":0.8,"weight":30},
"gloamingforest:entries/copper_stalactite":{"max_length":7,"ores":"minecraft:copper_ore","size_variation":0.6,"weight":25},
"gloamingforest:entries/diamond_stalactite":{"max_length":4,"ores":"minecraft:diamond_ore","size_variation":0.5,"weight":8},
"gloamingforest:entries/emerald_stalactite":{"max_length":3,"ores":"minecraft:emerald_ore","size_variation":0.5,"weight":8},
"gloamingforest:entries/glowstone_stalactite":{"max_length":8,"ores":"minecraft:glowstone","size_variation":0.5,"weight":20},
"gloamingforest:entries/gold_stalactite":{"max_length":6,"ores":"minecraft:gold_ore","size_variation":0.6,"weight":10},
"gloamingforest:entries/hill_stone_stalactite":{"max_length":11,"ores":"minecraft:stone","size_variation":0.25,"weight":1},
"gloamingforest:entries/ice_stalactite":{"max_length":10,"ores":"minecraft:ice","size_variation":0.6,"weight":1},
"gloamingforest:entries/iron_stalactite":{"max_length":8,"ores":"minecraft:iron_ore","size_variation":0.7,"weight":40},
"gloamingforest:entries/lapis_stalactite":{"max_length":8,"ores":"minecraft:lapis_ore","size_variation":0.8,"weight":12},
"gloamingforest:entries/packed_ice_stalactite":{"max_length":9,"ores":"minecraft:packed_ice","size_variation":0.5,"weight":1},
"gloamingforest:entries/redstone_stalactite":{"max_length":8,"ores":"minecraft:redstone_ore","size_variation":0.8,"weight":20}
})";

constexpr char kFallbackSpeleothemSettings[] = R"({
"gloamingforest:hydra_lair":{"type":"hydra_lair","lattice":{"spacing":4.5,"x_offset":3.897186,"x_spacing":7.794372,"z_offset":2.2498753,"z_spacing":4.5}},
"gloamingforest:large_hollow_hill":{"type":"large_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"gloamingforest:medium_hollow_hill":{"type":"medium_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"gloamingforest:small_hollow_hill":{"type":"small_hollow_hill","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}},
"gloamingforest:troll_cave":{"type":"troll_cave","lattice":{"spacing":4.5,"x_offset":3.897186,"x_spacing":7.794372,"z_offset":2.2498753,"z_spacing":4.5}},
"gloamingforest:yeti_cave":{"type":"yeti_cave","lattice":{"spacing":3.5,"x_offset":3.0311449,"x_spacing":6.0622897,"z_offset":1.7499031,"z_spacing":3.5}}
})";

std::map<std::string, Json> fallback(char const* text) {
    std::map<std::string, Json> out;
    Json doc = Json::parse(text); // items() must not outlive the document
    for (auto const& [k, v] : doc.items()) out[k] = v;
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
    auto woods = loadDir("gloaming/wood_palettes");
    if (woods.empty()) woods = fallback(kFallbackPalettes);
    palettes::load(woods);

    auto stal     = loadDir("gloaming/stalactites");
    auto settings = loadDir("gloaming/structure_speleothem_settings");
    if (stal.empty()) stal = fallback(kFallbackStalactites);
    if (settings.empty()) settings = fallback(kFallbackSpeleothemSettings);
    speleothems::load(stal, settings);

    auto defs         = loadDir("gloaming/template_definition");
    gDefinitionFiles  = defs.size();
    pools::rebuild(defs);

    templates::scan();
    log().info("structure kit: {} wood palettes, {} speleothem configs, {} template definitions, {} templates", palettes::ids().size(),
               speleothems::ids().size(), gDefinitionFiles, templates::ids().size());
}

} // namespace gf::structure::data
