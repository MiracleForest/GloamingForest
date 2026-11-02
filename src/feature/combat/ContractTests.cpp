// Selftests of F10 against the other W0 units' real implementations (prefix "combat.", testPos dx
// 6031..6049):
//   - TF's chests as tools/families declares them (F11, data/gf/containers/families.json) on the
//     inventory contract: the declarations, storage, a double chest's halves (one 54-slot screen,
//     the right half first; the comparator output of both halves; a broken half), the open / close
//     sounds, the opening stats through F9's gf::stats (open_chest, trigger_trapped_chest), the
//     chest block's own reactions to the openers (F11's lids and trapped chest signal), a loot
//     table unpacked through LootTable#fill with the opener and the opener's luck, and the slots of
//     a double chest over a world reload (combat.tf_chest_persist_write / _read around a restart);
//   - tag matchers through F9's gf::tags: damage type tags, loot entries and conditions;
//   - TF's MissingAdvancementToast (what showMissing sends) and, in the client build, the toast
//     F12 queues for it and the fx.particle route of companions through F12's native sink.
// Every test empties the containers it filled before its blocks go (nothing drops in the test row).

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <random>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/CombatInternal.h"
#include "feature/common/BlockData.h"
#include "feature/common/Redstone.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/fx/Fx.h"
#include "feature/fx/FxInternal.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/Loot.h"
#include "feature/loot/LootInternal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/progression/ProgressionInternal.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

#ifdef GF_CLIENT
#include "client/ui/Ui.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#endif

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
using gf::core::GlobalPos;
namespace db    = gf::game::db;
namespace items = gf::game::items;
namespace inv   = gf::inventory;

constexpr char kOakChest[]    = "twilightforest:twilight_oak_chest";
constexpr char kOakTrapped[]  = "twilightforest:twilight_oak_trapped_chest";
constexpr char kCanopyChest[] = "twilightforest:canopy_chest";
constexpr char kType[]        = "twilightforest:type";
constexpr char kFacing[]      = "twilightforest:facing";
constexpr char kLid[]         = "gloamingforest:lid";        // F11's lid (ChestLidController)
constexpr char kOpenCount[]   = "gloamingforest:open_count"; // F11's trapped chest signal
constexpr char kOpenStat[]    = "minecraft:open_chest";
constexpr char kTrapStat[]    = "minecraft:trigger_trapped_chest";

Player* playerById(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->getPlayer(id);
}

int countIn(std::vector<ItemStack> const& list, std::string const& id) {
    int n = 0;
    for (auto const& s : list)
        if (items::idOf(s) == id) n += items::countOf(s);
    return n;
}

int countInInventory(Player& p, std::string const& id) {
    int   n   = 0;
    auto& pin = p.getInventory();
    for (int i = 0; i < pin.getContainerSize(); ++i)
        if (items::idOf(pin.getItem(i)) == id) n += items::countOf(pin.getItem(i));
    return n;
}

void clearInventory(Player& p) {
    auto& pin = p.getInventory();
    for (int i = 0; i < pin.getContainerSize(); ++i) pin.setItem(i, ItemStack{});
    p.setOffhandSlot(ItemStack{});
}

/// The item entities within `radius` of `pos`: their stacks, and they are removed.
std::vector<ItemStack> takeItemEntitiesNear(GlobalPos const& pos, float radius) {
    std::vector<ItemStack> out;
    auto                   level = ll::service::getLevel();
    if (!level) return out;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<int>(a->getDimensionId()) != pos.dim) continue;
        auto const& at = a->getPosition();
        float       dx = at.x - (pos.x + .5f), dy = at.y - (pos.y + .5f), dz = at.z - (pos.z + .5f);
        if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
        out.push_back(static_cast<ItemActor*>(a)->item());
        a->remove();
    }
    return out;
}

std::optional<int> stateAt(GlobalPos const& pos, char const* state) {
    auto* region = gf::game::regionOf(pos.dim);
    if (!region) return std::nullopt;
    return gf::game::intState(region->getBlock(gf::game::blockPos(pos)), state);
}

std::string strStateAt(GlobalPos const& pos, char const* state) {
    auto* region = gf::game::regionOf(pos.dim);
    if (!region) return {};
    auto all = gf::game::statesOf(region->getBlock(gf::game::blockPos(pos)));
    auto it  = all.find(state);
    return it == all.end() ? std::string{} : it->second;
}

/// The test player counts as a client mod user while the scope lives (also when a `require` ends
/// the test early).
struct CompanionScope {
    std::int64_t id;
    explicit CompanionScope(Player& p) : id(p.getOrCreateUniqueID().rawID) { gf::net::setTestCompanion(p, true); }
    ~CompanionScope() {
        if (auto* p = playerById(id)) {
            (void)gf::net::takeTestMessages(*p);
            gf::net::setTestCompanion(*p, false);
        }
    }
    CompanionScope(CompanionScope const&)            = delete;
    CompanionScope& operator=(CompanionScope const&) = delete;
};

/// The containers of a test go without dropping anything when the test ends (its blocks are
/// restored after it): open screens are closed, proxies end, the block data (slots, a pending
/// table) is taken away.
struct ChestsScope {
    std::vector<GlobalPos> positions;
    std::int64_t           player = 0;
    ~ChestsScope() {
        if (auto* p = player ? playerById(player) : nullptr) {
            gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
            if (p->mContainerManager.get()) static_cast<ServerPlayer&>(*p).doDeleteContainerManager(false);
        }
        for (auto const& pos : positions) {
            if (inv::proxyOpen(pos)) inv::proxyEnd(pos);
            (void)gf::blockdata::take(pos);
        }
    }
};

/// The test player's right click on the top of `pos` with an empty hand, as the network handler
/// runs one (GameMode::useItemOn: the interactions dispatch, then F10's chest handler): it stands
/// two blocks south of the block looking at it first. `before` runs right before the click.
ll::coro::CoroTask<> rightClick(Context& ctx, GlobalPos pos, std::function<void(Player&)> before = {}) {
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        sp.teleport(Vec3{pos.x + .5f, static_cast<float>(pos.y), pos.z + 2.5f}, sp.getDimensionId());
        sp.simulateLookAt(gf::game::blockPos(pos), sim::LookDuration::Instant);
        sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(8); // the look applies on the player's next tick; Bedrock drops a use-on right after another
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    if (before) before(sp);
    auto  bp   = gf::game::blockPos(pos);
    auto& held = const_cast<ItemStack&>(sp.getSelectedItem());
    sp.mGameMode->useItemOn(held, bp, 1, Vec3{bp.x + .5f, bp.y + 1.f, bp.z + .5f}, HandSlot::Mainhand, nullptr, true);
}

/// The data of the last "screen.open" of the chest screen sent to the player (null: none).
JsonT openedChestScreen(Player& p) {
    JsonT data;
    for (auto const& m : gf::net::takeTestMessages(p))
        if (m.channel == "screen.open" && m.payload.value("type", std::string{}) == "inventory.chest") data = m.payload.value("data", JsonT::object());
    return data;
}

std::string shownId(JsonT const& data, std::size_t slot) {
    auto shown = data.value("items", JsonT::array());
    return slot < shown.size() && shown[slot].is_object() ? shown[slot].value("id", std::string{}) : std::string{};
}

/// Sets the current value of `p`'s luck attribute (Player#getLuck); the previous value, nullopt
/// when the player has no luck attribute.
std::optional<float> setLuck(Player& p, float value) {
    auto  ref  = p.getAttribute(SharedAttributes::LUCK());
    auto* inst = const_cast<AttributeInstance*>(static_cast<AttributeInstance const*>(ref.mPtr));
    if (!inst) return std::nullopt;
    float old           = inst->mCurrentValue;
    inst->mCurrentValue = value;
    return old;
}

} // namespace

// ---- TF's chests: the declarations tools/families writes ---------------------------------------------

GF_REGISTER_SELFTEST("combat.tf_chests_declared", [](Context& ctx) {
    static constexpr char const* kWoods[] = {"twilight_oak", "canopy", "mangrove", "dark", "time", "transformation", "mining", "sorting"};
    int                          good     = 0;
    for (auto const* wood : kWoods) {
        for (bool trapped : {false, true}) {
            auto id = std::string("twilightforest:") + wood + (trapped ? "_trapped_chest" : "_chest");
            auto d  = inv::declared(id);
            ctx.check(d.has_value(), id + " is a declared container (data/gf/containers/families.json)");
            if (!d) continue;
            bool rules  = d->slots == 27 && d->rows == 3 && d->chest && d->pairs && d->comparator;
            bool titles = d->title == "container.chest" && d->doubleTitle == "container.chestDouble";
            bool stat   = d->stat == (trapped ? kTrapStat : kOpenStat);
            bool sounds = d->openSound == "minecraft:block.chest.open" && d->closeSound == "minecraft:block.chest.close";
            ctx.check(rules && titles, id + ": 27 slots, pairs, the blocked rule, a comparator output, Java's titles");
            ctx.check(stat, id + ": the opening stat is " + (trapped ? kTrapStat : kOpenStat) + " (got " + d->stat + ")");
            ctx.check(sounds, id + ": ChestBlockEntity's open / close sounds");
            ctx.check(inv::handlerFor(id).has_value(), id + " has storage");
            if (rules && titles && stat && sounds) ++good;
        }
    }
    ctx.check(good == 16, "TF's 16 chests and trapped chests follow ChestBlock (" + std::to_string(good) + ")");
    ctx.check(gf::fx::resolveSound("minecraft:block.chest.open") == "random.chestopen" && gf::fx::resolveSound("minecraft:block.chest.close") == "random.chestclosed",
              "the chest sounds play as Bedrock's");
});

// ---- TF's double chest: storage, one screen, stats, sounds, comparators, a broken half -------------------

GF_REGISTER_SELFTEST("combat.tf_double_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace rs = gf::redstone;
    // facing north: a left half's partner is east of it (ChestBlock#getConnectedDirection)
    auto left  = ctx.testPos(6031);
    auto right = ctx.testPos(6032);
    auto lone  = ctx.testPos(6034);
    auto other = ctx.testPos(6035);
    ctx.require(co_await ctx.ensureLoaded(left), "test area loaded");
    ChestsScope chests{{left, right, lone, other}, ctx.player().getOrCreateUniqueID().rawID};
    ctx.require(ctx.setBlock(left, kOakChest, {{kType, "left"}, {kFacing, "north"}}), "the left half");
    ctx.require(ctx.setBlock(right, kOakChest, {{kType, "right"}, {kFacing, "north"}}), "the right half");
    auto parts = inv::partsOf(left);
    ctx.check(parts.size() == 2 && parts[0] == right && parts[1] == left, "the halves pair, the right one first (ChestBlock#combine)");
    ctx.check(inv::partsOf(right) == parts, "both halves show the same double chest");
    auto* region = ctx.region();
    auto  hl     = inv::find(*region, gf::game::blockPos(left));
    auto  hr     = inv::find(*region, gf::game::blockPos(right));
    ctx.require(hl && hr && hl->slots == 27 && hr->slots == 27, "each half stores its own 27 slots");
    hr->set(right, 0, *items::make("minecraft:redstone", 2));
    hl->set(left, 0, *items::make("minecraft:lapis_lazuli", 3));
    hl->set(left, 26, *items::make("minecraft:stick", 5));
    auto const* doc = gf::blockdata::get(left);
    ctx.check(doc && doc->contains("inventory") && items::countOf(hl->get(left, 26)) == 5, "the slots live in the half's block data (BlockData backend)");

    // a click of a client mod user on the left half: the 54-slot screen, the stat, the sound
    auto&    p = ctx.player();
    auto     pid = p.getOrCreateUniqueID().rawID;
    CompanionScope companion(p);
    clearInventory(p);
    long long opens   = gf::stats::value(p, kOpenStat);
    long long traps   = gf::stats::value(p, kTrapStat);
    auto      sounds  = inv::soundCounters();
    auto      packets = gf::fx::counters().soundPackets;
    (void)gf::net::takeTestMessages(p);
    co_await rightClick(ctx, left);
    auto* pp = playerById(pid);
    ctx.require(pp != nullptr, "the player is still there");
    auto data = openedChestScreen(*pp);
    ctx.require(data.is_object() && !data.empty(), "the click opened the chest screen (inventory.chest)");
    ctx.check(data.value("rows", 0) == 6 && data.value("slots", 0) == 54, "a six-row screen of 54 slots (" + data.dump().substr(0, 80) + ")");
    ctx.check(data.value("title", std::string{}) == gf::trFor(*pp, "container.chestDouble", "Large Chest"), "titled container.chestDouble (" + data.value("title", std::string{}) + ")");
    ctx.check(shownId(data, 0) == "minecraft:redstone" && shownId(data, 27) == "minecraft:lapis_lazuli" && shownId(data, 53) == "minecraft:stick",
              "the right half's slots first, then the left half's");
    ctx.check(gf::stats::value(*pp, kOpenStat) == opens + 1 && gf::stats::value(*pp, kTrapStat) == traps, "the click awarded minecraft:open_chest once (gf::stats)");
    ctx.check(inv::viewers(left) == 1 && inv::viewers(right) == 1, "both halves count the opener");
    ctx.check(inv::soundCounters().opened == sounds.opened + 1, "one open sound (the right half's, between the halves; the left half plays none)");
    ctx.check(gf::fx::counters().soundPackets > packets, "the sound reached the player");
    // a click on slot 27 is the left half's first slot
    auto pctx = JsonT{{"pos", {left.dim, left.x, left.y, left.z}}, {"rows", 6}, {"title", "container.chestDouble"}};
    gf::net::injectFromClient(*pp, "screen.action", {{"type", "inventory.chest"}, {"context", pctx}, {"action", {{"op", "click"}, {"slot", 27}, {"button", 0}, {"type", "quick_move"}}}});
    ctx.check(items::isEmpty(hl->get(left, 0)) && countInInventory(*pp, "minecraft:lapis_lazuli") == 3, "slot 27 is the left half's first slot (quick move)");
    // F11's lids follow the openers of both halves (ChestLidController: open in 10 ticks)
    co_await ctx.ticks(12);
    ctx.check(stateAt(left, kLid).value_or(-1) == 4 && stateAt(right, kLid).value_or(-1) == 4,
              "both lids opened (" + std::to_string(stateAt(left, kLid).value_or(-1)) + ", " + std::to_string(stateAt(right, kLid).value_or(-1)) + ")");
    pp = playerById(pid);
    ctx.require(pp != nullptr, "the player is still there");
    gf::net::injectFromClient(*pp, "screen.closed", {{"type", "inventory.chest"}});
    ctx.check(inv::viewers(left) == 0 && inv::viewers(right) == 0, "closing leaves both halves");
    ctx.check(inv::soundCounters().closed == sounds.closed + 1, "one close sound after the last opener");
    co_await ctx.ticks(12);
    ctx.check(stateAt(left, kLid).value_or(-1) == 0 && stateAt(right, kLid).value_or(-1) == 0, "both lids closed again");

    // the comparator output of each half: the double chest's 54 slots together
    region = ctx.region();
    for (int i = 0; i < 27; ++i) {
        hl->set(left, i, ItemStack{});
        hr->set(right, i, *items::make("minecraft:cobblestone", 64));
    }
    co_await ctx.ticks(1);
    region   = ctx.region();
    int sigL = rs::analogOutput(*region, gf::game::blockPos(left)).value_or(-1);
    int sigR = rs::analogOutput(*region, gf::game::blockPos(right)).value_or(-1);
    ctx.check(sigL == 8 && sigR == 8, "27 full stacks in the right half: both halves give a comparator 8 (" + std::to_string(sigL) + ", " + std::to_string(sigR) + ")");
    // a redstone conductor over either half: ChestBlock#getContainer finds no container (0 at both)
    auto above = left;
    above.y += 1;
    ctx.require(ctx.setBlock(above, "minecraft:stone"), "stone above the left half");
    co_await ctx.ticks(1);
    region = ctx.region();
    sigR   = rs::analogOutput(*region, gf::game::blockPos(right)).value_or(-1);
    ctx.check(inv::chestBlocked(right) && sigR == 0, "stone over the left half blocks the double chest: the right half gives 0 (" + std::to_string(sigR) + ")");
    ctx.require(ctx.setBlock(above, "minecraft:air"), "nothing above the chest");
    for (int i = 0; i < 27; ++i) hr->set(right, i, ItemStack{});

    // halves of different chests never pair (ChestBlock#combine: the same block)
    ctx.require(ctx.setBlock(lone, kOakChest, {{kType, "left"}, {kFacing, "north"}}) && ctx.setBlock(other, kCanopyChest, {{kType, "right"}, {kFacing, "north"}}),
                "a twilight oak half beside a canopy half");
    ctx.check(inv::partsOf(lone).size() == 1 && inv::partsOf(other).size() == 1, "a twilight oak and a canopy chest stay two chests");

    // a broken half drops its own slots; the other half keeps its slots and stands alone
    hr->set(right, 4, *items::make("minecraft:diamond", 4));
    hl->set(left, 9, *items::make("minecraft:emerald", 6));
    region = ctx.region();
    ctx.require(gf::game::clearBlock(*region, gf::game::blockPos(right)), "the right half is broken");
    auto dropped = takeItemEntitiesNear(right, 4);
    ctx.check(countIn(dropped, "minecraft:diamond") == 4 && countIn(dropped, "minecraft:emerald") == 0, "the broken half dropped its own slots (Containers#dropContents)");
    co_await ctx.ticks(1);
    ctx.check(inv::partsOf(left).size() == 1 && items::countOf(hl->get(left, 9)) == 6, "the other half keeps its slots and opens alone");
    ctx.check(strStateAt(left, kType) == "single", "the families kit made the left half a single chest (" + strStateAt(left, kType) + ")");
    hl->set(left, 9, ItemStack{});
    if (auto* again = playerById(pid)) clearInventory(*again);
    (void)takeItemEntitiesNear(left, 6);
});

// ---- TF's trapped chest: the vanilla screen, the stat, the signal -------------------------------------------

GF_REGISTER_SELFTEST("combat.tf_trapped_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(6036);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto pid = ctx.player().getOrCreateUniqueID().rawID;
    ChestsScope chests{{pos}, pid};
    ctx.require(ctx.setBlock(pos, kOakTrapped, {{kFacing, "north"}}), "a twilight oak trapped chest");
    auto h = inv::find(*ctx.region(), gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the trapped chest is a container");
    h->set(pos, 13, *items::make("minecraft:gold_ingot", 9));
    // a vanilla player: the game's chest screen through the chest minecart proxy
    long long opens  = gf::stats::value(ctx.player(), kOpenStat);
    long long traps  = gf::stats::value(ctx.player(), kTrapStat);
    int       before = inv::vanillaProxyCount();
    co_await rightClick(ctx, pos);
    ctx.check(inv::vanillaProxyCount() == before + 1, "a vanilla player's click opened the proxy chest screen");
    co_await ctx.ticks(6); // the screen opens once the proxy reached the client
    auto* p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    auto* proxy = inv::vanillaProxyContainer(pos);
    ctx.check(proxy && items::countOf(proxy->getItem(13)) == 9, "the proxy shows the trapped chest's slots");
    ctx.check(gf::stats::value(*p, kTrapStat) == traps + 1 && gf::stats::value(*p, kOpenStat) == opens, "TrappedChestBlock#getOpenChestStat: minecraft:trigger_trapped_chest, not open_chest");
    ctx.check(inv::viewers(pos) == 1, "the vanilla screen counts as an opener");
    ctx.check(stateAt(pos, kOpenCount).value_or(-1) == 1, "the trapped chest's signal is the opener count (F11: " + std::to_string(stateAt(pos, kOpenCount).value_or(-1)) + ")");
    static_cast<ServerPlayer&>(*p).doDeleteContainerManager(false);
    co_await ctx.ticks(3);
    ctx.check(inv::viewers(pos) == 0 && inv::vanillaProxyCount() == before, "closing the screen ends the proxy");
    ctx.check(stateAt(pos, kOpenCount).value_or(-1) == 0, "the signal went back to 0");
    // a client mod user: the client mod's three-row screen
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    CompanionScope companion(*p);
    (void)gf::net::takeTestMessages(*p);
    co_await rightClick(ctx, pos);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    auto data = openedChestScreen(*p);
    ctx.check(data.value("rows", 0) == 3 && data.value("slots", 0) == 27 && shownId(data, 13) == "minecraft:gold_ingot", "a client mod user gets the three-row chest screen");
    ctx.check(data.value("title", std::string{}) == gf::trFor(*p, "container.chest", "Chest"), "titled container.chest");
    ctx.check(gf::stats::value(*p, kTrapStat) == traps + 2, "every opening counts");
    ctx.check(stateAt(pos, kOpenCount).value_or(-1) == 1, "the signal follows the client mod's screen too");
    gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
    ctx.check(stateAt(pos, kOpenCount).value_or(-1) == 0 && inv::viewers(pos) == 0, "closed: no opener, no signal");
    h->set(pos, 13, ItemStack{});
});

// ---- TF's chest with a loot table: LootTable#fill with the opener and its luck -----------------------------

GF_REGISTER_SELFTEST("combat.tf_chest_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    // RandomizableContainer#unpackLootTable: THIS_ENTITY is the opener, the luck the opener's, no
    // killer player; LootTable#fill spreads the stacks over the empty slots
    loot::registerTable("gf_test:combat/tf_chest", JsonT::parse(R"({"type":"minecraft:chest","pools":[
        {"rolls":0,"bonus_rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald"}]},
        {"rolls":1,"conditions":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"minecraft:player"}}],
         "entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":12}]}]},
        {"rolls":1,"conditions":[{"condition":"minecraft:killed_by_player"}],"entries":[{"type":"minecraft:item","name":"minecraft:stick"}]}]})"));
    struct TableScope {
        ~TableScope() { gf::loot::registerTable("gf_test:combat/tf_chest", nullptr); }
    } tableScope;
    auto pos  = ctx.testPos(6037);
    auto real = ctx.testPos(6038);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto pid = ctx.player().getOrCreateUniqueID().rawID;
    ChestsScope chests{{pos, real}, pid};
    ctx.cleanupBlock(pos);
    ctx.cleanupBlock(real);
    auto* region = ctx.region();
    ctx.require(loot::placeContainer(*region, gf::game::blockPos(pos), kOakChest, 2, "gf_test:combat/tf_chest", 4242), "a twilight oak chest placed with a loot table");
    ctx.check(ctx.blockAt(pos) == kOakChest && strStateAt(pos, kFacing) == "north", "placeContainer turned the TF chest north");
    auto pending = inv::pendingLoot(pos);
    ctx.check(pending && pending->first == "gf_test:combat/tf_chest" && pending->second == 4242, "the table waits in the chest's block data");
    CompanionScope companion(ctx.player());
    float luck = 0.f;
    bool  hasLuck = false;
    co_await rightClick(ctx, pos, [&](Player& p) {
        hasLuck = setLuck(p, 3.f).has_value();
        luck    = loot::luckOf(&p);
    });
    auto* p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    (void)setLuck(*p, 0.f);
    if (!hasLuck) ctx.info("the player has no luck attribute: the luck part reads 0");
    auto data = openedChestScreen(*p);
    ctx.check(!data.empty(), "the click opened the chest");
    ctx.check(!inv::pendingLoot(pos), "opening unpacked the table");
    auto all = inv::contents(pos);
    int  emeralds = countIn(all, "minecraft:emerald"), diamonds = countIn(all, "minecraft:diamond"), sticks = countIn(all, "minecraft:stick");
    int  diamondSlots = static_cast<int>(std::count_if(all.begin(), all.end(), [](ItemStack const& s) { return items::idOf(s) == "minecraft:diamond"; }));
    int  expected     = static_cast<int>(std::floor(luck));
    ctx.check(emeralds == expected, "bonus_rolls with the opener's luck " + std::to_string(luck) + ": " + std::to_string(expected) + " emerald(s) (got " + std::to_string(emeralds) + ")");
    ctx.check(diamonds == 12, "THIS_ENTITY is the opener: its pool rolled (" + std::to_string(diamonds) + " diamonds)");
    ctx.check(diamondSlots >= 2, "LootTable#fill split the stack over several slots (" + std::to_string(diamondSlots) + ")");
    ctx.check(sticks == 0, "no killer player: killed_by_player fails");
    gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
    auto h = inv::find(*ctx.region(), gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the chest's handler");
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});

    // one of TF's own tables (a quest camp hut's junk: ten rolls)
    region = ctx.region();
    ctx.require(loot::placeContainer(*region, gf::game::blockPos(real), kOakChest, 3, "twilightforest:chests/hut_junk", 77), "a chest with twilightforest:chests/hut_junk");
    co_await rightClick(ctx, real);
    p = playerById(pid);
    ctx.require(p != nullptr, "the player is still there");
    (void)openedChestScreen(*p);
    auto junk  = inv::contents(real);
    int  stacks = static_cast<int>(std::count_if(junk.begin(), junk.end(), [](ItemStack const& s) { return !items::isEmpty(s); }));
    ctx.check(!inv::pendingLoot(real) && stacks >= 10, "TF's table filled the chest (" + std::to_string(stacks) + " stacks)");
    gf::net::injectFromClient(*p, "screen.closed", {{"type", "inventory.chest"}});
    auto hj = inv::find(*ctx.region(), gf::game::blockPos(real));
    if (hj)
        for (int i = 0; i < hj->slots; ++i) hj->set(real, i, ItemStack{});
});

// ---- TF's double chest keeps its slots over a world reload ---------------------------------------------

namespace {
constexpr char kTfPersistKey[] = "gf:combat:tf_chest_probe";

std::pair<GlobalPos, GlobalPos> persistHalves(Context& ctx, BlockSource const& region) {
    auto left  = ctx.testPos(6039);
    auto right = ctx.testPos(6040);
    left.y = right.y = std::max(left.y - 3, region.getMinHeight() + 1); // under the ground, as combat.container_persist_*
    return {left, right};
}

JsonT posJson(GlobalPos const& p) { return {p.dim, p.x, p.y, p.z}; }
GlobalPos posOf(JsonT const& j) { return GlobalPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>()}; }
} // namespace

GF_REGISTER_SELFTEST("combat.tf_chest_persist_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the overworld is available");
    auto [left, right] = persistHalves(ctx, *region);
    ctx.require(co_await ctx.ensureLoaded(left), "test area loaded");
    region = ctx.region();
    // the blocks that were there come back after the read (an earlier write's record wins)
    std::string wasL = ctx.blockAt(left), wasR = ctx.blockAt(right);
    if (auto prev = db::get(kTfPersistKey)) {
        wasL = prev->value("wasLeft", wasL);
        wasR = prev->value("wasRight", wasR);
    }
    auto const* l = gf::game::blockWithStates(kOakChest, {{kType, "left"}, {kFacing, "north"}});
    auto const* r = gf::game::blockWithStates(kOakChest, {{kType, "right"}, {kFacing, "north"}});
    ctx.require(l && r, "the twilight oak chest halves exist");
    auto place = [&](GlobalPos const& at, Block const& b) {
        auto bp = gf::game::blockPos(at);
        return &region->getBlock(bp) == &b || gf::game::setBlock(*region, bp, b); // an earlier write's chest stays
    };
    ctx.require(place(left, *l) && place(right, *r), "a double chest underground");
    auto hl = inv::find(*region, gf::game::blockPos(left));
    auto hr = inv::find(*region, gf::game::blockPos(right));
    ctx.require(hl && hr, "both halves are containers");
    std::random_device rd;
    int                n = 2 + static_cast<int>(rd() % 60), m = 2 + static_cast<int>(rd() % 60);
    hl->set(left, 13, *items::make("minecraft:gold_ingot", n));
    hr->set(right, 2, *items::make("minecraft:emerald", m));
    db::put(kTfPersistKey, {{"left", posJson(left)}, {"right", posJson(right)}, {"gold", n}, {"emeralds", m}, {"wasLeft", wasL}, {"wasRight", wasR}});
    ctx.info("tf chest marker written gold=" + std::to_string(n) + " emeralds=" + std::to_string(m));
});

GF_REGISTER_SELFTEST("combat.tf_chest_persist_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto marker = db::get(kTfPersistKey);
    if (!marker) {
        ctx.info("no tf chest marker (combat.tf_chest_persist_write did not run before): nothing to check");
        co_return;
    }
    db::erase(kTfPersistKey);
    auto left  = posOf((*marker)["left"]);
    auto right = posOf((*marker)["right"]);
    int  n = marker->value("gold", -1), m = marker->value("emeralds", -1);
    ctx.require(co_await ctx.ensureLoaded(left), "marker area loaded");
    auto* region = ctx.region();
    auto  hl     = inv::find(*region, gf::game::blockPos(left));
    auto  hr     = inv::find(*region, gf::game::blockPos(right));
    ctx.check(hl && hr, "both halves survived the reload");
    if (hl && hr) {
        auto gold = hl->get(left, 13), em = hr->get(right, 2);
        bool ok   = items::idOf(gold) == "minecraft:gold_ingot" && items::countOf(gold) == n && items::idOf(em) == "minecraft:emerald" && items::countOf(em) == m;
        ctx.check(ok, "each half kept its slots (" + std::to_string(items::countOf(gold)) + " gold, " + std::to_string(items::countOf(em)) + " emeralds)");
        auto parts = inv::partsOf(left);
        ctx.check(parts.size() == 2 && parts[0] == right, "the halves still pair");
        if (ok) ctx.info("tf chest marker verified gold=" + std::to_string(n) + " emeralds=" + std::to_string(m));
    }
    // the blocks that were there come back (the slots go without dropping)
    for (auto const& [pos, key] : {std::pair{left, "wasLeft"}, std::pair{right, "wasRight"}}) {
        (void)gf::blockdata::take(pos);
        auto was = marker->value(key, std::string{});
        if (auto const* original = was.empty() ? nullptr : gf::game::blockWith(was)) gf::game::setBlock(*region, gf::game::blockPos(pos), *original);
        else gf::game::clearBlock(*region, gf::game::blockPos(pos));
    }
    (void)takeItemEntitiesNear(left, 4);
});

// ---- tag matchers through F9's gf::tags ---------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.damage_type_tags", [](Context& ctx) {
    namespace tags = gf::tags;
    namespace c    = gf::combat;
    using R        = tags::Registry;
    // TF's generated damage type tags (DamageTypeTagGenerator): the data F9 loads
    struct Row {
        char const* tag;
        char const* member;
    };
    static constexpr Row rows[] = {
        {"minecraft:is_fire", "twilightforest:hydra_fire"},          {"minecraft:is_fire", "twilightforest:scorched"},
        {"minecraft:bypasses_armor", "twilightforest:lich_bolt"},     {"minecraft:bypasses_invulnerability", "twilightforest:expired"},
        {"minecraft:is_projectile", "twilightforest:thrown_axe"},     {"minecraft:is_explosion", "twilightforest:lich_bomb"},
        {"minecraft:is_fall", "twilightforest:yeeted"},               {"minecraft:no_knockback", "twilightforest:clamped"},
        {"neoforge:is_magic", "twilightforest:chilling_breath"},      {"neoforge:is_physical", "twilightforest:hydra_bite"},
        {"twilightforest:breaks_lich_shields", "twilightforest:twilight_scepter"}, {"twilightforest:breaks_lich_shields", "minecraft:magic"},
    };
    for (auto const& r : rows) {
        ctx.check(tags::has(R::DamageType, r.tag, r.member), std::string("gf::tags: ") + r.tag + " holds " + r.member);
        ctx.check(c::damageTypeHasTag(r.member, r.tag), std::string("the combat contract reads it: ") + r.member + " in " + r.tag);
    }
    auto fire = tags::members(R::DamageType, "minecraft:is_fire");
    ctx.check(std::find(fire.begin(), fire.end(), "twilightforest:hydra_mortar") != fire.end(), "members() lists TF's fire types (" + std::to_string(fire.size()) + ")");
    ctx.check(!c::damageTypeHasTag("twilightforest:hydra_bite", "minecraft:is_fire") && !c::damageTypeHasTag("twilightforest:lich_bolt", "minecraft:is_fall"), "non-members are not");
    // vanilla's own members (Mojang's data, no Bedrock data) stay answered
    ctx.check(c::damageTypeHasTag("minecraft:in_fire", "minecraft:is_fire") && c::damageTypeHasTag("minecraft:arrow", "minecraft:is_projectile"), "vanilla's own members");
    // what the tags decide (gf::combat::hurt's cause)
    using Cause = SharedTypes::Legacy::ActorDamageCause;
    ctx.check(c::causeFor("twilightforest:fire_jet", true) == Cause::Fire && c::causeFor("twilightforest:lich_bomb", true) == Cause::Magic, "the causes follow the data");
});

GF_REGISTER_SELFTEST("combat.loot_tags", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    namespace tags = gf::tags;
    // a tag entry: every member of TF's item tag (twilightforest:portal/activator = #c:gems/diamond)
    auto activators = tags::members(tags::Registry::Item, "twilightforest:portal/activator");
    ctx.require(std::find(activators.begin(), activators.end(), "minecraft:diamond") != activators.end(),
                "F9 resolves twilightforest:portal/activator through #c:gems/diamond (" + std::to_string(activators.size()) + " members)");
    loot::registerTable("gf_test:combat/tag_entry", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:tag","name":"twilightforest:portal/activator","expand":false}]}]})"));
    loot::LootContext lc;
    lc.seed  = 3;
    auto got = loot::roll("gf_test:combat/tag_entry", lc);
    loot::registerTable("gf_test:combat/tag_entry", nullptr);
    std::set<std::string> rolled;
    for (auto const& s : got) rolled.insert(items::idOf(s));
    ctx.check(rolled.contains("minecraft:diamond") && rolled.size() == got.size(), "a tag entry (expand false) yields one of each member");
    // match_tool with an item tag (#minecraft:axes, F9's vanilla bridge)
    loot::registerTable("gf_test:combat/axe_only", JsonT::parse(R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:match_tool","predicate":{"items":"#minecraft:axes"}}],
        "entries":[{"type":"minecraft:item","name":"minecraft:oak_log"}]}]})"));
    auto axe   = items::make("minecraft:iron_axe", 1);
    auto stick = items::make("minecraft:stick", 1);
    ctx.require(axe && stick, "an iron axe and a stick");
    lc.tool      = &*axe;
    auto withAxe = loot::roll("gf_test:combat/axe_only", lc);
    lc.tool      = &*stick;
    auto withStick = loot::roll("gf_test:combat/axe_only", lc);
    loot::registerTable("gf_test:combat/axe_only", nullptr);
    ctx.check(countIn(withAxe, "minecraft:oak_log") == 1 && withStick.empty(), "match_tool #minecraft:axes takes the axe, not the stick");
    // entity type tags, nested vanilla tags too
    ctx.check(tags::has(tags::Registry::EntityType, "twilightforest:lifedrain_drops_no_flesh", "minecraft:snow_golem"), "lifedrain_drops_no_flesh holds the snow golem");
    ctx.check(tags::has(tags::Registry::EntityType, "twilightforest:lifedrain_drops_no_flesh", "minecraft:skeleton"), "and the skeleton through #minecraft:skeletons");
    auto pos = ctx.testPos(6042);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    Vec3  at{pos.x + .5f, static_cast<float>(pos.y), pos.z + .5f};
    auto* golem = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:snow_golem"), nullptr, at, false, false, false);
    auto* cow   = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    struct Remove {
        std::vector<Actor*> actors;
        ~Remove() {
            for (auto* a : actors)
                if (a) a->remove();
        }
    } remove{{golem, cow}};
    ctx.require(golem && cow, "a snow golem and a cow");
    auto noFlesh = JsonT::parse(R"({"type": "#twilightforest:lifedrain_drops_no_flesh"})");
    ctx.check(loot::entityMatches(golem, noFlesh) && !loot::entityMatches(cow, noFlesh), "an entity predicate with TF's entity type tag");
    co_return;
});

// ---- MissingAdvancementToast ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.missing_advancement", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace pr = gf::progression;
    // TF: the advancement's display title and icon (DisplayInfo), else no_title and the portal miniature
    auto lich = pr::missingToastFor("twilightforest:progress_lich");
    ctx.check(lich.titleIsKey && lich.title == "advancement.twilightforest.kill_lich", "progress_lich's title (" + lich.title + ")");
    ctx.check(lich.icon == "twilightforest:lich_tower_miniature_structure", "progress_lich's icon (" + lich.icon + ")");
    auto none = pr::missingToastFor("gf_test:no_such_advancement");
    ctx.check(none.title == pr::kMissingNoTitle && none.titleIsKey && none.icon == pr::kMissingIcon, "no display: twilightforest.ui.advancement.no_title with the portal miniature");
    ctx.check(items::make(lich.icon).has_value() && items::make(pr::kMissingIcon).has_value(), "both icons are items the client can draw");
    auto pos = ctx.testPos(6041);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ctx.check(gf::trFor(p, pr::kMissingUpperText, "?") == "Advancement Required:", "UPPER_TEXT reads TF's text (" + gf::trFor(p, pr::kMissingUpperText, "?") + ")");
    {
        CompanionScope companion(p);
        (void)gf::net::takeTestMessages(p);
        pr::showMissing(p, "twilightforest:progress_lich");
        JsonT sent;
        for (auto const& m : gf::net::takeTestMessages(p))
            if (m.channel == "combat.missing_advancement") sent = m.payload;
        ctx.check(sent.value("advancement", std::string{}) == "twilightforest:progress_lich" && sent.value("title", std::string{}) == lich.title
                      && sent.value("titleIsKey", false) && sent.value("icon", std::string{}) == lich.icon,
                  "a client mod user gets the toast's title and icon (" + sent.dump() + ")");
    }
    // a vanilla player gets the game's toast (ToastRequestPacket): nothing on the companion channel
    pr::showMissing(ctx.player(), "twilightforest:progress_lich");
    ctx.check(gf::net::takeTestMessages(ctx.player()).empty(), "a vanilla player gets no companion message");
});

// ---- client build: the toast F12 shows, fx particles through F12's native sink ----------------------------

#ifdef GF_CLIENT
namespace {
namespace cl = gf::client;

/// The local player on the server side (a companion: the client mod's handshake), 0 when none.
std::int64_t localCompanion() {
    std::int64_t id    = 0;
    auto         level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&id](Player& p) {
        if (p.isSimulated() || !gf::net::isCompanion(p)) return true;
        id = p.getOrCreateUniqueID().rawID;
        return false;
    });
    return id;
}
} // namespace

// The server's showMissing reaches the client mod (combat.missing_advancement) and F12 queues TF's
// MissingAdvancementToast: white UPPER_TEXT, white title (cancelled again: nothing stays on screen).
GF_REGISTER_SELFTEST("combat.client_missing_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace pr = gf::progression;
    auto who     = localCompanion();
    if (who == 0) {
        ctx.info("no companion player in this world: skipped");
        co_return;
    }
    (void)cl::toast::cancel(pr::kMissingUpperText);
    {
        auto* p = playerById(who);
        ctx.require(p != nullptr, "the companion player");
        pr::showMissing(*p, "twilightforest:progress_lich");
    }
    std::optional<cl::toast::Style> style;
    for (int i = 0; i < 60 && !style; ++i) {
        co_await ctx.ticks(1);
        style = cl::toast::styleOfToast(pr::kMissingUpperText);
    }
    ctx.require(style.has_value(), "the client mod queued the missing advancement toast");
    auto want = cl::toast::styleOf(cl::toast::Kind::MissingAdvancement);
    ctx.check(style->titleColor == want.titleColor && style->bodyColor == want.bodyColor && style->titleColor == 0xFFFFFFFFu,
              "MissingAdvancementToast's colours: white UPPER_TEXT, white title");
    ctx.check(cl::toast::cancel(pr::kMissingUpperText) >= 1, "the toast is taken off again");
});

// gf::fx::particle for a companion: a particle with a native factory goes to F12's sink (spawnFromFx),
// a TF particle without one spawns its resource pack effect, a vanilla particle type goes as the
// game's SpawnParticleEffectPacket (not over fx.particle).
GF_REGISTER_SELFTEST("combat.client_fx_native", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fxc = gf::fx::client;
    auto who      = localCompanion();
    if (who == 0) {
        ctx.info("no companion player in this world: skipped");
        co_return;
    }
    ctx.check(fxc::hasNativeSink(), "F12 installed its native particle sink");
    constexpr char kId[] = "gf_test:combat_fx_native";
    static int     sRuns = 0;
    sRuns                = 0;
    cl::particles::registerNative(kId, [](Vec3 const&, Vec3 const& vel, JsonT const& params) {
        if (params.value("combat", 0) == 7 && vel.y > 0.4f) ++sRuns;
    });
    struct Unregister {
        ~Unregister() { gf::client::particles::unregisterNative("gf_test:combat_fx_native"); }
    } unregister;
    auto at = [who]() -> std::optional<std::pair<Vec3, Dimension*>> {
        auto* p = playerById(who);
        if (!p) return std::nullopt;
        return std::pair{p->getPosition() + Vec3{0, 1, 0}, &p->getDimension()};
    };
    auto before = fxc::clientCounters();
    {
        auto w = at();
        ctx.require(w.has_value(), "the companion player");
        gf::fx::particle(*w->second, kId, w->first, Vec3{0, 0.5f, 0}, 2, JsonT{{"combat", 7}});
    }
    for (int i = 0; i < 60 && fxc::clientCounters().native == before.native; ++i) co_await ctx.ticks(1);
    ctx.check(fxc::clientCounters().native == before.native + 1, "fx.particle went to the native sink");
    ctx.check(sRuns == 2, "F12's factory ran for both particles with the call's velocity and params (" + std::to_string(sRuns) + ")");
    // a TF particle id without a factory: its resource pack effect (vanilla clients see the same)
    std::string tfId;
    bool        hasFactory = false;
    for (auto const& [id, factory] : cl::particles::declared()) {
        if (id == "twilightforest:large_flame") {
            tfId       = id;
            hasFactory = factory;
        }
    }
    ctx.require(!tfId.empty(), "twilightforest:large_flame is one of TF's declared particles");
    ctx.require(!gf::fx::resolveParticle(tfId).effect.empty(), "the resource pack has its effect");
    before = fxc::clientCounters();
    {
        auto w = at();
        ctx.require(w.has_value(), "the companion player");
        gf::fx::particle(*w->second, tfId, w->first, {}, 3, JsonT{{"spread", {0.3, 0.3, 0.3}}});
    }
    for (int i = 0; i < 60 && fxc::clientCounters().native == before.native && fxc::clientCounters().effects == before.effects; ++i) co_await ctx.ticks(1);
    auto after = fxc::clientCounters();
    if (hasFactory) ctx.check(after.native == before.native + 1, "a TF particle with a native factory is drawn natively");
    else ctx.check(after.effects == before.effects + 1 && after.native == before.native, "a TF particle without a factory spawned its resource pack effect");
    // a vanilla particle type: the game's packet, not fx.particle
    before   = fxc::clientCounters();
    auto out = gf::fx::counters().particlePackets;
    {
        auto w = at();
        ctx.require(w.has_value(), "the companion player");
        gf::fx::particle(*w->second, "minecraft:flame", w->first, {}, 2);
    }
    co_await ctx.ticks(10);
    after = fxc::clientCounters();
    ctx.check(gf::fx::counters().particlePackets > out && after.native == before.native && after.effects == before.effects && after.silent == before.silent,
              "a vanilla particle type goes as the game's particle packet");
});
#endif
