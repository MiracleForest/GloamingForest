// The vanilla tag bridge of the tag runtime (see VanillaTags.h).
#include "feature/tags/VanillaTags.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/BlockPalette.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "ll/api/service/Bedrock.h"

namespace gf::tags::vanilla {

using Pred = bool (*)(std::string const& id);

/// One vanilla tag file: its own members (`ids`, `suffixes`, `native`, `pred`, minus `excludes`) and
/// the tags it includes (Java's "#minecraft:..." entries, walked by the tag runtime).
struct Rule {
    std::vector<std::string_view> ids;      ///< minecraft ids without the namespace
    std::vector<std::string_view> suffixes; ///< minecraft ids ending with one of these
    std::vector<std::string_view> excludes; ///< minecraft ids never matched by this rule ("*suffix": ids ending with it)
    std::vector<std::string_view> native;   ///< Bedrock block / item tags (any namespace)
    std::vector<std::string_view> include;  ///< other tags of the same registry (bridged, data, or both)
    Pred                          pred = nullptr;
    std::vector<HashedString>     nativeHashed{}; ///< `native` hashed once (rules())
};

namespace {

// Java 1.21.1's wood types (pale oak came with 1.21.4)
constexpr std::string_view kWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "bamboo", "crimson", "warped"};
constexpr std::string_view kColors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                        "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

/// Keeps generated names alive (rules hold string_views; built once, under the rules' static init).
std::string_view stored(std::string s) {
    static std::set<std::string> storage;
    return *storage.insert(std::move(s)).first;
}

std::vector<std::string_view> woodIds(std::string_view suffix, bool netherAndBamboo = true) {
    std::vector<std::string_view> out;
    for (auto w : kWoods) {
        if (!netherAndBamboo && (w == "bamboo" || w == "crimson" || w == "warped")) continue;
        out.push_back(stored(std::string(w) + std::string(suffix)));
    }
    return out;
}

/// "<colour><suffix>" for the 16 dye colours.
std::vector<std::string_view> colorIds(std::string_view suffix) {
    std::vector<std::string_view> out;
    for (auto c : kColors) out.push_back(stored(std::string(c) + std::string(suffix)));
    return out;
}

/// Java's minecraft:light: Bedrock's light_block_0 .. light_block_15 (one block per level; the
/// legacy light_block is only an alias of light_block_0 in 26.51).
std::vector<std::string_view> lightIds() {
    std::vector<std::string_view> out{"light_block"};
    for (int i = 0; i <= 15; ++i) out.push_back(stored("light_block_" + std::to_string(i)));
    return out;
}

/// The copper doors / trapdoors of the four oxidation stages, plain and waxed: "<stage><suffix>".
std::vector<std::string_view> copperIds(std::string_view suffix) {
    std::vector<std::string_view> out;
    for (std::string_view stage : {"", "exposed_", "weathered_", "oxidized_"}) {
        out.push_back(stored(std::string(stage) + "copper" + std::string(suffix)));
        out.push_back(stored("waxed_" + std::string(stage) + "copper" + std::string(suffix)));
    }
    return out;
}

std::vector<std::string_view> concat(std::vector<std::string_view> a, std::vector<std::string_view> const& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

/// Java 1.21.1's humanoid armour of one slot ("_helmet", ...): the six armour materials, plus the
/// turtle shell for the head. Only armour: carved pumpkins, skulls and mob heads (Bedrock's other
/// humanoid wearables) are no armour in Java's tags; TF's data adds TF's armour to these tags.
std::vector<std::string_view> armorIds(std::string_view suffix) {
    std::vector<std::string_view> out;
    for (std::string_view m : {"leather", "chainmail", "golden", "iron", "diamond", "netherite"}) out.push_back(stored(std::string(m) + std::string(suffix)));
    if (suffix == "_helmet") out.push_back("turtle_helmet");
    return out;
}

/// Bedrock content newer than Java 1.21.1 (the bridge's policy, VanillaTags.h): never a member of a
/// bridged tag through an id list, a name pattern or a predicate. The copper families count with
/// their waxed and oxidised variants (waxed_exposed_copper_chest, oxidized_copper_lantern, ...), and
/// the oxidised and waxed lightning rods are 1.21.9's (the plain one is older).
bool newerThan1211(std::string_view name) {
    std::string_view base         = name;
    bool             copperPrefix = false;
    if (base.starts_with("waxed_")) {
        base.remove_prefix(6);
        copperPrefix = true;
    }
    for (std::string_view stage : {"exposed_", "weathered_", "oxidized_"})
        if (base.starts_with(stage)) {
            base.remove_prefix(stage.size());
            copperPrefix = true;
            break;
        }
    if (copperPrefix && base == "lightning_rod") return true;
    for (std::string_view p : {"pale_", "stripped_pale_", "resin_", "chiseled_resin_", "creaking", "dried_ghast", "happy_ghast", "copper_golem"})
        if (base.starts_with(p)) return true;
    for (std::string_view x : {"_spear", "_harness", "_shelf"})
        if (base.ends_with(x)) return true;
    static std::unordered_set<std::string_view> const ids{
        "open_eyeblossom", "closed_eyeblossom", "bush", "firefly_bush", "short_dry_grass", "tall_dry_grass", "leaf_litter", "cactus_flower",
        "wildflowers", "blue_egg", "brown_egg", "copper_sword", "copper_pickaxe", "copper_axe", "copper_shovel", "copper_hoe",
        "copper_helmet", "copper_chestplate", "copper_leggings", "copper_boots", "copper_horse_armor", "copper_nugget", "copper_chest",
        "copper_bars", "copper_chain", "copper_lantern", "copper_torch", "nautilus", "zombie_nautilus", "camel_husk", "parched"};
    return ids.contains(base);
}

Item const* itemOf(std::string const& id) {
    auto ref  = ItemRegistryManager::getItemRegistry();
    auto item = ref.lookupByName(HashedString(id));
    return item.get();
}

bool isBoat(std::string const& id) {
    return id.starts_with("minecraft:") && id.ends_with("_boat") && !id.ends_with("_chest_boat");
}

using Key   = std::pair<Registry, std::string>;
using Rules = std::map<Key, Rule>;

Rules buildRules() {
    using R = Registry;
    Rules r;
    auto  block = [&](std::string const& tag, Rule rule) { r[{R::Block, tag}] = std::move(rule); };
    auto  item  = [&](std::string const& tag, Rule rule) { r[{R::Item, tag}] = std::move(rule); };
    auto  both  = [&](std::string const& tag, Rule const& rule) {
        r[{R::Block, tag}] = rule;
        r[{R::Item, tag}]  = rule;
    };
    auto entity    = [&](std::string const& tag, Rule rule) { r[{R::EntityType, tag}] = std::move(rule); };
    auto fluid     = [&](std::string const& tag, Rule rule) { r[{R::Fluid, tag}] = std::move(rule); };
    auto biome     = [&](std::string const& tag, Rule rule) { r[{R::Biome, tag}] = std::move(rule); };
    auto structure = [&](std::string const& tag, Rule rule) { r[{R::Structure, tag}] = std::move(rule); };

    // ---- wood --------------------------------------------------------------------------------------
    both("minecraft:crimson_stems", {.ids = {"crimson_stem", "stripped_crimson_stem", "crimson_hyphae", "stripped_crimson_hyphae"}});
    both("minecraft:warped_stems", {.ids = {"warped_stem", "stripped_warped_stem", "warped_hyphae", "stripped_warped_hyphae"}});
    both("minecraft:logs_that_burn", {.suffixes = {"_log", "_wood"}, .native = {}});
    block("minecraft:logs", {.native = {"log"}, .include = {"minecraft:logs_that_burn", "minecraft:crimson_stems", "minecraft:warped_stems"}});
    item("minecraft:logs", {.native = {"minecraft:logs"}, .include = {"minecraft:logs_that_burn", "minecraft:crimson_stems", "minecraft:warped_stems"}});
    // Java 1.21.1 (block only): the eight overworld trees' plain logs
    block("minecraft:overworld_natural_logs",
          {.ids = {"acacia_log", "birch_log", "oak_log", "jungle_log", "spruce_log", "dark_oak_log", "mangrove_log", "cherry_log"}});
    both("minecraft:planks", {.suffixes = {"_planks"}});
    both("minecraft:leaves", {.ids = {"azalea_leaves_flowered", "flowering_azalea_leaves"}, .suffixes = {"_leaves"}});
    both("minecraft:saplings", {.ids = {"azalea", "flowering_azalea", "mangrove_propagule"}, .suffixes = {"_sapling"}, .excludes = {"bamboo_sapling"}});
    both("minecraft:wooden_slabs", {.ids = woodIds("_slab")});
    both("minecraft:wooden_stairs", {.ids = woodIds("_stairs")});
    both("minecraft:wooden_fences", {.ids = concat(woodIds("_fence"), {"fence"})});
    both("minecraft:wooden_doors", {.ids = concat(woodIds("_door"), {"wooden_door"})});
    both("minecraft:wooden_trapdoors", {.ids = concat(woodIds("_trapdoor"), {"trapdoor"})});
    both("minecraft:wooden_buttons", {.ids = concat(woodIds("_button"), {"wooden_button"})});
    both("minecraft:wooden_pressure_plates", {.ids = concat(woodIds("_pressure_plate"), {"wooden_pressure_plate"})});
    both("minecraft:fence_gates", {.ids = {"fence_gate"}, .suffixes = {"_fence_gate"}});
    // Java 1.21.1: fences = #wooden_fences + nether_brick_fence; doors / trapdoors = the wooden ones,
    // iron's and the eight copper ones (block and item tags alike)
    both("minecraft:fences", {.ids = {"nether_brick_fence"}, .include = {"minecraft:wooden_fences"}});
    both("minecraft:doors", {.ids = concat(copperIds("_door"), {"iron_door"}), .include = {"minecraft:wooden_doors"}});
    both("minecraft:trapdoors", {.ids = concat(copperIds("_trapdoor"), {"iron_trapdoor"}), .include = {"minecraft:wooden_trapdoors"}});
    block("minecraft:standing_signs", {.ids = concat(woodIds("_standing_sign"), {"standing_sign"})});
    block("minecraft:wall_signs", {.ids = concat(woodIds("_wall_sign"), {"wall_sign"})});
    block("minecraft:signs", {.include = {"minecraft:standing_signs", "minecraft:wall_signs"}});
    block("minecraft:ceiling_hanging_signs", {.suffixes = {"_hanging_sign"}});
    block("minecraft:wall_hanging_signs", {.suffixes = {"_hanging_sign"}});
    block("minecraft:all_hanging_signs", {.include = {"minecraft:ceiling_hanging_signs", "minecraft:wall_hanging_signs"}});
    item("minecraft:signs", {.ids = woodIds("_sign")});
    item("minecraft:hanging_signs", {.suffixes = {"_hanging_sign"}});
    // Java 1.21.1: #boats holds #chest_boats
    item("minecraft:boats", {.ids = {"bamboo_raft"}, .include = {"minecraft:chest_boats"}, .pred = isBoat});
    item("minecraft:chest_boats", {.ids = {"bamboo_chest_raft"}, .suffixes = {"_chest_boat"}});

    // ---- building ----------------------------------------------------------------------------------
    both("minecraft:stairs", {.suffixes = {"_stairs"}, .include = {"minecraft:wooden_stairs"}});
    both("minecraft:slabs", {.suffixes = {"_slab"}, .excludes = {"double_stone_block_slab"}, .include = {"minecraft:wooden_slabs"}});
    both("minecraft:walls", {.suffixes = {"_wall"}});
    both("minecraft:stone_buttons", {.ids = {"stone_button", "polished_blackstone_button"}});
    both("minecraft:buttons", {.ids = {"wooden_button", "stone_button"}, .suffixes = {"_button"}, .include = {"minecraft:wooden_buttons", "minecraft:stone_buttons"}});
    both("minecraft:wool", {.suffixes = {"_wool"}});
    both("minecraft:wool_carpets", {.suffixes = {"_carpet"}, .excludes = {"moss_carpet", "pale_moss_carpet"}});
    both("minecraft:stone_bricks", {.ids = {"stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks", "chiseled_stone_bricks"}});
    both("minecraft:terracotta", {.ids = {"hardened_clay", "terracotta"}, .suffixes = {"_terracotta"}, .excludes = {"*_glazed_terracotta"}});
    both("minecraft:sand", {.ids = {"sand", "red_sand", "suspicious_sand"}});
    both("minecraft:ice", {.ids = {"ice", "packed_ice", "blue_ice", "frosted_ice"}});
    block("minecraft:snow", {.ids = {"snow_layer", "snow", "powder_snow"}});
    block("minecraft:fire", {.ids = {"fire", "soul_fire"}});
    block("minecraft:air", {.ids = {"air"}});
    block("minecraft:portals", {.ids = {"portal", "end_portal", "end_gateway"}});
    block("minecraft:flower_pots", {.ids = {"flower_pot"}});
    block("minecraft:climbable", {.ids = {"ladder", "vine", "scaffolding", "weeping_vines", "twisting_vines", "cave_vines", "cave_vines_body_with_berries", "cave_vines_head_with_berries"}});
    both("minecraft:beacon_base_blocks", {.ids = {"iron_block", "gold_block", "diamond_block", "emerald_block", "netherite_block"}});
    item("minecraft:beacon_payment_items", {.ids = {"iron_ingot", "gold_ingot", "emerald", "diamond", "netherite_ingot"}});

    // ---- ground ------------------------------------------------------------------------------------
    both("minecraft:dirt", {.ids = {"dirt", "coarse_dirt", "grass_block", "grass", "podzol", "mycelium", "rooted_dirt", "dirt_with_roots", "moss_block", "mud", "muddy_mangrove_roots"}});
    both("minecraft:nylium", {.ids = {"crimson_nylium", "warped_nylium"}});
    both("minecraft:wart_blocks", {.ids = {"nether_wart_block", "warped_wart_block"}});
    both("minecraft:base_stone_overworld", {.ids = {"stone", "granite", "diorite", "andesite", "tuff", "deepslate"}});
    both("minecraft:base_stone_nether", {.ids = {"netherrack", "basalt", "blackstone"}});
    both("minecraft:stone_ore_replaceables", {.ids = {"stone", "granite", "diorite", "andesite"}});
    both("minecraft:deepslate_ore_replaceables", {.ids = {"deepslate", "tuff"}});
    both("minecraft:coal_ores", {.ids = {"coal_ore", "deepslate_coal_ore"}});
    both("minecraft:iron_ores", {.ids = {"iron_ore", "deepslate_iron_ore"}});
    both("minecraft:gold_ores", {.ids = {"gold_ore", "deepslate_gold_ore", "nether_gold_ore"}});
    both("minecraft:copper_ores", {.ids = {"copper_ore", "deepslate_copper_ore"}});
    both("minecraft:redstone_ores", {.ids = {"redstone_ore", "deepslate_redstone_ore", "lit_redstone_ore", "lit_deepslate_redstone_ore"}});
    both("minecraft:lapis_ores", {.ids = {"lapis_ore", "deepslate_lapis_ore"}});
    both("minecraft:diamond_ores", {.ids = {"diamond_ore", "deepslate_diamond_ore"}});
    both("minecraft:emerald_ores", {.ids = {"emerald_ore", "deepslate_emerald_ore"}});
    // Java 1.21.1 lists minecraft:snow (the layer: Bedrock's snow_layer), not #minecraft:snow
    block("minecraft:overworld_carver_replaceables",
          {.ids = {"water", "flowing_water", "gravel", "suspicious_gravel", "sandstone", "red_sandstone", "calcite", "snow_layer", "packed_ice", "raw_iron_block",
                   "raw_copper_block"},
           .include = {"minecraft:base_stone_overworld", "minecraft:dirt", "minecraft:sand", "minecraft:terracotta", "minecraft:iron_ores", "minecraft:copper_ores"}});
    block("minecraft:nether_carver_replaceables",
          {.ids = {"soul_sand", "soul_soil"}, .include = {"minecraft:base_stone_overworld", "minecraft:base_stone_nether", "minecraft:dirt", "minecraft:nylium", "minecraft:wart_blocks"}});
    block("minecraft:cave_vines", {.ids = {"cave_vines", "cave_vines_body_with_berries", "cave_vines_head_with_berries"}});
    block("minecraft:moss_replaceable", {.include = {"minecraft:base_stone_overworld", "minecraft:cave_vines", "minecraft:dirt"}});
    block("minecraft:lush_ground_replaceable", {.ids = {"clay", "gravel", "sand"}, .include = {"minecraft:moss_replaceable"}});
    block("minecraft:sculk_replaceable",
          {.ids = {"sand", "red_sand", "gravel", "soul_sand", "soul_soil", "calcite", "smooth_basalt", "clay", "dripstone_block", "end_stone", "red_sandstone", "sandstone"},
           .include = {"minecraft:base_stone_overworld", "minecraft:dirt", "minecraft:terracotta", "minecraft:nylium", "minecraft:base_stone_nether"}});
    block("minecraft:mushroom_grow_block", {.ids = {"mycelium", "podzol", "crimson_nylium", "warped_nylium"}});
    block("minecraft:infiniburn_overworld", {.ids = {"netherrack", "magma"}});
    block("minecraft:strider_warm_blocks", {.ids = {"lava", "flowing_lava"}});
    block("minecraft:small_dripleaf_placeable", {.ids = {"clay", "moss_block"}});

    // ---- plants ------------------------------------------------------------------------------------
    both("minecraft:small_flowers",
         {.ids = {"dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy",
                  "cornflower", "lily_of_the_valley", "wither_rose", "torchflower", "red_flower", "yellow_flower"}});
    both("minecraft:tall_flowers", {.ids = {"sunflower", "lilac", "rose_bush", "peony", "pitcher_plant"}});
    both("minecraft:flowers",
         {.ids = {"flowering_azalea", "azalea_leaves_flowered", "mangrove_propagule", "cherry_leaves", "pink_petals", "chorus_flower", "spore_blossom"},
          .include = {"minecraft:small_flowers", "minecraft:tall_flowers"}});
    block("minecraft:crops", {.ids = {"wheat", "carrots", "potatoes", "beetroot", "melon_stem", "pumpkin_stem", "torchflower_crop", "pitcher_crop"}});
    // Java 1.21.1 with Bedrock ids (tall_seagrass is Bedrock's seagrass, void_air / cave_air its air,
    // light its 16 light blocks)
    block("minecraft:replaceable",
          {.ids = concat({"air", "water", "flowing_water", "lava", "flowing_lava", "short_grass", "tallgrass", "fern", "deadbush", "dead_bush", "seagrass", "fire",
                          "soul_fire", "snow_layer", "vine", "glow_lichen", "tall_grass", "large_fern", "structure_void", "bubble_column", "hanging_roots",
                          "crimson_roots", "warped_roots", "nether_sprouts"},
                         lightIds())});
    block("minecraft:replaceable_by_trees",
          {.ids = {"short_grass", "tallgrass", "fern", "deadbush", "dead_bush", "vine", "glow_lichen", "sunflower", "lilac", "rose_bush", "peony", "tall_grass",
                   "large_fern", "hanging_roots", "pitcher_plant", "water", "flowing_water", "seagrass", "warped_roots", "nether_sprouts", "crimson_roots"},
           .include = {"minecraft:leaves"}});
    block("minecraft:features_cannot_replace", {.ids = {"bedrock", "mob_spawner", "spawner", "chest", "end_portal_frame", "reinforced_deepslate", "trial_spawner", "vault"}});
    block("minecraft:lava_pool_stone_cannot_replace", {.include = {"minecraft:features_cannot_replace", "minecraft:leaves", "minecraft:logs"}});
    block("minecraft:frog_prefer_jump_to", {.ids = {"waterlily", "lily_pad", "big_dripleaf"}});

    // the block tags Java's mineable/* include (Java 1.21.1, Bedrock ids: undyed_shulker_box is the
    // plain shulker box, golden_rail the powered rail, standing_banner / wall_banner every banner;
    // Bedrock's water, lava and powder snow cauldrons are all minecraft:cauldron, told apart by its
    // cauldron_liquid state: Java's cauldron, water_cauldron, lava_cauldron and powder_snow_cauldron)
    block("minecraft:shulker_boxes", {.ids = concat(colorIds("_shulker_box"), {"shulker_box", "undyed_shulker_box"})});
    block("minecraft:anvil", {.ids = {"anvil", "chipped_anvil", "damaged_anvil"}});
    block("minecraft:cauldrons", {.ids = {"cauldron"}});
    block("minecraft:rails", {.ids = {"rail", "golden_rail", "detector_rail", "activator_rail"}});
    block("minecraft:bamboo_blocks", {.ids = {"bamboo_block", "stripped_bamboo_block"}});
    block("minecraft:concrete_powder", {.ids = colorIds("_concrete_powder")});
    block("minecraft:banners", {.ids = {"standing_banner", "wall_banner"}});

    // ---- mining ------------------------------------------------------------------------------------
    // Bedrock's native tags hold the vanilla blocks; the includes are Java 1.21.1's (they bring the
    // data members TF adds to those tags, as Java's nesting does)
    block("minecraft:mineable/pickaxe",
          {.native  = {"minecraft:is_pickaxe_item_destructible"},
           .include = {"minecraft:stone_buttons", "minecraft:walls", "minecraft:shulker_boxes", "minecraft:anvil", "minecraft:cauldrons", "minecraft:rails"}});
    block("minecraft:mineable/axe",
          {.native  = {"minecraft:is_axe_item_destructible"},
           .include = {"minecraft:banners", "minecraft:fence_gates", "minecraft:logs", "minecraft:planks", "minecraft:saplings", "minecraft:signs",
                       "minecraft:wooden_buttons", "minecraft:wooden_doors", "minecraft:wooden_fences", "minecraft:wooden_pressure_plates", "minecraft:wooden_slabs",
                       "minecraft:wooden_stairs", "minecraft:wooden_trapdoors", "minecraft:all_hanging_signs", "minecraft:bamboo_blocks"}});
    block("minecraft:mineable/shovel", {.native = {"minecraft:is_shovel_item_destructible"}, .include = {"minecraft:concrete_powder"}});
    block("minecraft:mineable/hoe", {.native = {"minecraft:is_hoe_item_destructible"}});
    block("minecraft:sword_efficient",
          {.native = {"minecraft:is_sword_item_destructible"}, .include = {"minecraft:leaves", "minecraft:saplings", "minecraft:small_flowers", "minecraft:crops"}});
    block("minecraft:needs_stone_tool", {.native = {"stone_pick_diggable"}});
    block("minecraft:needs_iron_tool", {.native = {"iron_pick_diggable"}});
    block("minecraft:needs_diamond_tool", {.native = {"diamond_pick_diggable"}});
    block("minecraft:incorrect_for_wooden_tool", {.include = {"minecraft:needs_stone_tool", "minecraft:needs_iron_tool", "minecraft:needs_diamond_tool"}});
    block("minecraft:incorrect_for_gold_tool", {.include = {"minecraft:needs_stone_tool", "minecraft:needs_iron_tool", "minecraft:needs_diamond_tool"}});
    block("minecraft:incorrect_for_stone_tool", {.include = {"minecraft:needs_iron_tool", "minecraft:needs_diamond_tool"}});
    block("minecraft:incorrect_for_iron_tool", {.include = {"minecraft:needs_diamond_tool"}});
    block("minecraft:incorrect_for_diamond_tool", {});
    block("minecraft:incorrect_for_netherite_tool", {});
    block("minecraft:wither_immune",
          {.ids = concat({"barrier", "bedrock", "end_portal", "end_portal_frame", "end_gateway", "command_block", "repeating_command_block", "chain_command_block",
                          "structure_block", "jigsaw", "moving_block", "reinforced_deepslate"},
                         lightIds())});
    block("minecraft:dragon_immune",
          {.ids = {"barrier", "bedrock", "end_portal", "end_portal_frame", "end_gateway", "command_block", "repeating_command_block", "chain_command_block",
                   "structure_block", "jigsaw", "moving_block", "obsidian", "crying_obsidian", "end_stone", "iron_bars", "respawn_anchor", "reinforced_deepslate"}});

    // ---- items -------------------------------------------------------------------------------------
    // Bedrock tags the mace minecraft:is_sword; Java's #swords does not hold it
    item("minecraft:swords", {.excludes = {"mace"}, .native = {"minecraft:is_sword"}});
    item("minecraft:pickaxes", {.native = {"minecraft:is_pickaxe"}});
    item("minecraft:axes", {.native = {"minecraft:is_axe"}});
    item("minecraft:shovels", {.native = {"minecraft:is_shovel"}});
    item("minecraft:hoes", {.native = {"minecraft:is_hoe"}});
    // Java 1.21.1's armour lists (no carved pumpkin, skull or mob head: Bedrock wears those, Java's
    // armour tags do not hold them)
    item("minecraft:head_armor", {.ids = armorIds("_helmet")});
    item("minecraft:chest_armor", {.ids = armorIds("_chestplate")});
    item("minecraft:leg_armor", {.ids = armorIds("_leggings")});
    item("minecraft:foot_armor", {.ids = armorIds("_boots")});
    // the enchantable tags nest as Java 1.21.1's do
    item("minecraft:enchantable/head_armor", {.include = {"minecraft:head_armor"}});
    item("minecraft:enchantable/chest_armor", {.include = {"minecraft:chest_armor"}});
    item("minecraft:enchantable/leg_armor", {.include = {"minecraft:leg_armor"}});
    item("minecraft:enchantable/foot_armor", {.include = {"minecraft:foot_armor"}});
    item("minecraft:enchantable/armor",
         {.include = {"minecraft:enchantable/foot_armor", "minecraft:enchantable/leg_armor", "minecraft:enchantable/chest_armor", "minecraft:enchantable/head_armor"}});
    item("minecraft:enchantable/sword", {.include = {"minecraft:swords"}});
    item("minecraft:enchantable/fire_aspect", {.ids = {"mace"}, .include = {"minecraft:enchantable/sword"}});
    item("minecraft:enchantable/sharp_weapon", {.include = {"minecraft:swords", "minecraft:axes"}});
    item("minecraft:enchantable/weapon", {.ids = {"mace"}, .include = {"minecraft:enchantable/sharp_weapon"}});
    item("minecraft:enchantable/mining", {.ids = {"shears"}, .include = {"minecraft:pickaxes", "minecraft:axes", "minecraft:shovels", "minecraft:hoes"}});
    item("minecraft:enchantable/mining_loot", {.include = {"minecraft:pickaxes", "minecraft:axes", "minecraft:shovels", "minecraft:hoes"}});
    item("minecraft:enchantable/durability",
         {.ids     = {"elytra", "shield", "bow", "crossbow", "trident", "flint_and_steel", "shears", "brush", "fishing_rod", "carrot_on_a_stick",
                      "warped_fungus_on_a_stick", "mace"},
          .include = {"minecraft:foot_armor", "minecraft:leg_armor", "minecraft:chest_armor", "minecraft:head_armor", "minecraft:swords", "minecraft:axes",
                      "minecraft:pickaxes", "minecraft:shovels", "minecraft:hoes"}});
    item("minecraft:enchantable/bow", {.ids = {"bow"}});
    item("minecraft:enchantable/crossbow", {.ids = {"crossbow"}});
    item("minecraft:enchantable/fishing", {.ids = {"fishing_rod"}});
    item("minecraft:enchantable/trident", {.ids = {"trident"}});
    item("minecraft:enchantable/mace", {.ids = {"mace"}});
    item("minecraft:enchantable/equippable",
         {.ids     = {"elytra", "carved_pumpkin"},
          .include = {"minecraft:foot_armor", "minecraft:leg_armor", "minecraft:chest_armor", "minecraft:head_armor", "minecraft:skulls"}});
    item("minecraft:enchantable/vanishing", {.ids = {"compass", "carved_pumpkin"}, .include = {"minecraft:enchantable/durability", "minecraft:skulls"}});
    item("minecraft:fishes", {.ids = {"cod", "salmon", "tropical_fish", "pufferfish", "cooked_cod", "cooked_salmon"}});
    item("minecraft:meat",
         {.ids = {"beef", "cooked_beef", "porkchop", "cooked_porkchop", "mutton", "cooked_mutton", "chicken", "cooked_chicken", "rabbit", "cooked_rabbit", "rotten_flesh"}});
    item("minecraft:candles", {.ids = {"candle"}, .suffixes = {"_candle"}});
    item("minecraft:skulls", {.ids = {"skull", "skeleton_skull", "wither_skeleton_skull", "zombie_head", "player_head", "creeper_head", "dragon_head", "piglin_head"}});
    item("minecraft:banners", {.ids = {"banner"}});
    item("minecraft:trim_materials", {.native = {"minecraft:trim_materials"}});
    item("minecraft:trimmable_armor", {.include = {"minecraft:foot_armor", "minecraft:leg_armor", "minecraft:chest_armor", "minecraft:head_armor"}});
    item("minecraft:dyeable", {.ids = {"leather_helmet", "leather_chestplate", "leather_leggings", "leather_boots", "leather_horse_armor", "wolf_armor"}});
    item("minecraft:freeze_immune_wearables", {.ids = {"leather_helmet", "leather_chestplate", "leather_leggings", "leather_boots", "leather_horse_armor"}});

    // ---- entity types ------------------------------------------------------------------------------
    entity("minecraft:skeletons", {.ids = {"skeleton", "stray", "wither_skeleton", "skeleton_horse", "bogged"}});
    entity("minecraft:zombies", {.ids = {"zombie", "husk", "drowned", "zombie_villager", "zombie_villager_v2", "zombie_horse", "zombie_pigman", "zoglin"}});
    entity("minecraft:undead", {.ids = {"wither", "phantom"}, .include = {"minecraft:skeletons", "minecraft:zombies"}});
    entity("minecraft:arthropod", {.ids = {"spider", "cave_spider", "bee", "silverfish", "endermite"}});
    entity("minecraft:arrows", {.ids = {"arrow"}});
    entity("minecraft:frog_food", {.ids = {"slime", "magma_cube"}});
    entity("minecraft:freeze_immune_entity_types", {.ids = {"stray", "polar_bear", "snow_golem", "wither"}});
    entity("minecraft:freeze_hurts_extra_types", {.ids = {"strider", "blaze", "magma_cube"}});
    entity("minecraft:fall_damage_immune",
           {.ids = {"iron_golem", "snow_golem", "shulker", "allay", "bat", "bee", "blaze", "cat", "chicken", "ghast", "phantom", "magma_cube", "ocelot", "parrot",
                    "wither", "breeze"}});
    // Java 1.21.1: #arrows, firework_rocket, snowball, small_fireball, fireball, wither_skull, egg,
    // trident, dragon_fireball, wind_charge, breeze_wind_charge
    entity("minecraft:impact_projectiles",
           {.ids     = {"fireworks_rocket", "snowball", "fireball", "small_fireball", "egg", "thrown_trident", "dragon_fireball", "wither_skull",
                        "wither_skull_dangerous", "wind_charge_projectile", "breeze_wind_charge_projectile"},
            .include = {"minecraft:arrows"}});
    entity("minecraft:redirectable_projectile", {.ids = {"fireball", "wind_charge_projectile", "breeze_wind_charge_projectile"}});
    entity("minecraft:powder_snow_walkable_mobs", {.ids = {"rabbit", "endermite", "silverfish", "fox"}});
    entity("minecraft:immune_to_oozing", {.ids = {"slime"}});
    entity("minecraft:immune_to_infested", {.ids = {"silverfish"}});

    // ---- biomes ------------------------------------------------------------------------------------
    // Java 1.21.1 (Bedrock's biomes have the same names): the spawner's BiomeTags.REDUCED_WATER_AMBIENT_SPAWNS
    biome("minecraft:is_river", {.ids = {"river", "frozen_river"}});
    biome("minecraft:reduce_water_ambient_spawns", {.include = {"minecraft:is_river"}});

    // ---- structures --------------------------------------------------------------------------------
    // Minecraft 1.21.1's data/minecraft/tags/worldgen/structure (every tag file), by Java structure id:
    // /locate and the structure HolderSets of TF's data name them (none generates in the Twilight
    // Forest: Java finds none there)
    structure("minecraft:cats_spawn_as_black", {.ids = {"swamp_hut"}});
    structure("minecraft:cats_spawn_in", {.ids = {"swamp_hut"}});
    structure("minecraft:dolphin_located", {.include = {"minecraft:ocean_ruin", "minecraft:shipwreck"}});
    structure("minecraft:eye_of_ender_located", {.ids = {"stronghold"}});
    structure("minecraft:mineshaft", {.ids = {"mineshaft", "mineshaft_mesa"}});
    structure("minecraft:ocean_ruin", {.ids = {"ocean_ruin_cold", "ocean_ruin_warm"}});
    structure("minecraft:on_ocean_explorer_maps", {.ids = {"monument"}});
    // the village and explorer map tags of the villager trade rebalance (added in 1.20.5)
    structure("minecraft:on_desert_village_maps", {.ids = {"village_desert"}});
    structure("minecraft:on_plains_village_maps", {.ids = {"village_plains"}});
    structure("minecraft:on_savanna_village_maps", {.ids = {"village_savanna"}});
    structure("minecraft:on_snowy_village_maps", {.ids = {"village_snowy"}});
    structure("minecraft:on_taiga_village_maps", {.ids = {"village_taiga"}});
    structure("minecraft:on_jungle_explorer_maps", {.ids = {"jungle_pyramid"}});
    structure("minecraft:on_swamp_explorer_maps", {.ids = {"swamp_hut"}});
    structure("minecraft:on_treasure_maps", {.ids = {"buried_treasure"}});
    structure("minecraft:on_trial_chambers_maps", {.ids = {"trial_chambers"}});
    structure("minecraft:on_woodland_explorer_maps", {.ids = {"mansion"}});
    structure("minecraft:ruined_portal",
              {.ids = {"ruined_portal_desert", "ruined_portal_jungle", "ruined_portal_mountain", "ruined_portal_nether", "ruined_portal_ocean", "ruined_portal",
                       "ruined_portal_swamp"}});
    structure("minecraft:shipwreck", {.ids = {"shipwreck", "shipwreck_beached"}});
    structure("minecraft:village", {.ids = {"village_plains", "village_desert", "village_savanna", "village_snowy", "village_taiga"}});

    // ---- NeoForge common tags (c:) -----------------------------------------------------------------
    // TF's data only adds TF entries to c: tags (replace: false); the vanilla members are NeoForge's
    // (21.1, data/c/tags), given here with Bedrock ids. Every c: tag TF's data and code reference.
    // ores and the ground they sit in
    both("c:ores/coal", {.include = {"minecraft:coal_ores"}});
    both("c:ores/copper", {.include = {"minecraft:copper_ores"}});
    both("c:ores/diamond", {.include = {"minecraft:diamond_ores"}});
    both("c:ores/emerald", {.include = {"minecraft:emerald_ores"}});
    both("c:ores/gold", {.include = {"minecraft:gold_ores"}});
    both("c:ores/iron", {.include = {"minecraft:iron_ores"}});
    both("c:ores/lapis", {.include = {"minecraft:lapis_ores"}});
    both("c:ores/redstone", {.include = {"minecraft:redstone_ores"}});
    both("c:ores/quartz", {.ids = {"quartz_ore", "nether_quartz_ore"}});
    both("c:ores/netherite_scrap", {.ids = {"ancient_debris"}});
    both("c:ores",
         {.include = {"c:ores/coal", "c:ores/copper", "c:ores/diamond", "c:ores/emerald", "c:ores/gold", "c:ores/iron", "c:ores/lapis", "c:ores/redstone",
                      "c:ores/quartz", "c:ores/netherite_scrap"}});
    both("c:ores_in_ground/stone",
         {.ids = {"coal_ore", "copper_ore", "diamond_ore", "emerald_ore", "gold_ore", "iron_ore", "lapis_ore", "redstone_ore", "lit_redstone_ore"}});
    both("c:ores_in_ground/deepslate",
         {.ids = {"deepslate_coal_ore", "deepslate_copper_ore", "deepslate_diamond_ore", "deepslate_emerald_ore", "deepslate_gold_ore", "deepslate_iron_ore",
                  "deepslate_lapis_ore", "deepslate_redstone_ore", "lit_deepslate_redstone_ore"}});
    both("c:ores_in_ground/netherrack", {.ids = {"nether_gold_ore", "quartz_ore", "nether_quartz_ore"}});
    both("c:ore_bearing_ground/stone", {.ids = {"stone"}});
    both("c:ore_bearing_ground/deepslate", {.ids = {"deepslate"}});
    both("c:ore_bearing_ground/netherrack", {.ids = {"netherrack"}});
    both("c:ore_rates/singular",
         {.ids = {"ancient_debris", "coal_ore", "deepslate_coal_ore", "deepslate_diamond_ore", "deepslate_emerald_ore", "deepslate_gold_ore", "deepslate_iron_ore",
                  "diamond_ore", "emerald_ore", "gold_ore", "iron_ore", "quartz_ore", "nether_quartz_ore"}});
    // ground and stone
    both("c:sands/colorless", {.ids = {"sand"}});
    both("c:sands/red", {.ids = {"red_sand"}});
    both("c:sands", {.include = {"c:sands/colorless", "c:sands/red"}});
    both("c:gravels", {.ids = {"gravel"}});
    both("c:end_stones", {.ids = {"end_stone"}});
    both("c:netherracks", {.ids = {"netherrack"}});
    both("c:obsidians", {.ids = {"obsidian", "crying_obsidian"}});
    both("c:stones", {.ids = {"stone", "andesite", "diorite", "granite", "tuff", "deepslate"}});
    both("c:cobblestones/normal", {.ids = {"cobblestone"}});
    both("c:cobblestones/mossy", {.ids = {"mossy_cobblestone"}});
    both("c:cobblestones/infested", {.ids = {"infested_cobblestone"}});
    both("c:cobblestones/deepslate", {.ids = {"cobbled_deepslate"}});
    both("c:cobblestones", {.include = {"c:cobblestones/normal", "c:cobblestones/mossy", "c:cobblestones/infested", "c:cobblestones/deepslate"}});
    both("c:sandstone/uncolored_blocks", {.ids = {"sandstone", "cut_sandstone", "chiseled_sandstone", "smooth_sandstone"}});
    both("c:sandstone/red_blocks", {.ids = {"red_sandstone", "cut_red_sandstone", "chiseled_red_sandstone", "smooth_red_sandstone"}});
    both("c:sandstone/blocks", {.include = {"c:sandstone/uncolored_blocks", "c:sandstone/red_blocks"}});
    both("c:glass_blocks/colorless", {.ids = {"glass"}});
    both("c:glass_blocks/tinted", {.ids = {"tinted_glass"}});
    both("c:glass_blocks/cheap", {.ids = concat(colorIds("_stained_glass"), {"glass"})});
    both("c:glass_blocks", {.include = {"c:glass_blocks/colorless", "c:glass_blocks/tinted", "c:glass_blocks/cheap"}});
    both("c:glass_panes/colorless", {.ids = {"glass_pane"}});
    both("c:glass_panes", {.ids = colorIds("_stained_glass_pane"), .include = {"c:glass_panes/colorless"}});
    // wooden things
    both("c:chests/wooden", {.ids = {"chest", "trapped_chest"}});
    both("c:chests/trapped", {.ids = {"trapped_chest"}});
    both("c:chests/ender", {.ids = {"ender_chest"}});
    both("c:chests", {.include = {"c:chests/wooden", "c:chests/trapped", "c:chests/ender"}});
    both("c:fence_gates/wooden", {.ids = {"fence_gate"}, .suffixes = {"_fence_gate"}});
    both("c:fences/wooden", {.include = {"minecraft:wooden_fences"}});
    both("c:fences/nether_brick", {.ids = {"nether_brick_fence"}});
    both("c:bookshelves", {.ids = {"bookshelf"}});
    both("c:player_workstations/crafting_tables", {.ids = {"crafting_table"}});
    both("c:player_workstations/furnaces", {.ids = {"furnace"}});
    both("c:stripped_logs",
         {.ids = {"stripped_oak_log", "stripped_spruce_log", "stripped_birch_log", "stripped_jungle_log", "stripped_acacia_log", "stripped_dark_oak_log",
                  "stripped_mangrove_log", "stripped_cherry_log", "stripped_crimson_stem", "stripped_warped_stem"}});
    both("c:stripped_woods",
         {.ids = {"stripped_oak_wood", "stripped_spruce_wood", "stripped_birch_wood", "stripped_jungle_wood", "stripped_acacia_wood", "stripped_dark_oak_wood",
                  "stripped_mangrove_wood", "stripped_cherry_wood", "stripped_crimson_hyphae", "stripped_warped_hyphae"}});
    // storage blocks
    both("c:storage_blocks/bone_meal", {.ids = {"bone_block"}});
    both("c:storage_blocks/coal", {.ids = {"coal_block"}});
    both("c:storage_blocks/copper", {.ids = {"copper_block"}});
    both("c:storage_blocks/diamond", {.ids = {"diamond_block"}});
    both("c:storage_blocks/dried_kelp", {.ids = {"dried_kelp_block"}});
    both("c:storage_blocks/emerald", {.ids = {"emerald_block"}});
    both("c:storage_blocks/gold", {.ids = {"gold_block"}});
    both("c:storage_blocks/iron", {.ids = {"iron_block"}});
    both("c:storage_blocks/lapis", {.ids = {"lapis_block"}});
    both("c:storage_blocks/netherite", {.ids = {"netherite_block"}});
    both("c:storage_blocks/raw_copper", {.ids = {"raw_copper_block"}});
    both("c:storage_blocks/raw_gold", {.ids = {"raw_gold_block"}});
    both("c:storage_blocks/raw_iron", {.ids = {"raw_iron_block"}});
    both("c:storage_blocks/redstone", {.ids = {"redstone_block"}});
    both("c:storage_blocks/slime", {.ids = {"slime"}});
    both("c:storage_blocks/wheat", {.ids = {"hay_block"}});
    both("c:storage_blocks",
         {.include = {"c:storage_blocks/bone_meal", "c:storage_blocks/coal", "c:storage_blocks/copper", "c:storage_blocks/diamond", "c:storage_blocks/dried_kelp",
                      "c:storage_blocks/emerald", "c:storage_blocks/gold", "c:storage_blocks/iron", "c:storage_blocks/lapis", "c:storage_blocks/netherite",
                      "c:storage_blocks/raw_copper", "c:storage_blocks/raw_gold", "c:storage_blocks/raw_iron", "c:storage_blocks/redstone", "c:storage_blocks/slime",
                      "c:storage_blocks/wheat"}});
    both("c:mushrooms", {.ids = {"red_mushroom", "brown_mushroom"}});
    both("c:ropes", {});
    // items only
    item("c:seeds/beetroot", {.ids = {"beetroot_seeds"}});
    item("c:seeds/melon", {.ids = {"melon_seeds"}});
    item("c:seeds/pumpkin", {.ids = {"pumpkin_seeds"}});
    item("c:seeds/torchflower", {.ids = {"torchflower_seeds"}});
    item("c:seeds/pitcher_pod", {.ids = {"pitcher_pod"}});
    item("c:seeds/wheat", {.ids = {"wheat_seeds"}});
    item("c:seeds", {.include = {"c:seeds/beetroot", "c:seeds/melon", "c:seeds/pumpkin", "c:seeds/torchflower", "c:seeds/pitcher_pod", "c:seeds/wheat"}});
    item("c:crops/beetroot", {.ids = {"beetroot"}});
    item("c:crops/cactus", {.ids = {"cactus"}});
    item("c:crops/carrot", {.ids = {"carrot"}});
    item("c:crops/cocoa_bean", {.ids = {"cocoa_beans"}});
    item("c:crops/melon", {.ids = {"melon_slice"}});
    item("c:crops/nether_wart", {.ids = {"nether_wart"}});
    item("c:crops/potato", {.ids = {"potato"}});
    item("c:crops/pumpkin", {.ids = {"pumpkin"}});
    item("c:crops/sugar_cane", {.ids = {"sugar_cane"}});
    item("c:crops/wheat", {.ids = {"wheat"}});
    item("c:crops",
         {.include = {"c:crops/beetroot", "c:crops/cactus", "c:crops/carrot", "c:crops/cocoa_bean", "c:crops/melon", "c:crops/nether_wart", "c:crops/potato",
                      "c:crops/pumpkin", "c:crops/sugar_cane", "c:crops/wheat"}});
    item("c:rods/wooden", {.ids = {"stick"}});
    item("c:rods/blaze", {.ids = {"blaze_rod"}});
    item("c:rods/breeze", {.ids = {"breeze_rod"}});
    item("c:rods", {.include = {"c:rods/wooden", "c:rods/blaze", "c:rods/breeze"}});
    item("c:ingots/iron", {.ids = {"iron_ingot"}});
    item("c:ingots/gold", {.ids = {"gold_ingot"}});
    item("c:ingots/copper", {.ids = {"copper_ingot"}});
    item("c:ingots/netherite", {.ids = {"netherite_ingot"}});
    item("c:ingots", {.include = {"c:ingots/iron", "c:ingots/gold", "c:ingots/copper", "c:ingots/netherite"}});
    item("c:nuggets/iron", {.ids = {"iron_nugget"}});
    item("c:nuggets/gold", {.ids = {"gold_nugget"}});
    item("c:nuggets", {.include = {"c:nuggets/iron", "c:nuggets/gold"}});
    item("c:raw_materials/iron", {.ids = {"raw_iron"}});
    item("c:raw_materials/gold", {.ids = {"raw_gold"}});
    item("c:raw_materials/copper", {.ids = {"raw_copper"}});
    item("c:raw_materials", {.include = {"c:raw_materials/iron", "c:raw_materials/gold", "c:raw_materials/copper"}});
    item("c:gems/amethyst", {.ids = {"amethyst_shard"}});
    item("c:gems/diamond", {.ids = {"diamond"}});
    item("c:gems/emerald", {.ids = {"emerald"}});
    item("c:gems/lapis", {.ids = {"lapis_lazuli"}});
    item("c:gems/prismarine", {.ids = {"prismarine_crystals"}});
    item("c:gems/quartz", {.ids = {"quartz"}});
    item("c:gems", {.include = {"c:gems/amethyst", "c:gems/diamond", "c:gems/emerald", "c:gems/lapis", "c:gems/prismarine", "c:gems/quartz"}});
    item("c:dusts/glowstone", {.ids = {"glowstone_dust"}});
    item("c:dusts/redstone", {.ids = {"redstone"}});
    item("c:dusts", {.include = {"c:dusts/glowstone", "c:dusts/redstone"}});
    item("c:ender_pearls", {.ids = {"ender_pearl"}});
    item("c:strings", {.ids = {"string"}});
    item("c:leathers", {.ids = {"leather"}});
    item("c:slime_balls", {.ids = {"slime_ball"}});
    item("c:bones", {.ids = {"bone"}});
    item("c:feathers", {.ids = {"feather"}});
    item("c:gunpowders", {.ids = {"gunpowder"}});
    item("c:eggs", {.ids = {"egg"}});
    item("c:paper", {.ids = {"paper"}});
    item("c:bricks/normal", {.ids = {"brick"}});
    item("c:bricks/nether", {.ids = {"netherbrick", "nether_brick"}});
    item("c:bricks", {.include = {"c:bricks/normal", "c:bricks/nether"}});
    item("c:music_discs", {.pred = [](std::string const& id) { return id.starts_with("minecraft:music_disc_"); }});
    item("c:tools/bow", {.ids = {"bow"}});
    item("c:tools/crossbow", {.ids = {"crossbow"}});
    item("c:tools/fishing_rod", {.ids = {"fishing_rod"}});
    item("c:tools/shield", {.ids = {"shield"}});
    item("c:tools/shear", {.ids = {"shears"}});
    item("c:tools/spear", {.ids = {"trident"}});
    item("c:tools/brush", {.ids = {"brush"}});
    item("c:tools/igniter", {.ids = {"flint_and_steel"}});
    item("c:tools/mace", {.ids = {"mace"}});
    item("c:tools/mining_tool", {.include = {"minecraft:pickaxes"}});
    item("c:tools/melee_weapon", {.include = {"minecraft:swords", "minecraft:axes"}});
    item("c:tools/range_weapon", {.include = {"c:tools/bow", "c:tools/crossbow", "c:tools/spear"}});
    item("c:tools",
         {.include = {"minecraft:swords", "minecraft:axes", "minecraft:pickaxes", "minecraft:shovels", "minecraft:hoes", "c:tools/bow", "c:tools/crossbow",
                      "c:tools/fishing_rod", "c:tools/shield", "c:tools/shear", "c:tools/spear", "c:tools/brush", "c:tools/igniter", "c:tools/mace"}});
    item("c:armors", {.include = {"minecraft:head_armor", "minecraft:chest_armor", "minecraft:leg_armor", "minecraft:foot_armor"}});
    item("c:enchantables", {.include = {"minecraft:enchantable/durability"}});
    // foods (NeoForge 21.1's c:foods/* with Bedrock ids)
    item("c:foods/fruit", {.ids = {"apple", "golden_apple", "enchanted_golden_apple", "appleenchanted", "chorus_fruit", "melon_slice"}});
    item("c:foods/vegetable", {.ids = {"carrot", "golden_carrot", "potato", "beetroot"}});
    item("c:foods/berry", {.ids = {"sweet_berries", "glow_berries"}});
    item("c:foods/bread", {.ids = {"bread"}});
    item("c:foods/cookie", {.ids = {"cookie"}});
    item("c:foods/raw_meat", {.ids = {"beef", "porkchop", "chicken", "rabbit", "mutton"}});
    item("c:foods/cooked_meat", {.ids = {"cooked_beef", "cooked_porkchop", "cooked_chicken", "cooked_rabbit", "cooked_mutton"}});
    item("c:foods/raw_fish", {.ids = {"cod", "salmon", "tropical_fish", "pufferfish"}});
    item("c:foods/cooked_fish", {.ids = {"cooked_cod", "cooked_salmon"}});
    item("c:foods/soup", {.ids = {"beetroot_soup", "mushroom_stew", "rabbit_stew", "suspicious_stew"}});
    item("c:foods/candy", {});
    item("c:foods/pie", {.ids = {"pumpkin_pie"}});
    item("c:foods/golden", {.ids = {"golden_apple", "enchanted_golden_apple", "appleenchanted", "golden_carrot"}});
    item("c:foods/edible_when_placed", {.ids = {"cake"}});
    item("c:foods/food_poisoning", {.ids = {"poisonous_potato", "pufferfish", "rotten_flesh", "spider_eye", "chicken", "suspicious_stew"}});
    item("c:foods",
         {.ids = {"baked_potato", "dried_kelp", "honey_bottle", "ominous_bottle"},
          .include = {"c:foods/fruit", "c:foods/vegetable", "c:foods/berry", "c:foods/bread", "c:foods/cookie", "c:foods/raw_meat", "c:foods/cooked_meat",
                      "c:foods/raw_fish", "c:foods/cooked_fish", "c:foods/soup", "c:foods/pie", "c:foods/golden", "c:foods/edible_when_placed",
                      "c:foods/food_poisoning"}});
    // entity types
    entity("c:bosses", {.ids = {"ender_dragon", "wither"}});
    entity("c:minecarts", {.ids = {"minecart", "chest_minecart", "command_block_minecart", "hopper_minecart", "tnt_minecart"}});
    entity("c:boats", {.ids = {"boat", "chest_boat"}});
    entity("c:capturing_not_supported", {});
    entity("c:teleporting_not_supported", {});

    // ---- fluids ------------------------------------------------------------------------------------
    fluid("minecraft:water", {.ids = {"water", "flowing_water"}});
    fluid("minecraft:lava", {.ids = {"lava", "flowing_lava"}});
    block("minecraft:lava", {.ids = {"lava", "flowing_lava"}});

    for (auto& [key, rule] : r)
        for (auto t : rule.native) rule.nativeHashed.emplace_back(t);
    return r;
}

Rules const& rules() {
    static Rules const r = buildRules();
    return r;
}

/// Whether `type` (`item`) has one of the rule's native tags (server thread: reads the game's types).
bool nativeHasAny(Rule const& rule, BlockType const* type, Item const* item) {
    for (auto const& t : rule.nativeHashed)
        if ((type && type->hasTag(t)) || (item && item->hasTag(t))) return true;
    return false;
}

bool excluded(Rule const& rule, std::string_view name) {
    for (auto e : rule.excludes)
        if (e.starts_with('*') ? name.ends_with(e.substr(1)) : name == e) return true;
    return false;
}

constexpr std::string_view kMinecraft = "minecraft:";

} // namespace

bool newerThanJava1211(std::string const& id) {
    return id.starts_with(kMinecraft) && newerThan1211(std::string_view(id).substr(kMinecraft.size()));
}

Rule const* find(Registry registry, std::string const& tag) {
    auto it = rules().find({registry, tag});
    return it == rules().end() ? nullptr : &it->second;
}

bool known(Registry registry, std::string const& tag) { return find(registry, tag) != nullptr; }

std::vector<std::string> includes(Rule const& rule) { return {rule.include.begin(), rule.include.end()}; }

std::vector<std::string> listedIds(Rule const& rule) {
    std::vector<std::string> out;
    for (auto id : rule.ids)
        if (!excluded(rule, id) && !newerThan1211(id)) out.push_back(std::string(kMinecraft) + std::string(id));
    return out;
}

bool hasPatterns(Rule const& rule) { return !rule.suffixes.empty() || !rule.native.empty() || rule.pred != nullptr; }

/// Per rule with native tags: the registered ids that have one of them.
struct NativeIndex {
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    using Ids = std::unordered_set<std::string, Hash, std::equal_to<>>;
    std::unordered_map<Rule const*, Ids> byRule;
    std::size_t                          size = 0;

    [[nodiscard]] bool has(Rule const& rule, std::string const& id) const {
        auto it = byRule.find(&rule);
        return it != byRule.end() && it->second.contains(id);
    }
};

std::shared_ptr<NativeIndex const> buildNativeIndex() {
    auto index = std::make_shared<NativeIndex>();
    for (auto registry : {Registry::Block, Registry::Item}) {
        std::vector<Rule const*> withNative;
        for (auto const& [key, rule] : rules())
            if (key.first == registry && !rule.nativeHashed.empty()) withNative.push_back(&rule);
        if (withNative.empty()) continue;
        // one lookup per registered id, then every rule's native tags on it
        for (auto const& id : registryIds(registry)) {
            BlockType const* type = nullptr;
            Item const*      item = nullptr;
            if (registry == Registry::Block) {
                auto t = BlockType::tryGetFromRegistry(HashedString(id));
                type   = t ? &*t : nullptr;
            } else {
                item = itemOf(id);
            }
            if (!type && !item) continue;
            for (auto const* rule : withNative)
                if (nativeHasAny(*rule, type, item) && index->byRule[rule].insert(id).second) ++index->size;
        }
    }
    return index;
}

std::size_t nativeIndexSize(NativeIndex const* native) { return native ? native->size : 0; }

bool sameNativeIndex(NativeIndex const* a, NativeIndex const* b) {
    if (nativeIndexSize(a) == 0 || nativeIndexSize(b) == 0) return nativeIndexSize(a) == nativeIndexSize(b);
    return a->size == b->size && a->byRule == b->byRule;
}

bool matchesPattern(Rule const& rule, std::string const& id, NativeIndex const* native) {
    // Bedrock's native tags keep newer content too: they are Bedrock's own mechanics (what a pickaxe
    // mines, what a sword or a log is; VanillaTags.h, policy)
    auto nativeHit = [&] { return native && !rule.nativeHashed.empty() && native->has(rule, id); };
    if (id.starts_with(kMinecraft)) {
        std::string_view name = std::string_view(id).substr(kMinecraft.size());
        if (excluded(rule, name)) return false;
        if (newerThan1211(name)) return nativeHit();
        for (auto s : rule.suffixes) // the cheap test first (world generation asks per block)
            if (name.size() > s.size() && name.ends_with(s)) return true;
    }
    return nativeHit() || (rule.pred && rule.pred(id));
}

std::vector<std::string> registryIds(Registry registry) {
    std::vector<std::string> out;
    if (registry == Registry::Block) {
        auto level = ll::service::getLevel();
        if (!level) return out;
        std::set<std::string> names;
        for (auto const* block : level->getBlockPalette().mBlockFromNetworkId.get())
            if (block) names.insert(block->getTypeName());
        out.assign(names.begin(), names.end());
    } else if (registry == Registry::Item) {
        auto ref = ItemRegistryManager::getItemRegistry();
        if (!ref.isRegistryInitialized()) return out;
        for (auto const& [name, item] : ref.getNameToItemMap()) out.push_back(name.getString());
    } else {
        // the bridge's id lists (every rule of the registry: the includes are rules of it too)
        std::set<std::string> ids;
        for (auto const& [key, rule] : rules())
            if (key.first == registry)
                for (auto& id : listedIds(rule)) ids.insert(std::move(id));
        out.assign(ids.begin(), ids.end());
    }
    return out;
}

std::vector<std::string> knownTags(Registry registry) {
    std::vector<std::string> out;
    for (auto const& [key, rule] : rules())
        if (key.first == registry) out.push_back(key.second);
    return out;
}

} // namespace gf::tags::vanilla
