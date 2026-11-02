// Java -> Bedrock ids of the vanilla content in TF's data (JavaIds.h).
#include "feature/tags/JavaIds.h"

#include <algorithm>
#include <unordered_map>

namespace gf::tags::javaids {

namespace {

using Table = std::unordered_map<std::string, std::vector<std::string>>;

/// Blocks whose Bedrock id differs (1.21 / 26.x names; a Java block with several Bedrock blocks
/// lists them all: lit / unlit variants, source / flowing fluids, the potted plants' flower pot).
Table const& blocks() {
    static Table const t = [] {
        Table m{
            {"minecraft:dirt_path", {"minecraft:grass_path"}},
            {"minecraft:snow", {"minecraft:snow_layer"}},
            {"minecraft:snow_block", {"minecraft:snow"}},
            {"minecraft:dead_bush", {"minecraft:deadbush"}},
            {"minecraft:sugar_cane", {"minecraft:reeds"}},
            {"minecraft:lily_pad", {"minecraft:waterlily"}},
            {"minecraft:cobweb", {"minecraft:web"}},
            {"minecraft:rooted_dirt", {"minecraft:dirt_with_roots"}},
            {"minecraft:magma_block", {"minecraft:magma"}},
            {"minecraft:spawner", {"minecraft:mob_spawner"}},
            {"minecraft:note_block", {"minecraft:noteblock"}},
            {"minecraft:melon", {"minecraft:melon_block"}},
            {"minecraft:jack_o_lantern", {"minecraft:lit_pumpkin"}},
            {"minecraft:terracotta", {"minecraft:hardened_clay"}},
            {"minecraft:slime_block", {"minecraft:slime"}},
            {"minecraft:nether_portal", {"minecraft:portal"}},
            {"minecraft:end_stone_bricks", {"minecraft:end_bricks"}},
            {"minecraft:nether_bricks", {"minecraft:nether_brick"}},
            {"minecraft:red_nether_bricks", {"minecraft:red_nether_brick"}},
            {"minecraft:bricks", {"minecraft:brick_block"}},
            {"minecraft:nether_quartz_ore", {"minecraft:quartz_ore"}},
            {"minecraft:powered_rail", {"minecraft:golden_rail"}},
            {"minecraft:tripwire", {"minecraft:trip_wire"}},
            {"minecraft:cave_air", {"minecraft:air"}},
            {"minecraft:void_air", {"minecraft:air"}},
            {"minecraft:wall_torch", {"minecraft:torch"}},
            {"minecraft:soul_wall_torch", {"minecraft:soul_torch"}},
            {"minecraft:redstone_torch", {"minecraft:redstone_torch", "minecraft:unlit_redstone_torch"}},
            {"minecraft:redstone_wall_torch", {"minecraft:redstone_torch", "minecraft:unlit_redstone_torch"}},
            {"minecraft:redstone_lamp", {"minecraft:redstone_lamp", "minecraft:lit_redstone_lamp"}},
            {"minecraft:redstone_ore", {"minecraft:redstone_ore", "minecraft:lit_redstone_ore"}},
            {"minecraft:deepslate_redstone_ore", {"minecraft:deepslate_redstone_ore", "minecraft:lit_deepslate_redstone_ore"}},
            {"minecraft:furnace", {"minecraft:furnace", "minecraft:lit_furnace"}},
            {"minecraft:blast_furnace", {"minecraft:blast_furnace", "minecraft:lit_blast_furnace"}},
            {"minecraft:smoker", {"minecraft:smoker", "minecraft:lit_smoker"}},
            {"minecraft:repeater", {"minecraft:unpowered_repeater", "minecraft:powered_repeater"}},
            {"minecraft:comparator", {"minecraft:unpowered_comparator", "minecraft:powered_comparator"}},
            {"minecraft:daylight_detector", {"minecraft:daylight_detector", "minecraft:daylight_detector_inverted"}},
            {"minecraft:water", {"minecraft:water", "minecraft:flowing_water"}},
            {"minecraft:lava", {"minecraft:lava", "minecraft:flowing_lava"}},
            {"minecraft:moving_piston", {"minecraft:moving_block"}},
            {"minecraft:piston_head", {"minecraft:piston_arm_collision"}},
            {"minecraft:sticky_piston_head", {"minecraft:sticky_piston_arm_collision"}},
            {"minecraft:attached_melon_stem", {"minecraft:melon_stem"}},
            {"minecraft:attached_pumpkin_stem", {"minecraft:pumpkin_stem"}},
            {"minecraft:big_dripleaf_stem", {"minecraft:big_dripleaf"}},
            {"minecraft:frogspawn", {"minecraft:frog_spawn"}},
            {"minecraft:tall_seagrass", {"minecraft:seagrass"}},
            {"minecraft:kelp_plant", {"minecraft:kelp"}},
            {"minecraft:weeping_vines_plant", {"minecraft:weeping_vines"}},
            {"minecraft:twisting_vines_plant", {"minecraft:twisting_vines"}},
            {"minecraft:cave_vines", {"minecraft:cave_vines", "minecraft:cave_vines_head_with_berries"}},
            {"minecraft:cave_vines_plant", {"minecraft:cave_vines_body_with_berries", "minecraft:cave_vines"}},
            {"minecraft:chain", {"minecraft:iron_chain"}}, // renamed in Bedrock 1.21.110 (and Java 1.21.9)
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
            {"minecraft:small_dripleaf", {"minecraft:small_dripleaf_block"}},
            // (minecraft:light_block is only an alias of light_block_0 in 26.51: the flattened levels)
            {"minecraft:light", {"minecraft:light_block_0", "minecraft:light_block_1", "minecraft:light_block_2",
                                 "minecraft:light_block_3", "minecraft:light_block_4", "minecraft:light_block_5", "minecraft:light_block_6",
                                 "minecraft:light_block_7", "minecraft:light_block_8", "minecraft:light_block_9", "minecraft:light_block_10",
                                 "minecraft:light_block_11", "minecraft:light_block_12", "minecraft:light_block_13", "minecraft:light_block_14",
                                 "minecraft:light_block_15"}},
            {"minecraft:skeleton_wall_skull", {"minecraft:skeleton_skull"}},
            {"minecraft:wither_skeleton_wall_skull", {"minecraft:wither_skeleton_skull"}},
            {"minecraft:zombie_wall_head", {"minecraft:zombie_head"}},
            {"minecraft:creeper_wall_head", {"minecraft:creeper_head"}},
            {"minecraft:player_wall_head", {"minecraft:player_head"}},
            {"minecraft:dragon_wall_head", {"minecraft:dragon_head"}},
            {"minecraft:piglin_wall_head", {"minecraft:piglin_head"}},
        };
        for (auto const* wood : {"spruce", "birch", "jungle", "acacia", "mangrove", "cherry", "bamboo", "crimson", "warped", "pale_oak"})
            m[std::string("minecraft:") + wood + "_sign"] = {std::string("minecraft:") + wood + "_standing_sign"};
        for (auto const* color : {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple",
                                  "blue", "brown", "green", "red", "black"})
            m[std::string("minecraft:") + color + "_bed"] = {"minecraft:bed"};
        for (auto const* plant : {"oak_sapling", "spruce_sapling", "birch_sapling", "jungle_sapling", "acacia_sapling", "dark_oak_sapling",
                                  "cherry_sapling", "mangrove_propagule", "fern", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet",
                                  "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley",
                                  "wither_rose", "torchflower", "red_mushroom", "brown_mushroom", "dead_bush", "cactus", "bamboo",
                                  "crimson_fungus", "warped_fungus", "crimson_roots", "warped_roots", "azalea_bush", "flowering_azalea_bush",
                                  "pale_oak_sapling", "closed_eyeblossom", "open_eyeblossom"})
            m[std::string("minecraft:potted_") + plant] = {"minecraft:flower_pot"};
        return m;
    }();
    return t;
}

/// Items whose Bedrock id differs (block items follow their block where Bedrock's item has the
/// block's name; a few plain items have names of their own). A Java item that Bedrock spells as
/// another item with a data value maps to that item (the value, Java's components, stays in the data).
Table const& items() {
    static Table const t{
        {"minecraft:nether_brick", {"minecraft:netherbrick"}},
        {"minecraft:nether_bricks", {"minecraft:nether_brick"}},
        {"minecraft:red_nether_bricks", {"minecraft:red_nether_brick"}},
        {"minecraft:snow", {"minecraft:snow_layer"}},
        {"minecraft:snow_block", {"minecraft:snow"}},
        {"minecraft:dirt_path", {"minecraft:grass_path"}},
        {"minecraft:magma_block", {"minecraft:magma"}},
        {"minecraft:lily_pad", {"minecraft:waterlily"}},
        {"minecraft:cobweb", {"minecraft:web"}},
        {"minecraft:dead_bush", {"minecraft:deadbush"}},
        {"minecraft:melon", {"minecraft:melon_block"}},
        {"minecraft:jack_o_lantern", {"minecraft:lit_pumpkin"}},
        {"minecraft:terracotta", {"minecraft:hardened_clay"}},
        {"minecraft:slime_block", {"minecraft:slime"}},
        {"minecraft:spawner", {"minecraft:mob_spawner"}},
        {"minecraft:note_block", {"minecraft:noteblock"}},
        {"minecraft:end_stone_bricks", {"minecraft:end_bricks"}},
        {"minecraft:bricks", {"minecraft:brick_block"}},
        {"minecraft:nether_quartz_ore", {"minecraft:quartz_ore"}},
        {"minecraft:powered_rail", {"minecraft:golden_rail"}},
        {"minecraft:rooted_dirt", {"minecraft:dirt_with_roots"}},
        {"minecraft:oak_door", {"minecraft:wooden_door"}},
        {"minecraft:oak_trapdoor", {"minecraft:trapdoor"}},
        {"minecraft:oak_fence_gate", {"minecraft:fence_gate"}},
        {"minecraft:oak_button", {"minecraft:wooden_button"}},
        {"minecraft:oak_pressure_plate", {"minecraft:wooden_pressure_plate"}},
        {"minecraft:shulker_box", {"minecraft:undyed_shulker_box"}},
        {"minecraft:chain", {"minecraft:iron_chain"}},
        {"minecraft:stone_stairs", {"minecraft:normal_stone_stairs"}},
        {"minecraft:cobblestone_stairs", {"minecraft:stone_stairs"}},
        {"minecraft:stone_slab", {"minecraft:normal_stone_slab"}},
        {"minecraft:item_frame", {"minecraft:frame"}},
        {"minecraft:glow_item_frame", {"minecraft:glow_frame"}},
        {"minecraft:map", {"minecraft:empty_map"}},
        // Bedrock's tipped arrows are minecraft:arrow with the potion as data value (Java's
        // potion_contents component; the uncrafting recipe's data keeps it)
        {"minecraft:tipped_arrow", {"minecraft:arrow"}},
        {"minecraft:small_dripleaf", {"minecraft:small_dripleaf_block"}},
        // Java's light item is level 15 by default; Bedrock has one item per level
        {"minecraft:light", {"minecraft:light_block_15", "minecraft:light_block_0", "minecraft:light_block_1", "minecraft:light_block_2", "minecraft:light_block_3",
                             "minecraft:light_block_4", "minecraft:light_block_5", "minecraft:light_block_6", "minecraft:light_block_7", "minecraft:light_block_8",
                             "minecraft:light_block_9", "minecraft:light_block_10", "minecraft:light_block_11", "minecraft:light_block_12",
                             "minecraft:light_block_13", "minecraft:light_block_14"}},
        {"minecraft:zombified_piglin_spawn_egg", {"minecraft:zombie_pigman_spawn_egg"}},
        // (Bedrock's evoker spawn egg is minecraft:evoker_spawn_egg, as Java's)
    };
    return t;
}

/// Entity types whose Bedrock id differs.
Table const& entities() {
    static Table const t{
        {"minecraft:zombified_piglin", {"minecraft:zombie_pigman"}},
        {"minecraft:evoker", {"minecraft:evocation_illager"}},
        {"minecraft:end_crystal", {"minecraft:ender_crystal"}},
        {"minecraft:experience_orb", {"minecraft:xp_orb"}},
        {"minecraft:experience_bottle", {"minecraft:xp_bottle"}},
        {"minecraft:eye_of_ender", {"minecraft:eye_of_ender_signal"}},
        {"minecraft:firework_rocket", {"minecraft:fireworks_rocket"}},
        {"minecraft:fishing_bobber", {"minecraft:fishing_hook"}},
        {"minecraft:trident", {"minecraft:thrown_trident"}},
        {"minecraft:potion", {"minecraft:splash_potion", "minecraft:lingering_potion"}},
        {"minecraft:wither_skull", {"minecraft:wither_skull", "minecraft:wither_skull_dangerous"}},
        // (Java's item frame entities are blocks on Bedrock, minecraft:frame / glow_frame: no entity type)
    };
    return t;
}

Table const* tableOf(std::string_view dir) {
    if (dir == "block" || dir == "blocks") return &blocks();
    if (dir == "item" || dir == "items") return &items();
    if (dir == "entity_type" || dir == "entity_types") return &entities();
    return nullptr;
}

} // namespace

std::vector<std::pair<std::string, std::vector<std::string>>> entries(std::string_view dir) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    if (auto const* t = tableOf(dir)) out.assign(t->begin(), t->end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> bedrock(std::string_view dir, std::string const& javaId) {
    if (javaId.starts_with("minecraft:"))
        if (auto const* t = tableOf(dir))
            if (auto it = t->find(javaId); it != t->end()) return it->second;
    return {javaId};
}

std::string primary(std::string_view dir, std::string const& javaId) { return bedrock(dir, javaId).front(); }

} // namespace gf::tags::javaids
