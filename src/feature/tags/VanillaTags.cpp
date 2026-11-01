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
constexpr std::string_view kOverworldWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak"};

std::vector<std::string_view> woodIds(std::string_view suffix, bool netherAndBamboo = true) {
    static std::set<std::string> storage; // keeps the generated names alive
    std::vector<std::string_view> out;
    for (auto w : kWoods) {
        if (!netherAndBamboo && (w == "bamboo" || w == "crimson" || w == "warped")) continue;
        auto [it, _] = storage.insert(std::string(w) + std::string(suffix));
        out.push_back(*it);
    }
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
    entity("minecraft:impact_projectiles",
           {.ids = {"arrow", "fireworks_rocket", "snowball", "fireball", "small_fireball", "egg", "thrown_trident", "dragon_fireball", "wither_skull",
                    "wither_skull_dangerous", "wind_charge_projectile", "breeze_wind_charge_projectile", "llama_spit", "shulker_bullet", "xp_bottle"}});
    entity("minecraft:redirectable_projectile", {.ids = {"fireball", "wind_charge_projectile", "breeze_wind_charge_projectile"}});
    entity("minecraft:powder_snow_walkable_mobs", {.ids = {"rabbit", "endermite", "silverfish", "fox"}});
    entity("minecraft:immune_to_oozing", {.ids = {"slime"}});
    entity("minecraft:immune_to_infested", {.ids = {"silverfish"}});

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
