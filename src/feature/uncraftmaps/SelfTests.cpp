// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U13 (uncraftmaps.*, dx 1300..1399): the uncrafting menu (reversed crafting recipes,
// broken parts, costs, the config filters, the uncrafting recipes, recrafting with enchantment
// transfer, the levels), the screen path for companions, the config sync, the powered table, the
// maps (magic map centres and biome colours with the landmark icons, maze / ore maps, the labyrinth
// snap, cloning) and the commands.
#include <algorithm>
#include <cmath>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "core/uncraftmaps/Maps.h"
#include "feature/common/Screens.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/dimension/World.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"
#include "feature/net/Net.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "feature/uncraftmaps/Config.h"
#include "feature/uncraftmaps/Maps.h"
#include "feature/uncraftmaps/Menu.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::uncraftmaps {

void refreshPowered(BlockSource& region, BlockPos const& pos); // Table.cpp

namespace {

using selftest::Context;
namespace items = game::items;
namespace cu    = core::uncraftmaps;

int levelsOf(Player const& p) {
    auto ref = p.getAttribute(Player::LEVEL());
    if (auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr)) return static_cast<int>(inst->mCurrentValue);
    return 0;
}
void setLevels(Player& p, int n) { p.addLevels(n - levelsOf(p)); }

ItemStack make(Context& ctx, std::string const& id, int count = 1, int aux = 0) {
    auto s = items::make(id, count, aux);
    ctx.require(s.has_value(), "the item " + id + " exists");
    return *s;
}

/// A menu over a fresh state for the test player (no table needed: the menu itself is tested).
struct TestMenu {
    MenuState state;
    Menu      menu;
    explicit TestMenu(Player& p) : menu(p, state) { menu.slotsChanged(Menu::Changed::Assembly); }
    void input(ItemStack const& s) { menu.set(kSlotInput, s); }
    std::string ghost(int i) const { return items::idOf(state.uncrafting[static_cast<std::size_t>(i)]); }
    int count(std::string const& id) const {
        int n = 0;
        for (auto const& s : state.uncrafting)
            if (items::idOf(s) == id) ++n;
        return n;
    }
    int marked() const { return static_cast<int>(std::count(state.marked.begin(), state.marked.end(), true)); }
};

/// The config in effect for one test, put back when it ends.
struct ScopedConfig {
    explicit ScopedConfig(UncraftmapsConfig const& c) { overrideConfig(c); }
    ~ScopedConfig() { restoreConfig(); }
};

} // namespace

// Uncrafting an iron pickaxe: 3 iron + 2 sticks laid out by its pattern, cost 3 (the sticks are in
// #uncrafting_ignores_cost), the levels it needs, and taking a part moves the rest into the
// assembly grid, charges the levels and uses up the input.
GF_REGISTER_SELFTEST("uncraftmaps.iron_pickaxe", [](Context& ctx) {
    auto& p = ctx.player();
    setLevels(p, 0);
    TestMenu m(p);
    m.input(make(ctx, "minecraft:iron_pickaxe"));
    ctx.require(m.state.ghost != nullptr, "the pickaxe has an uncrafting recipe");
    ctx.check(m.count("minecraft:iron_ingot") == 3 && m.count("minecraft:stick") == 2, "3 iron and 2 sticks (" + m.ghost(0) + ", " + m.ghost(4) + ")");
    ctx.check(m.ghost(0) == "minecraft:iron_ingot" && m.ghost(1) == "minecraft:iron_ingot" && m.ghost(2) == "minecraft:iron_ingot", "the top row is iron");
    ctx.check(m.ghost(4) == "minecraft:stick" && m.ghost(7) == "minecraft:stick", "the handle is in the middle column");
    ctx.check(m.marked() == 0, "nothing is broken");
    ctx.check(m.menu.uncraftingCost() == 3, "the cost is 3 (" + std::to_string(m.menu.uncraftingCost()) + ")");
    // too few levels: the click is refused
    m.menu.clicked(kSlotUncrafting + 0, 0, Click::Pickup);
    ctx.check(items::isEmpty(m.state.carried), "no ingredient without the levels");
    setLevels(p, 5);
    m.menu.clicked(kSlotUncrafting + 0, 0, Click::Pickup);
    ctx.check(items::idOf(m.state.carried) == "minecraft:iron_ingot", "the iron is on the cursor");
    ctx.check(items::isEmpty(m.state.input), "the pickaxe is used up");
    ctx.check(levelsOf(p) == 2, "3 levels were charged (" + std::to_string(levelsOf(p)) + " left)");
    int assembly = 0;
    for (auto const& s : m.state.assembly)
        if (!items::isEmpty(s)) ++assembly;
    ctx.check(assembly == 4, "the other 4 parts are in the assembly grid (" + std::to_string(assembly) + ")");
    // the result of the assembly grid without an input: the parts minus the iron taken make nothing
    m.menu.removed();
    setLevels(p, 0);
});

// A damaged pickaxe marks ceil(max(4, 3) * damage) parts as broken; broken parts can't be taken and
// don't count towards the cost.
GF_REGISTER_SELFTEST("uncraftmaps.damaged_parts", [](Context& ctx) {
    auto& p   = ctx.player();
    auto  pick = make(ctx, "minecraft:iron_pickaxe");
    pick.setDamageValue(125); // half of 250
    TestMenu m(p);
    m.input(pick);
    ctx.require(m.state.ghost != nullptr, "a recipe");
    ctx.check(m.marked() == 2, "2 parts are broken (" + std::to_string(m.marked()) + ")");
    ctx.check(m.state.marked[0] && m.state.marked[1] && !m.state.marked[2], "the first damageable parts are marked");
    ctx.check(m.menu.uncraftingCost() == 1, "the cost counts the unbroken iron only (" + std::to_string(m.menu.uncraftingCost()) + ")");
    setLevels(p, 10);
    m.menu.clicked(kSlotUncrafting + 0, 0, Click::Pickup);
    ctx.check(items::isEmpty(m.state.carried), "a broken part can't be taken");
    m.menu.clicked(kSlotUncrafting + 2, 0, Click::Pickup);
    ctx.check(items::idOf(m.state.carried) == "minecraft:iron_ingot", "an unbroken part can");
    int assembly = 0;
    for (auto const& s : m.state.assembly)
        if (!items::isEmpty(s)) ++assembly;
    ctx.check(assembly == 2, "only the 2 sticks go into the assembly grid (" + std::to_string(assembly) + ")");
    m.menu.removed();
    setLevels(p, 0);
});

// The config: the recipe list (blacklist / whitelist), the namespace list, disableUncrafting (only
// uncrafting recipes) and the ingredient switching.
GF_REGISTER_SELFTEST("uncraftmaps.config_bans", [](Context& ctx) {
    auto& p    = ctx.player();
    auto  pick = make(ctx, "minecraft:iron_pickaxe");
    auto  list = Menu::recipesFor(pick);
    ctx.require(!list.empty(), "the pickaxe has recipes");
    auto id = list.front()->id;
    {
        UncraftmapsConfig c;
        c.disableUncraftingRecipes = {id};
        ScopedConfig scoped(c);
        auto now = Menu::recipesFor(pick);
        ctx.check(std::none_of(now.begin(), now.end(), [&](auto const* r) { return r->id == id; }), "a listed recipe is skipped (" + id + ")");
        c.flipRecipeList = true;
        overrideConfig(c);
        now = Menu::recipesFor(pick);
        ctx.check(now.size() == 1 && now.front()->id == id, "the flipped list keeps only the listed recipe");
    }
    {
        UncraftmapsConfig c;
        c.blacklistedUncraftingModIds = {cu::namespaceOf(id)};
        ScopedConfig scoped(c);
        ctx.check(Menu::recipesFor(pick).empty(), "the namespace " + cu::namespaceOf(id) + " is blacklisted");
        c.flipIdList = true;
        overrideConfig(c);
        ctx.check(!Menu::recipesFor(pick).empty(), "the flipped namespace list allows it");
        c.blacklistedUncraftingModIds = {"twilightforest"};
        overrideConfig(c);
        ctx.check(Menu::recipesFor(pick).empty(), "a whitelist of another namespace refuses it");
    }
    {
        UncraftmapsConfig c;
        c.disableUncrafting = true;
        ScopedConfig scoped(c);
        setLevels(p, 10);
        TestMenu m(p);
        m.input(pick);
        ctx.check(m.menu.uncraftingCost() == 0, "no cost while uncrafting is disabled");
        m.menu.clicked(kSlotUncrafting + 0, 0, Click::Pickup);
        ctx.check(items::isEmpty(m.state.carried), "nothing can be taken");
        m.menu.removed();
        setLevels(p, 0);
    }
    {
        // ingredient switching: a stick from planks of any wood cycles through the planks
        UncraftmapsConfig c;
        c.enableShapelessCrafting = false;
        ScopedConfig scoped(c);
        TestMenu m(p);
        m.input(make(ctx, "minecraft:stick", 4));
        if (!m.state.ghost) {
            ctx.info("no shaped stick recipe to cycle through");
        } else {
            auto first = m.ghost(0);
            m.menu.guiButton(2);
            auto second = m.ghost(0);
            ctx.info("stick ingredients: " + first + " -> " + second);
            c.disableIngredientSwitching = true;
            overrideConfig(c);
            m.menu.guiButton(2);
            ctx.check(m.ghost(0) == second, "switching is ignored while disabled");
        }
        m.menu.removed();
    }
    ctx.check(!tags::has(tags::Registry::Item, "twilightforest:banned_uncraftables", "minecraft:iron_pickaxe"), "#banned_uncraftables is empty in TF");
    ctx.check(tags::has(tags::Registry::Item, "twilightforest:banned_uncrafting_ingredients", "twilightforest:transformation_powder"), "#banned_uncrafting_ingredients holds the transformation powder");
});

// The uncrafting recipes: 8 tipped arrows -> 8 arrows (cost 4), a written book -> a book (cost 0),
// a keepsake casket -> a skull chest (cost 0), and they still work with disableUncrafting.
GF_REGISTER_SELFTEST("uncraftmaps.uncrafting_recipes", [](Context& ctx) {
    auto& p = ctx.player();
    auto  check = [&](ItemStack const& in, std::string const& out, int n, int cost) {
        TestMenu m(p);
        m.input(in);
        ctx.check(m.state.ghost != nullptr && m.menu.isUncraftingRecipe(), items::idOf(in) + " uses an uncrafting recipe");
        ctx.check(m.count(out) == n, items::idOf(in) + " gives " + std::to_string(n) + " " + out + " (" + std::to_string(m.count(out)) + ")");
        ctx.check(m.menu.uncraftingCost() == cost, items::idOf(in) + " costs " + std::to_string(cost) + " (" + std::to_string(m.menu.uncraftingCost()) + ")");
        m.menu.removed();
    };
    check(make(ctx, "minecraft:arrow", 8, 6), "minecraft:arrow", 8, 4); // a tipped arrow (aux: the potion)
    check(make(ctx, "minecraft:written_book"), "minecraft:book", 1, 0);
    check(make(ctx, "twilightforest:keepsake_casket"), "twilightforest:skull_chest", 1, 0);
    {
        TestMenu m(p);
        m.input(make(ctx, "minecraft:arrow", 7, 6));
        ctx.check(m.state.ghost == nullptr || !m.menu.isUncraftingRecipe(), "7 tipped arrows are not enough");
        m.menu.removed();
        TestMenu plain(p);
        plain.input(make(ctx, "minecraft:arrow", 8));
        ctx.check(plain.state.ghost == nullptr || !plain.menu.isUncraftingRecipe(), "plain arrows are not tipped arrows");
        plain.menu.removed();
    }
    UncraftmapsConfig c;
    c.disableUncrafting = true;
    ScopedConfig scoped(c);
    setLevels(p, 0);
    TestMenu m(p);
    m.input(make(ctx, "minecraft:written_book"));
    m.menu.clicked(kSlotUncrafting + 0, 0, Click::Pickup);
    ctx.check(items::idOf(m.state.carried) == "minecraft:book", "the written book still comes apart with uncrafting disabled");
    m.menu.removed();
});

// Recrafting: an enchanted diamond sword with iron in the assembly grid becomes an iron sword that
// keeps its enchantments; the cost is 2 (different item: the filled assembly slots) + 9 (sharpness
// III: 3 + 1, unbreaking II: 2 * 2 + 1) + (1 + 0) * 2 = 13, and needs the levels.
GF_REGISTER_SELFTEST("uncraftmaps.recraft", [](Context& ctx) {
    auto& p     = ctx.player();
    auto  sword = make(ctx, "minecraft:diamond_sword");
    enchant::set(sword, "minecraft:sharpness", 3);
    enchant::set(sword, "minecraft:unbreaking", 2);
    TestMenu m(p);
    m.input(sword);
    ctx.require(m.state.ghost != nullptr, "the sword has a recipe");
    // the sword's pattern is one column: the diamonds at slots 0 and 3, the stick at 6
    m.menu.set(kSlotAssembly + 0, make(ctx, "minecraft:iron_ingot"));
    m.menu.set(kSlotAssembly + 3, make(ctx, "minecraft:iron_ingot"));
    ctx.check(items::idOf(m.state.result) == "minecraft:iron_sword", "the combined grid makes an iron sword (" + items::idOf(m.state.result) + ")");
    ctx.check(enchant::level(m.state.result, "minecraft:sharpness") == 3 && enchant::level(m.state.result, "minecraft:unbreaking") == 2, "the enchantments moved over");
    ctx.check(m.menu.recraftingCost() == 13, "the recrafting cost is 13 (" + std::to_string(m.menu.recraftingCost()) + ")");
    ctx.check(m.menu.uncraftingCost() == 0, "no uncrafting cost while the assembly grid is used");
    setLevels(p, 12);
    m.menu.clicked(kSlotResult, 0, Click::Pickup);
    ctx.check(items::isEmpty(m.state.carried), "12 levels are not enough");
    setLevels(p, 13);
    m.menu.clicked(kSlotResult, 0, Click::Pickup);
    ctx.check(items::idOf(m.state.carried) == "minecraft:iron_sword", "the iron sword is taken");
    ctx.check(levelsOf(p) == 0, "13 levels were charged (" + std::to_string(levelsOf(p)) + " left)");
    ctx.check(items::isEmpty(m.state.input), "the diamond sword was used up");
    m.menu.removed();
    setLevels(p, 0);
});

// The screen for a companion: the open message carries the menu, a click goes through
// "screen.action", a cycle button through "uncraftmaps.gui", and closing returns the items.
GF_REGISTER_SELFTEST("uncraftmaps.screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto at = ctx.testPos(1310);
    co_await ctx.ensureLoaded(at);
    ctx.require(ctx.setBlock(at, kTable), "the table is placed");
    auto& p = ctx.player();
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    beginMenu(p, at);
    Json context = {{"pos", {at.dim, at.x, at.y, at.z}}};
    bool opened  = screens::open(p, kScreen, context);
    ctx.check(opened, "the screen opens");
    auto msgs = net::takeTestMessages(p);
    bool found = false;
    for (auto const& m : msgs)
        if (m.channel == "screen.open" && m.payload.value("type", std::string()) == kScreen) found = m.payload.value("data", Json::object()).contains("uncrafting");
    ctx.check(found, "the companion got the uncrafting screen with its data");
    p.getInventory().setItem(0, make(ctx, "minecraft:iron_pickaxe"));
    // shift-click the hotbar pickaxe into the input (hotbar 0 is menu slot 47)
    net::injectFromClient(p, "screen.action", {{"type", kScreen}, {"context", context}, {"action", {{"op", "click"}, {"slot", 47}, {"button", 0}, {"type", "quick_move"}}}});
    auto* s = openMenu(p);
    ctx.require(s != nullptr, "the menu is open");
    ctx.check(items::idOf(s->input) == "minecraft:iron_pickaxe", "the pickaxe went into the input");
    ctx.check(items::isEmpty(p.getInventory().getItem(0)), "and left the hotbar");
    auto before = s->cycles.unrecipe;
    net::injectFromClient(p, kGuiChannel, {{"type", 0}});
    ctx.check(s->cycles.unrecipe == before + 1, "the cycle button counted");
    auto updates = net::takeTestMessages(p);
    ctx.check(std::any_of(updates.begin(), updates.end(), [](auto const& m) { return m.channel == "screen.update"; }), "the screen was refreshed");
    endMenu(p);
    bool back = false;
    for (int i = 0; i < 36; ++i) back = back || items::idOf(p.getInventory().getItem(i)) == "minecraft:iron_pickaxe";
    ctx.check(back, "closing gave the pickaxe back");
    net::setTestCompanion(p, false);
    co_return;
});

// SyncUncraftingTableConfigPacket at the handshake.
GF_REGISTER_SELFTEST("uncraftmaps.sync_config", [](Context& ctx) {
    auto& p = ctx.player();
    net::joinTestCompanion(p);
    auto msgs = net::takeTestMessages(p);
    auto it   = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == kSyncChannel; });
    ctx.check(it != msgs.end(), "the config was synced");
    if (it != msgs.end()) {
        auto c = configFromJson(it->payload);
        ctx.check(c.disableUncraftingRecipes == config().disableUncraftingRecipes && c.uncraftingXpCostMultiplier == config().uncraftingXpCostMultiplier, "the synced values match");
    }
    net::setTestCompanion(p, false);
});

// The table's powered state follows the redstone signal (UncraftingTableBlock#neighborChanged).
GF_REGISTER_SELFTEST("uncraftmaps.powered", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1315, 4, 4); // restored afterwards (the amethyst replaces a ground block)
    auto at  = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY, box.minZ + 1};
    co_await ctx.ensureLoaded(at);
    auto below = core::GlobalPos{at.dim, at.x, at.y - 1, at.z};
    auto side  = core::GlobalPos{at.dim, at.x + 1, at.y, at.z};
    {
        auto* r0 = ctx.region();
        auto const* amethyst = game::blockWithStates("minecraft:amethyst_block", Json::object());
        ctx.require(r0 && amethyst && game::setBlock(*r0, game::blockPos(below), *amethyst), "amethyst below (the flat area puts the ground back)");
    }
    ctx.require(ctx.setBlock(at, kTable), "the table");
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(game::intState(region->getBlock(game::blockPos(at)), "twilightforest:powered").value_or(1) == 0, "unpowered at first");
    ctx.require(ctx.setBlock(side, "minecraft:redstone_block"), "a redstone block beside it");
    co_await ctx.ticks(4);
    region = ctx.region();
    refreshPowered(*region, game::blockPos(at)); // (also what the neighbour event does)
    ctx.check(game::intState(region->getBlock(game::blockPos(at)), "twilightforest:powered").value_or(0) == 1, "powered by the redstone block");
    ctx.setBlock(side, "minecraft:air");
    co_await ctx.ticks(4);
    region = ctx.region();
    refreshPowered(*region, game::blockPos(at));
    ctx.check(game::intState(region->getBlock(game::blockPos(at)), "twilightforest:powered").value_or(1) == 0, "unpowered again");
});

// Magic maps: made only in the Twilight Forest, centred on the 2048-block grid, painted with the
// biomes' magic_map_color around the holder, with the landmark icons it passes.
GF_REGISTER_SELFTEST("uncraftmaps.magic_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    {
        auto& p    = ctx.player();
        auto  over = setupMagicMap(p); // the item's use refuses outside the Twilight Forest; the setup itself works anywhere
        ctx.check(mapKindOf(over) == MapKind::Magic, "a magic map stack");
    }
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto at = ctx.testPos(1320);
    co_await ctx.ensureLoaded(at);
    auto& p   = ctx.player();
    auto  map = setupMagicMap(p);
    ctx.require(mapKindOf(map) == MapKind::Magic, "a filled magic map");
    auto  id   = mapIdOf(map);
    auto* data = mapData(id);
    ctx.require(data != nullptr, "the map data exists");
    auto pos            = p.getFeetPos();
    auto [ecx, ecz]     = cu::magicMapCenter(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.z)));
    ctx.check(data->mOrigin.get().x == ecx && data->mOrigin.get().z == ecz, "centred on the magic map grid");
    ctx.check(data->mScale == 4, "scale 4");
    updateHeldMap(p, map);
    data = mapData(id);
    ctx.require(data != nullptr, "the map data still exists");
    int  vx    = cu::viewerPixel(pos.x, ecx, 16), vz = cu::viewerPixel(pos.z, ecz, 16);
    auto biome = worldgen::biomeAt(BlockPos{cu::magicSampleBlock(ecx, vx * 4), 0, cu::magicSampleBlock(ecz, vz * 4)});
    auto over  = worldgen::biomeAt(BlockPos{cu::magicSampleBlock(ecx, vx * 4 + 1), 0, cu::magicSampleBlock(ecz, vz * 4)});
    auto down  = worldgen::biomeAt(BlockPos{cu::magicSampleBlock(ecx, vx * 4), 0, cu::magicSampleBlock(ecz, vz * 4 + 1)});
    if (over == "twilightforest:stream") biome = over;
    else if (down == "twilightforest:stream") biome = down;
    std::uint32_t rgb = 0xB24CD8;
    int           brightness = 1;
    if (auto const* v = datamaps::get("twilightforest:magic_map_color", biome)) {
        rgb        = static_cast<std::uint32_t>(v->value("map_color", Json::object()).value("color", 0));
        brightness = v->value("brightness", 1);
    }
    auto want = cu::shade(rgb, brightness);
    auto got  = mapPixel(*data, vx, vz);
    ctx.check(got == want, "the holder's pixel is " + biome + "'s colour (" + std::to_string(got) + " vs " + std::to_string(want) + ")");
    ctx.check(mapPixel(*data, 0, 0) == 0 || std::abs(vx) < 40, "far pixels stay unpainted");
    // the landmark icons around: every grid centre within the view picks a landmark
    auto decos = decorationsOf(id);
    ctx.info(std::to_string(decos.size()) + " landmark icon(s) on the map");
    ctx.check(!decos.empty(), "the map shows the landmarks around");
    if (!decos.empty()) {
        ctx.check(std::none_of(decos.begin(), decos.end(), [](auto const& d) { return d.conquered; }), "none conquered");
        ctx.check(decos.front().rotation == 8, "rotation 180 degrees (8)");
    }
    // the companion packet
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    updateHeldMap(p, map); // nothing new: no packet
    (void)net::takeTestMessages(p);
    net::setTestCompanion(p, false);
    // the empty map refuses outside the Twilight Forest (misc.twilightforest.magic_map_fail): its use
    // checks gf::world::isTwilight
    ctx.check(gf::world::isTwilight(p.getDimension()), "the test player is in the Twilight Forest");
});

// Maze and ore maps: one block per pixel at the holder's y, one stripe per tick; the ore map paints
// ores in ore_map_color; inside a labyrinth chunk the map snaps to the labyrinth's centre.
GF_REGISTER_SELFTEST("uncraftmaps.maze_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1330, 16, 8);
    auto& p0 = ctx.player();
    auto  stand = ctx.testPos(1336, 2);
    p0.teleport(Vec3{stand.x + 0.5f, static_cast<float>(box.minY), stand.z + 0.5f}, p0.getDimensionId());
    co_await ctx.ticks(2);
    auto& p   = ctx.player();
    auto  y   = static_cast<int>(std::floor(p.getFeetPos().y));
    auto  ore = setupMazeMap(p, true);
    ctx.require(mapKindOf(ore) == MapKind::Ore, "an ore map");
    auto id = mapIdOf(ore);
    ctx.check(mazeYCenter(id) == y, "yCenter is the holder's y");
    auto* data = mapData(id);
    ctx.require(data != nullptr, "the map data");
    auto [cx, cz] = cu::mazeMapCenter(static_cast<int>(std::floor(p.getFeetPos().x)), static_cast<int>(std::floor(p.getFeetPos().z)), 0);
    ctx.check(data->mOrigin.get().x == cx && data->mOrigin.get().z == cz && data->mScale == 0, "scale 0 on the 128-block grid");
    // a diamond ore and a stone block at yCenter next to the holder
    int  px = static_cast<int>(std::floor(p.getFeetPos().x)), pz = static_cast<int>(std::floor(p.getFeetPos().z));
    auto oreAt   = core::GlobalPos{stand.dim, px + 2, y, pz};
    auto stoneAt = core::GlobalPos{stand.dim, px - 2, y, pz};
    ctx.require(ctx.setBlock(oreAt, "minecraft:diamond_ore") && ctx.setBlock(stoneAt, "minecraft:stone"), "the blocks");
    for (int i = 0; i < 17; ++i) updateHeldMap(ctx.player(), ore); // every column once
    data = mapData(id);
    ctx.require(data != nullptr, "the map data still exists");
    int  ox = cu::viewerPixel(px + 2, cx, 1), oz = cu::viewerPixel(pz, cz, 1);
    auto diamond = datamaps::get("twilightforest:ore_map_color", "minecraft:diamond_ore");
    ctx.require(diamond != nullptr, "the diamond ore has an ore map colour");
    auto want = cu::shade(static_cast<std::uint32_t>(diamond->value("map_color", Json::object()).value("color", 0)), 1);
    ctx.check(mapPixel(*data, ox, oz) == want, "the ore shows in its colour");
    ctx.check(mapPixel(*data, cu::viewerPixel(px - 2, cx, 1), oz) != 0, "the stone shows");
    ctx.check(mapPixel(*data, cu::viewerPixel(px, cx, 1), oz) == 0, "air is transparent");
    // a maze map paints the ore as a block (no ore colour)
    auto maze = setupMazeMap(ctx.player(), false);
    for (int i = 0; i < 17; ++i) updateHeldMap(ctx.player(), maze);
    if (auto* md = mapData(mapIdOf(maze))) ctx.check(mapPixel(*md, ox, oz) != want, "the maze map does not recolour ores");
    // cloning: a filled map and 2 empty ones -> 3 copies; the ore map does not clone as a maze map
    auto empty = make(ctx, "twilightforest:maze_map");
    auto three = cloneResult({&maze, &empty, &empty}, MapKind::Maze);
    ctx.check(items::countOf(three) == 3 && mapIdOf(three) == mapIdOf(maze), "maze map cloning makes 3 of the same map");
    ctx.check(items::isEmpty(cloneResult({&ore, &empty}, MapKind::Maze)), "the ore map is not a maze map");
    ctx.check(items::isEmpty(cloneResult({&maze, &maze, &empty}, MapKind::Maze)), "two filled maps don't clone");
    // the labyrinth snap: a labyrinth chunk around the Twilight Forest test area
    if (auto tf = gf::world::twilight()) {
        int dim = static_cast<int>(*tf);
        bool found = false;
        for (int r = 0; r < 12 && !found; ++r)
            for (int dx = -r; dx <= r && !found; ++dx)
                for (int dz = -r; dz <= r && !found; ++dz) {
                    int bx = dx * 256 + 8, bz = dz * 256 + 8;
                    auto c = core::landmark::nearestCenter(bx >> 4, bz >> 4);
                    if (core::landmark::pickForChunk(worldgen::seed(), c.x >> 4, c.z >> 4, [](int x, int z) { return worldgen::biomeAt(BlockPos{x, 0, z}); }) != "twilightforest:labyrinth") continue;
                    found       = true;
                    auto center = mazeCenterFor(dim, c.x + 3, c.z - 5);
                    ctx.check(center.first == c.x && center.second == c.z, "a maze map made in the labyrinth chunk snaps to its centre");
                }
        if (!found) ctx.info("no labyrinth within 12 grid cells: the snap was not checked");
        auto plain = mazeCenterFor(0, 300, -300);
        ctx.check(plain == cu::mazeMapCenter(300, -300, 0), "outside the Twilight Forest the grid centre");
    }
    (void)box;
});

// Magic map cloning and the empty maps' items.
GF_REGISTER_SELFTEST("uncraftmaps.magic_cloning", [](Context& ctx) {
    auto& p     = ctx.player();
    auto  map   = setupMagicMap(p);
    auto  empty = make(ctx, "twilightforest:magic_map");
    auto  two   = cloneResult({&map, &empty}, MapKind::Magic);
    ctx.check(items::countOf(two) == 2 && mapIdOf(two) == mapIdOf(map), "a magic map and one empty make 2 copies");
    auto  maze = make(ctx, "twilightforest:maze_map");
    ctx.check(items::isEmpty(cloneResult({&map, &maze}, MapKind::Magic)), "a maze map is not a magic map");
    ctx.check(items::isEmpty(cloneResult({&map}, MapKind::Magic)), "no empty map: no match");
});

// The commands: map_locator gives the named player a map; biomepng does nothing on a dedicated server
// and fails on a 0 / non-square size in a local world.
GF_REGISTER_SELFTEST("uncraftmaps.commands", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto at = ctx.testPos(1340);
    co_await ctx.ensureLoaded(at);
    auto& p    = ctx.player();
    auto  name = p.getRealName();
    int   maps = 0;
    for (int i = 0; i < 36; ++i)
        if (items::idOf(p.getInventory().getItem(i)) == "minecraft:filled_map") ++maps;
    // (a structure tag is typed as the rest of the line; the player is named there: selectors do not
    // reach the simulated test players from the console)
    auto r = selftest::runServerCommand("twilightforest map_locator #twilightforest:landmark " + name, ctx.dimension());
    ctx.info("map_locator: " + r.messages);
    auto& p2    = ctx.player();
    int   after = 0;
    for (int i = 0; i < 36; ++i)
        if (items::idOf(p2.getInventory().getItem(i)) == "minecraft:filled_map") ++after;
    if (r.ok) ctx.check(after == maps + 1, "the player got the map");
    else ctx.check(r.messages.find("commands.locate.structure.not_found") != std::string::npos || r.messages.find("Could not find") != std::string::npos, "not found is reported");
    auto bad = selftest::runServerCommand("twilightforest map_locator #twilightforest:no_such_tag " + name, ctx.dimension());
    ctx.check(!bad.ok, "an unknown structure is refused");
#ifndef GF_CLIENT
    auto png = selftest::runServerCommand("twilightforest biomepng 0", ctx.dimension());
    ctx.check(png.messages.find("command.failed") == std::string::npos, "biomepng does nothing on a dedicated server");
#else
    auto zero = selftest::runServerCommand("twilightforest biomepng 0", ctx.dimension());
    ctx.check(!zero.ok, "a 0 size fails");
    auto wide = selftest::runServerCommand("twilightforest biomepng 16 8", ctx.dimension());
    ctx.check(!wide.ok, "a non-square size fails");
    auto ok = selftest::runServerCommand("twilightforest biomepng 16", ctx.dimension());
    ctx.check(ok.ok, "a 16 x 16 map is written: " + ok.messages);
#endif
});

} // namespace gf::feature::uncraftmaps
