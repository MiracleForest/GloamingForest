// Selftests of F9 (prefix "item.", testPos dx 5900..5999): the item behaviour registry and its
// hooks, cooldowns, tooltips as lore, dispensers, custom enchantments through the anvil and the
// grindstone, carrier effects, recipes and special crafting, tags (data and the vanilla bridge),
// data maps, statistics, traits, Travellers' Gear modifiers and the creative catalog. They use the
// gf_test fixtures (assets/test) and synthetic data trees, and remove every registration they add.

#include <filesystem>
#include <fstream>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/platform/UUID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/inventory/network/crafting/CraftHandlerGrindstone.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"

#include "feature/catalog/Catalog.h"
#include "feature/catalog/CatalogInternal.h"
#include "feature/common/Interactions.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/effects/Effects.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Item.h"
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
    ctx.check(gf::tags::has(Registry::Item, "minecraft:boats", "minecraft:oak_boat") && !gf::tags::has(Registry::Item, "minecraft:boats", "minecraft:oak_chest_boat"),
              "boats exclude chest boats");
    ctx.check(gf::tags::has(Registry::EntityType, "minecraft:undead", "minecraft:stray"), "a stray is undead (nested bridge tags)");
    ctx.check(gf::tags::has(Registry::Fluid, "minecraft:water", "minecraft:flowing_water"), "flowing water is water");
    auto planks = gf::tags::members(Registry::Block, "minecraft:planks");
    ctx.check(std::find(planks.begin(), planks.end(), "minecraft:oak_planks") != planks.end(), "members enumerates the game's blocks for a bridged tag");
    ctx.info("minecraft:planks has " + std::to_string(planks.size()) + " members");
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
        // recipes
        auto const* shaped = gf::recipes::byId("gf_test:shaped");
        ctx.require(shaped != nullptr, "a shaped data recipe is loaded");
        ctx.check(shaped->width == 2 && shaped->height == 2 && shaped->ingredients.size() == 4, "the pattern is 2x2 row-major");
        ctx.check(shaped->ingredients[1].tag == "minecraft:planks" && shaped->ingredients[2].empty(), "keys map to ingredients, spaces to empty");
        ctx.check(shaped->resultItem == "minecraft:ladder" && shaped->resultCount == 3, "the result and its count");
        auto const* uncraft = gf::recipes::byId("gf_test:uncraft");
        ctx.check(uncraft && uncraft->type == "twilightforest:uncrafting" && uncraft->resultItem == "gf_test:box", "uncrafting: the input is the recipe's item");
        auto const* mod = gf::recipes::byId("gf_test:modifier");
        ctx.check(mod && mod->resultItem == "twilightforest:travellers_vest" && mod->ingredients.size() == 2, "travellers modifier recipe: the gear is the result");
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
    ui.setItem(first + 2, ItemStack::EMPTY_ITEM());
    auto refused = rc::internal::assembleSpecial(kFixture, rc::internal::gridOf(player, false).grid);
    ctx.check(!refused, "the matcher refuses a grid with one stick");
    ui.setItem(first, ItemStack::EMPTY_ITEM());
    rc::internal::unregisterSpecial(kFixture);
    ctx.check(!rc::internal::isSpecial(kFixture), "unregistered");
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
    ctx.check(loreHas(inv.getItem(1), sect() + "r" + sect() + "7Fixture tooltip"), "the tooltip line is the stack's lore, gray as TF writes it");
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
    co_await ctx.ticks(11);
    auto* p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(ticked >= 2, "the effect ticks every tickInterval (got " + std::to_string(ticked) + ")");
    fx::internal::forget(*p);
    ctx.check(fx::amplifier(*p, "gf_test:fixture_frosted") == 2, "the state comes back from the tag");
    // losing the carrier (milk) ends it
    p->removeEffect(2);
    co_await ctx.ticks(2);
    p = ll::service::getLevel()->getPlayer(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(!fx::has(*p, "gf_test:fixture_frosted"), "the effect ends with its carrier");
    // vanilla ids
    ctx.check(fx::apply(*p, "minecraft:speed", 40, 0) && fx::amplifier(*p, "minecraft:speed") == 0, "vanilla effects by Java id");
    fx::remove(*p, "minecraft:speed");
    ctx.check(!fx::has(*p, "minecraft:speed"), "removed");
    gf::net::setTestCompanion(*p, false);
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
    gf::gear::internal::unregisterModifier("gf_test:fixture_head");
    gf::gear::internal::unregisterModifier("gf_test:fixture_feet");
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
    gf::item::internal::unregister(kFixture);
    ctx.check(!ex::barOf(*stack), "no bar once unregistered");
});
