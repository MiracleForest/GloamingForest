// Unit U4 (woodwork) selftests, part 3 (woodwork.*, dx 400..499, rule R12): the drying rack's loot
// fill and particle rate, a chest boat's contents next to its own drop, the dried slime drops as
// food, tannin's craft remainder, a real furnace burning TF planks, and the enchanting power of 15
// canopy bookshelves.

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/server/ServerPlayer.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/containers/managers/models/EnchantingContainerManagerModel.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/effects/Effects.h"
#include "feature/ext/woodwork/BoatDrops.h"
#include "feature/ext/woodwork/ParticleTo.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/woodwork/DryingRack.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace ww    = gf::feature::woodwork;
namespace rack  = gf::feature::woodwork::rack;
namespace items = gf::game::items;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return p.offset(dx, dy, dz); }
SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

std::vector<ItemActor*> itemActorsNear(Context& ctx, GlobalPos const& p, float r = 6.f) {
    std::vector<ItemActor*> out;
    auto*                   region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - r, p.y - 3.f, p.z - r}, Vec3{p.x + r + 1.f, p.y + 4.f, p.z + r + 1.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") out.push_back(static_cast<ItemActor*>(a));
    return out;
}

/// Items of each id lying near `p` (counts summed).
std::map<std::string, int> dropsNear(Context& ctx, GlobalPos const& p) {
    std::map<std::string, int> out;
    for (auto* a : itemActorsNear(ctx, p)) out[items::idOf(a->item())] += a->item().mCount;
    return out;
}

void clearItems(Context& ctx, GlobalPos const& p) {
    for (auto* a : itemActorsNear(ctx, p)) a->remove();
}

int carried(Player& player, std::string const& id) {
    int   n   = 0;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (items::idOf(inv.getItem(i)) == id) n += inv.getItem(i).mCount;
    return n;
}

std::vector<Actor*> actorsNear(Context& ctx, GlobalPos const& p, char const* type) {
    std::vector<Actor*> out;
    auto*               region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - 4.f, p.y - 3.f, p.z - 4.f}, Vec3{p.x + 5.f, p.y + 4.f, p.z + 5.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == type) out.push_back(a);
    return out;
}

AttributeInstance* attribute(Player& p, Attribute const& a) {
    auto ref = p.getAttribute(a);
    return const_cast<AttributeInstance*>(static_cast<AttributeInstance const*>(ref.mPtr));
}

} // namespace

// ---- the drying rack's loot fill ------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.rack_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    // DryingRackBlockEntity#fillFromLootTable through loot::placeContainer (the structure kit's loot
    // markers): the table is rolled at once and the rack holds the LAST rolled stack, whole
    (void)co_await ctx.flatArea(405, 5);
    auto p = ctx.testPos(405, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto*    region = ctx.region();
    BlockPos bp     = gf::game::blockPos(p);
    constexpr char kRack[]    = "twilightforest:spruce_drying_rack";
    constexpr char kFixture[] = "gf_test:chests/fixture_chest";
    constexpr std::int64_t kSeed = 4242;
    gf::loot::LootContext lc;
    lc.region     = region;
    lc.origin     = Vec3{bp.x + 0.5f, bp.y + 0.5f, bp.z + 0.5f};
    lc.seed       = kSeed;
    auto expected = gf::loot::roll(kFixture, lc);
    ctx.require(!expected.empty(), "the fixture table rolls items");
    ctx.require(gf::loot::placeContainer(*region, bp, kRack, 2, kFixture, kSeed), "placeContainer puts a drying rack with the fixture table");
    auto r = rack::at(p);
    ctx.require(r.has_value() && !items::isEmpty(r->item), "the rack holds a rolled item at once");
    std::set<std::string> allowed{"gf_test:fixture_item", "minecraft:stick", "minecraft:diamond"};
    ctx.check(allowed.contains(items::idOf(r->item)), "the item comes from the table (" + items::idOf(r->item) + ")");
    ctx.check(items::idOf(r->item) == items::idOf(expected.back()) && r->item.mCount == expected.back().mCount,
              "the rack keeps the last rolled stack, whole (" + items::idOf(r->item) + " x" + std::to_string(r->item.mCount) + ", the roll's last " +
                  items::idOf(expected.back()) + " x" + std::to_string(expected.back().mCount) + ")");
    ctx.check(r->totalDryTime == 0 && r->dryTime == 0, "the timers keep their values (no setTheItem)");
    // a click takes it, a break drops it
    auto h = gf::inventory::find(*region, bp);
    ctx.require(h.has_value(), "the rack is an inventory");
    ctx.check(items::idOf(h->get(p, 0)) == items::idOf(r->item), "automation sees the loot item");
    gf::game::clearBlock(*region, bp);
    co_await ctx.ticks(3);
    ctx.check(dropsNear(ctx, p)[items::idOf(r->item)] >= 1, "a broken loot rack drops its item");
    clearItems(ctx, p);
    // TF's camp racks (U19's table): the empty entry leaves the rack empty, any item stays
    int filled = 0, empty = 0;
    for (std::int64_t seed = 1; seed <= 24; ++seed) {
        ctx.require(gf::loot::placeContainer(*region, bp, kRack, 2, "twilightforest:camp_drying_rack", seed), "a camp drying rack");
        auto c = rack::at(p);
        if (c && !items::isEmpty(c->item)) ++filled;
        else ++empty;
        if (auto hh = gf::inventory::find(*region, bp)) hh->set(p, 0, ItemStack{});
    }
    ctx.info("camp drying racks: " + std::to_string(filled) + " filled, " + std::to_string(empty) + " empty of 24");
    ctx.check(filled > 0, "camp drying racks get their jerky or leather");
    ctx.cleanupBlock(p);
});

// ---- the drying particle's rate ------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.rack_particle_rate", [](Context& ctx) -> ll::coro::CoroTask<> {
    // ClientLevel#animateTick's sampling: 667 * (P16^3 + P32^3) animateTick calls a tick, then 1/5
    namespace pt = gf::woodwork_ext::particles;
    double here = pt::animateTickChance(0, 0, 0);
    ctx.check(std::abs(here - 667.0 * (1.0 / 4096.0 + 1.0 / 32768.0)) < 1e-9, "next to the player: " + std::to_string(here) + " calls a tick");
    ctx.check(pt::animateTickChance(20, 0, 0) < 0.01, "20 blocks away: rarely (" + std::to_string(pt::animateTickChance(20, 0, 0)) + ")");
    ctx.check(pt::animateTickChance(32, 0, 0) == 0.0 && pt::animateTickChance(0, 40, 0) == 0.0, "32 or more blocks away: never");
    ctx.check(pt::animateTickChance(16, 0, 0) < pt::animateTickChance(15, 0, 0), "the chance falls off with the distance");
    // a drying rack next to the test player for 200 ticks: about 200 * 0.16 / 5 = 6.5 particles
    // (TF: about 0.7 a second); the old every-tick 1/5 roll gave 40
    (void)co_await ctx.flatArea(475, 5);
    auto p = ctx.testPos(475, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, "twilightforest:oak_drying_rack", {{"twilightforest:facing", "north"}}), "a drying rack");
    auto h = gf::inventory::find(*ctx.region(), gf::game::blockPos(p));
    ctx.require(h.has_value(), "the rack is an inventory");
    h->set(p, 0, *items::make("minecraft:kelp"));
    auto& sp = simPlayer(ctx);
    sp.teleport(Vec3{p.x + 0.5f, float(p.y), p.z + 2.5f}, sp.getDimensionId());
    co_await ctx.ticks(2);
    auto before = pt::sentCount();
    co_await ctx.ticks(200);
    auto sent = pt::sentCount() - before;
    ctx.info("drying particles in 200 ticks next to the rack: " + std::to_string(sent));
    ctx.check(sent <= 24, "TF's sampled rate, not a particle every 5 ticks (" + std::to_string(sent) + " in 200 ticks)");
    h->set(p, 0, ItemStack{});
    ctx.cleanupBlock(p);
});

// ---- a chest boat's contents next to its own drop ------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.boat_chest_contents", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Boat#getDropItem replaces only the boat's own item: vanilla boats in its chest stay
    (void)co_await ctx.flatArea(455, 5);
    auto c = ctx.testPos(455, 2);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    clearItems(ctx, c);
    auto r = gf::selftest::runServerCommand("summon twilightforest:chest_boat " + std::to_string(c.x) + " " + std::to_string(c.y) + " " + std::to_string(c.z) + " 0 0 twilightforest:dark", ctx.dimension());
    ctx.require(r.ok, "a dark chest boat: " + r.messages);
    co_await ctx.ticks(2);
    auto boats = actorsNear(ctx, c, "twilightforest:chest_boat");
    ctx.require(boats.size() == 1, "one chest boat (" + std::to_string(boats.size()) + ")");
    std::string own = gf::woodwork_ext::boatdrops::vanillaDropOf(*boats[0]);
    ctx.info("the game's own drop of the TF chest boat: " + own);
    ctx.require(!own.empty(), "the boat's wood is known");
    // the chest: a vanilla oak boat, and the very item the game drops for this boat
    std::string sel = "@e[type=twilightforest:chest_boat,x=" + std::to_string(c.x) + ",y=" + std::to_string(c.y) + ",z=" + std::to_string(c.z) + ",r=4,c=1]";
    auto put = [&](int slot, std::string const& item) {
        auto res = gf::selftest::runServerCommand("replaceitem entity " + sel + " slot.container " + std::to_string(slot) + " " + item + " 1", ctx.dimension());
        if (!res.ok || res.successCount == 0) res = gf::selftest::runServerCommand("replaceitem entity " + sel + " slot.inventory " + std::to_string(slot) + " " + item + " 1", ctx.dimension());
        return res;
    };
    r = put(0, "minecraft:oak_boat");
    ctx.require(r.ok && r.successCount > 0, "an oak boat in the chest: " + r.messages);
    r = put(1, own);
    ctx.require(r.ok && r.successCount > 0, "a " + own + " in the chest: " + r.messages);
    // the player breaks it from 3 blocks and steps away before anything is picked up
    auto& sp = simPlayer(ctx);
    sp.teleport(Vec3{c.x + 0.5f, float(c.y), c.z + 3.5f}, sp.getDimensionId());
    co_await ctx.ticks(4);
    for (int i = 0; i < 10 && !actorsNear(ctx, c, "twilightforest:chest_boat").empty(); ++i) {
        auto bs = actorsNear(ctx, c, "twilightforest:chest_boat");
        ctx.player().mGameMode->attack(*bs[0], bs[0]->getPosition());
        co_await ctx.ticks(3);
    }
    ctx.require(actorsNear(ctx, c, "twilightforest:chest_boat").empty(), "the chest boat broke");
    simPlayer(ctx).teleport(Vec3{c.x + 0.5f, float(c.y), c.z + 40.5f}, ctx.player().getDimensionId());
    co_await ctx.ticks(2);
    auto drops = dropsNear(ctx, c);
    auto& pl   = ctx.player();
    int   tf   = drops["twilightforest:dark_chest_boat"] + carried(pl, "twilightforest:dark_chest_boat");
    int   oak  = drops["minecraft:oak_boat"] + carried(pl, "minecraft:oak_boat");
    int   same = drops[own] + carried(pl, own);
    std::string seen;
    for (auto const& [id, n] : drops) seen += id + " x" + std::to_string(n) + " ";
    ctx.info("drops: " + seen);
    ctx.check(tf == 1, "exactly one TF dark chest boat item (" + std::to_string(tf) + ")");
    ctx.check(oak == 1, "the oak boat from the chest stays (" + std::to_string(oak) + ")");
    ctx.check(same == 1, "the " + own + " from the chest stays and the boat's own one is gone (" + std::to_string(same) + ")");
    clearItems(ctx, c);
});

// ---- the dried slime drops are food --------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.slime_food", [](Context& ctx) {
    // TFItems: gelatinous_slime_drop (nutrition 1, saturation 0.2, Speed 600 ticks),
    // gelatinous_maze_slime_drop (nutrition 2, saturation 0.3, Resistance 600 ticks)
    struct Food {
        char const* item;
        int         nutrition;
        char const* effect;
        unsigned    effectId; ///< the game's effect id (speed 1, resistance 11)
    };
    auto& player = ctx.player();
    for (auto const& f : {Food{"twilightforest:gelatinous_slime_drop", 1, "minecraft:speed", 1}, Food{"twilightforest:gelatinous_maze_slime_drop", 2, "minecraft:resistance", 11}}) {
        auto stack = items::make(f.item);
        ctx.require(stack.has_value(), std::string(f.item) + " exists");
        Item const* item = stack->mItem.get();
        ctx.require(item != nullptr, "its Item");
        ctx.check(item->isFood(), std::string(f.item) + " is food");
        ctx.check(item->getMaxUseDuration(&*stack) == 32, std::string(f.item) + " takes 32 ticks to eat (" + std::to_string(item->getMaxUseDuration(&*stack)) + ")");
        gf::effects::remove(player, f.effect);
        auto* hunger = attribute(player, Player::HUNGER());
        ctx.require(hunger != nullptr, "the player's hunger");
        hunger->mCurrentValue = 10.0f;
        ItemStack copy = *stack;
        item->useTimeDepleted(copy, &player.getLevel(), &player);
        float after = attribute(player, Player::HUNGER())->mCurrentValue;
        ctx.check(std::abs(after - (10.0f + f.nutrition)) < 0.01f, std::string(f.item) + " gives " + std::to_string(f.nutrition) + " hunger (" + std::to_string(after) + ")");
        auto const* inst = player.getEffect(f.effectId);
        ctx.check(inst != nullptr && inst->mDuration->mValue >= 590 && inst->mDuration->mValue <= 600 && inst->mAmplifier == 0,
                  std::string(f.item) + " gives " + f.effect + " I for 600 ticks (" + (inst ? std::to_string(inst->mDuration->mValue) : std::string("none")) + ")");
        gf::effects::remove(player, f.effect);
    }
    if (auto* hunger = attribute(player, Player::HUNGER())) hunger->mCurrentValue = 20.0f;
});

// ---- tannin's craft remainder ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.tannin_remainder", [](Context& ctx) {
    // TFItems.TANNIN: craftRemainder(Items.GLASS_BOTTLE): crafting treated leather gives the bottle back
    namespace rc = gf::recipes;
    auto& player = ctx.player();
    auto& ui     = player.mPlayerUIContainer.get();
    int   first  = static_cast<int>(PlayerUISlot::Crafting2x2Input1);
    for (int i = 0; i < 4; ++i) ui.setItem(first + i, ItemStack::EMPTY_ITEM());
    ui.setItem(first, *items::make("twilightforest:tannin"));
    ui.setItem(first + 3, *items::make("minecraft:leather"));
    auto grid = rc::internal::gridOf(player, false);
    ctx.check(rc::internal::isSpecial("twilightforest:treated_leather"), "treated_leather has a crafting remainder");
    auto candidates = rc::internal::specialCandidates("twilightforest:treated_leather", grid.grid);
    ctx.check(std::find(candidates.begin(), candidates.end(), "twilightforest:treated_leather") != candidates.end(), "the crafting hook finds the BP recipe for the grid");
    auto out = rc::internal::assembleSpecial("twilightforest:treated_leather", grid.grid);
    ctx.check(out && items::idOf(*out) == "twilightforest:treated_leather" && items::countOf(*out) == 1, "the craft makes one treated leather");
    auto bySlot = rc::internal::remaindersBySlot("twilightforest:treated_leather", grid.grid);
    ctx.check(bySlot.size() == 4 && items::idOf(bySlot[0]) == "minecraft:glass_bottle" && items::isEmpty(bySlot[3]), "a glass bottle goes back into the tannin's slot, nothing into the leather's");
    // ResultSlot#onTake after the craft emptied the grid
    for (int i = 0; i < 4; ++i) ui.setItem(first + i, ItemStack::EMPTY_ITEM());
    rc::internal::placeRemainders(player, grid.firstSlot, bySlot);
    ctx.check(items::idOf(ui.getItem(first)) == "minecraft:glass_bottle", "the bottle is in the grid");
    // another grid (two tannins) is no treated_leather craft
    ui.setItem(first, *items::make("twilightforest:tannin"));
    ui.setItem(first + 1, *items::make("twilightforest:tannin"));
    ui.setItem(first + 3, *items::make("minecraft:leather"));
    ctx.check(!rc::internal::assembleSpecial("twilightforest:treated_leather", rc::internal::gridOf(player, false).grid), "the matcher refuses other grids");
    for (int i = 0; i < 4; ++i) ui.setItem(first + i, ItemStack::EMPTY_ITEM());
});

// ---- a real furnace and a real enchanting table --------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.furnace_and_enchanting", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(465, 5);
    auto c = ctx.testPos(465, 2);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    auto* region = ctx.region();
    // a furnace with cobblestone and one canopy plank in its fuel slot burns it for 300 ticks
    auto f = at(c, -2, 0, 0);
    ctx.require(ctx.setBlock(f, "minecraft:furnace"), "a furnace");
    auto* actor = region->getBlockEntity(gf::game::blockPos(f));
    ctx.require(actor != nullptr, "the furnace's block actor");
    auto& furnace = static_cast<FurnaceBlockActor&>(*actor);
    furnace.setItem(0, *items::make("minecraft:cobblestone", 8));
    furnace.setItem(1, *items::make("twilightforest:canopy_planks", 1));
    int litDuration = 0;
    for (int i = 0; i < 20 && litDuration == 0; ++i) {
        co_await ctx.ticks(1);
        if (auto* a = ctx.region()->getBlockEntity(gf::game::blockPos(f))) litDuration = static_cast<FurnaceBlockActor&>(*a).mLitDuration;
    }
    auto* a2 = ctx.region()->getBlockEntity(gf::game::blockPos(f));
    ctx.check(litDuration >= 298 && litDuration <= 300, "a canopy plank in a real furnace burns 300 ticks (" + std::to_string(litDuration) + ")");
    ctx.check(a2 && items::isEmpty(static_cast<FurnaceBlockActor&>(*a2).getItem(1)), "the plank was taken from the fuel slot");
    if (a2) {
        auto& fa = static_cast<FurnaceBlockActor&>(*a2);
        for (int s = 0; s < 3; ++s) fa.setItem(s, ItemStack::EMPTY_ITEM());
    }
    ctx.cleanupBlock(f);
    // 15 canopy bookshelves around a table: the third option costs 30 levels, as with vanilla ones
    auto t = at(c, 1, 0, 0);
    ctx.require(ctx.setBlock(t, "minecraft:enchanting_table"), "an enchanting table");
    std::vector<GlobalPos> shelves;
    for (int dx = -2; dx <= 2 && shelves.size() < 15; ++dx)
        for (int dz = -2; dz <= 2 && shelves.size() < 15; ++dz)
            if (std::abs(dx) == 2 || std::abs(dz) == 2) shelves.push_back(at(t, dx, 0, dz));
    for (int dx = -2; dx <= 2 && shelves.size() < 15; ++dx) shelves.push_back(at(t, dx, 1, -2));
    for (auto const& s : shelves) ctx.require(ctx.setBlock(s, "twilightforest:canopy_bookshelf"), "a canopy bookshelf");
    co_await ctx.ticks(2);
    // the player opens the table (the game's own EnchantingContainerManagerModel), puts a sword and
    // lapis in, and the model recalculates its three options as the screen does
    auto& sp = simPlayer(ctx);
    sp.teleport(Vec3{t.x + 0.5f, float(t.y), t.z + 1.5f}, sp.getDimensionId());
    sp.simulateLookAt(gf::game::blockPos(t), sim::LookDuration::Instant);
    sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    co_await ctx.ticks(4);
    int best = 0, positions = 0;
    {
        BlockPos tb = gf::game::blockPos(t);
        positions   = static_cast<int>(EnchantUtils::getBookCasePositions(*ctx.region(), Vec3{float(tb.x), float(tb.y), float(tb.z)}).size());
        auto& player = simPlayer(ctx);
        if (!player.simulateInteract(tb, ScriptModuleMinecraft::ScriptFacing::Up)) {
            auto& held = const_cast<ItemStack&>(player.getSelectedItem());
            player.mGameMode->useItemOn(held, tb, static_cast<uchar>(1), Vec3{tb.x + 0.5f, tb.y + 0.75f, tb.z + 0.5f}, HandSlot::Mainhand, nullptr, true);
        }
    }
    co_await ctx.ticks(2);
    {
        auto& player = ctx.player();
        auto* mgr    = player.mContainerManager.get();
        if (!mgr) {
            // Bedrock opens the crafting-block screens client side: the server's model as
            // ServerPlayer's own open makes it (construct, postInit, set as the player's manager)
            auto made = std::make_shared<EnchantingContainerManagerModel>(ContainerID::First, player, gf::game::blockPos(t));
            made->postInit();
            static_cast<ServerPlayer&>(player)._setContainerManagerModel(made);
            mgr = player.mContainerManager.get();
        }
        ctx.info(std::string("container after the click: ") + (mgr ? std::to_string(static_cast<int>(mgr->getContainerType())) : std::string("none")));
        ctx.require(mgr != nullptr && mgr->getContainerType() == SharedTypes::Legacy::ContainerType::Enchantment, "the player opened the enchanting table");
        auto& model = static_cast<EnchantingContainerManagerModel&>(*mgr);
        auto& ui    = player.mPlayerUIContainer.get();
        ui.setItem(static_cast<int>(PlayerUISlot::EnchantingInput), *items::make("minecraft:iron_sword"));
        ui.setItem(static_cast<int>(PlayerUISlot::EnchantingMaterial), *items::make("minecraft:lapis_lazuli", 3));
        model.recalculateOptions();
        std::string costs;
        for (auto const& o : model.mEnchantOptions.get()) {
            best = std::max<int>(best, o.mCost);
            costs += std::to_string(static_cast<int>(o.mCost)) + " ";
        }
        ctx.info("enchanting options with 15 canopy bookshelves: " + costs + "(" + std::to_string(positions) + " bookcase positions)");
        ui.setItem(static_cast<int>(PlayerUISlot::EnchantingInput), ItemStack::EMPTY_ITEM());
        ui.setItem(static_cast<int>(PlayerUISlot::EnchantingMaterial), ItemStack::EMPTY_ITEM());
        static_cast<ServerPlayer&>(player).doDeleteContainerManager(false);
    }    ctx.check(positions >= 15, "the table counts 15 bookshelves (" + std::to_string(positions) + ")");
    ctx.check(best == 30, "the third option costs 30 levels (" + std::to_string(best) + ")");
    for (auto const& s : shelves) ctx.cleanupBlock(s);
    ctx.cleanupBlock(t);
});
