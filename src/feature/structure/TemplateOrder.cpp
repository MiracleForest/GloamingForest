// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// The order in which a template's blocks reach the processors and the world, as Minecraft 1.21.1's
// StructureTemplate keeps them: StructureTemplate#loadPalette sorts every block of a loaded
// template into one of three lists (addToLists) and buildInfoList joins them, each sorted by y,
// then x, then z:
//   0  blocks without block entity data whose collision shape is a full block,
//   1  every other block (partial and empty shapes: stairs, slabs, fences, plants, air, ...),
//   2  blocks with block entity data (a template saves it for every block entity: chests, spawners,
//      signs, ...; upstream's DATA structure blocks are the kit's data markers, also on a structure_void
//      cell).
// The order matters wherever a placement shares one random source between its blocks
// (StructurePlaceSettings#setRandom: upstream's template features and the graveyard): every processor
// that draws for a block moves the sequence for the blocks after it.
//
// The list is decided from the Java state the template names (the kit's templates name Java
// states): vanilla 1.21.1 blocks by family below, upstream 4.8 blocks by their classes
// (docs/spec/blockstates.md: BaseEntityBlock and the vanilla block entity bases for list 2; the
// vanilla families with partial shapes, the upstream classes overriding the collision or outline shape
// and the blocks GloamingBlocks registers with noCollission for list 1). The ids are frozen (rule R1), so
// the tables do not go stale.
// Blocks whose collision shape is full in one state and not in another are decided by the state
// the template names (javaStateList): a double slab is full (SlabType.DOUBLE: Shapes.block()), an
// extended piston or sticky piston is not (PistonBaseBlock: the base without its head), nor a
// vanished castle door ("vanish") or reappearing block ("vanished": CastleDoorBlock and
// VanishingBlock#getCollisionShape are empty then). No other vanilla 1.21.1 or upstream 4.8 block changes
// between a full and a partial collision shape with its state.
#include "feature/structure/Internal.h"

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "mc/world/level/block/Block.h"

namespace gf::structure::templates {

namespace {

using Set = std::unordered_set<std::string_view>;

/// upstream blocks with a block entity (BaseEntityBlock, ChestBlock, the sign blocks, AbstractFurnaceBlock,
/// ChiseledBookShelfBlock, EntityBlock such as RedThreadBlock), by path.
Set const& gloamingEntities() {
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
        "quest_ram_trophy", "quest_ram_wall_trophy", "reactor_debris", "red_thread", "sinister_spawner", "skeleton_skull_candle",
        "skeleton_wall_skull_candle", "skull_chest", "smoker", "snow_queen_boss_spawner", "snow_queen_trophy",
        "snow_queen_wall_trophy", "sorting_chest", "sorting_drying_rack", "sorting_hanging_sign", "sorting_sign",
        "sorting_trapped_chest", "sorting_wall_hanging_sign", "sorting_wall_sign", "spruce_drying_rack", "time_chest",
        "time_drying_rack", "time_hanging_sign", "time_sign", "time_trapped_chest", "time_wall_hanging_sign",
        "time_wall_sign", "transformation_chest", "transformation_drying_rack", "transformation_hanging_sign",
        "transformation_sign", "transformation_trapped_chest", "transformation_wall_hanging_sign",
        "transformation_wall_sign", "gloaming_oak_chest", "gloaming_oak_drying_rack", "gloaming_oak_hanging_sign",
        "gloaming_oak_sign", "gloaming_oak_trapped_chest", "gloaming_oak_wall_hanging_sign", "gloaming_wall_sign",
        "ur_ghast_boss_spawner", "ur_ghast_trophy", "ur_ghast_wall_trophy", "vangrove_drying_rack",
        "warped_drying_rack", "wither_skeleton_skull_candle", "wither_skeleton_wall_skull_candle",
        "zombie_skull_candle", "zombie_wall_skull_candle"};
    return s;
}

/// upstream blocks without a full collision block (vanilla families with partial shapes, upstream's shape
/// overrides, noCollission), by path. Double slabs are full (javaStateList checks the state first).
Set const& gloamingPartial() {
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
        "hollow_gloaming_oak_log_climbable", "hollow_gloaming_oak_log_horizontal", "hollow_gloaming_oak_log_vertical",
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
        "potted_transformation_sapling", "potted_gloaming_oak_sapling", "rainbow_oak_sapling", "raspberry_bush",
        "root_strand", "rope", "skyberry_bush", "slider", "sorting_banister", "sorting_button",
        "sorting_door", "sorting_fence", "sorting_fence_gate", "sorting_pressure_plate", "sorting_sapling",
        "sorting_slab", "sorting_stairs", "sorting_trapdoor", "spruce_banister", "stingberry_bush", "thorn_rose",
        "time_banister", "time_button", "time_door", "time_fence", "time_fence_gate", "time_pressure_plate",
        "time_sapling", "time_slab", "time_stairs", "time_trapdoor", "torchberry_plant", "transformation_banister",
        "transformation_button", "transformation_door", "transformation_fence", "transformation_fence_gate",
        "transformation_pressure_plate", "transformation_sapling", "transformation_slab", "transformation_stairs",
        "transformation_trapdoor", "trollber", "trollvidr", "trophy_pedestal", "gloaming_oak_banister",
        "gloaming_oak_button", "gloaming_oak_door", "gloaming_oak_fence", "gloaming_oak_fence_gate",
        "gloaming_oak_pressure_plate", "gloaming_oak_sapling", "gloaming_oak_slab", "gloaming_oak_stairs",
        "gloaming_oak_trapdoor", "gloaming_portal", "gloaming_portal_miniature_structure", "twisted_stone_pillar",
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
        "red_mushroom", "mangrove_propagule", "heavy_core", "flower_pot", "cake", "candle", "candle_cake", "iron_bars", "rail"};
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

/// Whether property `key` of a Java state's Properties is `value` (a template saves property
/// values as strings).
bool hasProperty(Json const& javaState, char const* key, std::string_view value) {
    if (!javaState.is_object()) return false;
    auto props = javaState.find("Properties");
    if (props == javaState.end() || !props->is_object()) return false;
    auto it = props->find(key);
    if (it == props->end()) return false;
    if (it->is_string()) return it->get<std::string>() == value;
    if (it->is_boolean()) return (it->get<bool>() ? "true" : "false") == value;
    return false;
}

/// The list of the blocks whose collision shape is full in some states and partial in others
/// (nullopt: the block's list does not depend on its state).
std::optional<int> javaStateList(std::string_view ns, std::string_view path, Json const& javaState) {
    // a double slab is a full block (SlabBlock: SlabType.DOUBLE has Shapes.block())
    if (path.ends_with("_slab")) {
        if (hasProperty(javaState, "type", "double")) return 0;
        return std::nullopt;
    }
    if (ns == "minecraft" && (path == "piston" || path == "sticky_piston"))
        return hasProperty(javaState, "extended", "true") ? 1 : 0; // PistonBaseBlock: an extended piston keeps only its base
    if (ns == "gloamingforest") {
        if (path.ends_with("_castle_door")) return hasProperty(javaState, "vanish", "true") ? 1 : 0;  // CastleDoorBlock
        if (path == "reappearing_block") return hasProperty(javaState, "vanished", "true") ? 1 : 0; // VanishingBlock
    }
    return std::nullopt;
}

} // namespace

int javaList(Json const& javaState) {
    std::string name;
    if (javaState.is_object()) name = javaState.value("Name", std::string{});
    else if (javaState.is_string()) name = javaState.get<std::string>();
    auto             colon = name.find(':');
    std::string_view ns    = colon == std::string::npos ? std::string_view("minecraft") : std::string_view(name).substr(0, colon);
    std::string_view path  = colon == std::string::npos ? std::string_view(name) : std::string_view(name).substr(colon + 1);
    if (auto byState = javaStateList(ns, path, javaState)) return *byState;
    if (ns == "gloamingforest") {
        if (gloamingEntities().contains(path)) return 2;
        return gloamingPartial().contains(path) ? 1 : 0;
    }
    if (ns != "minecraft") return 0; // other namespaces (the gf_test fixtures): plain blocks
    if (vanillaEntities().contains(path) || endsWithAny(path, kEntitySuffixes)) return 2;
    if (vanillaPartial().contains(path) || endsWithAny(path, kPartialSuffixes) || path.starts_with("potted_")) return 1;
    return 0;
}

bool randomizableContainer(Block const& block) {
    // by the Bedrock id of the block the placement put there (StructureTemplate checks the block
    // entity of the placed state): vanilla 1.21.1's RandomizableContainer block entities (chests,
    // barrels, hoppers, dispensers, droppers, shulker boxes, decorated pots, crafters) and upstream's (its
    // wooden chests and trapped chests are vanilla ChestBlocks; SkullChestBlockEntity and the
    // keepsake casket's extend RandomizableContainerBlockEntity)
    auto const&      id    = blocks::id(block);
    auto             colon = id.find(':');
    std::string_view ns    = colon == std::string::npos ? std::string_view("minecraft") : std::string_view(id).substr(0, colon);
    std::string_view path  = colon == std::string::npos ? std::string_view(id) : std::string_view(id).substr(colon + 1);
    if (ns == "gloamingforest") return path.ends_with("_chest") || path == "keepsake_casket";
    if (ns != "minecraft") return false;
    static Set const vanilla{"chest", "trapped_chest", "barrel", "hopper", "dispenser", "dropper", "decorated_pot", "crafter"};
    return vanilla.contains(path) || path.ends_with("shulker_box");
}

Block const* dataMarkerBlock() {
    // (not cached here: block types are registered per world on the client, blocks:: caches them)
    return blocks::fromJavaState(Json{{"Name", "minecraft:structure_block"}, {"Properties", {{"mode", "data"}}}});
}

} // namespace gf::structure::templates
