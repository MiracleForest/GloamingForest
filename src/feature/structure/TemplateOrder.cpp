// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The order in which a template's blocks reach the processors and the world, as Minecraft 1.21.1's
// StructureTemplate keeps them: StructureTemplate#loadPalette sorts every block of a loaded
// template into one of three lists (addToLists) and buildInfoList joins them, each sorted by y,
// then x, then z:
//   0  blocks without block entity data whose collision shape is a full block,
//   1  every other block (partial and empty shapes: stairs, slabs, fences, plants, air, ...),
//   2  blocks with block entity data (a template saves it for every block entity: chests, spawners,
//      signs, ...; TF's DATA structure blocks are the kit's data markers).
// The order matters wherever a placement shares one random source between its blocks
// (StructurePlaceSettings#setRandom: TF's template features and the graveyard): every processor
// that draws for a block moves the sequence for the blocks after it.
//
// The list is decided from the Java state the template names (the kit's templates name Java
// states): vanilla 1.21.1 blocks by family below, TF 4.8 blocks by their classes
// (docs/spec/blockstates.md: BaseEntityBlock and the vanilla block entity bases for list 2; the
// vanilla families with partial shapes, the TF classes overriding the collision or outline shape
// and the blocks TFBlocks registers with noCollission for list 1). TF blocks whose shape depends
// on a state templates leave at its default (castle doors and vanishing blocks, full until they
// vanish) are full blocks. The ids are frozen (rule R1), so the tables do not go stale.
#include "feature/structure/Internal.h"

#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>

namespace gf::structure::templates {

namespace {

using Set = std::unordered_set<std::string_view>;

/// TF blocks with a block entity (BaseEntityBlock, ChestBlock, the sign blocks, AbstractFurnaceBlock,
/// ChiseledBookShelfBlock), by path.
Set const& tfEntities() {
    static Set const s{
        "acacia_drying_rack", "alpha_yeti_boss_spawner", "alpha_yeti_trophy", "alpha_yeti_wall_trophy", "antibuilder",
        "bamboo_drying_rack", "beanstalk_grower", "birch_drying_rack", "brazier", "candelabra", "canopy_chest",
        "canopy_drying_rack", "canopy_hanging_sign", "canopy_sign", "canopy_trapped_chest", "canopy_wall_hanging_sign",
        "canopy_wall_sign", "carminite_builder", "carminite_reactor", "cherry_drying_rack", "chiseled_canopy_bookshelf",
        "cicada", "cicada_jar", "cinder_furnace", "creeper_skull_candle", "creeper_wall_skull_candle",
        "crimson_drying_rack", "dark_chest", "dark_drying_rack", "dark_hanging_sign", "dark_oak_drying_rack",
        "dark_sign", "dark_trapped_chest", "dark_wall_hanging_sign", "dark_wall_sign", "encased_fire_jet",
        "encased_smoker", "final_boss_boss_spawner", "fire_jet", "firefly", "firefly_jar", "ghast_trap",
        "hydra_boss_spawner", "hydra_trophy", "hydra_wall_trophy", "jungle_drying_rack", "keepsake_casket",
        "knight_phantom_boss_spawner", "knight_phantom_trophy", "knight_phantom_wall_trophy", "lich_boss_spawner",
        "lich_trophy", "lich_wall_trophy", "mangrove_chest", "mangrove_drying_rack", "mangrove_hanging_sign",
        "mangrove_sign", "mangrove_trapped_chest", "mangrove_wall_hanging_sign", "mangrove_wall_sign", "mason_jar",
        "mining_chest", "mining_drying_rack", "mining_hanging_sign", "mining_sign", "mining_trapped_chest",
        "mining_wall_hanging_sign", "mining_wall_sign", "minoshroom_boss_spawner", "minoshroom_trophy",
        "minoshroom_wall_trophy", "moonworm", "naga_boss_spawner", "naga_trophy", "naga_wall_trophy", "oak_drying_rack",
        "ominous_black_candle", "ominous_blue_candle", "ominous_brown_candle", "ominous_candle", "ominous_cyan_candle",
        "ominous_gray_candle", "ominous_green_candle", "ominous_light_blue_candle", "ominous_light_gray_candle",
        "ominous_lime_candle", "ominous_magenta_candle", "ominous_orange_candle", "ominous_pink_candle",
        "ominous_purple_candle", "ominous_red_candle", "ominous_white_candle", "ominous_yellow_candle",
        "piglin_skull_candle", "piglin_wall_skull_candle", "player_skull_candle", "player_wall_skull_candle",
        "quest_ram_trophy", "quest_ram_wall_trophy", "reactor_debris", "sinister_spawner", "skeleton_skull_candle",
        "skeleton_wall_skull_candle", "skull_chest", "smoker", "snow_queen_boss_spawner", "snow_queen_trophy",
        "snow_queen_wall_trophy", "sorting_chest", "sorting_drying_rack", "sorting_hanging_sign", "sorting_sign",
        "sorting_trapped_chest", "sorting_wall_hanging_sign", "sorting_wall_sign", "spruce_drying_rack", "time_chest",
        "time_drying_rack", "time_hanging_sign", "time_sign", "time_trapped_chest", "time_wall_hanging_sign",
        "time_wall_sign", "transformation_chest", "transformation_drying_rack", "transformation_hanging_sign",
        "transformation_sign", "transformation_trapped_chest", "transformation_wall_hanging_sign",
        "transformation_wall_sign", "twilight_oak_chest", "twilight_oak_drying_rack", "twilight_oak_hanging_sign",
        "twilight_oak_sign", "twilight_oak_trapped_chest", "twilight_oak_wall_hanging_sign", "twilight_wall_sign",
        "ur_ghast_boss_spawner", "ur_ghast_trophy", "ur_ghast_wall_trophy", "vangrove_drying_rack",
        "warped_drying_rack", "wither_skeleton_skull_candle", "wither_skeleton_wall_skull_candle",
        "zombie_skull_candle", "zombie_wall_skull_candle"};
    return s;
}

/// TF blocks without a full collision block (vanilla families with partial shapes, TF's shape
/// overrides, noCollission), by path. Double slabs are full (javaList checks the state first).
Set const& tfPartial() {
    static Set const s{
        "acacia_banister", "aurora_slab", "bamboo_banister", "birch_banister", "blackberry_bush", "blightberry_bush",
        "blue_force_field", "blueberry_bush", "bold_castle_brick_stairs", "brown_thorns", "burnt_thorns",
        "canopy_banister", "canopy_button", "canopy_door", "canopy_fence", "canopy_fence_gate", "canopy_pressure_plate",
        "canopy_sapling", "canopy_slab", "canopy_stairs", "canopy_trapdoor", "canopy_window_pane",
        "castle_brick_stairs", "cherry_banister", "clover_patch", "copper_oreberry", "coronation_carpet",
        "cracked_castle_brick_stairs", "cracked_nagastone_stairs_left", "cracked_nagastone_stairs_right",
        "crimson_banister", "dark_banister", "dark_button", "dark_door", "dark_fence", "dark_fence_gate",
        "dark_oak_banister", "dark_pressure_plate", "dark_slab", "dark_stairs", "dark_tower_miniature_structure",
        "dark_trapdoor", "darkwood_sapling", "duskberry_bush", "encased_castle_brick_stairs", "essence_oreberry",
        "experiment_115", "fallen_leaves", "fiddlehead", "firefly_particle_spawner", "gold_oreberry",
        "green_force_field", "green_thorns", "hedge", "hollow_acacia_log_climbable", "hollow_acacia_log_horizontal",
        "hollow_acacia_log_vertical", "hollow_birch_log_climbable", "hollow_birch_log_horizontal",
        "hollow_birch_log_vertical", "hollow_canopy_log_climbable", "hollow_canopy_log_horizontal",
        "hollow_canopy_log_vertical", "hollow_cherry_log_climbable", "hollow_cherry_log_horizontal",
        "hollow_cherry_log_vertical", "hollow_crimson_stem_climbable", "hollow_crimson_stem_horizontal",
        "hollow_crimson_stem_vertical", "hollow_dark_log_climbable", "hollow_dark_log_horizontal",
        "hollow_dark_log_vertical", "hollow_dark_oak_log_climbable", "hollow_dark_oak_log_horizontal",
        "hollow_dark_oak_log_vertical", "hollow_jungle_log_climbable", "hollow_jungle_log_horizontal",
        "hollow_jungle_log_vertical", "hollow_mangrove_log_climbable", "hollow_mangrove_log_horizontal",
        "hollow_mangrove_log_vertical", "hollow_mining_log_climbable", "hollow_mining_log_horizontal",
        "hollow_mining_log_vertical", "hollow_oak_log_climbable", "hollow_oak_log_horizontal",
        "hollow_oak_log_vertical", "hollow_oak_sapling", "hollow_sorting_log_climbable",
        "hollow_sorting_log_horizontal", "hollow_sorting_log_vertical", "hollow_spruce_log_climbable",
        "hollow_spruce_log_horizontal", "hollow_spruce_log_vertical", "hollow_time_log_climbable",
        "hollow_time_log_horizontal", "hollow_time_log_vertical", "hollow_transformation_log_climbable",
        "hollow_transformation_log_horizontal", "hollow_transformation_log_vertical",
        "hollow_twilight_oak_log_climbable", "hollow_twilight_oak_log_horizontal", "hollow_twilight_oak_log_vertical",
        "hollow_vangrove_log_climbable", "hollow_vangrove_log_horizontal", "hollow_vangrove_log_vertical",
        "hollow_warped_stem_climbable", "hollow_warped_stem_horizontal", "hollow_warped_stem_vertical", "huge_lily_pad",
        "huge_water_lily", "iron_ladder", "iron_oreberry", "jungle_banister", "knightmetal_block",
        "lich_tower_miniature_structure", "maloberry_bush", "mangrove_banister", "mangrove_button", "mangrove_door",
        "mangrove_fence", "mangrove_fence_gate", "mangrove_pressure_plate", "mangrove_sapling", "mangrove_slab",
        "mangrove_stairs", "mangrove_trapdoor", "mayapple", "mining_banister", "mining_button", "mining_door",
        "mining_fence", "mining_fence_gate", "mining_pressure_plate", "mining_sapling", "mining_slab", "mining_stairs",
        "mining_trapdoor", "minotaur_labyrinth_miniature_structure", "moss_patch", "mossy_castle_brick_stairs",
        "mossy_nagastone_stairs_left", "mossy_nagastone_stairs_right", "mushgloom",
        "naga_courtyard_miniature_structure", "nagastone_stairs_left", "nagastone_stairs_right", "oak_banister",
        "ominous_fire", "orange_force_field", "pink_force_field", "potted_canopy_sapling", "potted_darkwood_sapling",
        "potted_dead_thorn", "potted_fiddlehead", "potted_green_thorn", "potted_hollow_oak_sapling",
        "potted_mangrove_sapling", "potted_mayapple", "potted_mining_sapling", "potted_mushgloom",
        "potted_rainbow_oak_sapling", "potted_sorting_sapling", "potted_thorn", "potted_time_sapling",
        "potted_transformation_sapling", "potted_twilight_oak_sapling", "rainbow_oak_sapling", "raspberry_bush",
        "red_thread", "root_strand", "rope", "skyberry_bush", "slider", "sorting_banister", "sorting_button",
        "sorting_door", "sorting_fence", "sorting_fence_gate", "sorting_pressure_plate", "sorting_sapling",
        "sorting_slab", "sorting_stairs", "sorting_trapdoor", "spruce_banister", "stingberry_bush", "thorn_rose",
        "time_banister", "time_button", "time_door", "time_fence", "time_fence_gate", "time_pressure_plate",
        "time_sapling", "time_slab", "time_stairs", "time_trapdoor", "torchberry_plant", "transformation_banister",
        "transformation_button", "transformation_door", "transformation_fence", "transformation_fence_gate",
        "transformation_pressure_plate", "transformation_sapling", "transformation_slab", "transformation_stairs",
        "transformation_trapdoor", "trollber", "trollvidr", "trophy_pedestal", "twilight_oak_banister",
        "twilight_oak_button", "twilight_oak_door", "twilight_oak_fence", "twilight_oak_fence_gate",
        "twilight_oak_pressure_plate", "twilight_oak_sapling", "twilight_oak_slab", "twilight_oak_stairs",
        "twilight_oak_trapdoor", "twilight_portal", "twilight_portal_miniature_structure", "twisted_stone_pillar",
        "uberous_soil", "unripe_trollber", "vangrove_banister", "violet_force_field", "warped_banister", "wispy_cloud",
        "worn_castle_brick_stairs", "wrought_iron_fence"};
    return s;
}

/// Vanilla 1.21.1 blocks with a block entity that no family suffix below covers, by path.
Set const& vanillaEntities() {
    static Set const s{
        "chest", "trapped_chest", "ender_chest", "barrel", "furnace", "blast_furnace", "smoker", "dispenser", "dropper", "hopper",
        "brewing_stand", "enchanting_table", "beacon", "spawner", "trial_spawner", "vault", "jukebox", "lectern", "bell", "campfire",
        "soul_campfire", "conduit", "daylight_detector", "comparator", "end_portal", "end_gateway", "structure_block", "jigsaw",
        "command_block", "chain_command_block", "repeating_command_block", "moving_piston", "beehive", "bee_nest", "sculk_sensor",
        "calibrated_sculk_sensor", "sculk_catalyst", "sculk_shrieker", "chiseled_bookshelf", "decorated_pot", "crafter",
        "suspicious_sand", "suspicious_gravel", "shulker_box", "skeleton_skull", "skeleton_wall_skull", "wither_skeleton_skull",
        "wither_skeleton_wall_skull", "zombie_head", "zombie_wall_head", "player_head", "player_wall_head", "creeper_head",
        "creeper_wall_head", "dragon_head", "dragon_wall_head", "piglin_head", "piglin_wall_head"};
    return s;
}

/// Vanilla 1.21.1 blocks without a full collision block that no family suffix below covers, by path.
Set const& vanillaPartial() {
    static Set const s{
        "air", "cave_air", "void_air", "structure_void", "light", "water", "lava", "bubble_column", "fire", "soul_fire", "torch",
        "wall_torch", "soul_torch", "soul_wall_torch", "redstone_torch", "redstone_wall_torch", "lantern", "soul_lantern", "chain",
        "ladder", "vine", "cave_vines", "cave_vines_plant", "twisting_vines", "twisting_vines_plant", "weeping_vines",
        "weeping_vines_plant", "glow_lichen", "sculk_vein", "cobweb", "short_grass", "tall_grass", "fern", "large_fern", "dead_bush",
        "seagrass", "tall_seagrass", "kelp", "kelp_plant", "sugar_cane", "bamboo", "bamboo_sapling", "cactus", "lily_pad",
        "sea_pickle", "turtle_egg", "sniffer_egg", "frogspawn", "snow", "powder_snow", "farmland", "dirt_path", "soul_sand", "mud",
        "honey_block", "anvil", "chipped_anvil", "damaged_anvil", "cauldron", "water_cauldron", "lava_cauldron", "powder_snow_cauldron",
        "stonecutter", "grindstone", "end_rod", "lightning_rod", "dragon_egg", "end_portal_frame", "nether_portal", "redstone_wire",
        "repeater", "lever", "tripwire", "tripwire_hook", "scaffolding", "piston_head", "chorus_plant", "cocoa", "sweet_berry_bush",
        "wheat", "carrots", "potatoes", "beetroots", "melon_stem", "pumpkin_stem", "attached_melon_stem", "attached_pumpkin_stem",
        "nether_wart", "pointed_dripstone", "small_amethyst_bud", "medium_amethyst_bud", "large_amethyst_bud", "amethyst_cluster",
        "composter", "big_dripleaf", "big_dripleaf_stem", "small_dripleaf", "spore_blossom", "hanging_roots", "azalea",
        "flowering_azalea", "pink_petals", "pitcher_plant", "pitcher_crop", "torchflower", "torchflower_crop", "dandelion", "poppy",
        "blue_orchid", "allium", "azure_bluet", "oxeye_daisy", "cornflower", "lily_of_the_valley", "wither_rose", "sunflower", "lilac",
        "rose_bush", "peony", "crimson_roots", "warped_roots", "nether_sprouts", "crimson_fungus", "warped_fungus", "brown_mushroom",
        "red_mushroom", "mangrove_propagule", "heavy_core", "flower_pot", "cake", "candle", "iron_bars", "rail"};
    return s;
}

/// Families of vanilla blocks without a full collision block, by path suffix.
constexpr std::string_view kPartialSuffixes[] = {"_stairs",  "_slab",      "_fence",       "_fence_gate", "_wall",     "_pane",
                                                 "_door",    "_trapdoor",  "_button",      "_pressure_plate", "_carpet", "_candle",
                                                 "_candle_cake", "_rail",  "_sapling",     "_tulip",      "_coral",    "_coral_fan",
                                                 "_wall_fan"};
/// Families of vanilla blocks with a block entity, by path suffix (beds, banners, signs, shulker boxes).
constexpr std::string_view kEntitySuffixes[] = {"_bed", "_banner", "_sign", "_hanging_sign", "_shulker_box"};

template <std::size_t N>
bool endsWithAny(std::string_view path, std::string_view const (&suffixes)[N]) {
    for (auto const& s : suffixes)
        if (path.ends_with(s)) return true;
    return false;
}

} // namespace

int javaList(Json const& javaState) {
    std::string name;
    if (javaState.is_object()) name = javaState.value("Name", std::string{});
    else if (javaState.is_string()) name = javaState.get<std::string>();
    auto             colon = name.find(':');
    std::string_view ns    = colon == std::string::npos ? std::string_view("minecraft") : std::string_view(name).substr(0, colon);
    std::string_view path  = colon == std::string::npos ? std::string_view(name) : std::string_view(name).substr(colon + 1);
    // a double slab is a full block (SlabBlock: SlabType.DOUBLE has Shapes.block())
    if (path.ends_with("_slab") && javaState.is_object()) {
        auto props = javaState.value("Properties", Json::object());
        if (props.is_object() && props.contains("type") && props["type"].is_string() && props["type"].get<std::string>() == "double") return 0;
    }
    if (ns == "twilightforest") {
        if (tfEntities().contains(path)) return 2;
        return tfPartial().contains(path) ? 1 : 0;
    }
    if (ns != "minecraft") return 0; // other namespaces (the gf_test fixtures): plain blocks
    if (vanillaEntities().contains(path) || endsWithAny(path, kEntitySuffixes)) return 2;
    if (vanillaPartial().contains(path) || endsWithAny(path, kPartialSuffixes) || path.starts_with("potted_")) return 1;
    return 0;
}

} // namespace gf::structure::templates
