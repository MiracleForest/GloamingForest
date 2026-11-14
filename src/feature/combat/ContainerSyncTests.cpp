// Selftests of F10's container change notices and native container migration (prefix "combat.",
// testPos dx 6070..6077):
//   - combat.inventory_screen_refresh: every client mod viewer of a container gets fresh data at
//     the end of the tick it changed (a handler's write, a native container's setItem as a hopper
//     makes it, ext::markChanged), in the context its screen was opened with; nothing while nothing
//     changes; a player's own click is answered once (feature/ext/combat/InventoryChanges.h);
//   - combat.gf_chest_screen_refresh: upstream's double chest opened by code (no click): a change of either
//     half refreshes the screen in its own context (the half it was opened through), whichever
//     backend the chests use (BlockData on main before F11's native chests, native after);
//   - combat.inventory_native_migrate: a native container whose stacks still wait in block data
//     shows them where a move puts them (reads, the comparator output), the first write moves them,
//     and ext::migrateBlockDataToNative moves them on demand (feature/ext/combat/InventoryMigrate.h).
// The native parts use the combat scenario's probe block gf_combat:native_chest
// (tools/e2e/server/scenarios/combat.prelaunch.ps1); without it they say so and pass. Every test
// empties what it filled before its blocks go (nothing drops in the test row).

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/combat/CombatTestUtil.h"
#include "feature/common/BlockData.h"
#include "feature/ext/combat/InventoryChanges.h"
#include "feature/ext/combat/InventoryMigrate.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
using gf::core::GlobalPos;
namespace items = gf::game::items;
namespace inv   = gf::inventory;
using gf::combat::testutil::clearInventory;
using gf::combat::testutil::countInInventory;
using gf::combat::testutil::takeItemEntitiesNear;

constexpr char kFixture[]     = "gf_test:fixture_block";
constexpr char kNativeChest[] = "gf_combat:native_chest"; // the combat scenario's probe (minecraft:block_entity)
constexpr char kProbeType[]   = "gf_combat:type";
constexpr char kProbeFacing[] = "gf_combat:facing";
constexpr char kOakChest[]    = "gloamingforest:gloaming_oak_chest";
constexpr char kType[]        = "gloamingforest:type";
constexpr char kFacing[]      = "gloamingforest:facing";

Player* playerById(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->getPlayer(id);
}

/// A container declaration for one test (gone when the test ends, also on a failed `require`).
struct DeclaredScope {
    explicit DeclaredScope(inv::Declared const& d) : block(d.block) { inv::declare(d); }
    ~DeclaredScope() { inv::unregisterBlock(block); }
    DeclaredScope(DeclaredScope const&)            = delete;
    DeclaredScope& operator=(DeclaredScope const&) = delete;
    std::string block;
};

/// Empties the container at `pos` without dropping anything: its native container's slots, its
/// block data (slots, a pending table).
void emptyContainer(GlobalPos const& pos) {
    if (inv::proxyOpen(pos)) inv::proxyEnd(pos);
    if (auto* region = gf::game::regionOf(pos.dim); region && region->hasChunksAt(gf::game::blockPos(pos), 0, false)) {
        if (auto* c = inv::nativeContainer(*region, gf::game::blockPos(pos)))
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    }
    (void)gf::blockdata::take(pos);
}

/// The test's players count as client mod users while the scope lives; at its end their chest
/// screens close (screen.closed), the recorded messages go and the containers are emptied.
struct ViewersScope {
    std::vector<std::int64_t> players;
    std::vector<GlobalPos>    containers;
    ~ViewersScope() {
        for (auto id : players)
            if (auto* p = playerById(id)) {
                gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
                (void)gf::net::takeTestMessages(*p);
                gf::net::setTestCompanion(*p, false);
            }
        for (auto const& pos : containers) emptyContainer(pos);
    }
    void add(Player& p) {
        players.push_back(p.getOrCreateUniqueID().rawID);
        gf::net::setTestCompanion(p, true);
    }
};

/// Puts the player two blocks south of `pos`, in reach of it (a screen out of reach closes).
void standBy(Player& p, GlobalPos const& pos) {
    static_cast<SimulatedPlayer&>(p).teleport(Vec3{pos.x + .5f, static_cast<float>(pos.y), pos.z + 2.5f}, p.getDimensionId());
}

/// The chest screen updates ("screen.update" of inventory.chest) sent to the player since the last
/// call, oldest first (the other recorded messages go too).
std::vector<JsonT> updatesOf(Player& p) {
    std::vector<JsonT> out;
    for (auto const& m : gf::net::takeTestMessages(p))
        if (m.channel == "screen.update" && m.payload.value("type", std::string{}) == "inventory.chest") out.push_back(m.payload);
    return out;
}

std::string shownId(JsonT const& update, std::size_t slot) {
    auto shown = update.value("data", JsonT::object()).value("items", JsonT::array());
    return slot < shown.size() && shown[slot].is_object() ? shown[slot].value("id", std::string{}) : std::string{};
}

int shownCount(JsonT const& update, std::size_t slot) {
    auto shown = update.value("data", JsonT::object()).value("items", JsonT::array());
    return slot < shown.size() && shown[slot].is_object() ? shown[slot].value("count", 0) : 0;
}

bool contextIs(JsonT const& update, GlobalPos const& pos) {
    auto p = update.value("context", JsonT::object()).value("pos", JsonT::array());
    return p == JsonT{pos.dim, pos.x, pos.y, pos.z};
}

bool probeAvailable() { return gf::game::blockWithStates(kNativeChest, {{kProbeType, "single"}, {kProbeFacing, "north"}}) != nullptr; }

} // namespace

// ---- every client mod viewer follows a container's changes ----------------------------------------------

GF_REGISTER_SELFTEST("combat.inventory_screen_refresh", [](Context& ctx) -> ll::coro::CoroTask<> {
    DeclaredScope fixture({.block = kFixture, .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
    auto          pos = ctx.testPos(6070);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kFixture), "a 27-slot container (BlockData backend)");
    std::optional<DeclaredScope> native; // (declared before the viewers' scope: its container is emptied while declared)
    ViewersScope                 scope;
    scope.containers.push_back(pos);
    auto& second = ctx.spawnPlayer("gf_combat_viewer");
    auto  bId    = second.getOrCreateUniqueID().rawID;
    auto  aId    = ctx.player().getOrCreateUniqueID().rawID;
    scope.add(ctx.player());
    scope.add(second);
    standBy(ctx.player(), pos);
    standBy(second, pos);
    co_await ctx.ticks(2);
    auto* a = playerById(aId);
    auto* b = playerById(bId);
    ctx.require(a && b, "both players are there");
    auto h = inv::find(*ctx.region(), gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the container handler");
    clearInventory(*a);
    // both open the screen by code (no click)
    ctx.require(inv::openChestLike(*a, pos, 3, "container.chest") && inv::openChestLike(*b, pos, 3, "container.chest"), "both players' chest screens open");
    ctx.check(inv::ext::chestScreenOf(*a) == pos && inv::ext::chestScreenOf(*b) == pos, "each screen's context is the container's position");
    (void)updatesOf(*a);
    (void)updatesOf(*b);
    // nothing changes: no data is sent (no polling)
    auto refreshed = inv::screenRefreshes();
    co_await ctx.ticks(3);
    a = playerById(aId);
    b = playerById(bId);
    ctx.require(a && b, "both players are still there");
    ctx.check(updatesOf(*a).empty() && updatesOf(*b).empty() && inv::screenRefreshes() == refreshed, "no update while the container does not change");
    // a write through the handler: both viewers get it at the end of the tick, once each
    auto count = inv::ext::changeCount(pos);
    h->set(pos, 4, *items::make("minecraft:cobblestone", 7));
    ctx.check(inv::ext::changeCount(pos) > count, "the write bumped the container's change counter");
    ctx.check(updatesOf(*a).empty(), "the update waits for the end of the tick (one per tick, as broadcastChanges)");
    co_await ctx.ticks(2);
    a = playerById(aId);
    b = playerById(bId);
    ctx.require(a && b, "both players are still there");
    auto ua = updatesOf(*a), ub = updatesOf(*b);
    ctx.check(ua.size() == 1 && ub.size() == 1, "each viewer got one update (" + std::to_string(ua.size()) + ", " + std::to_string(ub.size()) + ")");
    if (!ua.empty() && !ub.empty()) {
        ctx.check(shownId(ua.back(), 4) == "minecraft:cobblestone" && shownCount(ua.back(), 4) == 7 && shownId(ub.back(), 4) == "minecraft:cobblestone",
                  "the updates show the new stack");
        ctx.check(contextIs(ua.back(), pos) && contextIs(ub.back(), pos), "in the context the screens were opened with");
    }
    // the first player's own click: answered once (the action's update), the other viewer follows
    auto pctx = JsonT{{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", 3}, {"title", "container.chest"}};
    gf::net::injectFromClient(*a, "screen.action", {{"type", "inventory.chest"}, {"context", pctx}, {"action", {{"op", "click"}, {"slot", 4}, {"button", 0}, {"type", "quick_move"}}}});
    ctx.check(countInInventory(*a, "minecraft:cobblestone") == 7 && items::isEmpty(h->get(pos, 4)), "the click moved the stack into the inventory");
    ua = updatesOf(*a);
    ctx.check(ua.size() == 1 && shownId(ua.back(), 4).empty(), "the click was answered with fresh data");
    co_await ctx.ticks(2);
    a = playerById(aId);
    b = playerById(bId);
    ctx.require(a && b, "both players are still there");
    ua = updatesOf(*a);
    ub = updatesOf(*b);
    ctx.check(ua.empty(), "the clicking player gets no second update (" + std::to_string(ua.size()) + ")");
    ctx.check(ub.size() == 1 && shownId(ub.back(), 4).empty(), "the other viewer sees the slot emptied");
    // a change behind the contract, told with ext::markChanged
    count = inv::ext::changeCount(pos);
    inv::ext::markChanged(pos);
    ctx.check(inv::ext::changeCount(pos) > count, "markChanged bumps the counter");
    co_await ctx.ticks(2);
    a = playerById(aId);
    b = playerById(bId);
    ctx.require(a && b, "both players are still there");
    ctx.check(updatesOf(*a).size() == 1 && updatesOf(*b).size() == 1, "markChanged refreshed both viewers");
    // a closed screen gets nothing
    gf::net::injectFromClient(*b, "screen.closed", {{"type", "inventory.chest"}});
    ctx.check(!inv::ext::chestScreenOf(*b), "the closed screen has no context");
    h->set(pos, 0, *items::make("minecraft:stick", 1));
    co_await ctx.ticks(2);
    a = playerById(aId);
    b = playerById(bId);
    ctx.require(a && b, "both players are still there");
    ctx.check(updatesOf(*a).size() == 1 && updatesOf(*b).empty(), "only the open screen follows");
    h->set(pos, 0, ItemStack{});
    clearInventory(*a);

    // a native container written as a hopper writes it (Container::setItem)
    if (!probeAvailable()) {
        ctx.info("no gf_combat:native_chest probe block in this run (the combat scenario adds it): the native container is not checked");
        co_return;
    }
    native.emplace(inv::Declared{.block = kNativeChest, .rows = 3, .slots = 27, .title = "container.chest", .skin = "", .pairs = true});
    auto npos = ctx.testPos(6072);
    ctx.require(ctx.setBlock(npos, kNativeChest, {{kProbeType, "single"}, {kProbeFacing, "north"}}), "a native chest placed after its declaration");
    scope.containers.push_back(npos);
    auto* c = inv::nativeContainer(*ctx.region(), gf::game::blockPos(npos));
    ctx.require(c != nullptr, "its block actor holds the contract's game container");
    a = playerById(aId);
    ctx.require(a != nullptr, "the player is still there");
    standBy(*a, npos);
    ctx.require(inv::openChestLike(*a, npos, 3, "container.chest"), "its screen opens by code");
    (void)updatesOf(*a);
    c->setItem(2, *items::make("minecraft:emerald", 5));
    co_await ctx.ticks(2);
    a = playerById(aId);
    ctx.require(a != nullptr, "the player is still there");
    ua = updatesOf(*a);
    ctx.check(ua.size() == 1 && shownId(ua.back(), 2) == "minecraft:emerald" && contextIs(ua.back(), npos),
              "the native container's setItem refreshed the screen (" + std::to_string(ua.size()) + ")");
    c = inv::nativeContainer(*ctx.region(), gf::game::blockPos(npos));
    if (c) c->setItem(2, ItemStack{});
});

// ---- upstream's double chest opened by code: refreshed in its own context ---------------------------------------

GF_REGISTER_SELFTEST("combat.gf_chest_screen_refresh", [](Context& ctx) -> ll::coro::CoroTask<> {
    // facing north: a left half's partner is east of it (ChestBlock#getConnectedDirection)
    auto left  = ctx.testPos(6073);
    auto right = ctx.testPos(6074);
    ctx.require(co_await ctx.ensureLoaded(left), "test area loaded");
    ViewersScope scope;
    scope.containers = {left, right};
    ctx.require(ctx.setBlock(left, kOakChest, {{kType, "left"}, {kFacing, "north"}}), "the left half");
    ctx.require(ctx.setBlock(right, kOakChest, {{kType, "right"}, {kFacing, "north"}}), "the right half");
    auto* region = ctx.region();
    auto  hl     = inv::find(*region, gf::game::blockPos(left));
    auto  hr     = inv::find(*region, gf::game::blockPos(right));
    ctx.require(hl && hr, "both halves are containers");
    auto* nativeLeft = inv::nativeContainer(*region, gf::game::blockPos(left));
    ctx.info(nativeLeft ? "upstream's chests use the native backend" : "upstream's chests use the BlockData backend");
    auto pid = ctx.player().getOrCreateUniqueID().rawID;
    scope.add(ctx.player());
    standBy(ctx.player(), left);
    co_await ctx.ticks(2);
    auto* p = playerById(pid);
    ctx.require(p != nullptr, "the player is there");
    // opened by code through the left half: no click (F11 cannot know the screen's context)
    ctx.require(inv::openChestLike(*p, left, 3, "container.chest"), "the double chest opens by code");
    ctx.check(inv::openedParts(*p).size() == 2 && inv::ext::chestScreenOf(*p) == left, "a 54-slot screen whose context is the left half");
    (void)updatesOf(*p);
    co_await ctx.ticks(3);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    ctx.check(updatesOf(*p).empty(), "no update while nothing changes");
    // the right half changes (the upper 27 slots)
    hr->set(right, 3, *items::make("minecraft:redstone", 9));
    co_await ctx.ticks(2);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    auto u = updatesOf(*p);
    ctx.check(u.size() == 1, "the right half's change refreshed the screen (" + std::to_string(u.size()) + ")");
    if (!u.empty()) {
        ctx.check(contextIs(u.back(), left), "in the screen's own context, the left half (" + u.back().value("context", JsonT{}).dump() + ")");
        ctx.check(shownId(u.back(), 3) == "minecraft:redstone" && shownCount(u.back(), 3) == 9, "slot 3 shows the right half's new stack");
        ctx.check(u.back().value("data", JsonT::object()).value("slots", 0) == 54, "still the 54-slot screen");
    }
    ctx.check(inv::openedParts(*p).size() == 2, "the screen stays open");
    // the left half changes: through its game container as a hopper writes it (native), or through
    // the BlockData backend
    nativeLeft = inv::nativeContainer(*ctx.region(), gf::game::blockPos(left));
    if (nativeLeft) nativeLeft->setItem(5, *items::make("minecraft:lapis_lazuli", 4));
    else hl->set(left, 5, *items::make("minecraft:lapis_lazuli", 4));
    co_await ctx.ticks(2);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    u = updatesOf(*p);
    ctx.check(u.size() == 1 && shownId(u.back(), 32) == "minecraft:lapis_lazuli" && contextIs(u.back(), left),
              std::string("the left half's change (") + (nativeLeft ? "its game container's setItem" : "its block data") + ") shows at slot 32");
    co_await ctx.ticks(3);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    ctx.check(updatesOf(*p).empty(), "and nothing more after it");
    hr->set(right, 3, ItemStack{});
    hl->set(left, 5, ItemStack{});
    gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
    co_await ctx.ticks(1);
    (void)takeItemEntitiesNear(left, 4);
});

// ---- a native container's stacks still in block data ------------------------------------------------------

GF_REGISTER_SELFTEST("combat.inventory_native_migrate", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!probeAvailable()) {
        ctx.info("no gf_combat:native_chest probe block in this run (the combat scenario adds it): nothing to check");
        co_return;
    }
    DeclaredScope native({.block = kNativeChest, .rows = 3, .slots = 27, .title = "container.chest", .skin = "", .pairs = true});
    auto          pos = ctx.testPos(6076);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ViewersScope scope;
    scope.containers.push_back(pos);
    ctx.require(ctx.setBlock(pos, kNativeChest, {{kProbeType, "single"}, {kProbeFacing, "north"}}), "a native chest placed after its declaration");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    auto* c      = inv::nativeContainer(*region, bp);
    ctx.require(c != nullptr, "its block actor holds the contract's game container");
    auto h = inv::find(*region, bp);
    ctx.require(h.has_value(), "the container handler");
    // stacks stored in block data before the block had its block actor: 14 full stacks of
    // cobblestone in slots 0..13; the native container already holds a diamond in slot 0
    c->setItem(0, *items::make("minecraft:diamond", 1));
    JsonT stored = JsonT::array();
    for (int i = 0; i < 14; ++i) stored.push_back(items::toJson(*items::make("minecraft:cobblestone", 64)));
    JsonT layout     = JsonT::object();
    layout["items"] = stored; // the BlockData backend's layout ("inventory": {"items": [...]})
    gf::blockdata::set(*region, bp, "inventory", layout);
    ctx.check(inv::ext::hasBlockDataStacks(pos), "stacks wait in block data");
    // reads: each waiting stack where a move puts it (slot 0 is taken: its stack goes to the first
    // empty slot, 14)
    auto all = inv::contents(pos);
    bool view = all.size() == 27 && items::idOf(all[0]) == "minecraft:diamond" && items::idOf(all[1]) == "minecraft:cobblestone"
             && items::idOf(all[13]) == "minecraft:cobblestone" && items::idOf(all[14]) == "minecraft:cobblestone" && items::isEmpty(all[15]);
    ctx.check(view, "a read shows the native slots with the waiting stacks where a move puts them");
    ctx.check(items::idOf(h->get(pos, 14)) == "minecraft:cobblestone", "the handler reads them too");
    ctx.check(items::isEmpty(c->getItem(1)) && inv::ext::hasBlockDataStacks(pos), "a read moves nothing");
    co_await ctx.ticks(1); // (the comparator output is read once a tick)
    // Mth.lerpDiscrete((14 + 1/64) / 27, 0, 15) = 8 (the diamond alone would give 1)
    int signal = inv::comparatorSignal(pos).value_or(-1);
    ctx.check(signal == 8, "the comparator output counts the waiting stacks: 14 full stacks and a diamond give 8 (" + std::to_string(signal) + ")");
    // the first write moves them, then writes
    auto count = inv::ext::changeCount(pos);
    h->set(pos, 20, *items::make("minecraft:gold_ingot", 2));
    c = inv::nativeContainer(*ctx.region(), bp);
    ctx.require(c != nullptr, "the native container is still there");
    bool moved = items::idOf(c->getItem(0)) == "minecraft:diamond" && items::countOf(c->getItem(1)) == 64 && items::countOf(c->getItem(14)) == 64
              && items::countOf(c->getItem(20)) == 2;
    ctx.check(moved && !inv::ext::hasBlockDataStacks(pos), "the first write moved the waiting stacks into the block actor, then wrote");
    auto const* doc = gf::blockdata::get(pos);
    ctx.check(!doc || !doc->contains("inventory") || !(*doc)["inventory"].contains("items"), "the block data keeps no slots");
    ctx.check(inv::ext::changeCount(pos) > count, "the move and the write are changes");
    co_await ctx.ticks(1);
    signal = inv::comparatorSignal(pos).value_or(-1);
    ctx.check(signal == 8, "the comparator output is the same after the move (" + std::to_string(signal) + ")");
    // ext::migrateBlockDataToNative: a stack whose slot is taken goes to the first empty slot; the
    // pending loot table stays in block data
    JsonT withLoot = JsonT::object();
    withLoot["items"] = JsonT::array({items::toJson(*items::make("minecraft:stick", 3))});
    withLoot["loot"]  = JsonT{{"table", "gf_test:chests/fixture_chest"}, {"seed", 5}};
    gf::blockdata::set(*ctx.region(), bp, "inventory", withLoot);
    ctx.check(inv::ext::migrateBlockDataToNative(pos) == 1, "one stack moved");
    c = inv::nativeContainer(*ctx.region(), bp);
    ctx.require(c != nullptr, "the native container is still there");
    ctx.check(items::idOf(c->getItem(15)) == "minecraft:stick" && items::idOf(c->getItem(0)) == "minecraft:diamond", "into the first empty slot (15), slot 0 kept its diamond");
    auto pending = inv::pendingLoot(pos);
    ctx.check(pending && pending->first == "gf_test:chests/fixture_chest" && pending->second == 5, "the pending loot table stays in block data");
    ctx.check(inv::ext::migrateBlockDataToNative(pos) == 0, "nothing waits any more: nothing moves");
    // no room: without drops nothing moves; with drops the stack drops at the block
    for (int i = 0; i < c->getContainerSize(); ++i)
        if (items::isEmpty(c->getItem(i))) c->setItem(i, *items::make("minecraft:dirt", 1));
    JsonT full = JsonT::object();
    full["items"] = JsonT::array({items::toJson(*items::make("minecraft:emerald", 6))});
    gf::blockdata::set(*ctx.region(), bp, "inventory", full);
    ctx.check(inv::ext::migrateBlockDataToNative(pos, false) == 0 && inv::ext::hasBlockDataStacks(pos), "a full container takes nothing without drops (the stack waits)");
    ctx.check(inv::ext::migrateBlockDataToNative(pos, true) == 0 && !inv::ext::hasBlockDataStacks(pos), "with drops the stack leaves the block data");
    auto dropped = takeItemEntitiesNear(pos, 3);
    ctx.check(gf::combat::testutil::countIn(dropped, "minecraft:emerald") == 6, "and drops at the block (Containers#dropContents)");
    emptyContainer(pos);
});
