// Selftests of F9 (prefix "item.", testPos dx 5900..5999): the item behaviour registry and its
// hooks, cooldowns, tooltips as lore, dispensers, custom enchantments through the anvil and the
// grindstone, carrier effects, recipes and special crafting, tags (data and the vanilla bridge),
// data maps, statistics, traits, Travellers' Gear modifiers and the creative catalog. They use the
// gf_test fixtures (assets/test) and synthetic data trees, and remove every registration they add.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <tuple>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/platform/UUID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/ContainerID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/inventory/network/crafting/CraftHandlerGrindstone.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/catalog/Catalog.h"
#include "feature/catalog/CatalogInternal.h"
#include "feature/common/Interactions.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/datamaps/DataMapsInternal.h"
#include "feature/effects/Effects.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/DataMaps.h"
#include "feature/ext/item/Enchant.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/item/Recipes.h"
#include "feature/ext/item/Tags.h"
#include "feature/gear/Gear.h"
#include "feature/gear/GearInternal.h"
#include "feature/item/DataFiles.h"
#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "feature/net/Net.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/stats/Stats.h"
#include "feature/stats/StatsInternal.h"
#include "feature/tags/Tags.h"
#include "feature/tags/TagsInternal.h"
#include "feature/traits/Traits.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonDoc = nlohmann::json;
namespace items = gf::game::items;
namespace ia    = gf::interactions;
namespace data  = gf::feature::item::data;

constexpr char kFixture[] = "gf_test:fixture_item";

std::string const& sect() {
    static std::string const s = "\xC2\xA7";
    return s;
}

/// The player's experience level.
int levelsOf(Player const& player) {
    auto ref = player.getAttribute(Player::LEVEL());
    auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
    return inst ? static_cast<int>(inst->mCurrentValue) : 0;
}

bool loreHas(ItemStack const& stack, std::string const& text) {
    for (auto const& l : stack.getCustomLore())
        if (l.find(text) != std::string::npos) return true;
    return false;
}

/// Writes a synthetic data tree (files by path below data/) to a temp directory.
std::filesystem::path writeTree(std::vector<std::pair<std::string, JsonDoc>> const& files) {
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() / ("gf-item-data-" + std::to_string(rd()));
    for (auto const& [rel, doc] : files) {
        auto p = dir / rel;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream(p) << doc.dump(2);
    }
    return dir;
}

/// Loads a synthetic tree for the duration of a test (the real data comes back afterwards).
struct ScopedTree {
    std::filesystem::path dir;
    explicit ScopedTree(std::vector<std::pair<std::string, JsonDoc>> const& files) : dir(writeTree(files)) {
        data::setRoot(dir);
        data::reloadAll();
    }
    ~ScopedTree() {
        data::setRoot({});
        data::reloadAll();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

// ---- tags --------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.tags_fixture", [](Context& ctx) {
    using gf::tags::Registry;
    ctx.require(gf::tags::internal::fileCount() > 0, "tag files are loaded (gf_test fixture tag at least)");
    ctx.check(gf::tags::has(Registry::Block, "gf_test:fixture_blocks", "gf_test:fixture_block"), "fixture tag holds the fixture block");
    ctx.check(gf::tags::has(Registry::Block, "#gf_test:fixture_blocks", "minecraft:gold_block"), "a leading # is accepted; vanilla member found");
    ctx.check(!gf::tags::has(Registry::Block, "gf_test:fixture_blocks", "minecraft:stone"), "stone is not in the fixture tag");
    auto m = gf::tags::members(Registry::Block, "gf_test:fixture_blocks");
    ctx.check(m.size() == 2, "members lists the two fixture members (got " + std::to_string(m.size()) + ")");
    // the interaction matcher asks gf::tags
    ctx.check(ia::matches(ia::Match::tag("gf_test:fixture_blocks"), "gf_test:fixture_block", ""), "interaction tag matcher uses the tag runtime");
});

GF_REGISTER_SELFTEST("item.tags_vanilla_bridge", [](Context& ctx) {
    using gf::tags::Registry;
    ctx.check(gf::tags::has(Registry::Block, "minecraft:logs", "minecraft:oak_log"), "oak_log is in minecraft:logs");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:logs", "minecraft:stripped_warped_hyphae"), "logs include the nether stems");
    ctx.check(!gf::tags::has(Registry::Block, "minecraft:logs", "minecraft:oak_planks"), "planks are no logs");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:planks", "minecraft:cherry_planks"), "cherry planks are planks");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:mineable/pickaxe", "minecraft:stone"), "stone is mineable with a pickaxe (native tag)");
    ctx.check(!gf::tags::has(Registry::Block, "minecraft:mineable/pickaxe", "minecraft:dirt"), "dirt is not");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:dirt", "minecraft:podzol"), "podzol is dirt");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:terracotta", "minecraft:red_terracotta"), "red terracotta is terracotta");
    ctx.check(!gf::tags::has(Registry::Block, "minecraft:terracotta", "minecraft:red_glazed_terracotta"), "glazed terracotta is not");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:swords", "minecraft:diamond_sword"), "diamond sword is a sword (native item tag)");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:enchantable/chest_armor", "minecraft:iron_chestplate"), "iron chestplate: chest armour");
    ctx.check(!gf::tags::has(Registry::Item, "minecraft:enchantable/chest_armor", "minecraft:iron_helmet"), "an iron helmet is not chest armour");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:enchantable/armor", "minecraft:iron_helmet"), "an iron helmet is armour");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:enchantable/durability", "minecraft:iron_pickaxe"), "an iron pickaxe has durability");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:boats", "minecraft:oak_boat") && gf::tags::has(Registry::Item, "minecraft:boats", "minecraft:oak_chest_boat"),
              "boats hold #chest_boats (Java 1.21.1)");
    ctx.check(gf::tags::has(Registry::Item, "minecraft:chest_boats", "minecraft:oak_chest_boat") && !gf::tags::has(Registry::Item, "minecraft:chest_boats", "minecraft:oak_boat"),
              "chest boats exclude plain boats");
    ctx.check(gf::tags::has(Registry::EntityType, "minecraft:undead", "minecraft:stray"), "a stray is undead (nested bridge tags)");
    ctx.check(gf::tags::has(Registry::Fluid, "minecraft:water", "minecraft:flowing_water"), "flowing water is water");
    auto planks = gf::tags::members(Registry::Block, "minecraft:planks");
    ctx.check(std::find(planks.begin(), planks.end(), "minecraft:oak_planks") != planks.end(), "members enumerates the game's blocks for a bridged tag");
    ctx.info("minecraft:planks has " + std::to_string(planks.size()) + " members");
});

// The NeoForge common tags (c:) TF's data nests: their vanilla members come from the bridge.
GF_REGISTER_SELFTEST("item.tags_common", [](Context& ctx) {
    using gf::tags::Registry;
    // TF's own tags, when F1's import is in the data tree
    auto itemTags = gf::tags::internal::tagsOf("item");
    if (std::find(itemTags.begin(), itemTags.end(), "twilightforest:squirrel_tempt_items") != itemTags.end()) {
        ctx.check(gf::tags::has(Registry::Item, "twilightforest:squirrel_tempt_items", "minecraft:wheat_seeds"), "TF's squirrel_tempt_items (#c:seeds) holds wheat seeds");
        ctx.check(gf::tags::has(Registry::Item, "twilightforest:boar_tempt_items", "minecraft:carrot"), "TF's boar_tempt_items (#c:crops/carrot) holds carrots");
    } else {
        ctx.info("TF's tag data is not in this build (F1's import): the synthetic tree stands in");
    }
    ScopedTree tree({
        {"gf_test/tags/item/tempt.json", {{"values", {"#c:seeds"}}}},
        {"gf_test/tags/item/boar.json", {{"values", {"#c:crops/carrot", "#c:crops/potato", "#c:crops/beetroot"}}}},
        {"gf_test/tags/block/ore_safe.json",
         {{"values", {"#minecraft:dirt", "#c:gravels", "#c:sands", "#c:end_stones", "#c:ore_bearing_ground/root", "#minecraft:base_stone_nether"}}}},
        {"c/tags/block/ore_bearing_ground/root.json", {{"values", {"gf_test:root"}}}},
        {"gf_test/tags/block/druid.json", {{"values", {"#minecraft:leaves", "#c:ores"}}}},
        {"gf_test/tags/entity_type/no_throw.json", {{"values", {"#c:bosses", "gf_test:boss"}}}},
    });
    ctx.check(gf::tags::has(Registry::Item, "gf_test:tempt", "minecraft:wheat_seeds") && gf::tags::has(Registry::Item, "gf_test:tempt", "minecraft:pitcher_pod"),
              "#c:seeds: wheat seeds and the pitcher pod");
    ctx.check(!gf::tags::has(Registry::Item, "gf_test:tempt", "minecraft:wheat"), "wheat is no seed");
    ctx.check(gf::tags::has(Registry::Item, "gf_test:boar", "minecraft:carrot") && gf::tags::has(Registry::Item, "gf_test:boar", "minecraft:potato")
                  && gf::tags::has(Registry::Item, "gf_test:boar", "minecraft:beetroot"),
              "#c:crops/carrot, potato, beetroot");
    auto tempt = gf::tags::members(Registry::Item, "gf_test:tempt");
    ctx.check(std::find(tempt.begin(), tempt.end(), "minecraft:melon_seeds") != tempt.end(), "members enumerates c: members (" + std::to_string(tempt.size()) + ")");
    for (auto const* id : {"minecraft:gravel", "minecraft:sand", "minecraft:end_stone", "minecraft:netherrack", "minecraft:podzol", "gf_test:root"})
        ctx.check(gf::tags::has(Registry::Block, "gf_test:ore_safe", id), std::string("ore_safe_replace_block style tag holds ") + id);
    ctx.check(!gf::tags::has(Registry::Block, "gf_test:ore_safe", "minecraft:diamond_block"), "but no diamond block");
    for (auto const* id : {"minecraft:iron_ore", "minecraft:deepslate_diamond_ore", "minecraft:nether_gold_ore", "minecraft:ancient_debris", "minecraft:lit_redstone_ore"})
        ctx.check(gf::tags::has(Registry::Block, "gf_test:druid", id), std::string("#c:ores holds ") + id);
    ctx.check(gf::tags::has(Registry::EntityType, "gf_test:no_throw", "minecraft:ender_dragon"), "#c:bosses holds the ender dragon");
    // recipe ingredients by c: tag
    namespace rc = gf::recipes;
    ctx.check(rc::internal::matches(rc::internal::parseIngredient(JsonDoc{{"tag", "c:rods/wooden"}}), "minecraft:stick"), "c:rods/wooden takes a stick");
    ctx.check(rc::internal::matches(rc::internal::parseIngredient(JsonDoc{{"tag", "c:ender_pearls"}}), "minecraft:ender_pearl"), "c:ender_pearls takes an ender pearl");
    ctx.check(rc::internal::matches(rc::internal::parseIngredient(JsonDoc{{"tag", "c:ingots/iron"}}), "minecraft:iron_ingot"), "c:ingots/iron takes an iron ingot");
    ctx.check(gf::tags::has(Registry::Item, "c:dusts/redstone", "minecraft:redstone") && gf::tags::has(Registry::Item, "c:tools/shield", "minecraft:shield"),
              "the c: tags TF's code reads (dusts/redstone, tools/shield)");
});

// ext::hasIn on a vanilla registry directory answers as gf::tags (bridge and cache shared).
GF_REGISTER_SELFTEST("item.tags_ext_dirs", [](Context& ctx) {
    using gf::tags::Registry;
    gf::tags::internal::reload();
    ctx.check(gf::tags::ext::hasIn("block", "minecraft:logs", "minecraft:oak_log"), "ext::hasIn(\"block\", minecraft:logs) uses the bridge");
    ctx.check(gf::tags::has(Registry::Block, "minecraft:logs", "minecraft:oak_log"), "tags::has afterwards still answers through the bridge");
    ctx.check(gf::tags::ext::hasIn("blocks", "minecraft:planks", "minecraft:oak_planks"), "the plural directory too");
    auto planks = gf::tags::ext::membersIn("block", "minecraft:planks");
    ctx.check(std::find(planks.begin(), planks.end(), "minecraft:oak_planks") != planks.end(), "membersIn of a vanilla directory lists the bridge's members");
});

// TF's data names vanilla content by Java id: tags and data maps hold the game's Bedrock ids.
GF_REGISTER_SELFTEST("item.tags_java_ids", [](Context& ctx) {
    using gf::tags::Registry;
    // TF's own data, when F1's import is in this build
    if (gf::datamaps::get("twilightforest:crumble_horn", "minecraft:cobblestone")) {
        auto const* bricks = gf::datamaps::get("twilightforest:crumble_horn", "minecraft:nether_brick");
        ctx.check(bricks != nullptr, "crumble_horn's minecraft:nether_bricks is Bedrock's nether_brick block");
        ctx.check(gf::datamaps::get("twilightforest:crumble_horn", "minecraft:dirt_with_roots") != nullptr, "rooted_dirt is dirt_with_roots");
        auto const* piglin = gf::datamaps::get("twilightforest:ominous_fire", "minecraft:piglin");
        ctx.check(piglin && piglin->value("transform_to", std::string()) == "minecraft:zombie_pigman", "ominous_fire turns a piglin into Bedrock's zombie_pigman");
    } else {
        ctx.info("TF's data maps are not in this build (F1's import): the synthetic tree stands in");
    }
    // the map's id field (as a consumer registers it); other fields keep their strings
    gf::datamaps::ext::registerIdField("gf_test:crumble", "crumble_to", "block");
    ScopedTree tree({
        {"gf_test/tags/block/paths.json", {{"values", {"minecraft:dirt_path", "minecraft:snow_block", "minecraft:water", "minecraft:stone"}}}},
        {"gf_test/tags/item/bricks.json", {{"values", {"minecraft:nether_brick", "minecraft:nether_bricks"}}}},
        {"gf_test/tags/entity_type/pigs.json", {{"values", {"minecraft:zombified_piglin"}}}},
        {"gf_test/data_maps/block/crumble.json",
         {{"values", {{"minecraft:nether_bricks", {{"crumble_chance", 0.5}, {"crumble_to", "minecraft:magma_block"}, {"sound", "minecraft:snow"}}}}}}},
        {"gf_test/data_maps/block/other.json", {{"values", {{"minecraft:stone", {{"crumble_to", "minecraft:magma_block"}}}}}}},
    });
    ctx.check(gf::tags::has(Registry::Block, "gf_test:paths", "minecraft:grass_path"), "minecraft:dirt_path is grass_path");
    ctx.check(gf::tags::has(Registry::Block, "gf_test:paths", "minecraft:snow") && !gf::tags::has(Registry::Block, "gf_test:paths", "minecraft:snow_layer"),
              "Java's snow_block is Bedrock's snow (and not the snow layer)");
    ctx.check(gf::tags::has(Registry::Block, "gf_test:paths", "minecraft:flowing_water"), "water holds both fluid blocks");
    ctx.check(gf::tags::has(Registry::Block, "gf_test:paths", "minecraft:stone"), "ids Bedrock shares stay as they are");
    ctx.check(gf::tags::has(Registry::Item, "gf_test:bricks", "minecraft:netherbrick") && gf::tags::has(Registry::Item, "gf_test:bricks", "minecraft:nether_brick"),
              "items: the nether brick item is netherbrick, the bricks block item nether_brick");
    ctx.check(gf::tags::has(Registry::EntityType, "gf_test:pigs", "minecraft:zombie_pigman"), "entities: zombified_piglin is zombie_pigman");
    auto const* v = gf::datamaps::get("gf_test:crumble", "minecraft:nether_brick");
    ctx.check(v && v->value("crumble_to", std::string()) == "minecraft:magma", "data map keys and their values' block ids are Bedrock's");
    ctx.check(v && v->value("sound", std::string()) == "minecraft:snow", "a field that is no id field keeps its string (Java's minecraft:snow is no block here)");
    auto const* other = gf::datamaps::get("gf_test:other", "minecraft:stone");
    ctx.check(other && other->value("crumble_to", std::string()) == "minecraft:magma_block", "a map without registered id fields keeps its values");
    // TF's own maps' fields are built in (CrumbledBlock, EntityTransformation)
    auto fieldOf = [](std::string const& map, std::string const& field) {
        auto f  = gf::datamaps::internal::idFields(map);
        auto it = f.find(field);
        return it == f.end() ? std::string() : it->second;
    };
    ctx.check(fieldOf("twilightforest:crumble_horn", "crumble_to") == "block" && fieldOf("twilightforest:ominous_fire", "transform_to") == "entity_type"
                  && fieldOf("twilightforest:transformation_powder", "transform_to") == "entity_type",
              "crumble_to (block) and transform_to (entity type) are TF's id fields");
    gf::datamaps::internal::unregisterIdField("gf_test:crumble", "crumble_to");
});

// ---- synthetic data: tags, data maps, recipes, enchantments, travellers modifiers --------------------

GF_REGISTER_SELFTEST("item.data_reload", [](Context& ctx) {
    using gf::tags::Registry;
    {
        ScopedTree tree({
            {"gf_test/tags/block/inner.json", {{"values", {"gf_test:a", {{"id", "gf_test:b"}, {"required", false}}}}}},
            {"gf_test/tags/block/outer.json", {{"values", {"#gf_test:inner", "gf_test:c", "#minecraft:logs"}}}},
            {"gf_test/tags/blocks/legacy.json", {{"values", {"gf_test:old"}}}},
            {"gf_test/tags/twilight/wood_palettes/common.json", {{"values", {"gf_test:palette"}}}},
            {"gf_test/data_maps/block/crumble.json",
             {{"values", {{"gf_test:a", {{"crumble_chance", 0.5}}}, {"#gf_test:inner", {{"crumble_chance", 0.2}}}, {"gf_test:gone", 1}}}, {"remove", {"gf_test:gone"}}}},
            {"neoforge/data_maps/item/compostables.json", {{"values", {{kFixture, {{"chance", 0.65}}}}}}},
            {"neoforge/data_maps/item/furnace_fuels.json", {{"values", {{kFixture, {{"burn_time", 800}}}}}}},
            {"gf_test/recipe/shaped.json",
             {{"type", "minecraft:crafting_shaped"}, {"pattern", {"ab", " a"}}, {"key", {{"a", {{"item", "minecraft:stick"}}}, {"b", {{"tag", "minecraft:planks"}}}}}, {"result", {{"id", "minecraft:ladder"}, {"count", 3}}}}},
            {"gf_test/recipe/uncraft.json",
             {{"type", "twilightforest:uncrafting"}, {"cost", 0}, {"input", {{"item", "gf_test:box"}}}, {"key", {{"C", {{"item", "minecraft:chest"}}}}}, {"pattern", {"C"}}}},
            {"gf_test/recipe/modifier.json",
             {{"type", "twilightforest:travellers_gear_modifier_shapeless_recipe"},
              {"ingredients", {{{"item", "minecraft:feather"}}, {{"item", "twilightforest:travellers_vest"}}}},
              {"modifier_key", "twilightforest:stealth"}}},
            {"gf_test/recipe/scepter.json",
             {{"type", "twilightforest:scepter_repair"}, {"durability", 9}, {"repair_ingredients", {{{"item", "minecraft:golden_apple"}}}}, {"scepter", "gf_test:scepter"}}},
            {"gf_test/recipe/cloth.json", {{"type", "twilightforest:emperors_cloth_recipe"}, {"category", "misc"}}},
            {"gf/recipes/unit.json", {{"gf_test:bundled", {{"type", "twilightforest:drying"}, {"input", {{"item", "minecraft:kelp"}}}, {"result", {{"id", "minecraft:dried_kelp"}}}, {"filter_time", 40}}}}},
            {"gf_test/enchantment/fixture.json",
             {{"anvil_cost", 8}, {"max_level", 3}, {"weight", 1}, {"description", {{"translate", "enchantment.gf_test.fixture"}}}, {"supported_items", "#minecraft:enchantable/armor"},
              {"exclusive_set", {"minecraft:thorns"}}, {"min_cost", {{"base", 5}, {"per_level_above_first", 9}}}, {"max_cost", {{"base", 20}, {"per_level_above_first", 9}}}}},
            {"gf_test/twilight/travellers_modifiers/fixture.json", {{"type", "twilightforest:component"}, {"equipment_slots", "head"}, {"component", {{"gf_test:x", JsonDoc::object()}}}}},
        });
        // tags
        ctx.check(gf::tags::has(Registry::Block, "gf_test:outer", "gf_test:b"), "nested tags resolve (optional entries too)");
        ctx.check(gf::tags::has(Registry::Block, "gf_test:outer", "minecraft:birch_log"), "a nested vanilla tag goes through the bridge");
        ctx.check(gf::tags::has(Registry::Block, "gf_test:legacy", "gf_test:old"), "plural tag directories are read");
        ctx.check(gf::tags::ext::hasIn("twilight", "gf_test:wood_palettes/common", "gf_test:palette"), "tags of TF's own registries by directory");
        // data maps
        auto const* a = gf::datamaps::get("gf_test:crumble", "gf_test:a");
        ctx.check(a && a->value("crumble_chance", 0.0) == 0.5, "data map value by id");
        auto const* b = gf::datamaps::get("gf_test:crumble", "gf_test:b");
        ctx.check(b && b->value("crumble_chance", 0.0) == 0.2, "data map value through a tag key");
        ctx.check(gf::datamaps::get("gf_test:crumble", "gf_test:gone") == nullptr, "removed entries are gone");
        ctx.check(gf::datamaps::keys("gf_test:crumble").size() == 2, "keys lists ids and tag members");
        // the NeoForge maps the game applies: the composter's chance, the furnace's fuel value
        ctx.check(ComposterBlock::getFillChance(items::make(kFixture).value_or(ItemStack::EMPTY_ITEM())) == 65, "compostables: the composter takes the item at 65 %");
        auto coal    = FurnaceBlockActor::getBurnDuration(items::make("minecraft:coal").value_or(ItemStack::EMPTY_ITEM()), 200.f);
        auto fixture = FurnaceBlockActor::getBurnDuration(items::make(kFixture).value_or(ItemStack::EMPTY_ITEM()), 200.f);
        ctx.check(coal > 0 && std::abs(fixture - coal / 2) < 0.01f * coal, "furnace_fuels: 800 ticks burn half as long as coal (" + std::to_string(fixture) + " / " + std::to_string(coal) + ")");
        ctx.check(ComposterBlock::getFillChance(items::make("minecraft:stone").value_or(ItemStack::EMPTY_ITEM())) == 0, "vanilla items keep Bedrock's (stone: none)");
        // a value handed out survives a reload (the old snapshot is kept)
        auto const* kept = gf::datamaps::get("gf_test:crumble", "gf_test:a");
        data::reloadAll();
        ctx.check(kept && kept->value("crumble_chance", 0.0) == 0.5, "a data map value stays valid across a reload");
        // recipes
        auto const* shaped = gf::recipes::byId("gf_test:shaped");
        ctx.require(shaped != nullptr, "a shaped data recipe is loaded");
        // a pointer handed out survives a reload (the previous generation is kept)
        data::reloadAll();
        ctx.check(shaped->id == "gf_test:shaped" && shaped->resultItem == "minecraft:ladder", "a recipe pointer stays valid across a data reload");
        ctx.check(gf::recipes::byId("gf_test:shaped") != shaped, "the reload made a new generation");
        ctx.check(shaped->width == 2 && shaped->height == 2 && shaped->ingredients.size() == 4, "the pattern is 2x2 row-major");
        ctx.check(shaped->ingredients[1].tag == "minecraft:planks" && shaped->ingredients[2].empty(), "keys map to ingredients, spaces to empty");
        ctx.check(shaped->resultItem == "minecraft:ladder" && shaped->resultCount == 3, "the result and its count");
        auto const* uncraft = gf::recipes::byId("gf_test:uncraft");
        ctx.check(uncraft && uncraft->type == "twilightforest:uncrafting" && uncraft->resultItem == "gf_test:box", "uncrafting: the input is the recipe's item");
        auto const* mod = gf::recipes::byId("gf_test:modifier");
        ctx.check(mod && mod->resultItem.empty() && mod->data.value("gear_item", std::string()) == "twilightforest:travellers_vest" && mod->ingredients.size() == 2,
                  "travellers modifier recipe: no result (Java's EMPTY), the gear in data");
        bool listed = false;
        for (auto const* r : gf::recipes::producing("twilightforest:travellers_vest")) listed = listed || r->id == "gf_test:modifier";
        ctx.check(!listed, "producing (the uncrafting table) does not list a modifier recipe");
        auto const* scepter = gf::recipes::byId("gf_test:scepter");
        ctx.check(scepter && scepter->resultItem == "gf_test:scepter" && scepter->ingredients.size() == 2, "scepter repair: scepter + repair ingredients");
        ctx.check(gf::recipes::byId("gf_test:cloth") != nullptr, "code-only TF recipes are indexed");
        ctx.check(gf::recipes::byId("gf_test:bundled") != nullptr, "unit bundles of data/gf/recipes are read");
        bool found = false;
        for (auto const* r : gf::recipes::producing("minecraft:ladder")) found = found || r->id == "gf_test:shaped";
        ctx.check(found, "producing finds the data recipe by its result");
        // a loader registered by an owner replaces the built-in one at the next query
        gf::recipes::registerType("gf_test:custom", [](std::string const& id, JsonDoc const& j) -> std::optional<gf::recipes::CraftingRecipe> {
            gf::recipes::CraftingRecipe r;
            r.id         = id;
            r.resultItem = j.value("out", std::string());
            return r;
        });
        // enchantment specs from the data
        auto spec = gf::enchant::internal::specOf("gf_test:fixture");
        ctx.check(spec && spec->maxLevel == 3 && spec->anvilCost == 8 && spec->minCost.perLevelAboveFirst == 9, "enchantment spec read from the data");
        ctx.check(spec && !spec->discoverable, "not in #minecraft:in_enchanting_table: not discoverable (as TF's four)");
        // travellers modifiers
        auto helmet = items::make("minecraft:iron_helmet");
        ctx.require(helmet.has_value(), "an iron helmet");
        ctx.check(gf::gear::add(*helmet, "gf_test:fixture"), "a head modifier from the data goes on a helmet");
    }
    ctx.check(gf::recipes::byId("gf_test:shaped") == nullptr, "the real data is back after the synthetic tree");
});

// ---- recipes -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.recipes_fixture", [](Context& ctx) {
    auto const* shaped = gf::recipes::byId("gf_test:fixture_shaped");
    ctx.require(shaped != nullptr, "the fixture shaped recipe (assets/test/data) is loaded");
    ctx.check(shaped->type == "minecraft:crafting_shaped" && shaped->width == 1 && shaped->height == 2, "1x2 shaped recipe");
    ctx.check(shaped->resultItem == kFixture && shaped->resultCount == 2, "result: 2 fixture items");
    ctx.check(gf::recipes::internal::matches(shaped->ingredients[0], "minecraft:stick"), "the ingredient is a stick");
    auto const* drying = gf::recipes::byId("gf_test:fixture_drying");
    ctx.require(drying != nullptr, "the fixture drying recipe is loaded");
    ctx.check(drying->type == "twilightforest:drying" && drying->resultItem == "minecraft:stick", "drying recipe: stick from the fixture item");
    ctx.check(!gf::recipes::ofType("twilightforest:drying").empty(), "ofType lists drying recipes");
    auto const* first = gf::recipes::findFirst("twilightforest:drying", [](auto const& r) { return r.id == "gf_test:fixture_drying"; });
    ctx.check(first == drying, "findFirst finds it");
    bool data = false, native = false;
    for (auto const* r : gf::recipes::producing(kFixture)) {
        data   = data || r->id == "gf_test:fixture_shaped";
        native = native || r->id == "gf_test:fixture_item";
    }
    ctx.check(data, "producing lists the data recipe of the fixture item");
    ctx.check(native, "producing lists the game's crafting recipe of the fixture item (test BP)");
    auto n = gf::recipes::internal::nativeRecipeCount();
    ctx.check(n > 200, "the game's crafting table recipes are indexed (got " + std::to_string(n) + ")");
    ctx.check(!gf::recipes::producing("minecraft:stick").empty(), "vanilla items have recipes (sticks)");
});

GF_REGISTER_SELFTEST("item.special_crafting", [](Context& ctx) {
    namespace rc = gf::recipes;
    static int assembled = 0;
    assembled            = 0;
    rc::registerSpecialCrafting(
        kFixture,
        [](rc::CraftingGrid const& g) {
            int sticks = 0;
            for (auto const* s : g.slots)
                if (s && items::idOf(*s) == "minecraft:stick") ++sticks;
            return sticks == 2;
        },
        [](rc::CraftingGrid const&) {
            ++assembled;
            auto out = items::make(kFixture, 2).value_or(ItemStack::EMPTY_ITEM());
            gf::item::setData(out, "special", true);
            return out;
        },
        [](rc::CraftingGrid const&) { return std::vector<ItemStack>{items::make("minecraft:stick").value_or(ItemStack::EMPTY_ITEM())}; }
    );
    auto& player = ctx.player();
    auto& ui     = player.mPlayerUIContainer.get();
    auto  stick  = items::make("minecraft:stick");
    ctx.require(stick.has_value(), "a stick");
    int first = static_cast<int>(PlayerUISlot::Crafting2x2Input1);
    ui.setItem(first, *stick);     // (0, 0)
    ui.setItem(first + 2, *stick); // (0, 1)
    auto grid = rc::internal::gridOf(player, false);
    ctx.check(grid.grid.width == 2 && grid.grid.slots.size() == 4, "the 2x2 inventory grid");
    auto out = rc::internal::assembleSpecial(kFixture, grid.grid);
    ctx.check(out && items::idOf(*out) == kFixture && items::countOf(*out) == 2, "the assembler makes the result");
    ctx.check(out && gf::item::getData(*out, "special") == true, "with its item data");
    ctx.check(assembled == 1, "the assembler ran once");
    ctx.check(rc::internal::remainderOf(kFixture, grid.grid).size() == 1, "the remainder is one stick");
    auto bySlot = rc::internal::remaindersBySlot(kFixture, grid.grid);
    ctx.check(bySlot.size() == 4 && items::idOf(bySlot[0]) == "minecraft:stick" && items::isEmpty(bySlot[1]), "one remainder per grid slot");
    // ResultSlot#onTake: the craft consumed the grid, the remainder goes back into its slot
    ui.setItem(first, ItemStack::EMPTY_ITEM());
    ui.setItem(first + 2, ItemStack::EMPTY_ITEM());
    rc::internal::placeRemainders(player, grid.firstSlot, bySlot);
    ctx.check(items::idOf(ui.getItem(first)) == "minecraft:stick" && items::isEmpty(ui.getItem(first + 1)), "the remainder is back in the emptied grid slot");
    // a slot still holding something else: the remainder goes to the inventory
    ui.setItem(first, *items::make("minecraft:dirt"));
    int sticksBefore = 0;
    for (int i = 0; i < player.getInventory().getContainerSize(); ++i)
        if (items::idOf(player.getInventory().getItem(i)) == "minecraft:stick") sticksBefore += items::countOf(player.getInventory().getItem(i));
    rc::internal::placeRemainders(player, grid.firstSlot, bySlot);
    int sticksAfter = 0;
    for (int i = 0; i < player.getInventory().getContainerSize(); ++i)
        if (items::idOf(player.getInventory().getItem(i)) == "minecraft:stick") sticksAfter += items::countOf(player.getInventory().getItem(i));
    ctx.check(sticksAfter == sticksBefore + 1 && items::idOf(ui.getItem(first)) == "minecraft:dirt", "a full slot sends the remainder to the inventory");
    ui.setItem(first, *stick);
    ui.setItem(first + 2, *stick);
    for (int i = 0; i < player.getInventory().getContainerSize(); ++i)
        if (items::idOf(player.getInventory().getItem(i)) == "minecraft:stick") player.getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    ui.setItem(first + 2, ItemStack::EMPTY_ITEM());
    auto refused = rc::internal::assembleSpecial(kFixture, rc::internal::gridOf(player, false).grid);
    ctx.check(!refused, "the matcher refuses a grid with one stick");
    ui.setItem(first, ItemStack::EMPTY_ITEM());
    rc::internal::unregisterSpecial(kFixture);
    ctx.check(!rc::internal::isSpecial(kFixture), "unregistered");
    // a variable layout: another BP recipe id runs the same special recipe
    rc::registerSpecialCrafting("gf_test:layouts", [](rc::CraftingGrid const&) { return true; }, [](rc::CraftingGrid const&) {
        return items::make(kFixture).value_or(ItemStack::EMPTY_ITEM());
    });
    gf::recipes::ext::addSpecialLayout("gf_test:layouts", "gf_test:layouts_3");
    ctx.check(rc::internal::isSpecial("gf_test:layouts_3") && rc::internal::assembleSpecial("gf_test:layouts_3", grid.grid).has_value(),
              "a layout's BP recipe id reaches the special recipe");
    rc::internal::unregisterSpecial("gf_test:layouts");
    ctx.check(!rc::internal::isSpecial("gf_test:layouts_3"), "its layouts go with it");
});

namespace {
/// A 3x3 grid of stacks by "x,y" (row-major), for the crafting hook's resolution.
struct TestGrid {
    std::vector<ItemStack>     stacks = std::vector<ItemStack>(9, ItemStack::EMPTY_ITEM());
    gf::recipes::CraftingGrid  grid;
    TestGrid(std::initializer_list<std::tuple<int, int, char const*>> cells) {
        for (auto const& [x, y, id] : cells) stacks[static_cast<std::size_t>(y * 3 + x)] = items::make(id).value_or(ItemStack::EMPTY_ITEM());
        grid.width = grid.height = 3;
        for (auto const& s : stacks) grid.slots.push_back(&s);
    }
};
} // namespace

// TF's code-only data recipes have the special recipes' ids (twilightforest:casket_repair_recipe):
// the crafting hook takes the BP recipe of the id, never the data recipe that shadows it in byId.
GF_REGISTER_SELFTEST("item.special_crafting_shadowed", [](Context& ctx) {
    namespace rc = gf::recipes;
    ctx.require(rc::internal::nativeById(kFixture) != nullptr, "the test BP recipe gf_test:fixture_item is one of the game's recipes");
    rc::internal::setTestDataRecipe(kFixture, {{"type", "twilightforest:casket_repair_recipe"}, {"category", "misc"}});
    auto const* data = rc::byId(kFixture);
    ctx.check(data && data->type == "twilightforest:casket_repair_recipe" && data->resultItem.empty(),
              "a SimpleCraftingRecipeSerializer data recipe of the same id shadows it in byId (no result item)");
    auto const* bp = rc::internal::nativeById(kFixture);
    ctx.check(bp && bp->resultItem == kFixture, "the BP recipe stays the game's recipe of the id");
    rc::registerSpecialCrafting(
        kFixture,
        [](rc::CraftingGrid const& g) {
            int sticks = 0;
            for (auto const* s : g.slots) sticks += s && items::idOf(*s) == "minecraft:stick";
            return sticks == 2;
        },
        [](rc::CraftingGrid const&) { return items::make(kFixture, 2).value_or(ItemStack::EMPTY_ITEM()); }
    );
    TestGrid column({{1, 0, "minecraft:stick"}, {1, 1, "minecraft:stick"}});
    ctx.check(bp && rc::internal::fits(*bp, column.grid), "two sticks in a column fit the BP recipe's shape");
    ctx.check(rc::internal::resolveSpecial("", kFixture, column.grid) == kFixture, "found by its result despite the data twin");
    ctx.check(rc::internal::resolveSpecial(kFixture, kFixture, column.grid) == kFixture, "and by the request's recipe");
    TestGrid one({{1, 1, "minecraft:stick"}});
    bool refused = false;
    ctx.check(rc::internal::resolveSpecial(kFixture, kFixture, one.grid, &refused).empty() && refused, "the named special refuses a grid its matcher does not take");
    auto made = rc::internal::assembleSpecial(kFixture, column.grid);
    ctx.check(made && items::countOf(*made) == 2, "the assembler's stack, not the BP recipe's static result");
    rc::internal::unregisterSpecial(kFixture);
    rc::internal::setTestDataRecipe(kFixture, JsonDoc());
    auto const* back = rc::byId(kFixture);
    ctx.check(back && back->type == "minecraft:crafting_shaped" && back->resultItem == kFixture, "without the data twin byId is the BP recipe again");
});

// Several special recipes (or layouts) giving one item: each candidate's matcher is asked, those
// whose BP shape fits the grid first, as Java's RecipeManager takes the first recipe that matches.
GF_REGISTER_SELFTEST("item.special_crafting_shared_result", [](Context& ctx) {
    namespace rc = gf::recipes;
    auto shapeless = [](std::string id, std::vector<char const*> ingredients) {
        rc::CraftingRecipe r;
        r.id         = std::move(id);
        r.type       = "minecraft:crafting_shapeless";
        r.resultItem = kFixture;
        for (auto const* i : ingredients) r.ingredients.push_back(rc::Ingredient{{i}, {}});
        r.data = JsonDoc{{"type", r.type}, {"native", true}};
        return r;
    };
    auto sticks = shapeless("gf_test:twin_a", {"minecraft:stick", "minecraft:stick"});
    auto paper  = shapeless("gf_test:twin_b", {"minecraft:stick", "minecraft:paper"});
    auto first  = shapeless("gf_test:twin_0", {"minecraft:stick", "minecraft:stick"});
    for (auto const* r : {&sticks, &paper, &first}) rc::internal::setTestNativeRecipe(*r);
    auto tagged = [](char const* tag) {
        return [tag](rc::CraftingGrid const&) {
            auto out = items::make(kFixture).value_or(ItemStack::EMPTY_ITEM());
            gf::item::setData(out, "twin", tag);
            return out;
        };
    };
    auto count = [](rc::CraftingGrid const& g, char const* id) {
        int n = 0;
        for (auto const* s : g.slots) n += s && items::idOf(*s) == id;
        return n;
    };
    // a special whose layouts are two BP recipes (the vest / gloves merge kind) and one of its own
    rc::registerSpecialCrafting("gf_test:twin_special", [&](rc::CraftingGrid const& g) { return count(g, "minecraft:stick") == 2; }, tagged("a"));
    rc::ext::addSpecialLayout("gf_test:twin_special", "gf_test:twin_a");
    rc::registerSpecialCrafting("gf_test:twin_b", [&](rc::CraftingGrid const& g) { return count(g, "minecraft:paper") == 1; }, tagged("b"));
    // sorted first and fitting too, but its matcher refuses: the next candidate crafts
    rc::registerSpecialCrafting("gf_test:twin_0", [](rc::CraftingGrid const&) { return false; }, tagged("0"));
    TestGrid two({{0, 0, "minecraft:stick"}, {2, 2, "minecraft:stick"}});
    TestGrid mixed({{1, 1, "minecraft:stick"}, {0, 2, "minecraft:paper"}});
    auto     candidates = rc::internal::specialCandidates(kFixture, two.grid);
    ctx.check(candidates.size() == 2 && candidates[0] == "gf_test:twin_0" && candidates[1] == "gf_test:twin_a",
              "candidates: the special BP recipes of the item that fit the grid, in id order (got " + std::to_string(candidates.size()) + ")");
    auto a = rc::internal::resolveSpecial("", kFixture, two.grid);
    ctx.check(a == "gf_test:twin_a", "a refusing matcher passes the craft on to the next candidate (got " + a + ")");
    auto made = rc::internal::assembleSpecial(a, two.grid);
    ctx.check(made && gf::item::getData(*made, "twin") == "a", "the layout runs its special recipe's assembler");
    auto b = rc::internal::resolveSpecial("", kFixture, mixed.grid);
    ctx.check(b == "gf_test:twin_b", "a stick and a paper go to the other special of the same item (got " + b + ")");
    ctx.check(rc::internal::resolveSpecial("gf_test:twin_b", kFixture, mixed.grid) == "gf_test:twin_b", "the request's recipe decides when it is known");
    TestGrid none({{0, 0, "minecraft:paper"}});
    ctx.check(rc::internal::specialCandidates(kFixture, none.grid).empty() && rc::internal::resolveSpecial("", kFixture, none.grid).empty(),
              "no special BP recipe fits a lone paper (twin_b's loose matcher is not asked): the crafted recipe's own result");
    for (auto const* id : {"gf_test:twin_special", "gf_test:twin_b", "gf_test:twin_0"}) rc::internal::unregisterSpecial(id);
    for (auto const* r : {&sticks, &paper, &first}) rc::internal::setTestNativeRecipe(*r, true);
    ctx.check(rc::internal::nativeById("gf_test:twin_a") == nullptr, "the stand-ins are gone");
});

// NeoForge's recipe conditions: TF's jeed integration recipes (mod_loaded) are not there (and no
// "no loader" warning names their type); the uncrafting table recipe follows its condition.
GF_REGISTER_SELFTEST("item.recipes_conditions", [](Context& ctx) {
    namespace rc = gf::recipes;
    using J      = JsonDoc;
    ctx.check(!rc::internal::conditionsHold(J::array({{{"type", "neoforge:mod_loaded"}, {"modid", "jeed"}}})), "mod_loaded jeed: false");
    ctx.check(rc::internal::conditionsHold(J::array({{{"type", "neoforge:mod_loaded"}, {"modid", "twilightforest"}}})), "mod_loaded twilightforest: true");
    ctx.check(rc::internal::conditionsHold(J::array({{{"type", "neoforge:not"}, {"value", {{"type", "neoforge:false"}}}}})), "not false");
    ctx.check(!rc::internal::conditionsHold(J::array({{{"type", "neoforge:and"}, {"values", J::array({{{"type", "neoforge:true"}}, {{"type", "neoforge:false"}}})}}})), "and");
    ctx.check(rc::internal::conditionsHold(J::array({{{"type", "neoforge:or"}, {"values", J::array({{{"type", "neoforge:false"}}, {{"type", "neoforge:true"}}})}}})), "or");
    ctx.check(rc::internal::conditionsHold(J::array({{{"type", "neoforge:item_exists"}, {"item", "twilightforest:naga_scale"}}}))
                  && !rc::internal::conditionsHold(J::array({{{"type", "neoforge:item_exists"}, {"item", "othermod:thing"}}})),
              "item_exists: TF's items, no other mod's");
    ctx.check(rc::internal::conditionsHold(J::array({{{"type", "twilightforest:uncrafting_table_enabled"}}})), "uncrafting_table_enabled: TF's default (enabled)");
    // TF's own data, when F1's import is in this build
    if (rc::byId("twilightforest:uncrafting_table")) {
        ctx.check(rc::byId("twilightforest:jeed/strength") == nullptr, "TF's jeed recipes are not loaded");
        ctx.check(!rc::internal::typesWithoutLoader().contains("jeed:effect_provider"), "and their type is not reported as unknown");
    } else {
        ctx.info("TF's recipe data is not in this build (F1's import): the synthetic tree stands in");
    }
    {
        ScopedTree tree({
            {"gf_test/recipe/jeed.json",
             {{"neoforge:conditions", J::array({{{"type", "neoforge:mod_loaded"}, {"modid", "jeed"}}})}, {"type", "jeed:effect_provider"}, {"effect", "minecraft:strength"}}},
            {"gf_test/recipe/table.json",
             {{"neoforge:conditions", J::array({{{"type", "twilightforest:uncrafting_table_enabled"}}})},
              {"type", "minecraft:crafting_shaped"},
              {"pattern", {"##", "##"}},
              {"key", {{"#", {{"item", "minecraft:oak_planks"}}}}},
              {"result", {{"id", "minecraft:crafting_table"}}}}},
        });
        ctx.check(rc::byId("gf_test:jeed") == nullptr && !rc::internal::typesWithoutLoader().contains("jeed:effect_provider"), "a failing condition skips the recipe quietly");
        ctx.check(rc::byId("gf_test:table") != nullptr, "the table recipe loads while the table is enabled");
        rc::ext::registerCondition("twilightforest:uncrafting_table_enabled", [](JsonDoc const&) { return false; });
        ctx.check(rc::byId("gf_test:table") == nullptr, "an owner's condition (disableEntireTable) removes it at the next query");
        rc::internal::unregisterCondition("twilightforest:uncrafting_table_enabled");
        ctx.check(rc::byId("gf_test:table") != nullptr, "and the default brings it back");
    }
});

// ---- items -------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.behaviour", [](Context& ctx) -> ll::coro::CoroTask<> {
    static int uses = 0, invTicks = 0, releases = 0, finishes = 0, hurts = 0, equips = 0, unequips = 0;
    uses = invTicks = releases = finishes = hurts = equips = unequips = 0;
    gf::item::registerItem(
        kFixture,
        {
            .use = [](Player&, ItemStack& s) {
                ++uses;
                gf::item::setData(s, "used", uses);
                return gf::item::Result::Consume;
            },
            .release       = [](Player&, ItemStack&, int) { ++releases; },
            .finishUsing   = [](Player&, ItemStack&) { ++finishes; },
            .hurtEnemy     = [](ItemStack&, Actor&, Actor&) { return ++hurts > 0; },
            .destroySpeed  = [](ItemStack const&, Block const&) -> std::optional<float> { return 42.0f; },
            .inventoryTick = [](ItemStack& s, Player&, int, bool selected) {
                ++invTicks;
                if (selected) gf::item::setData(s, "ticked", true);
            },
            .attackBonus = [](ItemStack const&, Actor&, Player&) { return 3.5f; },
        }
    );
    gf::item::registerItem(
        "minecraft:iron_helmet",
        {.onEquip = [](Player&, ItemStack const&, int slot) { equips += slot == 0; }, .onUnequip = [](Player&, ItemStack const&, int slot) { unequips += slot == 0; }}
    );
    auto& player = ctx.player();
    auto  stack  = items::make(kFixture);
    ctx.require(stack.has_value(), "the fixture item exists (test pack)");
    auto& inv  = player.getInventory();
    int   slot = player.getSelectedItemSlot();
    inv.setItem(slot, *stack);
    // use: the interaction dispatch, as a right-click
    std::string id = kFixture;
    auto        r  = ia::dispatchUse(ia::UseContext{player, inv.getItem(slot), id, false});
    ctx.check(r == ia::Result::Consume && uses == 1, "use runs through the interaction dispatch");
    ctx.check(gf::item::getData(inv.getItem(slot), "used") == 1, "use changed the hand stack in place");
    // inventory tick
    gf::item::internal::tickPlayer(player, false);
    ctx.check(invTicks >= 1 && gf::item::getData(inv.getItem(slot), "ticked") == true, "inventoryTick ran on the selected slot and was written back");
    // the ComponentItem virtuals
    ItemStack   copy = inv.getItem(slot);
    Item const* item = copy.mItem.get();
    ctx.require(item != nullptr, "the fixture's Item");
    item->releaseUsing(copy, &player, 0);
    ctx.check(releases == 1, "releaseUsing reaches the behaviour");
    item->useTimeDepleted(copy, &player.getLevel(), &player);
    ctx.check(finishes == 1, "useTimeDepleted reaches finishUsing");
    auto& victim = ctx.spawnPlayer("item_victim");
    item->hurtActor(copy, victim, player);
    ctx.check(hurts == 1, "hurtActor reaches hurtEnemy");
    auto const* stone = gf::game::blockWith("minecraft:stone");
    ctx.check(stone && item->getDestroySpeed(copy, *stone) == 42.0f, "getDestroySpeed asks destroySpeed");
    float bonus = EnchantUtils::getMeleeDamageBonus(victim, player);
    ctx.check(bonus >= 3.5f, "the melee bonus includes attackBonus (got " + std::to_string(bonus) + ")");
    // equip / unequip
    auto helmet = items::make("minecraft:iron_helmet");
    ctx.require(helmet.has_value(), "an iron helmet");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, *helmet);
    gf::item::internal::tickPlayer(player, false);
    ctx.check(equips == 1, "onEquip when the helmet is put on");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack::EMPTY_ITEM());
    gf::item::internal::tickPlayer(player, false);
    ctx.check(unequips == 1, "onUnequip when it comes off");
    gf::item::internal::unregister(kFixture);
    gf::item::internal::unregister("minecraft:iron_helmet");
    ctx.check(gf::item::internal::behaviourOf(kFixture) == nullptr, "unregistered");
    inv.setItem(slot, ItemStack::EMPTY_ITEM());
    co_return;
});

// Java's details of the item hooks: Inventory#tick's armour indices, use ticks written back, the
// post-hit durability after hurtEnemy, equipment seen again after a rejoin.
GF_REGISTER_SELFTEST("item.behaviour_java", [](Context& ctx) {
    static std::vector<std::pair<int, bool>> armorTicks;
    static int                               hits = 0, equips = 0;
    static bool                              hitResult = true;
    armorTicks.clear();
    hits = equips = 0;
    auto& player   = ctx.player();
    int   selected = player.getSelectedItemSlot();
    // Inventory#tick: the boots are index 0 of the armour list, "selected" when the hotbar selection is 0
    gf::item::registerItem("minecraft:iron_boots", {.inventoryTick = [](ItemStack&, Player&, int slot, bool sel) { armorTicks.emplace_back(slot, sel); }});
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, *items::make("minecraft:iron_boots"));
    gf::item::internal::tickPlayer(player, false);
    ctx.check(armorTicks.size() == 1 && armorTicks[0].first == 0 && armorTicks[0].second == (selected == 0), "worn boots tick as Java's armour index 0, selected iff the hotbar selection is 0");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, ItemStack::EMPTY_ITEM());
    gf::item::internal::unregister("minecraft:iron_boots");
    // equipment seen again after the player left (the worn list is forgotten on disconnect)
    gf::item::registerItem("minecraft:iron_helmet", {.onEquip = [](Player&, ItemStack const&, int) { ++equips; }});
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, *items::make("minecraft:iron_helmet"));
    gf::item::internal::tickPlayer(player, false);
    gf::item::internal::tickPlayer(player, false);
    ctx.check(equips == 1, "onEquip once while worn");
    gf::item::internal::forgetWorn(player);
    gf::item::internal::tickPlayer(player, false);
    ctx.check(equips == 2, "and again after a rejoin");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack::EMPTY_ITEM());
    gf::item::internal::tickPlayer(player, false);
    gf::item::internal::unregister("minecraft:iron_helmet");
    // use ticks: the change goes back to the slot the item is used from
    gf::item::registerItem(kFixture, {.useTick = [](Player&, ItemStack& s, int) { gf::item::setData(s, "charged", true); }});
    auto& inv = player.getInventory();
    inv.setItem(selected, *items::make(kFixture));
    auto& inUse            = player.mItemInUse.get();
    inUse.mItem.get()      = inv.getItem(selected);
    inUse.mSlot->mContainerId = ContainerID::Inventory;
    inUse.mSlot->mSlot        = selected;
    gf::item::internal::tickPlayer(player, false);
    ctx.check(gf::item::getData(inv.getItem(selected), "charged") == true, "a useTick change reaches the hand slot");
    inUse.mItem.get() = ItemStack::EMPTY_ITEM();
    inv.setItem(selected, ItemStack::EMPTY_ITEM());
    gf::item::internal::unregister(kFixture);
    // hurtEnemy true: the item's own post-hit handling (its durability) runs; false: it does not
    gf::item::registerItem("minecraft:iron_sword", {.hurtEnemy = [](ItemStack&, Actor&, Actor&) {
                                                        ++hits;
                                                        return hitResult;
                                                    }});
    auto& victim = ctx.spawnPlayer("item_java_victim");
    auto  sword  = *items::make("minecraft:iron_sword");
    hitResult    = true;
    sword.mItem->hurtActor(sword, victim, player);
    if (hits == 1) {
        int afterTrue = sword.getDamageValue();
        ctx.check(afterTrue > 0, "hurtEnemy true: the sword lost durability (" + std::to_string(afterTrue) + ")");
        hitResult = false;
        sword.mItem->hurtActor(sword, victim, player);
        ctx.check(sword.getDamageValue() == afterTrue, "hurtEnemy false: no durability lost");
    } else {
        ctx.info("minecraft:iron_sword is not a component item here (the hook serves TF's data-driven items)");
    }
    gf::item::internal::unregister("minecraft:iron_sword");
});

GF_REGISTER_SELFTEST("item.cooldown", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto uid = ctx.player().getOrCreateUniqueID();
    gf::item::cooldown(ctx.player(), kFixture, 40);
    int left = gf::item::cooldownLeft(ctx.player(), kFixture);
    ctx.check(left > 30 && left <= 40, "a 40 tick cooldown starts (got " + std::to_string(left) + ")");
    co_await ctx.ticks(10);
    auto* p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player is still there");
    int later = gf::item::cooldownLeft(*p, kFixture);
    ctx.check(later < left, "the cooldown runs down (" + std::to_string(later) + ")");
    ctx.check(gf::item::cooldownLeft(*p, "gf_test:other") == 0, "other categories have none");
});

GF_REGISTER_SELFTEST("item.tooltip_lines", [](Context& ctx) {
    gf::item::internal::unregister(kFixture); // nothing else on the fixture item (item.spike_client's line)
    gf::enchant::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .nameKey = "enchantment.gf_test.fixture_react"});
    gf::item::addTooltip(kFixture, [](ItemStack const&, std::vector<std::string>& lines, Player const*) { lines.push_back("Fixture tooltip"); });
    auto& player = ctx.player();
    auto  stack  = items::make(kFixture);
    ctx.require(stack.has_value(), "the fixture item");
    auto& inv = player.getInventory();
    inv.setItem(1, *stack);
    gf::item::internal::tickPlayer(player, true);
    ctx.check(loreHas(inv.getItem(1), sect() + "r" + sect() + "fFixture tooltip"), "the unstyled tooltip line is the stack's lore, white as Java draws it");
    ItemStack enchanted = inv.getItem(1);
    gf::enchant::set(enchanted, "gf_test:fixture_react", 2);
    ctx.check(loreHas(enchanted, sect() + "7Fixture React II"), "a custom enchantment is a lore line named like an enchantment");
    ctx.check(enchanted.getCustomLore().size() == 2, "tooltip line + enchantment line (got " + std::to_string(enchanted.getCustomLore().size()) + ")");
    auto lines = gf::item::ext::tooltipLines(enchanted, &player);
    ctx.check(lines.size() == 2 && lines[0] == "Fixture tooltip", "Java's order: the item's lines, then the enchantments");
    // the lore survives other lore and is replaced, not repeated
    auto lore = enchanted.getCustomLore();
    lore.push_back("someone else's line");
    enchanted.setCustomLore(lore);
    gf::item::internal::refreshLore(enchanted, &player);
    gf::item::internal::refreshLore(enchanted, &player);
    ctx.check(enchanted.getCustomLore().size() == 3 && loreHas(enchanted, "someone else's line"), "foreign lore is kept, ours is not doubled");
    gf::item::internal::unregister(kFixture);
    gf::enchant::set(enchanted, "gf_test:fixture_react", 0);
    gf::item::internal::refreshLore(enchanted, &player);
    ctx.check(enchanted.getCustomLore().size() == 1, "without tooltips and enchantments only the foreign line stays");
    inv.setItem(1, ItemStack::EMPTY_ITEM());
    gf::enchant::internal::unregisterType("gf_test:fixture_react");
});

GF_REGISTER_SELFTEST("item.dispense_fixture", [](Context& ctx) -> ll::coro::CoroTask<> {
    static gf::core::GlobalPos seenPos{};
    static int                 seenFacing = -1, calls = 0;
    calls                                  = 0;
    gf::item::registerDispense(kFixture, [](BlockSource& region, BlockPos const& at, int facing, ItemStack& stack) {
        ++calls;
        seenPos    = gf::game::globalPos(region, at);
        seenFacing = facing;
        stack      = items::withCount(stack, items::countOf(stack) - 1);
        return true;
    });
    auto pos = ctx.testPos(5905);
    pos.y += 1;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:dispenser", {{"facing_direction", 1}}), "a dispenser facing up");
    co_await ctx.ticks(1);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the overworld");
    auto  bp = gf::game::blockPos(pos);
    auto* ba = region->getBlockEntity(bp);
    ctx.require(ba != nullptr, "the dispenser's block actor");
    auto* container = static_cast<DispenserBlockActor*>(ba)->getContainer();
    ctx.require(container != nullptr, "its container");
    container->setItem(0, items::make(kFixture, 3).value_or(ItemStack::EMPTY_ITEM()));
    auto const& block = region->getBlock(bp);
    static_cast<DispenserBlock const&>(block.getBlockType()).dispenseFrom(*region, bp);
    ctx.check(calls == 1, "the registered dispense behaviour ran");
    ctx.check(seenPos == pos, "it got the dispenser's position");
    ctx.check(seenFacing == 1, "and its facing (up)");
    ctx.check(items::countOf(container->getItem(0)) == 2, "the changed stack went back into the dispenser");
    // OptionalDispenseItemBehavior failing: the item stays (no default ejection), the fail sound plays
    gf::item::registerDispense(kFixture, [](BlockSource&, BlockPos const&, int, ItemStack&) {
        ++calls;
        return false;
    });
    static_cast<DispenserBlock const&>(region->getBlock(bp).getBlockType()).dispenseFrom(*region, bp);
    ctx.check(calls == 2, "the failing behaviour ran");
    ctx.check(items::countOf(container->getItem(0)) == 2, "a failed dispense keeps the item in the dispenser (not ejected)");
    container->setItem(0, ItemStack::EMPTY_ITEM());
    gf::item::internal::unregister(kFixture);
});

// ---- enchantments ------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.enchant_anvil", [](Context& ctx) {
    namespace en = gf::enchant;
    en::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .nameKey = "enchantment.gf_test.fixture_react"});
    en::registerType("gf_test:fixture_chill", {.maxLevel = 3, .anvilCost = 8, .exclusiveWith = {"gf_test:fixture_react"}});
    auto sword = items::make("minecraft:iron_sword");
    auto book  = items::make("minecraft:enchanted_book");
    ctx.require(sword && book, "an iron sword and an enchanted book");
    en::set(*sword, "gf_test:fixture_react", 1);
    en::set(*book, "gf_test:fixture_react", 1);
    ctx.check(en::level(*sword, "gf_test:fixture_react") == 1 && en::level(*book, "gf_test:fixture_react") == 1, "levels stored on item and book");
    ctx.check(en::all(*sword).contains("gf_test:fixture_react"), "all() lists the custom enchantment");
    ctx.check(sword->isEnchanted(), "the glint marker makes the stack enchanted for clients (spike S6)");
    // the anvil: Bedrock's combine, then Java's merge of the custom enchantments
    ItemStack out  = *sword;
    int       cost = EnchantUtils::combineEnchantedItems(*sword, *book, out, true);
    ctx.check(en::level(out, "gf_test:fixture_react") == 2, "equal levels merge to the next level through the anvil");
    ctx.check(cost >= 8, "the anvil cost includes anvilCost/2 * level for a book (got " + std::to_string(cost) + ")");
    ItemStack book3 = *book;
    en::set(book3, "gf_test:fixture_react", 3);
    ItemStack out3 = *sword;
    EnchantUtils::combineEnchantedItems(*sword, book3, out3, true);
    ctx.check(en::level(out3, "gf_test:fixture_react") == 3, "a higher level wins");
    ItemStack chillBook = *book;
    en::set(chillBook, "gf_test:fixture_react", 0);
    en::set(chillBook, "gf_test:fixture_chill", 1);
    ItemStack outChill = *sword;
    EnchantUtils::combineEnchantedItems(*sword, chillBook, outChill, true);
    ctx.check(en::level(outChill, "gf_test:fixture_chill") == 0, "an exclusive enchantment does not join");
    // vanilla ids go to Bedrock's enchantments
    en::set(out, "minecraft:sharpness", 3);
    ctx.check(en::level(out, "minecraft:sharpness") == 3 && en::all(out)["minecraft:sharpness"] == 3, "vanilla enchantments by Java id");
    // the grindstone: every custom enchantment goes, with its experience
    int xp = CraftHandlerGrindstone::_getExperienceFromItem(out);
    ctx.check(xp >= 5 + 9, "grindstone experience includes the custom min cost (got " + std::to_string(xp) + ")");
    EnchantUtils::removeEnchants(out);
    ctx.check(en::level(out, "gf_test:fixture_react") == 0 && en::level(out, "minecraft:sharpness") == 0, "the grindstone removed both");
    ctx.check(!gf::enchant::internal::custom(out).size(), "no custom data left");
    // AnvilMenu's mutable set: two mutually exclusive enchantments of one book, the first joins and
    // the second conflicts with it (checked against the set so far, not the first item only)
    ItemStack both = *book;
    en::set(both, "gf_test:fixture_react", 0);
    en::set(both, "gf_test:fixture_chill", 1);
    en::set(both, "gf_test:fixture_react", 2);
    auto      plain   = items::make("minecraft:iron_sword").value_or(ItemStack::EMPTY_ITEM());
    ItemStack outBoth = plain;
    en::internal::mergeAnvil(plain, both, outBoth, true);
    int joined = (en::level(outBoth, "gf_test:fixture_chill") > 0 ? 1 : 0) + (en::level(outBoth, "gf_test:fixture_react") > 0 ? 1 : 0);
    ctx.check(joined == 1, "of two exclusive enchantments on one book only one joins (got " + std::to_string(joined) + ")");
    // a vanilla enchantment the engine merged that conflicts with a custom one is left out
    en::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .exclusiveWith = {"minecraft:sharpness"}});
    ItemStack sharpBook = items::make("minecraft:enchanted_book").value_or(ItemStack::EMPTY_ITEM());
    en::set(sharpBook, "minecraft:sharpness", 2);
    ItemStack reactSword = plain;
    en::set(reactSword, "gf_test:fixture_react", 1);
    ItemStack outSharp = reactSword;
    EnchantUtils::combineEnchantedItems(reactSword, sharpBook, outSharp, true);
    ctx.check(en::level(outSharp, "minecraft:sharpness") == 0 && en::level(outSharp, "gf_test:fixture_react") == 1,
              "a vanilla enchantment exclusive with a custom one does not join (got sharpness " + std::to_string(en::level(outSharp, "minecraft:sharpness")) + ")");
    // creative players (instabuild) skip the supported-items check, as Java's AnvilMenu
    en::registerType("gf_test:fixture_boots", {.maxLevel = 1, .anvilCost = 2, .supportedItems = "#minecraft:foot_armor"});
    ItemStack bootsBook = items::make("minecraft:enchanted_book").value_or(ItemStack::EMPTY_ITEM());
    en::set(bootsBook, "gf_test:fixture_boots", 1);
    ItemStack survivalOut = plain, creativeOut = plain;
    en::internal::mergeAnvil(plain, bootsBook, survivalOut, true, false);
    en::internal::mergeAnvil(plain, bootsBook, creativeOut, true, true);
    ctx.check(en::level(survivalOut, "gf_test:fixture_boots") == 0, "an enchantment for boots does not go on a sword in survival");
    ctx.check(en::level(creativeOut, "gf_test:fixture_boots") == 1, "but does for a creative player");
    for (auto const* id : {"gf_test:fixture_react", "gf_test:fixture_chill", "gf_test:fixture_boots"}) en::internal::unregisterType(id);
});

// treasure / tradeable / discoverable are Java's enchantment tags for a registered spec too: an
// owner's registerType does not make an enchantment the table offers unless its tag (or setFlags) does.
GF_REGISTER_SELFTEST("item.enchant_flags", [](Context& ctx) {
    namespace en = gf::enchant;
    en::registerType("gf_test:flag_probe", {.maxLevel = 1, .weight = 1000, .minCost = {1, 0}, .maxCost = {100, 0}});
    auto spec = en::internal::specOf("gf_test:flag_probe");
    ctx.require(spec.has_value(), "the spec is registered");
    ctx.check(!spec->discoverable && !spec->treasure && !spec->tradeable, "the Spec's defaults do not count: not in the tags, no flag");
    auto rolled = [&] {
        for (int seed = 0; seed < 40; ++seed)
            for (int option = 0; option < 3; ++option)
                if (en::internal::rollTable("minecraft:book", 1, 30, seed, option).contains("gf_test:flag_probe")) return true;
        return false;
    };
    ctx.check(!rolled(), "the enchanting table never offers it");
    en::ext::setFlags("gf_test:flag_probe", {.discoverable = true});
    ctx.check(en::internal::specOf("gf_test:flag_probe")->discoverable && en::internal::anyTableEnchantment(), "an explicit flag wins over the tags");
    ctx.check(rolled(), "and the table rolls it then (weight 1000)");
    en::ext::clearFlags("gf_test:flag_probe");
    ctx.check(!en::internal::specOf("gf_test:flag_probe")->discoverable && !rolled(), "back to the tags");
    // TF's four, when F1's import is in this build: none is offered by the table, none is treasure
    for (auto const* id : {"twilightforest:fire_react", "twilightforest:chill_aura", "twilightforest:destruction", "twilightforest:renewal"})
        if (auto s = en::internal::specOf(id)) ctx.check(!s->discoverable && !s->treasure, std::string(id) + " is neither discoverable nor treasure (TF's tags)");
    en::internal::unregisterType("gf_test:flag_probe");
});

// The vanilla client's anvil fallback: a TF enchanted book (custom enchantments only) applied from a
// form with AnvilMenu's numbers.
GF_REGISTER_SELFTEST("item.enchant_anvil_form", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace en = gf::enchant;
    en::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .supportedItems = "minecraft:iron_sword"});
    auto uid = ctx.player().getOrCreateUniqueID();
    auto get = [&]() -> Player* { return ll::service::getLevel()->getPlayer(uid); };
    auto make = [](char const* id, int react) {
        auto s = items::make(id).value_or(ItemStack::EMPTY_ITEM());
        if (react > 0) gf::enchant::set(s, "gf_test:fixture_react", react);
        return s;
    };
    auto& inv = get()->getInventory();
    inv.setItem(0, make("minecraft:iron_sword", 1));
    inv.setItem(1, make("minecraft:enchanted_book", 1));
    inv.setItem(2, make("minecraft:diamond", 0));
    inv.setItem(3, make("minecraft:iron_sword", 0));
    inv.setItem(4, make("minecraft:iron_pickaxe", 0));
    ctx.check(en::internal::isCustomOnlyBook(inv.getItem(1)), "a book with only a custom enchantment is the fallback's");
    {
        auto mixed = make("minecraft:enchanted_book", 1);
        en::set(mixed, "minecraft:sharpness", 1);
        ctx.check(!en::internal::isCustomOnlyBook(mixed), "a book with a vanilla enchantment too goes through the anvil screen");
    }
    auto offers = en::internal::anvilOffers(*get(), 1);
    ctx.check(offers.size() == 2 && offers[0].targetSlot == 0 && offers[1].targetSlot == 3, "offers: the two swords (not the diamond, not the pickaxe it does not support)");
    if (offers.size() == 2) {
        ctx.check(en::level(offers[0].result, "gf_test:fixture_react") == 2 && offers[0].cost == 8, "I + I = II at max(1, 8 / 2) * 2 = 8 levels (got " + std::to_string(offers[0].cost) + ")");
        ctx.check(en::level(offers[1].result, "gf_test:fixture_react") == 1 && offers[1].cost == 4, "a plain sword: I at 4 levels");
        ctx.check(offers[0].result.getBaseRepairCost() == 1, "the result's prior work is 0 * 2 + 1");
    }
    // AnvilMenu#mayPickup / onTake
    int before = levelsOf(*get());
    get()->addLevels(10 - before);
    ctx.check(en::internal::applyAnvilOffer(*get(), 1, 0, nullptr), "applied with 10 levels");
    ctx.check(en::level(get()->getInventory().getItem(0), "gf_test:fixture_react") == 2, "the sword has II");
    ctx.check(items::isEmpty(get()->getInventory().getItem(1)), "the book is used up");
    get()->getInventory().setItem(1, make("minecraft:enchanted_book", 3));
    auto again = en::internal::anvilOffer(*get(), 1, 0);
    ctx.check(again && again->cost == 4 * 3 + 1 && en::level(again->result, "gf_test:fixture_react") == 3,
              "II + III: III at 12 levels plus the sword's prior work (1) (got " + std::to_string(again ? again->cost : -1) + ")");
    ctx.check(!en::internal::applyAnvilOffer(*get(), 1, 3, nullptr) && en::level(get()->getInventory().getItem(3), "gf_test:fixture_react") == 0,
              "not enough levels (2 left, 12 needed): nothing changes");
    // the click on an anvil: vanilla players get the form (the anvil screen does not open), companions
    // the screen (the dispatch as the use-on hook runs it; no anvil is placed: the handler goes by id)
    auto pos = ctx.testPos(5910);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* p = get();
    ctx.require(p != nullptr, "the player");
    p->getInventory().setItem(p->getSelectedItemSlot(), make("minecraft:enchanted_book", 1));
    std::string       blockId = "minecraft:anvil", itemId = "minecraft:enchanted_book";
    auto const&       block   = ctx.region()->getBlock(gf::game::blockPos(pos));
    auto const&       held    = p->getInventory().getItem(p->getSelectedItemSlot());
    ia::UseOnContext  c{*p, held, itemId, block, blockId, pos, 1, {0.5, 1.0, 0.5}, false, true};
    int               forms = en::internal::anvilFormCount(*p);
    ctx.check(ia::dispatchUseOn(c) == ia::Result::Pass, "a vanilla player's use opens the anvil as usual");
    gf::net::setTestCompanion(*p, true);
    ctx.check(ia::dispatchUseOn(c) == ia::Result::Pass, "a companion's use too (its client previews the merge)");
    gf::net::setTestCompanion(*p, false);
    co_await ctx.ticks(36); // no anvil screen opened (simulated player): the form follows after 30 ticks
    p = get();
    ctx.require(p != nullptr, "the player");
    ctx.check(en::internal::anvilFormCount(*p) == forms + 1, "the vanilla player got the anvil form, the companion none");
    for (int i = 0; i < 5; ++i) p->getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    p->getInventory().setItem(p->getSelectedItemSlot(), ItemStack::EMPTY_ITEM());
    p->addLevels(before - levelsOf(*p));
    en::internal::unregisterType("gf_test:fixture_react");
});

// The anvil form's answer comes later: Java's AnvilMenu#stillValid (the anvil in place, the player in
// reach), the book and the item the button showed; a creative player puts a book on any item.
GF_REGISTER_SELFTEST("item.enchant_anvil_choice", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace en = gf::enchant;
    using R      = en::internal::AnvilChoiceResult;
    en::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .supportedItems = "minecraft:iron_sword"});
    en::registerType("gf_test:fixture_other", {.maxLevel = 3, .anvilCost = 8, .supportedItems = "minecraft:iron_sword"});
    auto uid  = ctx.player().getOrCreateUniqueID();
    auto get  = [&]() -> Player* { return ll::service::getLevel()->getPlayer(uid); };
    auto book = [](char const* enchantment) {
        auto s = items::make("minecraft:enchanted_book").value_or(ItemStack::EMPTY_ITEM());
        gf::enchant::set(s, enchantment, 1);
        return s;
    };
    auto anvil = ctx.testPos(5915, 2);
    ctx.require(co_await ctx.ensureLoaded(anvil), "test area loaded");
    auto bp = gf::game::blockPos(anvil);
    ctx.require(ctx.setBlock(anvil, "minecraft:anvil"), "an anvil");
    auto* p = get();
    ctx.require(p != nullptr, "the player");
    auto mode = p->getPlayerGameType();
    p->setPlayerGameType(GameType::Survival);
    // the player 2.5 blocks south of the anvil, `dx` blocks east of it
    auto moveTo = [&](float dx) {
        if (auto* q = get()) q->teleport(Vec3{static_cast<float>(bp.x) + 0.5f + dx, static_cast<float>(bp.y), static_cast<float>(bp.z) + 2.5f}, q->getDimensionId());
    };
    moveTo(0.f);
    co_await ctx.ticks(2);
    p = get();
    ctx.require(p != nullptr, "the player");
    ctx.require(ctx.blockAt(anvil) == "minecraft:anvil", "the anvil stands on the test row's ground (got " + ctx.blockAt(anvil) + ")");
    int before = levelsOf(*p);
    p->addLevels(10 - before);
    p->getInventory().setItem(0, items::make("minecraft:iron_sword").value_or(ItemStack::EMPTY_ITEM()));
    p->getInventory().setItem(1, book("gf_test:fixture_react"));
    p->getInventory().setItem(2, items::make("minecraft:diamond").value_or(ItemStack::EMPTY_ITEM()));
    co_await ctx.ticks(2); // the inventory tick has seen the stacks (lore), as before a form is sent
    p = get();
    ctx.require(p != nullptr, "the player");
    auto& inv = p->getInventory();
    ctx.check(en::internal::stillAtAnvil(*p, bp), "2.5 blocks from the anvil: in reach");
    ctx.check(!en::internal::stillAtAnvil(*p, BlockPos{bp.x + 12, bp.y, bp.z}), "an anvil 12 blocks away: out of reach (4.5 + 4)");
    auto offer = en::internal::anvilOffer(*p, 1, 0);
    ctx.require(offer.has_value() && offer->cost == 4, "the sword's offer: fixture_react I at 4 levels");
    ItemStack const listed = inv.getItem(1);
    en::internal::AnvilChoice choice{1, 0, anvil, listed, inv.getItem(0), offer->cost};
    // another TF book of the same cost in the book's slot: not the book the form listed
    inv.setItem(1, book("gf_test:fixture_other"));
    auto swapped = en::internal::anvilOffer(*p, 1, 0);
    ctx.check(swapped.has_value() && swapped->cost == 4, "(the other book makes an offer of the same cost)");
    ctx.check(en::internal::takeAnvilChoice(*p, choice) == R::Changed, "a swapped book is not applied");
    ctx.check(en::level(inv.getItem(0), "gf_test:fixture_other") == 0, "the sword did not get the other book's enchantment");
    inv.setItem(1, listed);
    // the answer from far away (ItemCombinerMenu#stillValid)
    moveTo(12.f);
    co_await ctx.ticks(2);
    p = get();
    ctx.require(p != nullptr, "the player");
    ctx.info("eyes at " + std::to_string(p->getEyePos().x - bp.x) + " blocks east of the anvil");
    ctx.check(en::internal::takeAnvilChoice(*p, choice) == R::TooFar, "12 blocks from the anvil the choice is refused");
    ctx.check(en::level(p->getInventory().getItem(0), "gf_test:fixture_react") == 0 && levelsOf(*p) == 10, "nothing taken, nothing given");
    moveTo(0.f);
    co_await ctx.ticks(2);
    p = get();
    ctx.require(p != nullptr, "the player");
    ctx.check(en::internal::takeAnvilChoice(*p, choice) == R::Applied, "back at the anvil the choice is taken");
    ctx.check(en::level(p->getInventory().getItem(0), "gf_test:fixture_react") == 1 && levelsOf(*p) == 6, "the sword has I for 4 levels");
    // the anvil gone: no answer
    p->getInventory().setItem(1, book("gf_test:fixture_react"));
    auto again = en::internal::anvilOffer(*p, 1, 0);
    gf::game::clearBlock(*ctx.region(), bp);
    co_await ctx.ticks(1);
    p = get();
    ctx.require(p != nullptr, "the player");
    if (again) ctx.check(en::internal::takeAnvilChoice(*p, {1, 0, anvil, p->getInventory().getItem(1), p->getInventory().getItem(0), again->cost}) == R::NoAnvil, "without the anvil the choice is refused");
    // survival: no offer for the diamond; creative (instabuild): any item takes the book (AnvilMenu's flag1)
    ctx.check(!en::internal::anvilOffer(*p, 1, 2).has_value(), "survival: a diamond takes no fixture_react");
    p->setPlayerGameType(GameType::Creative);
    co_await ctx.ticks(1);
    p = get();
    ctx.require(p != nullptr, "the player");
    auto creative = en::internal::anvilOffer(*p, 1, 2);
    ctx.check(creative.has_value() && en::level(creative->result, "gf_test:fixture_react") == 1, "creative: the diamond takes the book (every item can store enchantments)");
    p->setPlayerGameType(mode);
    for (int i = 0; i < 3; ++i) p->getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    p->addLevels(before - levelsOf(*p));
    en::internal::unregisterType("gf_test:fixture_other");
    en::internal::unregisterType("gf_test:fixture_react");
});

// ---- effects -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.effect_carrier", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::effects;
    static int ticked = 0;
    ticked            = 0;
    fx::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD, .tick = [](Actor&, int) { ++ticked; }, .tickInterval = 5});
    auto uid = ctx.player().getOrCreateUniqueID();
    gf::net::setTestCompanion(ctx.player(), true);
    (void)gf::net::takeTestMessages(ctx.player());
    ctx.require(fx::apply(ctx.player(), "gf_test:fixture_frosted", 100, 1), "the custom effect applies");
    ctx.check(fx::has(ctx.player(), "gf_test:fixture_frosted") && fx::amplifier(ctx.player(), "gf_test:fixture_frosted") == 1, "has / amplifier");
    auto const* carrier = ctx.player().getEffect(2u); // MobEffectIds::MovementSlowdown
    ctx.check(carrier && carrier->mAmplifier == 1, "the vanilla carrier (Slowness II) is on the player");
    ctx.check(carrier && !carrier->mEffectVisible, "with hidden particles");
    bool tagged = false;
    for (auto const& t : ctx.player().getTags()) tagged = tagged || t.starts_with("gf_fx|gf_test:fixture_frosted|1|");
    ctx.check(tagged, "persisted as an actor tag");
    bool synced = false;
    for (auto const& m : gf::net::takeTestMessages(ctx.player())) synced = synced || m.channel == "effects.sync";
    ctx.check(synced, "a companion gets effects.sync");
    ctx.check(!fx::apply(ctx.player(), "gf_test:fixture_frosted", 50, 1), "a shorter one of the same level changes nothing (Java's update)");
    ctx.check(fx::apply(ctx.player(), "gf_test:fixture_frosted", 20, 2), "a higher level replaces it");
    {
        auto const* c2 = ctx.player().getEffect(2u);
        ctx.check(c2 && c2->mAmplifier == 2, "the carrier follows the stronger level");
        auto list = fx::internal::activeOf(ctx.player());
        ctx.check(list.size() == 1 && list[0].hidden.size() == 1 && list[0].hidden[0].first == 1, "the longer, weaker one waits hidden (MobEffectInstance#hiddenEffect)");
    }
    co_await ctx.ticks(11);
    auto* p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(ticked >= 2, "the effect ticks every tickInterval (got " + std::to_string(ticked) + ")");
    fx::internal::forget(*p);
    ctx.check(fx::amplifier(*p, "gf_test:fixture_frosted") == 2, "the state comes back from the tag");
    ctx.check(fx::internal::activeOf(*p).front().hidden.size() == 1, "with its hidden instance");
    // the tag is rewritten once a second: the state read back may be up to 20 ticks old
    co_await ctx.ticks(25);
    p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(fx::amplifier(*p, "gf_test:fixture_frosted") == 1, "the hidden level resumes when the stronger one ends");
    auto const* resumed = p->getEffect(2u);
    ctx.check(resumed && resumed->mAmplifier == 1, "and the carrier with it");
    // a carrier lost on its own (an expired vanilla instance of the same id) comes back
    p->removeEffect(2);
    co_await ctx.ticks(2);
    p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(fx::has(*p, "gf_test:fixture_frosted") && p->getEffect(2u) != nullptr, "the carrier is put back while the effect lasts");
    // milk / effect clear (removeAllEffects) end it, as on Java
    p->removeAllEffects();
    co_await ctx.ticks(1);
    p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(!fx::has(*p, "gf_test:fixture_frosted"), "removeAllEffects ends the custom effect");
    bool left = false;
    for (auto const& t : p->getTags()) left = left || t.starts_with("gf_fx|");
    ctx.check(!left, "and its persistence tag");
    // vanilla ids
    ctx.check(fx::apply(*p, "minecraft:speed", 40, 0) && fx::amplifier(*p, "minecraft:speed") == 0, "vanilla effects by Java id");
    fx::remove(*p, "minecraft:speed");
    ctx.check(!fx::has(*p, "minecraft:speed"), "removed");
    gf::net::setTestCompanion(*p, false);
    fx::internal::unregisterType("gf_test:fixture_frosted");
});

// A vanilla instance of the carrier's effect (a Slowness potion) is never lost to a custom effect.
GF_REGISTER_SELFTEST("item.effect_carrier_vanilla", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::effects;
    fx::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
    auto uid = ctx.player().getOrCreateUniqueID();
    auto get = [&]() -> Player* { return ll::service::getLevel()->getPlayer(uid); };
    // a weaker potion is replaced while the custom effect lasts and comes back after it
    ctx.require(fx::apply(*get(), "minecraft:slowness", 3600, 0), "a Slowness I potion (3 min)");
    ctx.require(fx::apply(*get(), "gf_test:fixture_frosted", 20, 1), "frosted II over it");
    auto const* c = get()->getEffect(2u);
    ctx.check(c && c->mAmplifier == 1 && !c->mEffectVisible, "the carrier (Slowness II, hidden particles) replaced the potion meanwhile");
    fx::remove(*get(), "gf_test:fixture_frosted");
    c = get()->getEffect(2u);
    ctx.check(c && c->mAmplifier == 0 && c->mDuration->mValue > 3000 && c->mEffectVisible, "removing frosted puts the potion back with its time and particles");
    // the same when the custom effect runs out
    ctx.require(fx::apply(*get(), "gf_test:fixture_frosted", 5, 1), "a short frosted II over the potion");
    co_await ctx.ticks(8);
    c = get()->getEffect(2u);
    ctx.check(!fx::has(*get(), "gf_test:fixture_frosted") && c && c->mAmplifier == 0 && c->mDuration->mValue > 3000, "the potion is back when frosted expires");
    get()->removeEffect(2);
    // a stronger potion is left alone and survives the custom effect's removal
    ctx.require(fx::apply(*get(), "minecraft:slowness", 600, 2), "a Slowness III potion");
    ctx.require(fx::apply(*get(), "gf_test:fixture_frosted", 100, 1), "frosted II under it");
    c = get()->getEffect(2u);
    ctx.check(c && c->mAmplifier == 2 && c->mEffectVisible, "the stronger potion stays the instance (it carries the slowness)");
    fx::remove(*get(), "gf_test:fixture_frosted");
    c = get()->getEffect(2u);
    ctx.check(c && c->mAmplifier == 2 && c->mDuration->mValue > 500, "and removing frosted does not remove the potion");
    get()->removeEffect(2);
    fx::internal::unregisterType("gf_test:fixture_frosted");
});

// A state read back from a persistence tag older than the carrier (an unload or a restart between
// two tag writes) follows the carrier the actor kept, so the carrier is still known as its own.
GF_REGISTER_SELFTEST("item.effect_carrier_reload", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::effects;
    fx::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
    auto uid = ctx.player().getOrCreateUniqueID();
    auto get = [&]() -> Player* { return ll::service::getLevel()->getPlayer(uid); };
    ctx.require(fx::apply(*get(), "gf_test:fixture_frosted", 400, 1), "frosted II for 20 s");
    co_await ctx.ticks(3);
    auto* p = get();
    ctx.require(p != nullptr, "the player");
    auto const* carrier = p->getEffect(2u);
    ctx.require(carrier != nullptr, "its carrier");
    int now = carrier->mDuration->mValue;
    // the tag as the last periodic write left it: 15 ticks before the actor unloaded
    for (auto const& t : p->getTags())
        if (t.starts_with(fx::internal::kTagPrefix)) p->removeTag(t);
    p->addTag(std::string(fx::internal::kTagPrefix) + "gf_test:fixture_frosted|1|" + std::to_string(now + 15) + "|0:" + std::to_string(now + 115) + "|");
    fx::internal::forget(*p); // unloaded: the next query reads the tag
    auto list = fx::internal::activeOf(*p);
    ctx.check(list.size() == 1 && list[0].ticks == now, "the reloaded state follows its carrier (" + std::to_string(list.empty() ? -1 : list[0].ticks) + " / " + std::to_string(now) + ")");
    ctx.check(list.size() == 1 && list[0].hidden.size() == 1 && list[0].hidden[0].second == now + 100, "its hidden instance lost the same ticks");
    ctx.check(fx::internal::carrierIsOurs(*p, *fx::internal::specOf("gf_test:fixture_frosted"), list.front()), "the carrier is known as its own");
    fx::remove(*p, "gf_test:fixture_frosted");
    ctx.check(p->getEffect(2u) == nullptr, "removing the effect takes its carrier away (no foreign Slowness left)");
    // a vanilla instance that only looks alike (another amplifier) stays foreign
    ctx.require(fx::apply(*p, "minecraft:slowness", 600, 0), "a Slowness I potion");
    p->addTag(std::string(fx::internal::kTagPrefix) + "gf_test:fixture_frosted|1|610||");
    fx::internal::forget(*p);
    list = fx::internal::activeOf(*p);
    ctx.check(list.size() == 1 && list[0].ticks == 610, "a carrier of another level is not taken for the effect's own");
    fx::internal::clearAll(*p);
    p->removeEffect(2);
    fx::internal::unregisterType("gf_test:fixture_frosted");
});

// ---- player data -------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.stats_traits", [](Context& ctx) {
    auto& player = ctx.player();
    gf::net::setTestCompanion(player, true);
    (void)gf::net::takeTestMessages(player);
    auto before = gf::stats::value(player, "twilightforest:bugs_squished");
    gf::stats::award(player, "twilightforest:bugs_squished", 2);
    ctx.check(gf::stats::value(player, "twilightforest:bugs_squished") == before + 2, "award adds to the statistic");
    gf::traits::set(player, "gf_test:fixture_trait", true);
    ctx.check(gf::traits::get(player, "gf_test:fixture_trait"), "a trait reads back");
    auto doc = gf::game::db::get("gf:traits:" + player.getUuid().asString());
    ctx.check(doc && doc->value("gf_test:fixture_trait", false), "traits persist in the world DB");
    bool traits = false, stats = false;
    for (auto const& m : gf::net::takeTestMessages(player)) {
        traits = traits || (m.channel == "traits.sync" && m.payload["traits"].value("gf_test:fixture_trait", false));
        stats  = stats || m.channel == "stats.sync";
    }
    ctx.check(traits && stats, "the companion got traits.sync and stats.sync");
    gf::traits::set(player, "gf_test:fixture_trait", false);
    ctx.check(!gf::traits::get(player, "gf_test:fixture_trait") && !gf::game::db::get("gf:traits:" + player.getUuid().asString()), "cleared");
    gf::stats::internal::forget(player); // no record of the simulated player stays in the world DB
    ctx.check(!gf::game::db::get("gf:stats:" + player.getUuid().asString()), "the statistics record is gone");
    gf::net::setTestCompanion(player, false);
});

GF_REGISTER_SELFTEST("item.gear_modifiers", [](Context& ctx) {
    gf::gear::internal::registerModifier("gf_test:fixture_head", "head");
    gf::gear::internal::registerModifier("gf_test:fixture_feet", "feet");
    auto helmet = items::make("minecraft:iron_helmet");
    ctx.require(helmet.has_value(), "an iron helmet");
    ctx.check(gf::gear::add(*helmet, "gf_test:fixture_head"), "a head modifier goes on a helmet");
    ctx.check(!gf::gear::add(*helmet, "gf_test:fixture_feet"), "a feet modifier does not");
    ctx.check(!gf::gear::add(*helmet, "gf_test:unknown"), "unknown modifiers do not");
    ctx.check(gf::gear::modifiers(*helmet).contains("gf_test:fixture_head"), "modifiers lists it");
    auto& player = ctx.player();
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, *helmet);
    ctx.check(gf::gear::level(player, "gf_test:fixture_head") == 1, "level over the worn gear");
    ctx.check(gf::gear::remove(*helmet, "gf_test:fixture_head") && gf::gear::modifiers(*helmet).empty(), "removed");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack::EMPTY_ITEM());
    ctx.check(gf::gear::level(player, "gf_test:fixture_head") == 0, "nothing worn, level 0");
    // getStackForGroup: a group of several armour slots finds no stack (Java returns EMPTY)
    gf::gear::internal::registerModifier("gf_test:fixture_armor", "armor");
    auto boots = items::make("minecraft:iron_boots");
    ctx.require(boots.has_value(), "iron boots");
    ctx.check(gf::gear::add(*boots, "gf_test:fixture_armor") && gf::gear::modifiers(*boots).contains("gf_test:fixture_armor"), "an armor-group modifier goes on boots");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, *boots);
    ctx.check(gf::gear::level(player, "gf_test:fixture_armor") == 0, "but is not active through the player (Java's getStackForGroup)");
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, ItemStack::EMPTY_ITEM());
    ctx.check(gf::gear::level(player, "gf_test:unknown") == 0, "an unknown modifier is never active");
    gf::gear::internal::unregisterModifier("gf_test:fixture_head");
    gf::gear::internal::unregisterModifier("gf_test:fixture_feet");
    gf::gear::internal::unregisterModifier("gf_test:fixture_armor");
});

// ---- catalog and rarity ------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("item.catalog", [](Context& ctx) {
    auto const& groups = gf::catalog::groups();
    ctx.require(groups.size() == 4, "TF's four creative tabs");
    ctx.check(groups[0].id == "twilightforest:blocks" && groups[1].id == "twilightforest:items" && groups[2].id == "twilightforest:equipment"
                  && groups[3].id == "twilightforest:food",
              "in TF's order");
    ctx.check(groups[2].nameKey == "itemGroup.twilightforest.equipment", "with TF's titles");
    ctx.check(gf::catalog::groupOf("minecraft:stone").empty(), "vanilla items are in no TF tab");
    auto names = gf::catalog::internal::groupNames();
    ctx.check(!names.empty(), "the server's creative registry is readable (" + std::to_string(names.size()) + " groups)");
    std::size_t tf = 0;
    for (auto const& g : groups) tf += g.items.size();
    ctx.info("TF catalog items: " + std::to_string(tf));
});

GF_REGISTER_SELFTEST("item.rarity", [](Context& ctx) {
    namespace ex = gf::item::ext;
    ctx.check(ex::rarityOf("twilightforest:naga_trophy") == ex::Rarity::Twilight, "trophies are TWILIGHT");
    ctx.check(ex::rarityOf("twilightforest:lich_banner_pattern") == ex::Rarity::Twilight, "banner patterns are TWILIGHT");
    ctx.check(std::string(ex::rarityColor(ex::Rarity::Twilight)) == sect() + "2", "TWILIGHT names are dark green");
    ctx.check(ex::rarityOf("minecraft:stone") == ex::Rarity::Common, "others are common");
    gf::item::setBar(kFixture, [](ItemStack const&) -> std::optional<gf::item::Bar> { return gf::item::Bar{1.5f, 0x123456}; });
    auto stack = items::make(kFixture);
    ctx.require(stack.has_value(), "the fixture item");
    auto bar = ex::barOf(*stack);
    ctx.check(bar && bar->fraction == 1.0f && bar->rgb == 0x123456, "setBar's bar, fraction clamped to 1");
    // a bar from the stack's item data (BrittleFlaskItem's doses): two stacks of one item differ
    gf::item::setBar(kFixture, [](ItemStack const& s) -> std::optional<gf::item::Bar> {
        auto doses = gf::item::getData(s, "doses");
        if (!doses.is_number_integer()) return std::nullopt;
        return gf::item::Bar{static_cast<float>(doses.get<int>()) / 4.0f, 0xFF8000};
    });
    auto one = *stack, three = *stack;
    gf::item::setData(one, "doses", 1);
    gf::item::setData(three, "doses", 3);
    ctx.check(ex::barOf(one) && ex::barOf(one)->fraction == 0.25f && ex::barOf(three)->fraction == 0.75f, "the bar follows the stack's data");
    ctx.check(!ex::barOf(*stack), "and hides without it");
    gf::item::internal::unregister(kFixture);
    ctx.check(!ex::barOf(*stack), "no bar once unregistered");
    // ItemStack#getRarity: an enchanted UNCOMMON stack shows RARE, RARE shows EPIC, TWILIGHT stays
    ex::setRarity(kFixture, ex::Rarity::Uncommon);
    ItemStack enchanted = *stack;
    gf::enchant::set(enchanted, "minecraft:unbreaking", 1);
    ctx.check(ex::rarityOf(*stack) == ex::Rarity::Uncommon && ex::rarityOf(enchanted) == ex::Rarity::Rare, "enchanted uncommon -> rare");
    ex::setRarity(kFixture, ex::Rarity::Rare);
    ctx.check(ex::rarityOf(enchanted) == ex::Rarity::Epic, "enchanted rare -> epic");
    ex::setRarity(kFixture, ex::Rarity::Twilight);
    ctx.check(ex::rarityOf(enchanted) == ex::Rarity::Twilight, "TWILIGHT stays");
    ex::setRarity(kFixture, ex::Rarity::Common);
});

// TFItemDisplayContextEnumExtension#JARRED: its own display transform, else FIXED's.
GF_REGISTER_SELFTEST("item.display_jarred", [](Context& ctx) {
    namespace ex = gf::item::ext;
    gf::item::internal::clearDisplayTransforms(kFixture);
    ctx.check(!ex::displayTransform(kFixture, ex::kJarred), "no transform without a model's numbers");
    ex::setDisplayTransform(kFixture, "fixed", {.rotation = {0, 180, 0}, .translation = {0, 4, -2}});
    auto t = ex::displayTransform(kFixture, ex::kJarred);
    ctx.check(t && t->rotation[1] == 180 && t->translation[2] == -2, "JARRED falls back to FIXED");
    ex::setDisplayTransform(kFixture, ex::kJarred, {.rotation = {90, 0, 180}});
    t = ex::displayTransform(kFixture, ex::kJarred);
    ctx.check(t && t->rotation[0] == 90 && t->translation[1] == 0, "an item's own JARRED transform wins");
    ctx.check(!ex::displayTransform(kFixture, "gui"), "other contexts have no fallback to FIXED");
    ctx.check(ex::kJarredContext == 64, "the ItemContext flag after Head (32)");
    gf::item::internal::clearDisplayTransforms(kFixture);
});
