// The vanilla tag bridge of the tag runtime (see VanillaTags.h).
#include "feature/tags/VanillaTags.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/BlockPalette.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "ll/api/service/Bedrock.h"

#include "game/Items.h"

namespace gf::tags::vanilla {

namespace {

using Pred = bool (*)(std::string const& id);

struct Rule {
    std::vector<std::string_view> ids;      ///< minecraft ids without the namespace
    std::vector<std::string_view> suffixes; ///< minecraft ids ending with one of these
    std::vector<std::string_view> excludes; ///< minecraft ids never in the tag ("*suffix": ids ending with it)
    std::vector<std::string_view> native;   ///< Bedrock block / item tags (any namespace)
    std::vector<std::string_view> include;  ///< other bridged tags of the same registry
    Pred                          pred = nullptr;
};

constexpr std::string_view kWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak", "bamboo", "crimson", "warped"};
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

std::vector<std::string_view> concat(std::vector<std::string_view> a, std::vector<std::string_view> const& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

/// The stack's armour slot (-1: not humanoid armour).
int armorSlotOf(std::string const& id) {
    static std::unordered_map<std::string, int> cache;
    static std::mutex                           m;
    {
        std::lock_guard lock(m);
        if (auto it = cache.find(id); it != cache.end()) return it->second;
    }
    int  slot  = -1;
    auto stack = game::items::make(id);
    if (stack && (stack->isArmorItem() || stack->isHumanoidWearableItem())) {
        auto s = stack->getArmorSlot();
        if (s >= SharedTypes::Legacy::ArmorSlot::Head && s <= SharedTypes::Legacy::ArmorSlot::Feet) slot = static_cast<int>(s);
    }
    std::lock_guard lock(m);
    cache[id] = slot;
    return slot;
}

Item const* itemOf(std::string const& id) {
    auto ref  = ItemRegistryManager::getItemRegistry();
    auto item = ref.lookupByName(HashedString(id));
    return item.get();
}

bool isHead(std::string const& id) { return armorSlotOf(id) == 0; }
bool isChest(std::string const& id) { return armorSlotOf(id) == 1; }
bool isLegs(std::string const& id) { return armorSlotOf(id) == 2; }
bool isFeet(std::string const& id) { return armorSlotOf(id) == 3; }
bool isArmor(std::string const& id) { return armorSlotOf(id) >= 0; }
bool isBoat(std::string const& id) {
    return id.starts_with("minecraft:") && id.ends_with("_boat") && !id.ends_with("_chest_boat");
}
bool isDamageable(std::string const& id) {
    auto const* item = itemOf(id);
    return item && item->isDamageable();
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
    auto entity = [&](std::string const& tag, Rule rule) { r[{R::EntityType, tag}] = std::move(rule); };
    auto fluid  = [&](std::string const& tag, Rule rule) { r[{R::Fluid, tag}] = std::move(rule); };

    // ---- wood --------------------------------------------------------------------------------------
    both("minecraft:crimson_stems", {.ids = {"crimson_stem", "stripped_crimson_stem", "crimson_hyphae", "stripped_crimson_hyphae"}});
    both("minecraft:warped_stems", {.ids = {"warped_stem", "stripped_warped_stem", "warped_hyphae", "stripped_warped_hyphae"}});
    both("minecraft:logs_that_burn", {.suffixes = {"_log", "_wood"}, .native = {}});
    block("minecraft:logs", {.native = {"log"}, .include = {"minecraft:logs_that_burn", "minecraft:crimson_stems", "minecraft:warped_stems"}});
    item("minecraft:logs", {.native = {"minecraft:logs"}, .include = {"minecraft:logs_that_burn", "minecraft:crimson_stems", "minecraft:warped_stems"}});
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
    block("minecraft:standing_signs", {.ids = concat(woodIds("_standing_sign"), {"standing_sign"})});
    block("minecraft:wall_signs", {.ids = concat(woodIds("_wall_sign"), {"wall_sign"})});
    block("minecraft:ceiling_hanging_signs", {.suffixes = {"_hanging_sign"}});
    block("minecraft:wall_hanging_signs", {.suffixes = {"_hanging_sign"}});
    item("minecraft:signs", {.ids = woodIds("_sign")});
    item("minecraft:hanging_signs", {.suffixes = {"_hanging_sign"}});
    item("minecraft:boats", {.ids = {"bamboo_raft"}, .pred = isBoat});
    item("minecraft:chest_boats", {.ids = {"bamboo_chest_raft"}, .suffixes = {"_chest_boat"}});

    // ---- building ----------------------------------------------------------------------------------
    both("minecraft:stairs", {.suffixes = {"_stairs"}});
    both("minecraft:slabs", {.suffixes = {"_slab"}, .excludes = {"double_stone_block_slab"}});
    both("minecraft:walls", {.suffixes = {"_wall"}});
    both("minecraft:buttons", {.ids = {"wooden_button", "stone_button"}, .suffixes = {"_button"}});
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
    both("minecraft:dirt", {.ids = {"dirt", "coarse_dirt", "grass_block", "grass", "podzol", "mycelium", "rooted_dirt", "dirt_with_roots", "moss_block", "mud", "muddy_mangrove_roots", "pale_moss_block"}});
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
    block("minecraft:overworld_carver_replaceables",
          {.ids = {"water", "gravel", "suspicious_gravel", "sandstone", "red_sandstone", "calcite", "packed_ice", "raw_iron_block", "raw_copper_block"},
           .include = {"minecraft:base_stone_overworld", "minecraft:dirt", "minecraft:sand", "minecraft:terracotta", "minecraft:iron_ores", "minecraft:copper_ores", "minecraft:snow"}});
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
                  "cornflower", "lily_of_the_valley", "wither_rose", "torchflower", "closed_eyeblossom", "open_eyeblossom", "red_flower", "yellow_flower"}});
    both("minecraft:tall_flowers", {.ids = {"sunflower", "lilac", "rose_bush", "peony", "pitcher_plant"}});
    both("minecraft:flowers",
         {.ids = {"flowering_azalea", "azalea_leaves_flowered", "mangrove_propagule", "cherry_leaves", "pink_petals", "chorus_flower", "spore_blossom", "cactus_flower", "wildflowers"},
          .include = {"minecraft:small_flowers", "minecraft:tall_flowers"}});
    block("minecraft:crops", {.ids = {"wheat", "carrots", "potatoes", "beetroot", "melon_stem", "pumpkin_stem", "torchflower_crop", "pitcher_crop"}});
    block("minecraft:replaceable",
          {.ids = {"air", "water", "flowing_water", "lava", "flowing_lava", "short_grass", "tallgrass", "fern", "deadbush", "dead_bush", "seagrass", "fire", "soul_fire",
                   "snow_layer", "vine", "glow_lichen", "light_block", "tall_grass", "large_fern", "structure_void", "hanging_roots", "crimson_roots", "warped_roots",
                   "nether_sprouts", "bush", "firefly_bush", "short_dry_grass", "tall_dry_grass", "leaf_litter"}});
    block("minecraft:replaceable_by_trees",
          {.ids = {"short_grass", "tallgrass", "fern", "deadbush", "dead_bush", "vine", "glow_lichen", "sunflower", "lilac", "rose_bush", "peony", "tall_grass",
                   "large_fern", "hanging_roots", "pitcher_plant", "water", "flowing_water", "seagrass", "warped_roots", "nether_sprouts", "crimson_roots",
                   "leaf_litter", "bush", "firefly_bush", "short_dry_grass", "tall_dry_grass"},
           .include = {"minecraft:leaves"}});
    block("minecraft:features_cannot_replace", {.ids = {"bedrock", "mob_spawner", "spawner", "chest", "end_portal_frame", "reinforced_deepslate", "trial_spawner", "vault"}});
    block("minecraft:lava_pool_stone_cannot_replace", {.include = {"minecraft:features_cannot_replace", "minecraft:leaves", "minecraft:logs"}});
    block("minecraft:frog_prefer_jump_to", {.ids = {"waterlily", "lily_pad", "big_dripleaf"}});

    // ---- mining ------------------------------------------------------------------------------------
    block("minecraft:mineable/pickaxe", {.native = {"minecraft:is_pickaxe_item_destructible"}});
    block("minecraft:mineable/axe", {.native = {"minecraft:is_axe_item_destructible"}});
    block("minecraft:mineable/shovel", {.native = {"minecraft:is_shovel_item_destructible"}});
    block("minecraft:mineable/hoe", {.native = {"minecraft:is_hoe_item_destructible"}});
    block("minecraft:sword_efficient", {.native = {"minecraft:is_sword_item_destructible"}, .include = {"minecraft:leaves"}});
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
          {.ids = {"barrier", "bedrock", "end_portal", "end_portal_frame", "end_gateway", "command_block", "repeating_command_block", "chain_command_block",
                   "structure_block", "jigsaw", "moving_block", "light_block", "reinforced_deepslate"}});
    block("minecraft:dragon_immune",
          {.ids = {"barrier", "bedrock", "end_portal", "end_portal_frame", "end_gateway", "command_block", "repeating_command_block", "chain_command_block",
                   "structure_block", "jigsaw", "moving_block", "obsidian", "crying_obsidian", "end_stone", "iron_bars", "respawn_anchor", "reinforced_deepslate"}});

    // ---- items -------------------------------------------------------------------------------------
    item("minecraft:swords", {.native = {"minecraft:is_sword"}});
    item("minecraft:pickaxes", {.native = {"minecraft:is_pickaxe"}});
    item("minecraft:axes", {.native = {"minecraft:is_axe"}});
    item("minecraft:shovels", {.native = {"minecraft:is_shovel"}});
    item("minecraft:hoes", {.native = {"minecraft:is_hoe"}});
    item("minecraft:head_armor", {.pred = isHead});
    item("minecraft:chest_armor", {.pred = isChest});
    item("minecraft:leg_armor", {.pred = isLegs});
    item("minecraft:foot_armor", {.pred = isFeet});
    item("minecraft:enchantable/head_armor", {.pred = isHead});
    item("minecraft:enchantable/chest_armor", {.pred = isChest});
    item("minecraft:enchantable/leg_armor", {.pred = isLegs});
    item("minecraft:enchantable/foot_armor", {.pred = isFeet});
    item("minecraft:enchantable/armor", {.pred = isArmor});
    item("minecraft:enchantable/sword", {.include = {"minecraft:swords"}});
    item("minecraft:enchantable/fire_aspect", {.include = {"minecraft:swords"}});
    item("minecraft:enchantable/sharp_weapon", {.include = {"minecraft:swords", "minecraft:axes"}});
    item("minecraft:enchantable/weapon", {.include = {"minecraft:swords", "minecraft:axes"}});
    item("minecraft:enchantable/mining", {.ids = {"shears"}, .include = {"minecraft:pickaxes", "minecraft:axes", "minecraft:shovels", "minecraft:hoes"}});
    item("minecraft:enchantable/mining_loot", {.include = {"minecraft:pickaxes", "minecraft:axes", "minecraft:shovels", "minecraft:hoes"}});
    item("minecraft:enchantable/durability", {.pred = isDamageable});
    item("minecraft:enchantable/bow", {.ids = {"bow"}});
    item("minecraft:enchantable/crossbow", {.ids = {"crossbow"}});
    item("minecraft:enchantable/fishing", {.ids = {"fishing_rod"}});
    item("minecraft:enchantable/trident", {.ids = {"trident"}});
    item("minecraft:enchantable/equippable",
         {.ids = {"elytra", "carved_pumpkin", "skull", "skeleton_skull", "wither_skeleton_skull", "zombie_head", "player_head", "creeper_head", "dragon_head", "piglin_head"},
          .pred = isArmor});
    item("minecraft:enchantable/vanishing", {.ids = {"compass", "recovery_compass", "carved_pumpkin", "skull"}, .include = {"minecraft:enchantable/durability"}});
    item("minecraft:fishes", {.ids = {"cod", "salmon", "tropical_fish", "pufferfish", "cooked_cod", "cooked_salmon"}});
    item("minecraft:meat",
         {.ids = {"beef", "cooked_beef", "porkchop", "cooked_porkchop", "mutton", "cooked_mutton", "chicken", "cooked_chicken", "rabbit", "cooked_rabbit", "rotten_flesh"}});
    item("minecraft:candles", {.ids = {"candle"}, .suffixes = {"_candle"}});
    item("minecraft:skulls", {.ids = {"skull", "skeleton_skull", "wither_skeleton_skull", "zombie_head", "player_head", "creeper_head", "dragon_head", "piglin_head"}});
    item("minecraft:banners", {.ids = {"banner"}});
    item("minecraft:trim_materials", {.native = {"minecraft:trim_materials"}});
    item("minecraft:trimmable_armor", {.native = {"minecraft:trimmable_armors"}});
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
                    "wither", "breeze", "happy_ghast"}});
    // Java 1.21.1: #arrows, firework_rocket, snowball, small_fireball, fireball, wither_skull, egg,
    // trident, dragon_fireball, wind_charge, breeze_wind_charge
    entity("minecraft:impact_projectiles",
           {.ids = {"arrow", "fireworks_rocket", "snowball", "fireball", "small_fireball", "egg", "thrown_trident", "dragon_fireball", "wither_skull",
                    "wither_skull_dangerous", "wind_charge_projectile", "breeze_wind_charge_projectile"}});
    entity("minecraft:redirectable_projectile", {.ids = {"fireball", "wind_charge_projectile", "breeze_wind_charge_projectile"}});
    entity("minecraft:powder_snow_walkable_mobs", {.ids = {"rabbit", "endermite", "silverfish", "fox"}});
    entity("minecraft:immune_to_oozing", {.ids = {"slime"}});
    entity("minecraft:immune_to_infested", {.ids = {"silverfish"}});

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
                  "stripped_mangrove_log", "stripped_cherry_log", "stripped_pale_oak_log", "stripped_crimson_stem", "stripped_warped_stem"}});
    both("c:stripped_woods",
         {.ids = {"stripped_oak_wood", "stripped_spruce_wood", "stripped_birch_wood", "stripped_jungle_wood", "stripped_acacia_wood", "stripped_dark_oak_wood",
                  "stripped_mangrove_wood", "stripped_cherry_wood", "stripped_pale_oak_wood", "stripped_crimson_hyphae", "stripped_warped_hyphae"}});
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
    item("c:eggs", {.ids = {"egg", "blue_egg", "brown_egg"}});
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
    return r;
}

Rules const& rules() {
    static Rules const r = buildRules();
    return r;
}

Rule const* ruleOf(Registry registry, std::string const& tag) {
    auto it = rules().find({registry, tag});
    return it == rules().end() ? nullptr : &it->second;
}

bool nativeHas(Registry registry, std::string const& id, std::string_view tag) {
    if (registry == Registry::Block) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        return type && type->hasTag(HashedString(std::string(tag)));
    }
    if (registry == Registry::Item) {
        auto const* item = itemOf(id);
        return item && item->hasTag(HashedString(std::string(tag)));
    }
    return false;
}

bool ruleHas(Registry registry, Rule const& rule, std::string const& id, int depth) {
    if (depth > 8) return false;
    constexpr std::string_view mc = "minecraft:";
    if (id.starts_with(mc)) {
        std::string_view name = std::string_view(id).substr(mc.size());
        for (auto e : rule.excludes) {
            if (e.starts_with('*') ? name.ends_with(e.substr(1)) : name == e) return false;
        }
        if (std::find(rule.ids.begin(), rule.ids.end(), name) != rule.ids.end()) return true;
        for (auto s : rule.suffixes)
            if (name.size() > s.size() && name.ends_with(s)) return true;
    }
    for (auto t : rule.native)
        if (nativeHas(registry, id, t)) return true;
    if (rule.pred && rule.pred(id)) return true;
    for (auto t : rule.include)
        if (auto const* sub = ruleOf(registry, std::string(t)); sub && ruleHas(registry, *sub, id, depth + 1)) return true;
    return false;
}

/// Every id of the registry (blocks / items from the game, others from the bridge's lists).
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
        std::set<std::string> ids;
        std::function<void(Rule const&, int)> collect = [&](Rule const& rule, int depth) {
            if (depth > 8) return;
            for (auto i : rule.ids) ids.insert("minecraft:" + std::string(i));
            for (auto t : rule.include)
                if (auto const* sub = ruleOf(registry, std::string(t))) collect(*sub, depth + 1);
        };
        for (auto const& [key, rule] : rules())
            if (key.first == registry) collect(rule, 0);
        out.assign(ids.begin(), ids.end());
    }
    return out;
}

} // namespace

bool known(Registry registry, std::string const& tag) { return ruleOf(registry, tag) != nullptr; }

bool has(Registry registry, std::string const& tag, std::string const& id) {
    auto const* rule = ruleOf(registry, tag);
    return rule && !id.empty() && ruleHas(registry, *rule, id, 0);
}

std::vector<std::string> members(Registry registry, std::string const& tag) {
    std::vector<std::string> out;
    auto const*              rule = ruleOf(registry, tag);
    if (!rule) return out;
    for (auto const& id : registryIds(registry))
        if (ruleHas(registry, *rule, id, 0)) out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> knownTags(Registry registry) {
    std::vector<std::string> out;
    for (auto const& [key, rule] : rules())
        if (key.first == registry) out.push_back(key.second);
    return out;
}

} // namespace gf::tags::vanilla
