// Block families (F11): selftests of the chests' native containers (testPos dx 6100..6199): TF's
// chests keep their slots in the block actor of minecraft:block_entity (F10's native backend), so
// hoppers, hopper minecarts, droppers and crafters fill and empty them as Java's do (Hoppers.cpp: a
// double chest is one CompoundContainer, the right half first, the blocked rule ignored),
// comparators read a single chest and the double inventory (0 while a half is blocked), client mod
// users' open chest screens show what a hopper, another player or code changed, the stacks survive
// a server restart, and a chest whose stacks were kept in block data (no block actor) moves them
// into its container (Chests.cpp migrateChest: in its chunk's or a neighbouring chunk's first tick,
// before a hopper reaches it, or the tick after a player opens it) and loses none when broken before
// that.

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/CrafterBlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/ext/combat/InventoryChanges.h"
#include "feature/ext/combat/InventoryMigrate.h"
#include "feature/ext/families/Hoppers.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace fam   = gf::feature::families;
namespace inv   = gf::inventory;
namespace items = gf::game::items;
using JsonT     = nlohmann::json; ///< (the game's headers have a ::Json namespace)

constexpr char kChest[]        = "twilightforest:canopy_chest";
constexpr char kTrappedChest[] = "twilightforest:canopy_trapped_chest";
constexpr char kFacingState[]  = "twilightforest:facing";
constexpr char kTypeState[]    = "twilightforest:type";

GlobalPos at(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
BlockPos    bp(GlobalPos const& p) { return gf::game::blockPos(p); }
std::string where(GlobalPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

/// This process (a world reload restarts the server).
std::int64_t sessionId() {
    static std::int64_t const id = static_cast<std::int64_t>(std::random_device{}()) << 20 | static_cast<std::int64_t>(std::random_device{}() & 0xFFFFF);
    return id;
}

/// TF's chests are in this build (F1's skeleton through tools/families).
bool tfChests(Context& ctx) {
    if (fam::entryOf(kChest) && gf::game::blockWith(kChest) && gf::game::blockWith(kTrappedChest)) return true;
    ctx.info("no TF chests in this build: skipped");
    return false;
}

/// A vanilla hopper's or dropper's container at `p` (nullptr: none).
Container* vanillaContainer(BlockSource& region, GlobalPos const& p) {
    auto* a = region.getBlockEntity(bp(p));
    if (!a) return nullptr;
    if (a->getType() == BlockActorType::Hopper) return static_cast<Container*>(static_cast<HopperBlockActor*>(a));
    if (a->getType() == BlockActorType::Dropper) return static_cast<Container*>(static_cast<DropperBlockActor*>(a));
    if (a->getType() == BlockActorType::Crafter) return static_cast<Container*>(static_cast<CrafterBlockActor*>(a));
    return nullptr;
}

/// How many items of `id` a container holds.
int countIn(Container const* c, std::string const& id) {
    int n = 0;
    for (int i = 0; c && i < c->getContainerSize(); ++i)
        if (auto const& s = c->getItem(i); !items::isEmpty(s) && items::idOf(s) == id) n += items::countOf(s);
    return n;
}
int countIn(std::vector<ItemStack> const& slots, std::string const& id) {
    int n = 0;
    for (auto const& s : slots)
        if (!items::isEmpty(s) && items::idOf(s) == id) n += items::countOf(s);
    return n;
}
bool emptySlots(std::vector<ItemStack> const& slots) {
    return std::all_of(slots.begin(), slots.end(), [](ItemStack const& s) { return items::isEmpty(s); });
}
std::string slotId(std::vector<ItemStack> const& slots, std::size_t i) { return i < slots.size() && !items::isEmpty(slots[i]) ? items::idOf(slots[i]) : std::string("empty"); }

ItemStack stack(std::string const& id, int n) { return items::make(id, n).value_or(ItemStack{}); }

/// Places a chest of `id` without ctx.setBlock (kept after the test for a reader).
bool placeKept(BlockSource& region, GlobalPos const& p, std::string const& id, JsonT const& states) {
    auto const* b = gf::game::blockWithStates(id, states);
    return b && gf::game::setBlock(region, bp(p), *b);
}

/// Empties a container block (native slots and block data stacks) and clears it, so nothing drops.
void emptyAndClear(BlockSource& region, GlobalPos const& p) {
    if (auto* c = inv::nativeContainer(region, bp(p))) {
        (void)gf::inventory::ext::migrateBlockDataToNative(p, true); // (stacks still waiting in block data)
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    } else if (auto h = inv::find(region, bp(p))) {
        for (int i = 0; i < h->slots; ++i) h->set(p, i, ItemStack{}); // (F10's BlockData backend)
    }
    if (auto* c = vanillaContainer(region, p))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    gf::game::clearBlock(region, bp(p));
}

/// Whether stacks of the chest at `p` wait in block data: F10's BlockData backend without a block
/// actor, else F10's waiting stacks (feature/ext/combat/InventoryMigrate.h).
bool waitingInBlockData(BlockSource& region, GlobalPos const& p) {
    if (inv::nativeContainer(region, bp(p))) return gf::inventory::ext::hasBlockDataStacks(p);
    return region.getBlockEntity(bp(p)) == nullptr && !emptySlots(inv::contents(p));
}

/// Waits up to `ticks` ticks for `done`.
ll::coro::CoroTask<bool> until(Context& ctx, int ticks, std::function<bool()> done) {
    for (int i = 0; i < ticks; ++i) {
        if (done()) co_return true;
        co_await ctx.ticks(1);
    }
    co_return done();
}

/// A comparator at `comp` (its rear towards the container north of it) and the block it powers at
/// `out`, each on a stone.
bool placeComparator(Context& ctx, GlobalPos const& comp, GlobalPos const& out) {
    if (!ctx.setBlock(at(comp, 0, -1, 0), "minecraft:stone") || !ctx.setBlock(at(out, 0, -1, 0), "minecraft:stone")) return false;
    bool placed = ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", "north"}})
               || ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"direction", 2}});
    return placed && ctx.setBlock(out, "minecraft:stone");
}

/// Puts `s` in slot `slot` of the chest at `p` the way F10's BlockData backend keeps it (through the
/// inventory contract's own handler while the chest has no block actor), leaving it without a block
/// actor: a chest saved before the chests had block actors.
bool stacksWithoutActor(BlockSource& region, GlobalPos const& p, int slot, ItemStack const& s) {
    auto* chunk = region.getChunkAt(bp(p));
    if (!chunk) return false;
    (void)chunk->removeBlockEntity(bp(p));
    auto h = inv::find(region, bp(p));
    if (region.getBlockEntity(bp(p)) || !h) return false;
    h->set(p, slot, s);
    auto shown = inv::contents(p);
    return !region.getBlockEntity(bp(p)) && static_cast<int>(shown.size()) > slot && items::idOf(shown[static_cast<std::size_t>(slot)]) == items::idOf(s)
        && items::countOf(shown[static_cast<std::size_t>(slot)]) == items::countOf(s);
}

/// stacksWithoutActor, then the chest gets an empty block actor again: a chest saved before the
/// chests had block actors, after the reload made its block actor and before its stacks moved.
bool stacksInBlockData(BlockSource& region, GlobalPos const& p, int slot, ItemStack const& s) {
    if (!stacksWithoutActor(region, p, slot, s)) return false;
    auto* chunk = region.getChunkAt(bp(p));
    if (!chunk) return false;
    auto actor = VanillaBlockActorFactory::createBlockActor(bp(p), region.getBlock(bp(p)).getBlockType());
    if (!actor || actor->getType() != BlockActorType::DataDriven) return false;
    chunk->_placeBlockEntity(actor);
    auto* c = inv::nativeContainer(region, bp(p));
    return c != nullptr && countIn(c, items::idOf(s)) == 0 && gf::inventory::ext::hasBlockDataStacks(p);
}

/// Forgets the waiting loads of the chunks of `ps` (a chunk the test area loaded may still be on the
/// loaded-chunk retry, which would give a chest the test left without a block actor one meanwhile).
void forgetLoads(std::initializer_list<GlobalPos> ps) {
    for (auto const& p : ps) fam::detail::forgetLoadedChunk(p.dim, bp(p).x >> 4, bp(p).z >> 4);
}

/// A player of the test by its unique id (nullptr: gone).
SimulatedPlayer* simPlayerById(Context& ctx, std::int64_t uid) {
    auto* region = ctx.region();
    if (!region) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return static_cast<SimulatedPlayer*>(region->getLevel().getPlayer(id));
}

/// The test player `uid` (the test's own player by default) stands south of `p` looking at it with
/// an empty hand, then right-clicks its top as the network handler runs a click
/// (GameMode::useItemOn, a first event).
ll::coro::CoroTask<> clickChestAs(Context& ctx, std::int64_t uid, GlobalPos p) {
    if (auto* sp = simPlayerById(ctx, uid)) {
        sp->teleport(Vec3{p.x + .5f, static_cast<float>(p.y), p.z + 2.5f}, sp->getDimensionId());
        sp->simulateLookAt(bp(p), sim::LookDuration::Instant);
        sp->simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(8); // the look applies on the player's next tick; Bedrock drops a use-on right after another
    if (auto* sp = simPlayerById(ctx, uid)) {
        auto  b    = bp(p);
        auto& held = const_cast<ItemStack&>(sp->getSelectedItem());
        sp->mGameMode->useItemOn(held, b, 1, Vec3{b.x + .5f, b.y + 1.f, b.z + .5f}, HandSlot::Mainhand, nullptr, true);
    }
    co_await ctx.ticks(1);
}
ll::coro::CoroTask<> clickChest(Context& ctx, GlobalPos p) { co_await clickChestAs(ctx, ctx.player().getOrCreateUniqueID().rawID, p); }

/// The items of `id` lying in the world within 3 blocks of `p`.
int droppedNear(Context& ctx, GlobalPos const& p, std::string const& id) {
    auto* region = ctx.region();
    if (!region) return 0;
    AABB box{Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f}, Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}};
    int  n = 0;
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item")
            if (auto const& s = static_cast<ItemActor*>(a)->item(); !items::isEmpty(s) && items::idOf(s) == id) n += items::countOf(s);
    return n;
}
void clearDropsNear(Context& ctx, GlobalPos const& p) {
    auto* region = ctx.region();
    if (!region) return;
    AABB box{Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f}, Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") a->remove();
}

/// The client mod's messages of the inventory contract's chest screen sent to the player since the
/// last call: the last payload of each channel ("screen.open", "screen.update", "screen.close").
std::map<std::string, JsonT> chestScreenMessages(Player& player) {
    std::map<std::string, JsonT> out;
    for (auto const& m : gf::net::takeTestMessages(player))
        if (m.channel.rfind("screen.", 0) == 0 && m.payload.value("type", std::string{}) == "inventory.chest") out[m.channel] = m.payload;
    return out;
}

/// The stack a chest screen message's data shows in `slot` ("empty x0" for none).
std::string shownSlot(JsonT const& payload, std::size_t slot) {
    auto shown = payload.value("data", JsonT::object()).value("items", JsonT::array());
    if (slot >= shown.size() || shown[slot].is_null()) return "empty x0";
    auto s = items::fromJson(shown[slot]);
    return items::isEmpty(s) ? "empty x0" : items::idOf(s) + " x" + std::to_string(items::countOf(s));
}

/// The player counts as a client mod user while it lives; its chest screen is closed at the end.
struct CompanionScope {
    Context& ctx;
    explicit CompanionScope(Context& c) : ctx(c) {
        gf::net::setTestCompanion(ctx.player(), true);
        (void)gf::net::takeTestMessages(ctx.player());
    }
    ~CompanionScope() {
        gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
        (void)gf::net::takeTestMessages(ctx.player());
        gf::net::setTestCompanion(ctx.player(), false);
    }
    CompanionScope(CompanionScope const&)            = delete;
    CompanionScope& operator=(CompanionScope const&) = delete;
};

/// A block's hopper visibility set for a test, back to the default when it ends.
struct VisibilityScope {
    std::string id;
    VisibilityScope(std::string blockId, bool visible) : id(std::move(blockId)) { gf::families_ext::setHopperVisible(id, visible); }
    ~VisibilityScope() { gf::families_ext::resetHopperVisible(id); }
    VisibilityScope(VisibilityScope const&)            = delete;
    VisibilityScope& operator=(VisibilityScope const&) = delete;
};

} // namespace

GF_REGISTER_SELFTEST("families.chest_hoppers", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java: TF's chests are ChestBlock / ChestBlockEntity, so hoppers fill them from above and from
    // the sides and empty them from below (HopperBlockEntity#getContainerAt); a double chest is one
    // CompoundContainer of 54 slots, the RIGHT half first (a hopper on the left half fills the right
    // half's first slot, one under the left half takes from the right half); a comparator reads the
    // double inventory (ChestBlock#getAnalogOutputSignal: getRedstoneSignalFromContainer of both)
    // and a single chest's own; while a half is blocked (a redstone conductor on it) the comparator
    // reads 0 (getContainer(.., false) finds none) but hoppers still reach it (getContainer(.., true))
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6145, 5);
    auto p   = ctx.testPos(6145, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    // ---- a single chest, raised a block: a hopper on it, one beside it, then one under it ----
    auto s = at(p, 1, 1, 1);
    ctx.require(ctx.setBlock(s, kChest, {{kFacingState, "north"}}), "a canopy chest");
    co_await ctx.ticks(1);
    auto* own = inv::nativeContainer(region, bp(s));
    ctx.require(own != nullptr, "the TF chest keeps its slots in its block actor (minecraft:block_entity: F10's native backend)");
    ctx.check(own->getContainerSize() == 27, "27 slots (" + std::to_string(own->getContainerSize()) + ")");
    if (auto* seen = fam::hopperContainerAt(region, bp(s)); seen) {
        seen->setItem(4, stack("minecraft:flint", 1));
        bool same = seen->getContainerSize() == 27 && items::idOf(own->getItem(4)) == "minecraft:flint";
        seen->setItem(4, ItemStack{});
        ctx.check(same && items::isEmpty(own->getItem(4)), "a hopper sees the chest's own 27 slots (a write reaches its block actor)");
    } else {
        ctx.check(false, "a hopper sees the chest's own container");
    }
    ctx.check(gf::families_ext::hopperVisible(kChest) && gf::families_ext::hopperVisible(kTrappedChest) && !gf::families_ext::hopperVisible("minecraft:stone"),
              "hoppers reach the chest-like declarations (TF's chests), nothing else by default (feature/ext/families/Hoppers.h)");
    // a comparator south of the chest (its rear towards it), a block in front of it
    auto sComp = at(s, 0, 0, 1), sOut = at(s, 0, 0, 2);
    ctx.require(placeComparator(ctx, sComp, sOut), "a comparator behind the single chest and a block in front of it");
    auto sOutput = [&] { return gf::redstone::signalFrom(region, bp(sComp), bp(sOut)); };
    co_await ctx.ticks(10);
    ctx.check(sOutput() == 0, "an empty single chest gives the comparator 0 (" + std::to_string(sOutput()) + ")");
    auto top = at(s, 0, 1, 0), side = at(s, 1, 0, 0), under = at(s, 0, -1, 0);
    ctx.require(ctx.setBlock(top, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on the chest, facing down");
    ctx.require(ctx.setBlock(side, "minecraft:hopper", {{"facing_direction", 4}}), "a hopper east of it, facing west into it");
    co_await ctx.ticks(1);
    auto* ht = vanillaContainer(region, top);
    auto* hs = vanillaContainer(region, side);
    ctx.require(ht && hs, "the hoppers' containers");
    ht->setItem(0, stack("minecraft:dirt", 2));
    hs->setItem(0, stack("minecraft:cobblestone", 2));
    bool in = co_await until(ctx, 100, [&] {
        auto c = inv::contents(s);
        return countIn(c, "minecraft:dirt") == 2 && countIn(c, "minecraft:cobblestone") == 2;
    });
    auto c = inv::contents(s);
    ctx.check(in, "the hopper on top and the one at the side put their items in (" + std::to_string(countIn(c, "minecraft:dirt")) + " dirt, " + std::to_string(countIn(c, "minecraft:cobblestone")) + " cobblestone)");
    ht = vanillaContainer(region, top);
    hs = vanillaContainer(region, side);
    ctx.check(countIn(ht, "minecraft:dirt") == 0 && countIn(hs, "minecraft:cobblestone") == 0, "and are empty");
    // the single chest's comparator: 4 items of 64 in 27 slots, then 27 full stacks (Mth.lerpDiscrete)
    co_await ctx.ticks(10);
    ctx.check(sOutput() == 1 && inv::comparatorSignal(s).value_or(-1) == 1, "the single chest's comparator reads 1 from the hoppers' items (" + std::to_string(sOutput()) + ")");
    {
        auto hsg  = inv::find(region, bp(s));
        ctx.require(hsg.has_value(), "the single chest's handler");
        auto kept = inv::contents(s);
        for (int i = 0; i < 27; ++i) hsg->set(s, i, stack("minecraft:cobblestone", 64));
        co_await ctx.ticks(10);
        ctx.check(sOutput() == 15 && inv::comparatorSignal(s).value_or(-1) == 15, "a full single chest gives 15 (" + std::to_string(sOutput()) + ")");
        for (int i = 0; i < 27; ++i) hsg->set(s, i, i < static_cast<int>(kept.size()) ? kept[static_cast<std::size_t>(i)] : ItemStack{});
    }
    for (auto const& q : {sComp, sOut}) emptyAndClear(region, q);
    // the hopper under the chest pulls its stacks out, one item per transfer
    ctx.require(ctx.setBlock(under, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper under the chest");
    bool out = co_await until(ctx, 140, [&] { return emptySlots(inv::contents(s)); });
    auto* hu = vanillaContainer(region, under);
    ctx.check(out && countIn(hu, "minecraft:dirt") == 2 && countIn(hu, "minecraft:cobblestone") == 2,
              "the hopper under it took everything out (" + std::to_string(countIn(hu, "minecraft:dirt")) + " dirt, " + std::to_string(countIn(hu, "minecraft:cobblestone")) + " cobblestone)");
    for (auto const& q : {top, side, under, s}) emptyAndClear(region, q);
    co_await ctx.ticks(2);

    // ---- a double chest facing north: the east half is the RIGHT (first) one ----
    auto a = at(p, 1, 1, 1), b = at(p, 2, 1, 1);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    auto parts = inv::partsOf(a);
    ctx.require(parts.size() == 2 && parts[0] == b && parts[1] == a, "the halves pair, the right half first (ChestBlock#combine)");
    auto* view = fam::hopperContainerAt(region, bp(a));
    ctx.check(view && view->getContainerSize() == 54 && view == fam::hopperContainerAt(region, bp(b)), "a hopper sees both halves as one 54-slot container from either half");
    // a comparator south of the right half (its rear towards the chest), a stone in front of it
    auto comp = at(b, 0, 0, 1), outStone = at(b, 0, 0, 2);
    ctx.require(ctx.setBlock(at(comp, 0, -1, 0), "minecraft:stone") && ctx.setBlock(at(outStone, 0, -1, 0), "minecraft:stone"), "ground under the comparator");
    bool comparator = ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", "north"}})
                   || ctx.setBlock(comp, "minecraft:unpowered_comparator", {{"direction", 2}});
    ctx.require(comparator && ctx.setBlock(outStone, "minecraft:stone"), "a comparator behind the right half and a block in front of it");
    co_await ctx.ticks(10);
    auto output = [&] { return gf::redstone::signalFrom(region, bp(comp), bp(outStone)); };
    ctx.check(output() == 0, "an empty double chest gives the comparator 0 (" + std::to_string(output()) + ")");
    // a hopper on the LEFT half and one at the left half's west side: the right half fills first
    auto topA = at(a, 0, 1, 0), westA = at(a, -1, 0, 0);
    ctx.require(ctx.setBlock(topA, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on the left half");
    ctx.require(ctx.setBlock(westA, "minecraft:hopper", {{"facing_direction", 5}}), "a hopper west of the left half, facing east into it");
    co_await ctx.ticks(1);
    ctx.require(vanillaContainer(region, topA) && vanillaContainer(region, westA), "their containers");
    vanillaContainer(region, topA)->setItem(0, stack("minecraft:gold_ingot", 1));
    vanillaContainer(region, westA)->setItem(0, stack("minecraft:iron_ingot", 1));
    bool both = co_await until(ctx, 60, [&] {
        auto r = inv::contents(b);
        return countIn(r, "minecraft:gold_ingot") == 1 && countIn(r, "minecraft:iron_ingot") == 1;
    });
    auto right = inv::contents(b), left = inv::contents(a);
    ctx.check(both && emptySlots(left), "the hoppers on and beside the LEFT half filled the RIGHT half first (CompoundContainer: " + slotId(right, 0) + ", " + slotId(right, 1) + "; left half " + (emptySlots(left) ? "empty" : "not empty") + ")");
    // the comparator reads the double inventory: 2 items of 64 in 54 slots
    co_await ctx.ticks(10);
    auto sa = inv::comparatorSignal(a).value_or(-1), sb = inv::comparatorSignal(b).value_or(-1);
    ctx.check(sa == 1 && sb == 1 && output() == 1, "the comparator reads 1 from the hoppers' items (halves " + std::to_string(sa) + ", " + std::to_string(sb) + ", output " + std::to_string(output()) + ")");
    // a stone on the right half blocks the chest (ChestBlock#isChestBlockedAt): the comparator reads
    // 0 (getContainer(.., false) finds no container), the hopper beside the left half still puts its
    // emerald in (getContainer(.., true))
    auto block = at(b, 0, 1, 0);
    ctx.require(ctx.setBlock(block, "minecraft:stone"), "a stone on the right half");
    co_await ctx.ticks(10);
    ctx.check(inv::chestBlocked(a) && output() == 0 && inv::comparatorSignal(a).value_or(-1) == 0 && inv::comparatorSignal(b).value_or(-1) == 0,
              "a blocked double chest gives the comparator 0 (output " + std::to_string(output()) + ")");
    ctx.require(vanillaContainer(region, westA) != nullptr, "the hopper beside the left half");
    vanillaContainer(region, westA)->setItem(0, stack("minecraft:emerald", 1));
    bool blockedIn = co_await until(ctx, 60, [&] { return countIn(inv::contents(b), "minecraft:emerald") == 1; });
    ctx.check(blockedIn && countIn(vanillaContainer(region, westA), "minecraft:emerald") == 0, "the hopper still put its emerald into the blocked chest (its right half)");
    co_await ctx.ticks(10);
    ctx.check(output() == 0, "and the comparator still reads 0 (" + std::to_string(output()) + ")");
    emptyAndClear(region, block);
    co_await ctx.ticks(10);
    ctx.check(!inv::chestBlocked(a) && output() == 1, "the stone gone, it reads 1 again from 3 items (" + std::to_string(output()) + ")");
    auto hb = inv::find(region, bp(b));
    ctx.require(hb.has_value(), "the right half's handler");
    for (int i = 0; i < 27; ++i) hb->set(b, i, stack("minecraft:cobblestone", 64));
    co_await ctx.ticks(10);
    ctx.check(output() == 8 && inv::comparatorSignal(a).value_or(-1) == 8, "27 full stacks in the 54 slots give 8 (Mth.lerpDiscrete: output " + std::to_string(output()) + ")");
    for (int i = 0; i < 27; ++i) hb->set(b, i, ItemStack{});
    // a hopper under the LEFT half takes from the RIGHT half first: a stack in each half (the left
    // half's first slot, the right half's sixth), and HopperBlockEntity#suckInItems tries the
    // CompoundContainer's slots in order (the right half's are 0..26, the left half's 27..53)
    for (auto const& q : {topA, westA}) emptyAndClear(region, q);
    auto ha = inv::find(region, bp(a));
    ctx.require(ha.has_value(), "the left half's handler");
    ha->set(a, 0, stack("minecraft:diamond", 1));
    hb->set(b, 5, stack("minecraft:emerald", 1));
    auto underA = at(a, 0, -1, 0);
    ctx.require(ctx.setBlock(underA, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper under the left half");
    bool first = co_await until(ctx, 60, [&] {
        auto* h = vanillaContainer(region, underA);
        return countIn(h, "minecraft:emerald") + countIn(h, "minecraft:diamond") >= 1;
    });
    auto* hu1         = vanillaContainer(region, underA);
    int   emeraldsIn1 = countIn(hu1, "minecraft:emerald"), diamondsIn1 = countIn(hu1, "minecraft:diamond");
    ctx.check(first && emeraldsIn1 == 1 && diamondsIn1 == 0 && countIn(inv::contents(a), "minecraft:diamond") == 1,
              "the hopper under the left half took the RIGHT half's emerald first, the left half's diamond still in it (" + std::to_string(emeraldsIn1) + " emerald, "
                  + std::to_string(diamondsIn1) + " diamond in the hopper)");
    bool took = co_await until(ctx, 60, [&] { return countIn(vanillaContainer(region, underA), "minecraft:diamond") == 1; });
    ctx.check(took && emptySlots(inv::contents(a)) && emptySlots(inv::contents(b)), "then the left half's diamond (both halves empty)");
    co_await ctx.ticks(10);
    ctx.check(output() == 0, "and the comparator reads 0 again (" + std::to_string(output()) + ")");
    for (auto const& q : {underA, comp, outStone, a, b}) emptyAndClear(region, q);
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_hoppers_more", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the trapped chest takes a hopper's items too; a dropper drops into the chest in front of it
    // (DropperBlock#dispenseFrom); a hopper's first look at an unopened chest rolls its loot table
    // (RandomizableContainer#unpackLootTable(null)); a hopper minecart under a chest empties it; a
    // block hidden from hoppers (feature/ext/families/Hoppers.h) gives them nothing
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    // a trapped chest with a hopper on it and a dropper east of it facing west
    auto t = at(p, 1, 1, 1), top = at(t, 0, 1, 0), dropper = at(t, 1, 0, 0);
    ctx.require(ctx.setBlock(t, kTrappedChest, {{kFacingState, "north"}}), "a canopy trapped chest");
    ctx.require(ctx.setBlock(top, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on it");
    ctx.require(ctx.setBlock(dropper, "minecraft:dropper", {{"facing_direction", 4}}), "a dropper east of it facing west");
    co_await ctx.ticks(1);
    ctx.require(inv::nativeContainer(region, bp(t)) != nullptr, "the trapped chest keeps its slots in its block actor");
    ctx.require(vanillaContainer(region, top) && vanillaContainer(region, dropper), "the hopper's and the dropper's containers");
    {
        // hidden from hoppers: no container, the hopper on it keeps its apple
        VisibilityScope hidden(kTrappedChest, false);
        ctx.check(!gf::families_ext::hopperVisible(kTrappedChest) && fam::hopperContainerAt(region, bp(t)) == nullptr, "a trapped chest hidden from hoppers gives them no container");
        vanillaContainer(region, top)->setItem(0, stack("minecraft:apple", 1));
        co_await ctx.ticks(24); // three of the hopper's 8-tick transfers
        ctx.check(countIn(inv::contents(t), "minecraft:apple") == 0 && countIn(vanillaContainer(region, top), "minecraft:apple") == 1, "and the hopper on it keeps its apple");
    }
    ctx.check(gf::families_ext::hopperVisible(kTrappedChest) && fam::hopperContainerAt(region, bp(t)) != nullptr, "back to the default, hoppers reach it again");
    vanillaContainer(region, dropper)->setItem(0, stack("minecraft:bread", 1));
    ctx.require(ctx.setBlock(at(dropper, 1, 0, 0), "minecraft:redstone_block"), "a redstone block powers the dropper");
    bool filled = co_await until(ctx, 60, [&] {
        auto in = inv::contents(t);
        return countIn(in, "minecraft:apple") == 1 && countIn(in, "minecraft:bread") == 1;
    });
    auto in = inv::contents(t);
    ctx.check(filled, "the hopper and the dropper filled the trapped chest (" + std::to_string(countIn(in, "minecraft:apple")) + " apple, " + std::to_string(countIn(in, "minecraft:bread")) + " bread)");
    ctx.check(countIn(vanillaContainer(region, dropper), "minecraft:bread") == 0, "the dropper dropped into the chest, not into the world");
    for (auto const& q : {at(dropper, 1, 0, 0), dropper, top, t}) emptyAndClear(region, q);
    // an unopened loot chest: the hopper under it unpacks the table
    auto l = at(p, 1, 1, 3), underL = at(l, 0, -1, 0);
    ctx.require(ctx.setBlock(l, kChest, {{kFacingState, "north"}}), "a loot chest");
    co_await ctx.ticks(1);
    auto hl = inv::find(region, bp(l));
    ctx.require(hl.has_value(), "the loot chest's handler");
    hl->setLootTable(l, "gf_test:chests/fixture_chest", 42);
    ctx.require(inv::pendingLoot(l).has_value(), "its table is pending");
    ctx.require(ctx.setBlock(underL, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper under it");
    bool unpacked = co_await until(ctx, 40, [&] { return !inv::pendingLoot(l).has_value(); });
    co_await ctx.ticks(20);
    auto* hu = vanillaContainer(region, underL);
    int   got = countIn(hu, "gf_test:fixture_item") + countIn(hu, "minecraft:stick") + countIn(hu, "minecraft:diamond");
    ctx.check(unpacked && got >= 1, "the hopper's look rolled the table and it took the loot (" + std::to_string(got) + " item(s) of gf_test:chests/fixture_chest)");
    for (auto const& q : {underL, l}) emptyAndClear(region, q);
    // a hopper minecart on a rail under a chest
    auto rail = at(p, 3, 0, 3), m = at(rail, 0, 1, 0);
    ctx.require(ctx.setBlock(rail, "minecraft:rail", {{"rail_direction", 0}}), "a rail");
    ctx.require(ctx.setBlock(m, kChest, {{kFacingState, "north"}}), "a chest over the rail");
    co_await ctx.ticks(1);
    auto hm = inv::find(region, bp(m));
    ctx.require(hm.has_value(), "the chest's handler");
    hm->set(m, 3, stack("minecraft:stick", 2));
    ActorUniqueID id;
    auto*         cart = CommandUtils::spawnEntityAt(region, Vec3{rail.x + .5f, static_cast<float>(rail.y), rail.z + .5f}, ActorDefinitionIdentifier("minecraft:hopper_minecart"), id, nullptr);
    ctx.require(cart != nullptr, "a hopper minecart on the rail");
    auto cartContainer = [&]() -> Container* {
        auto* level = ctx.region() ? &ctx.region()->getLevel() : nullptr;
        auto* e     = level ? level->fetchEntity(id, false) : nullptr;
        if (!e) return nullptr;
        auto comp = e->getEntityContext().tryGetComponent<ContainerComponent>();
        return comp ? static_cast<Container*>(&comp->mContainer.get()) : nullptr;
    };
    bool carted = co_await until(ctx, 100, [&] { return countIn(cartContainer(), "minecraft:stick") == 2; });
    ctx.check(carted && emptySlots(inv::contents(m)), "the hopper minecart emptied the chest above it (" + std::to_string(countIn(cartContainer(), "minecraft:stick")) + " sticks)");
    if (auto* level = ctx.region() ? &ctx.region()->getLevel() : nullptr)
        if (auto* e = level->fetchEntity(id, false)) {
            if (auto* cc = cartContainer())
                for (int i = 0; i < cc->getContainerSize(); ++i) cc->setItem(i, ItemStack{});
            e->remove();
        }
    for (auto const& q : {m, rail}) emptyAndClear(region, q);
    // a declaration changed later (inventory::declare, unregisterBlock) reaches the kept lookups in
    // the next tick: the fixture chest redeclared without ChestBlock's rules is out of reach
    if (auto original = inv::declared("gf_test:family_chest"); original && gf::game::blockWith("gf_test:family_chest")) {
        auto f = at(p, 4, 1, 1);
        ctx.require(ctx.setBlock(f, "gf_test:family_chest", {{kFacingState, "north"}}), "a fixture chest");
        co_await ctx.ticks(1);
        ctx.check(fam::hopperContainerAt(region, bp(f)) != nullptr, "hoppers reach the fixture chest (declared chest-like)");
        auto hidden  = *original;
        hidden.chest = false;
        inv::declare(hidden);
        co_await ctx.ticks(1);
        bool gone = fam::hopperContainerAt(region, bp(f)) == nullptr;
        inv::declare(*original);
        co_await ctx.ticks(1);
        bool back = fam::hopperContainerAt(region, bp(f)) != nullptr;
        ctx.check(gone && back, "redeclared without the chest rules it is out of their reach in the next tick, and back once declared again (" + std::string(gone ? "gone" : "still seen") + ", "
                                    + (back ? "back" : "not back") + ")");
        emptyAndClear(region, f);
    }
    (void)box;
});

// ---- persistence and the block data migration (the e2e scenario restarts the server between) ----------

namespace {
constexpr char kChestsMarker[] = "gf:families:selftest_chests";
}

GF_REGISTER_SELFTEST("families.chest_persist_a_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a double chest with stacks in both halves' block actors, and a chest as an earlier build left
    // it: no block actor, its stacks in F10's block data backend (families.chest_persist_b_read
    // reads them after a reload, or in the same session)
    if (!tfChests(ctx)) co_return;
    auto q = ctx.testPos(6128, 1);
    ctx.require(co_await ctx.ensureLoaded(q), "test area loaded");
    auto& region = *ctx.region();
    auto  left = q, right = at(q, 1, 0, 0), old = at(q, 0, 0, 2);
    // without ctx.setBlock (restored to air after the test): they stay for the reader; the flat
    // world's ground carries them
    ctx.require(placeKept(region, left, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(placeKept(region, right, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half");
    ctx.require(placeKept(region, old, kChest, {{kFacingState, "north"}}), "a third chest");
    co_await ctx.ticks(2);
    auto* cl = inv::nativeContainer(region, bp(left));
    auto* cr = inv::nativeContainer(region, bp(right));
    ctx.require(cl && cr && inv::nativeContainer(region, bp(old)), "each chest has its block actor's container");
    cr->setItem(0, stack("minecraft:diamond", 7));
    cl->setItem(26, stack("minecraft:emerald", 3));
    // the third chest as a world saved before the chests had block actors: none, and its stacks
    // in block data
    auto* chunk = region.getChunkAt(bp(old));
    ctx.require(chunk != nullptr, "the third chest's chunk");
    {
        auto removed = chunk->removeBlockEntity(bp(old));
        ctx.require(removed != nullptr && region.getBlockEntity(bp(old)) == nullptr, "its block actor removed");
    }
    // written by the inventory contract itself (its BlockData backend: what migrateChest moves)
    auto ho = inv::find(region, bp(old));
    ctx.require(ho.has_value(), "the third chest's handler");
    ho->set(old, 1, stack("minecraft:apple", 5));
    auto shown = inv::contents(old);
    ctx.check(countIn(shown, "minecraft:apple") == 5 && slotId(shown, 1) == "minecraft:apple", "without a block actor the chest shows its block data stacks (F10's BlockData backend)");
    gf::game::db::put(kChestsMarker, {{"session", sessionId()}});
    ctx.info("chests written at " + where(q));
    co_return;
});

GF_REGISTER_SELFTEST("families.chest_persist_b_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!tfChests(ctx)) co_return;
    auto q = ctx.testPos(6128, 1);
    ctx.require(co_await ctx.ensureLoaded(q), "test area loaded");
    auto& region = *ctx.region();
    auto  left = q, right = at(q, 1, 0, 0), old = at(q, 0, 0, 2);
    auto  marker = gf::game::db::get(kChestsMarker);
    gf::game::db::erase(kChestsMarker);
    bool written = marker && ctx.blockAt(left) == kChest && ctx.blockAt(right) == kChest && ctx.blockAt(old) == kChest;
    if (!written) {
        ctx.info("no chests written before: nothing to check");
        for (auto const& p : {left, right, old}) emptyAndClear(region, p);
        co_return;
    }
    bool reloaded = marker->value("session", std::int64_t{0}) != sessionId();
    if (reloaded) {
        // the chunk's first tick after the load moves the third chest's block data stacks into the
        // block actor (Chests.cpp settleChunk -> migrateChest)
        co_await until(ctx, 60, [&] { return inv::nativeContainer(region, bp(old)) && !waitingInBlockData(region, old); });
    } else {
        ctx.check(fam::migrateChest(region, bp(old)) == 1, "the third chest's block data stack moves into its container");
    }
    auto* cl = inv::nativeContainer(region, bp(left));
    auto* cr = inv::nativeContainer(region, bp(right));
    ctx.check(cr && countIn(cr, "minecraft:diamond") == 7 && items::idOf(cr->getItem(0)) == "minecraft:diamond", "the right half's diamonds are in its block actor" + std::string(reloaded ? " after the reload" : ""));
    ctx.check(cl && countIn(cl, "minecraft:emerald") == 3 && items::idOf(cl->getItem(26)) == "minecraft:emerald", "the left half's emeralds are in its block actor" + std::string(reloaded ? " after the reload" : ""));
    ctx.check(!waitingInBlockData(region, left) && !waitingInBlockData(region, right), "nothing of theirs in block data");
    auto* co = inv::nativeContainer(region, bp(old));
    ctx.check(co && items::idOf(co->getItem(1)) == "minecraft:apple" && items::countOf(co->getItem(1)) == 5,
              "the chest saved without a block actor has one now, with the 5 apples in their slot (" + std::string(co ? slotId({co->getItem(1)}, 0) : "no block actor") + ")");
    ctx.check(!waitingInBlockData(region, old), "and nothing of it is left in block data");
    auto shown = inv::contents(old);
    ctx.check(countIn(shown, "minecraft:apple") == 5, "the inventory contract shows the moved stacks");
    if (reloaded) ctx.info("chests verified after the reload at " + where(q));
    for (auto const& p : {left, right, old}) emptyAndClear(region, p);
});

// ---- what a hopper changes reaches an open client mod chest screen ------------------------------------

GF_REGISTER_SELFTEST("families.chest_hoppers_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java's ChestMenu shows a hopper's change at once (AbstractContainerMenu#broadcastChanges): a
    // client mod user's open chest screen gets fresh data after the tick a hopper filled the chest,
    // for a single chest and for a double chest opened on its LEFT half (the screen's context; the
    // hopper on that half fills the right half's first slot)
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6145, 5);
    auto p   = ctx.testPos(6145, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto&          region = *ctx.region();
    CompanionScope companion(ctx);
    // ---- a single chest ----
    auto s = at(p, 1, 0, 1), top = at(s, 0, 1, 0);
    ctx.require(ctx.setBlock(s, kChest, {{kFacingState, "north"}}), "a canopy chest");
    co_await clickChest(ctx, s);
    auto msgs = chestScreenMessages(ctx.player());
    ctx.require(msgs.contains("screen.open"), "a client mod user's click opened the chest screen");
    ctx.check(inv::openedParts(ctx.player()).size() == 1, "of the single chest");
    ctx.require(ctx.setBlock(top, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on the chest");
    co_await ctx.ticks(1);
    ctx.require(vanillaContainer(region, top) != nullptr, "its container");
    vanillaContainer(region, top)->setItem(0, stack("minecraft:gold_ingot", 2));
    bool in = co_await until(ctx, 60, [&] { return countIn(inv::contents(s), "minecraft:gold_ingot") == 2; });
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(in, "the hopper filled the chest");
    ctx.check(msgs.contains("screen.update") && shownSlot(msgs["screen.update"], 0) == "minecraft:gold_ingot x2",
              "the open screen shows the hopper's 2 gold ingots in the first slot (" + (msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], 0) : std::string("no update")) + ")");
    ctx.check(!msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 1, "and stays open");
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    for (auto const& q : {top, s}) emptyAndClear(region, q);
    co_await ctx.ticks(2);
    // ---- a double chest opened on its LEFT half ----
    auto a = at(p, 1, 0, 1), b = at(p, 2, 0, 1), topA = at(a, 0, 1, 0);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    (void)chestScreenMessages(ctx.player());
    co_await clickChest(ctx, a);
    msgs = chestScreenMessages(ctx.player());
    ctx.require(msgs.contains("screen.open"), "the click on the left half opened the double chest's screen");
    ctx.check(inv::openedParts(ctx.player()).size() == 2, "of both halves");
    ctx.require(ctx.setBlock(topA, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on the left half");
    co_await ctx.ticks(1);
    ctx.require(vanillaContainer(region, topA) != nullptr, "its container");
    vanillaContainer(region, topA)->setItem(0, stack("minecraft:diamond", 1));
    bool both = co_await until(ctx, 60, [&] { return countIn(inv::contents(b), "minecraft:diamond") == 1; });
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(both, "the hopper on the left half filled the right half");
    ctx.check(msgs.contains("screen.update") && shownSlot(msgs["screen.update"], 0) == "minecraft:diamond x1" && shownSlot(msgs["screen.update"], 27) == "empty x0",
              "the open screen shows the diamond in the right half's first slot, the first of the 54 (" + (msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], 0) : std::string("no update")) + ")");
    ctx.check(!msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 2, "and stays open");
    // ---- a second client mod user looking inside: every viewer sees every change ----
    // (Java: each player's ChestMenu#broadcastChanges; here another player's click and code's write)
    emptyAndClear(region, topA);
    auto& two  = ctx.spawnPlayer("gf_chest_viewer");
    auto  uid2 = two.getOrCreateUniqueID().rawID;
    gf::net::setTestCompanion(two, true);
    co_await clickChestAs(ctx, uid2, a);
    auto* p2 = simPlayerById(ctx, uid2);
    ctx.require(p2 != nullptr, "the second player");
    msgs = chestScreenMessages(*p2);
    ctx.require(msgs.contains("screen.open") && inv::openedParts(*p2).size() == 2, "the second player's click on the left half opened the double chest's screen too");
    ctx.check(shownSlot(msgs["screen.open"], 0) == "minecraft:diamond x1", "showing the diamond (" + shownSlot(msgs["screen.open"], 0) + ")");
    (void)chestScreenMessages(ctx.player());
    // the first player takes the diamond out with a shift-click on its screen
    auto pctx = JsonT{{"pos", {a.dim, a.x, a.y, a.z}}, {"rows", 6}, {"title", "container.chestDouble"}};
    gf::net::injectFromClient(ctx.player(), "screen.action", {{"type", "inventory.chest"}, {"context", pctx}, {"action", {{"op", "click"}, {"slot", 0}, {"button", 0}, {"type", "quick_move"}}}});
    co_await ctx.ticks(2);
    ctx.check(countIn(inv::contents(b), "minecraft:diamond") == 0, "the first player's shift-click took the diamond out");
    p2   = simPlayerById(ctx, uid2);
    msgs = p2 ? chestScreenMessages(*p2) : std::map<std::string, JsonT>{};
    ctx.check(msgs.contains("screen.update") && shownSlot(msgs["screen.update"], 0) == "empty x0",
              "the second player's open screen shows the first slot empty (" + (msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], 0) : std::string("no update")) + ")");
    // code writes a stack into the right half: both screens show it
    (void)chestScreenMessages(ctx.player());
    auto hb = inv::find(region, bp(b));
    ctx.require(hb.has_value(), "the right half's handler");
    hb->set(b, 1, stack("minecraft:apple", 3));
    co_await ctx.ticks(2);
    auto m1 = chestScreenMessages(ctx.player());
    p2      = simPlayerById(ctx, uid2);
    auto m2 = p2 ? chestScreenMessages(*p2) : std::map<std::string, JsonT>{};
    ctx.check(m1.contains("screen.update") && shownSlot(m1["screen.update"], 1) == "minecraft:apple x3" && m2.contains("screen.update") && shownSlot(m2["screen.update"], 1) == "minecraft:apple x3",
              "a stack code wrote shows on both screens (" + (m1.contains("screen.update") ? shownSlot(m1["screen.update"], 1) : std::string("no update")) + ", "
                  + (m2.contains("screen.update") ? shownSlot(m2["screen.update"], 1) : std::string("no update")) + ")");
    ctx.check(inv::openedParts(ctx.player()).size() == 2 && p2 && inv::openedParts(*p2).size() == 2, "and both stay open");
    if (p2) {
        gf::net::injectFromClient(*p2, "screen.closed", {{"type", "inventory.chest"}});
        (void)gf::net::takeTestMessages(*p2);
        gf::net::setTestCompanion(*p2, false);
    }
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    for (auto const& q : {topA, a, b}) emptyAndClear(region, q);
    (void)box;
});

// ---- a chest whose stacks wait in block data moves them before a hopper or a player reaches it ---------

GF_REGISTER_SELFTEST("families.chest_migrate_lazy", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a chest saved before the chests had block actors gets an empty one when its chunk loads; its
    // stacks wait in block data until the chunk's first tick moves them (families.chest_persist_b_read
    // covers that). A hopper's look at it moves them first: the hopper reaches a double chest of 54
    // slots with the stacks in place (not one half). A player's click opens a screen that shows them
    // where they wait (F10's reads), and the next tick moves them.
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    // ---- a double chest, raised a block: the right half's stacks still in block data ----
    auto a = at(p, 1, 1, 1), b = at(p, 2, 1, 1), underA = at(a, 0, -1, 0);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    ctx.require(stacksInBlockData(region, b, 0, stack("minecraft:diamond", 3)), "the right half's 3 diamonds in block data, its block actor empty");
    auto* view = fam::hopperContainerAt(region, bp(a));
    ctx.check(view && view->getContainerSize() == 54 && items::idOf(view->getItem(0)) == "minecraft:diamond" && items::countOf(view->getItem(0)) == 3,
              "a hopper's look at the LEFT half moves the right half's diamonds into its block actor first: 54 slots, the diamonds in the first (" + std::string(view ? slotId({view->getItem(0)}, 0) : "no container") + ")");
    auto* cb = inv::nativeContainer(region, bp(b));
    ctx.check(cb && countIn(cb, "minecraft:diamond") == 3 && !waitingInBlockData(region, b), "they are in the right half's block actor, nothing left in block data");
    ctx.require(ctx.setBlock(underA, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper under the left half");
    bool took = co_await until(ctx, 60, [&] { return countIn(vanillaContainer(region, underA), "minecraft:diamond") >= 1; });
    ctx.check(took, "the hopper under the left half takes them out");
    for (auto const& q : {underA, a, b}) emptyAndClear(region, q);
    co_await ctx.ticks(2);
    // ---- a single chest opened by a client mod user ----
    auto c = at(p, 3, 0, 1);
    ctx.require(ctx.setBlock(c, kChest, {{kFacingState, "north"}}), "a single chest");
    co_await ctx.ticks(1);
    ctx.require(stacksInBlockData(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data, its block actor empty");
    {
        CompanionScope companion(ctx);
        co_await clickChest(ctx, c);
        auto msgs = chestScreenMessages(ctx.player());
        ctx.require(msgs.contains("screen.open"), "the click opened the chest screen");
        ctx.check(shownSlot(msgs["screen.open"], 4) == "minecraft:emerald x2", "the screen shows the emeralds in their slot (" + shownSlot(msgs["screen.open"], 4) + ")");
        co_await ctx.ticks(2);
        auto* cc = inv::nativeContainer(region, bp(c));
        ctx.check(cc && countIn(cc, "minecraft:emerald") == 2 && items::idOf(cc->getItem(4)) == "minecraft:emerald" && !waitingInBlockData(region, c), "the tick after the click moved them into its block actor, in their slot");
        msgs = chestScreenMessages(ctx.player());
        ctx.check(!msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 1, "the screen stays open");
        gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    }
    emptyAndClear(region, c);
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_migrate_click", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a chest saved without a block actor, its stacks still in block data, clicked before anything
    // moved them: no block actor is made inside the click (GameMode::useItemOn); the screen shows the
    // block data stacks (F10's BlockData backend) and the next tick gives the chest its block actor
    // with them, the screen staying open and following the block actor's container from then on
    // (F10's change notices)
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  c      = at(p, 3, 0, 1);
    ctx.require(ctx.setBlock(c, kChest, {{kFacingState, "north"}}), "a canopy chest");
    co_await ctx.ticks(1);
    ctx.require(stacksWithoutActor(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data, no block actor");
    CompanionScope companion(ctx);
    auto           uid = ctx.player().getOrCreateUniqueID().rawID;
    if (auto* sp = simPlayerById(ctx, uid)) {
        sp->teleport(Vec3{c.x + .5f, static_cast<float>(c.y), c.z + 2.5f}, sp->getDimensionId());
        sp->simulateLookAt(bp(c), sim::LookDuration::Instant);
        sp->simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(8);
    auto* sp = simPlayerById(ctx, uid);
    ctx.require(sp != nullptr, "the player");
    auto  b    = bp(c);
    auto& held = const_cast<ItemStack&>(sp->getSelectedItem());
    sp->mGameMode->useItemOn(held, b, 1, Vec3{b.x + .5f, b.y + 1.f, b.z + .5f}, HandSlot::Mainhand, nullptr, true);
    bool noActor = region.getBlockEntity(b) == nullptr; // (still inside the click's tick)
    bool waiting = waitingInBlockData(region, c);
    auto msgs    = chestScreenMessages(*sp);
    ctx.check(noActor && waiting, "no block actor was made and nothing moved inside the click");
    ctx.require(msgs.contains("screen.open"), "the click opened the chest screen");
    ctx.check(shownSlot(msgs["screen.open"], 4) == "minecraft:emerald x2", "showing the block data emeralds in their slot (" + shownSlot(msgs["screen.open"], 4) + ")");
    co_await ctx.ticks(2);
    auto* cc = inv::nativeContainer(region, b);
    ctx.check(cc && countIn(cc, "minecraft:emerald") == 2 && items::idOf(cc->getItem(4)) == "minecraft:emerald" && !waitingInBlockData(region, c),
              "the next tick gave the chest its block actor with the emeralds in their slot");
    msgs = chestScreenMessages(ctx.player());
    ctx.check(!msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 1, "the screen stays open");

    if (cc) cc->setItem(5, stack("minecraft:flint", 1));
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(msgs.contains("screen.update") && shownSlot(msgs["screen.update"], 5) == "minecraft:flint x1",
              "a write to its new block actor's container shows on the open screen (" + (msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], 5) : std::string("no update")) + ")");
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    co_await ctx.ticks(1);
    emptyAndClear(region, c);
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_migrate_click_double", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a double chest whose RIGHT half was saved without a block actor (its stacks in block data) while
    // the left half has its own, clicked on the LEFT half: the click settles the half it did not hit
    // too, on the next tick, never inside GameMode::useItemOn; meanwhile the screen shows the native
    // stacks of the left half and the block data stacks of the right half side by side (the right half
    // first: slots 0..26)
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // (the block source is taken again after every co_await: R16)
    auto region = [&]() -> BlockSource& { return *ctx.region(); };
    auto  a = at(p, 1, 0, 1), b = at(p, 2, 0, 1);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    auto* ca = inv::nativeContainer(region(), bp(a));
    ctx.require(ca != nullptr, "the left half has its block actor");
    ca->setItem(0, stack("minecraft:flint", 1));
    ctx.require(stacksWithoutActor(region(), b, 4, stack("minecraft:emerald", 2)), "the right half's 2 emeralds in block data, no block actor");
    forgetLoads({a, b});
    ctx.require(inv::nativeContainer(region(), bp(a)) != nullptr, "the left half keeps its block actor");
    CompanionScope companion(ctx);
    auto           uid = ctx.player().getOrCreateUniqueID().rawID;
    if (auto* sp = simPlayerById(ctx, uid)) {
        sp->teleport(Vec3{a.x + .5f, static_cast<float>(a.y), a.z + 2.5f}, sp->getDimensionId());
        sp->simulateLookAt(bp(a), sim::LookDuration::Instant);
        sp->simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(8);
    auto* sp = simPlayerById(ctx, uid);
    ctx.require(sp != nullptr, "the player");
    ctx.require(region().getBlockEntity(bp(b)) == nullptr && waitingInBlockData(region(), b), "the right half still waits (no block actor, emeralds in block data) before the click");
    auto  ba   = bp(a);
    auto& held = const_cast<ItemStack&>(sp->getSelectedItem());
    sp->mGameMode->useItemOn(held, ba, 1, Vec3{ba.x + .5f, ba.y + 1.f, ba.z + .5f}, HandSlot::Mainhand, nullptr, true);
    bool noActor = region().getBlockEntity(bp(b)) == nullptr; // (still inside the click's tick)
    bool waiting = waitingInBlockData(region(), b);
    auto msgs    = chestScreenMessages(*sp);
    ctx.check(noActor && waiting, "no block actor was made for the right half and nothing moved inside the click");
    ctx.require(msgs.contains("screen.open"), "the click on the left half opened the chest screen");
    ctx.check(inv::openedParts(*sp).size() == 2, "of both halves");
    ctx.check(shownSlot(msgs["screen.open"], 4) == "minecraft:emerald x2" && shownSlot(msgs["screen.open"], 27) == "minecraft:flint x1",
              "showing the right half's block data emeralds in slot 4 and the left half's native flint in slot 27 (" + shownSlot(msgs["screen.open"], 4) + ", "
                  + shownSlot(msgs["screen.open"], 27) + ")");
    co_await ctx.ticks(2);
    auto* cb = inv::nativeContainer(region(), bp(b));
    ca       = inv::nativeContainer(region(), bp(a));
    ctx.check(cb && countIn(cb, "minecraft:emerald") == 2 && items::idOf(cb->getItem(4)) == "minecraft:emerald" && !waitingInBlockData(region(), b),
              "the next tick gave the right half its block actor with the emeralds in their slot");
    ctx.check(ca && countIn(ca, "minecraft:flint") == 1 && !waitingInBlockData(region(), a), "the left half keeps its flint, natively");
    msgs = chestScreenMessages(ctx.player());
    ctx.check(!msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 2, "the screen stays open on both halves");
    if (cb) cb->setItem(5, stack("minecraft:apple", 1));
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(msgs.contains("screen.update") && shownSlot(msgs["screen.update"], 5) == "minecraft:apple x1",
              "a write to the right half's new block actor shows on the open screen (" + (msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], 5) : std::string("no update")) + ")");
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    co_await ctx.ticks(1);
    for (auto const& q : {a, b}) emptyAndClear(region(), q);
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_hopper_half_without_actor", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a double chest whose RIGHT half has no block actor (a chunk that loaded and has not been
    // settled: not fully loaded yet, or not ticking) and whose left half has one: a hopper's look at
    // the left half makes no block actor inside the game's hopper code and sees the left half alone
    // that tick; the right half gets its block actor (with its block data stacks) at the end of the
    // tick, and from then on the hopper sees Java's 54-slot CompoundContainer
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto region = [&]() -> BlockSource& { return *ctx.region(); }; // (taken again after every co_await: R16)
    auto a = at(p, 1, 0, 1), b = at(p, 2, 0, 1);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    ctx.require(inv::nativeContainer(region(), bp(a)) != nullptr, "the left half has its block actor");
    ctx.require(stacksWithoutActor(region(), b, 4, stack("minecraft:emerald", 2)), "the right half's 2 emeralds in block data, no block actor");
    forgetLoads({a, b});
    co_await ctx.ticks(3);
    ctx.require(region().getBlockEntity(bp(b)) == nullptr && waitingInBlockData(region(), b), "nothing else gives the right half its block actor meanwhile (the chunk ticked long ago)");
    auto* view = fam::hopperContainerAt(region(), bp(a));
    ctx.check(view && view->getContainerSize() == 27, "the hopper's first look sees the left half alone (" + std::to_string(view ? view->getContainerSize() : 0) + " slots)");
    ctx.check(region().getBlockEntity(bp(b)) == nullptr, "no block actor was made inside the look");
    co_await ctx.ticks(2);
    auto* cb = inv::nativeContainer(region(), bp(b));
    ctx.check(cb && countIn(cb, "minecraft:emerald") == 2 && items::idOf(cb->getItem(4)) == "minecraft:emerald" && !waitingInBlockData(region(), b),
              "the end of that tick gave the right half its block actor with the emeralds in their slot");
    view = fam::hopperContainerAt(region(), bp(a));
    ctx.check(view && view->getContainerSize() == 54 && items::idOf(view->getItem(4)) == "minecraft:emerald",
              "the next look sees the double chest's 54 slots, the right half first (" + std::to_string(view ? view->getContainerSize() : 0) + " slots)");
    // a look at the half WITHOUT a block actor (Java's getContainer reaches the double chest from
    // either half): nothing that tick, no block actor made inside the look; at the end of the tick
    // the half gets its block actor, and the next look sees the 54 slots from that half too
    ctx.require(stacksWithoutActor(region(), b, 5, stack("minecraft:apple", 1)), "the right half without its block actor again, an apple in block data");
    forgetLoads({a, b});
    co_await ctx.ticks(3);
    ctx.require(region().getBlockEntity(bp(b)) == nullptr && waitingInBlockData(region(), b), "nothing else gives the right half its block actor meanwhile");
    view = fam::hopperContainerAt(region(), bp(b));
    ctx.check(view == nullptr, "the hopper's first look at the right half sees nothing yet");
    ctx.check(region().getBlockEntity(bp(b)) == nullptr, "no block actor was made inside that look");
    co_await ctx.ticks(2);
    cb = inv::nativeContainer(region(), bp(b));
    ctx.check(cb && items::idOf(cb->getItem(5)) == "minecraft:apple" && !waitingInBlockData(region(), b),
              "the end of that tick gave the right half its block actor with the apple in its slot");
    view = fam::hopperContainerAt(region(), bp(b));
    ctx.check(view && view->getContainerSize() == 54 && items::idOf(view->getItem(5)) == "minecraft:apple",
              "the next look at the right half sees the 54 slots, the right half first (" + std::to_string(view ? view->getContainerSize() : 0) + " slots)");
    for (auto const& q : {a, b}) emptyAndClear(region(), q);
    (void)box;
});

// ---- the context of a double chest's screen ----------------------------------------------------------

GF_REGISTER_SELFTEST("families.chest_screen_context", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a client mod chest screen of a TF double chest is refreshed under its context, the half the
    // inventory contract opened it for (feature/ext/combat/InventoryChanges.h): the half a player
    // clicked, or the half code opened it for. A click that opens nothing (sneaking with an item: the
    // item is used) leaves no screen; a screen code opens over another takes the new context
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6145, 5);
    auto p   = ctx.testPos(6145, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  a = at(p, 1, 0, 1), b = at(p, 2, 0, 1);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    co_await ctx.ticks(2);
    CompanionScope companion(ctx);
    auto           uid = ctx.player().getOrCreateUniqueID().rawID;
    if (auto* sp = simPlayerById(ctx, uid)) sp->teleport(Vec3{a.x + .5f, static_cast<float>(a.y), a.z + 2.5f}, sp->getDimensionId());
    co_await ctx.ticks(2);
    auto hb = inv::find(region, bp(b));
    ctx.require(hb.has_value(), "the right half's handler");
    (void)chestScreenMessages(ctx.player());
    auto updated = [&](std::map<std::string, JsonT>& msgs, std::size_t slot) { return msgs.contains("screen.update") ? shownSlot(msgs["screen.update"], slot) : std::string("no update"); };
    // ---- a sneaking click on the left half with a stick, then code opens the double chest for the right half ----
    {
        auto&        pl      = ctx.player();
        auto         held    = stack("minecraft:stick", 1);
        std::string  heldId  = "minecraft:stick";
        std::string  blockId = kChest;
        Block const& block   = region.getBlock(bp(a));
        gf::interactions::UseOnContext use{pl, held, heldId, block, blockId, a, 1, {0.5, 1.0, 0.5}, true, true};
        (void)gf::interactions::dispatchUseOn(use);
        ctx.check(inv::openedParts(pl).empty() && !gf::inventory::ext::chestScreenOf(pl), "a sneaking click with a stick opens no screen (ServerPlayerGameMode#useItemOn: the item is used)");
        ctx.require(inv::openChestLike(pl, b, 3, "container.chest"), "code opens the double chest's screen for the right half in the same tick");
        auto context = gf::inventory::ext::chestScreenOf(pl);
        ctx.check(inv::openedParts(pl).size() == 2 && context && *context == b, "its context is the right half");
    }
    hb->set(b, 2, stack("minecraft:apple", 1));
    co_await ctx.ticks(2);
    auto msgs = chestScreenMessages(ctx.player());
    ctx.check(updated(msgs, 2) == "minecraft:apple x1" && !msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 2,
              "a change refreshes the screen code opened, and it stays open (" + updated(msgs, 2) + ")");
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    co_await ctx.ticks(1);
    // ---- the player's own click on the left half: the left half is the context, a change refreshes the screen ----
    co_await clickChest(ctx, a);
    msgs = chestScreenMessages(ctx.player());
    ctx.require(msgs.contains("screen.open"), "the click on the left half opened the double chest's screen");
    auto context = gf::inventory::ext::chestScreenOf(ctx.player());
    ctx.check(context && *context == a, "the clicked half is the screen's context");
    hb->set(b, 3, stack("minecraft:bread", 2));
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(updated(msgs, 3) == "minecraft:bread x2" && !msgs.contains("screen.close"), "a change refreshes the open screen (" + updated(msgs, 3) + ")");
    // ---- code opens the double chest again over it, for the right half: the context follows ----
    ctx.require(inv::openChestLike(ctx.player(), b, 3, "container.chest"), "code opens the double chest's screen again, for the right half");
    context = gf::inventory::ext::chestScreenOf(ctx.player());
    ctx.check(context && *context == b, "the new screen's context is the right half");
    (void)chestScreenMessages(ctx.player());
    hb->set(b, 4, stack("minecraft:carrot", 1));
    co_await ctx.ticks(2);
    msgs = chestScreenMessages(ctx.player());
    ctx.check(updated(msgs, 4) == "minecraft:carrot x1" && !msgs.contains("screen.close") && inv::openedParts(ctx.player()).size() == 2,
              "a change refreshes it and it stays open (" + updated(msgs, 4) + ")");
    gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
    for (auto const& q : {a, b}) emptyAndClear(region, q);
    (void)box;
});
// ---- a chest broken while its stacks wait in block data loses none; a neighbouring chunk moves them ---

GF_REGISTER_SELFTEST("families.chest_migrate_removed", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a chest saved before the chests had block actors, in a chunk loaded but not yet ticked (its
    // stacks still in block data), broken by an explosion from a ticking neighbour or by code:
    // every stack drops, as Java's Containers#dropContents (F10 drops the block data stacks with
    // the block, blockdata::onErase, and the block actor's own, DataDrivenBlockActor::onRemoved).
    // A neighbouring chunk's first tick moves such a chest's stacks into its block actor (Chests.cpp
    // settleNeighbourChests), so a comparator or a hopper next to an unticked chunk reads them.
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6100, 5);
    auto p   = ctx.testPos(6100, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  c      = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(c, kChest, {{kFacingState, "north"}}), "a canopy chest");
    co_await ctx.ticks(1);
    ctx.require(stacksInBlockData(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data, its block actor empty");
    auto* cc = inv::nativeContainer(region, bp(c));
    ctx.require(cc != nullptr, "its block actor's container");
    cc->setItem(0, stack("minecraft:flint", 1));
    clearDropsNear(ctx, c);
    // broken by code (the block change of an explosion: no player)
    ctx.require(gf::game::clearBlock(region, bp(c)), "the chest broken");
    co_await ctx.ticks(2);
    int emeralds = droppedNear(ctx, c, "minecraft:emerald"), flint = droppedNear(ctx, c, "minecraft:flint");
    ctx.check(emeralds == 2 && flint == 1, "its block data stacks and its block actor's dropped (" + std::to_string(emeralds) + " emeralds, " + std::to_string(flint) + " flint)");
    ctx.check(!waitingInBlockData(region, c), "and its block data went with it");
    clearDropsNear(ctx, c);
    // the first tick of the chunk east of the chest's moves its stacks (the chest's own chunk may be
    // loaded without ticking)
    ctx.require(ctx.setBlock(c, kChest, {{kFacingState, "north"}}), "a canopy chest again");
    co_await ctx.ticks(1);
    ctx.require(stacksInBlockData(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data, its block actor empty");
    co_await ctx.ticks(1);
    ctx.info("while they wait in block data the inventory contract reads " + std::to_string(countIn(inv::contents(c), "minecraft:emerald")) + " emerald(s), the comparator "
             + std::to_string(inv::comparatorSignal(c).value_or(-1)));
    int moved = fam::detail::settleNeighbourChests(region, (bp(c).x >> 4) + 1, bp(c).z >> 4);
    co_await ctx.ticks(1);
    auto* after = inv::nativeContainer(region, bp(c));
    ctx.check(moved >= 1 && after && countIn(after, "minecraft:emerald") == 2 && items::idOf(after->getItem(4)) == "minecraft:emerald"
                  && !waitingInBlockData(region, c),
              "the neighbouring chunk's first tick moved them into its block actor, in their slot (" + std::to_string(moved) + " stack(s) moved)");
    ctx.check(inv::comparatorSignal(c).value_or(-1) == 1, "and the comparator reads them (" + std::to_string(inv::comparatorSignal(c).value_or(-1)) + ")");
    // a comparator reads the stacks while they still wait in block data (F10's reads show each where
    // the move would put it: feature/ext/combat/InventoryMigrate.h), without moving them
    for (int i = 0; i < 27; ++i)
        if (auto* own = inv::nativeContainer(region, bp(c))) own->setItem(i, ItemStack{});
    ctx.require(stacksInBlockData(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data again, its block actor empty");
    co_await ctx.ticks(1);
    auto signal = gf::redstone::analogOutput(region, bp(c));
    auto* read  = inv::nativeContainer(region, bp(c));
    ctx.check(signal.value_or(-1) == 1 && read && countIn(read, "minecraft:emerald") == 0 && waitingInBlockData(region, c),
              "a comparator reads 1 from the waiting stacks, which stay in block data (" + std::to_string(signal.value_or(-1)) + ")");
    emptyAndClear(region, c);
    co_await ctx.ticks(1);
    // a chunk that loads (ticking or not) gives its chests saved without a block actor one, with their
    // stacks, at the end of the tick it loaded
    ctx.require(ctx.setBlock(c, kChest, {{kFacingState, "north"}}), "a canopy chest once more");
    co_await ctx.ticks(1);
    ctx.require(stacksWithoutActor(region, c, 4, stack("minecraft:emerald", 2)), "its 2 emeralds in block data, no block actor (a chunk saved before the chests had block actors)");
    fam::detail::chunkLoaded(c.dim, bp(c).x >> 4, bp(c).z >> 4);
    fam::detail::settleLoadedChunksNow();
    auto* loaded = inv::nativeContainer(region, bp(c));
    ctx.check(loaded && countIn(loaded, "minecraft:emerald") == 2 && items::idOf(loaded->getItem(4)) == "minecraft:emerald" && !waitingInBlockData(region, c),
              "the end of the tick its chunk loaded gave it a block actor with the emeralds in their slot");
    emptyAndClear(region, c);
    // the game's chunk loads reach the families (BlockActorLevelListener::onChunkLoaded)
    auto before = fam::detail::chunkLoadsSeen();
    bool far    = co_await ctx.ensureLoaded(at(p, 2048, 0, 0));
    ctx.check(far && fam::detail::chunkLoadsSeen() > before, "the chunks loaded far away were heard (" + std::to_string(fam::detail::chunkLoadsSeen() - before) + " chunk loads)");
    (void)box;
});

// ---- a crafter puts what it crafts into the chest in front of it ---------------------------------------

GF_REGISTER_SELFTEST("families.chest_crafter", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java 1.21 CrafterBlock#dispenseItem: HopperBlockEntity.getContainerAt(level, pos.relative(front))
    // finds the chest in front of the crafter and HopperBlockEntity.addItem puts the crafted stack in
    // it (a double chest: its CompoundContainer, the RIGHT half first); only what does not fit would
    // be ejected into the world
    if (!tfChests(ctx)) co_return;
    auto box = co_await ctx.flatArea(6145, 5);
    auto p   = ctx.testPos(6145, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    // ---- a single chest west of a crafter that faces it; a redstone block east of the crafter ----
    auto s = at(p, 1, 0, 1), crafter = at(p, 2, 0, 1), power = at(p, 3, 0, 1);
    ctx.require(ctx.setBlock(s, kChest, {{kFacingState, "north"}}), "a canopy chest");
    ctx.require(ctx.setBlock(crafter, "minecraft:crafter", {{"orientation", "west_up"}}), "a crafter east of it facing it (orientation west_up)");
    co_await ctx.ticks(1);
    ctx.require(vanillaContainer(region, crafter) != nullptr, "the crafter's container");
    vanillaContainer(region, crafter)->setItem(0, stack("minecraft:oak_log", 1));
    clearDropsNear(ctx, crafter);
    ctx.require(ctx.setBlock(power, "minecraft:redstone_block"), "a redstone block powers the crafter");
    bool in = co_await until(ctx, 60, [&] { return countIn(inv::contents(s), "minecraft:oak_planks") == 4; });
    co_await ctx.ticks(4);
    int planks = countIn(inv::contents(s), "minecraft:oak_planks"), ejected = droppedNear(ctx, crafter, "minecraft:oak_planks");
    ctx.check(in && ejected == 0, "the crafter put its 4 oak planks into the chest in front of it (" + std::to_string(planks) + " in the chest, " + std::to_string(ejected) + " ejected)");
    ctx.check(countIn(vanillaContainer(region, crafter), "minecraft:oak_log") == 0, "it used its log");
    for (auto const& q : {power, crafter, s}) emptyAndClear(region, q);
    clearDropsNear(ctx, crafter);
    co_await ctx.ticks(2);
    // ---- a double chest: a crafter south of its LEFT half facing north fills the RIGHT half first ----
    auto a = at(p, 1, 0, 1), b = at(p, 2, 0, 1), under = at(p, 1, 0, 2), power2 = at(p, 1, 0, 3);
    ctx.require(ctx.setBlock(a, kChest, {{kFacingState, "north"}, {kTypeState, "left"}}), "the left half");
    ctx.require(ctx.setBlock(b, kChest, {{kFacingState, "north"}, {kTypeState, "right"}}), "the right half east of it");
    ctx.require(ctx.setBlock(under, "minecraft:crafter", {{"orientation", "north_up"}}), "a crafter south of the left half facing it");
    co_await ctx.ticks(1);
    ctx.require(vanillaContainer(region, under) != nullptr, "its container");
    vanillaContainer(region, under)->setItem(4, stack("minecraft:oak_log", 1));
    ctx.require(ctx.setBlock(power2, "minecraft:redstone_block"), "a redstone block powers it");
    bool right = co_await until(ctx, 60, [&] { return countIn(inv::contents(b), "minecraft:oak_planks") == 4; });
    co_await ctx.ticks(4);
    ejected = droppedNear(ctx, under, "minecraft:oak_planks");
    ctx.check(right && emptySlots(inv::contents(a)) && ejected == 0 && slotId(inv::contents(b), 0) == "minecraft:oak_planks",
              "the crafter facing the LEFT half filled the RIGHT half's first slot (CompoundContainer; " + std::to_string(countIn(inv::contents(b), "minecraft:oak_planks")) + " in it, "
                  + std::to_string(ejected) + " ejected)");
    for (auto const& q : {power2, under, a, b}) emptyAndClear(region, q);
    clearDropsNear(ctx, under);
    (void)box;
});
