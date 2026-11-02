// Block families (F11): selftests of the chests' native containers (testPos dx 6100..6199): TF's
// chests keep their slots in the block actor of minecraft:block_entity (F10's native backend), so
// hoppers, hopper minecarts and droppers fill and empty them as Java's do (Hoppers.cpp: a double
// chest is one CompoundContainer, the right half first), comparators read the double inventory,
// the stacks survive a server restart, and a chest whose stacks were kept in block data (no block
// actor) moves them into its container (Chests.cpp migrateChest).

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Redstone.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
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

/// Empties a container block (native slots and block data) and clears it, so nothing drops.
void emptyAndClear(BlockSource& region, GlobalPos const& p) {
    if (auto* c = inv::nativeContainer(region, bp(p)))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    if (auto* c = vanillaContainer(region, p))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    gf::blockdata::set(region, bp(p), "inventory", nullptr);
    gf::game::clearBlock(region, bp(p));
}

/// Waits up to `ticks` ticks for `done`.
ll::coro::CoroTask<bool> until(Context& ctx, int ticks, std::function<bool()> done) {
    for (int i = 0; i < ticks; ++i) {
        if (done()) co_return true;
        co_await ctx.ticks(1);
    }
    co_return done();
}

} // namespace

GF_REGISTER_SELFTEST("families.chest_hoppers", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java: TF's chests are ChestBlock / ChestBlockEntity, so hoppers fill them from above and from
    // the sides and empty them from below (HopperBlockEntity#getContainerAt); a double chest is one
    // CompoundContainer of 54 slots, the RIGHT half first (a hopper on the left half fills the right
    // half's first slot, one under the left half takes from the right half); a comparator reads the
    // double inventory (ChestBlock#getAnalogOutputSignal: getRedstoneSignalFromContainer of both)
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
    ctx.check(fam::hopperContainerAt(region, bp(s)) == own, "a hopper sees the chest's own container");
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
    auto hb = inv::find(region, bp(b));
    ctx.require(hb.has_value(), "the right half's handler");
    for (int i = 0; i < 27; ++i) hb->set(b, i, stack("minecraft:cobblestone", 64));
    co_await ctx.ticks(10);
    ctx.check(output() == 8 && inv::comparatorSignal(a).value_or(-1) == 8, "27 full stacks in the 54 slots give 8 (Mth.lerpDiscrete: output " + std::to_string(output()) + ")");
    for (int i = 0; i < 27; ++i) hb->set(b, i, ItemStack{});
    // a hopper under the LEFT half takes from the right half first
    for (auto const& q : {topA, westA}) emptyAndClear(region, q);
    hb->set(b, 5, stack("minecraft:diamond", 1));
    auto underA = at(a, 0, -1, 0);
    ctx.require(ctx.setBlock(underA, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper under the left half");
    bool took = co_await until(ctx, 60, [&] { return countIn(vanillaContainer(region, underA), "minecraft:diamond") == 1; });
    ctx.check(took && emptySlots(inv::contents(b)), "the hopper under the left half took the right half's diamond");
    co_await ctx.ticks(10);
    ctx.check(output() == 0, "and the comparator reads 0 again (" + std::to_string(output()) + ")");
    for (auto const& q : {underA, comp, outStone, a, b}) emptyAndClear(region, q);
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_hoppers_more", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the trapped chest takes a hopper's items too; a dropper drops into the chest in front of it
    // (DropperBlock#dispenseFrom); a hopper's first look at an unopened chest rolls its loot table
    // (RandomizableContainer#unpackLootTable(null)); a hopper minecart under a chest empties it
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
    vanillaContainer(region, top)->setItem(0, stack("minecraft:apple", 1));
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
    gf::blockdata::set(region, bp(old), "inventory", JsonT{{"items", JsonT::array({nullptr, items::toJson(stack("minecraft:apple", 5))})}});
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
        co_await until(ctx, 60, [&] { return gf::blockdata::get(region, bp(old), "inventory") == nullptr; });
    } else {
        ctx.check(fam::migrateChest(region, bp(old)) == 1, "the third chest's block data stack moves into its container");
    }
    auto* cl = inv::nativeContainer(region, bp(left));
    auto* cr = inv::nativeContainer(region, bp(right));
    ctx.check(cr && countIn(cr, "minecraft:diamond") == 7 && items::idOf(cr->getItem(0)) == "minecraft:diamond", "the right half's diamonds are in its block actor" + std::string(reloaded ? " after the reload" : ""));
    ctx.check(cl && countIn(cl, "minecraft:emerald") == 3 && items::idOf(cl->getItem(26)) == "minecraft:emerald", "the left half's emeralds are in its block actor" + std::string(reloaded ? " after the reload" : ""));
    ctx.check(!gf::blockdata::get(region, bp(left), "inventory") && !gf::blockdata::get(region, bp(right), "inventory"), "nothing of theirs in block data");
    auto* co = inv::nativeContainer(region, bp(old));
    ctx.check(co && items::idOf(co->getItem(1)) == "minecraft:apple" && items::countOf(co->getItem(1)) == 5,
              "the chest saved without a block actor has one now, with the 5 apples in their slot (" + std::string(co ? slotId({co->getItem(1)}, 0) : "no block actor") + ")");
    ctx.check(gf::blockdata::get(region, bp(old), "inventory") == nullptr, "and its block data is gone");
    auto shown = inv::contents(old);
    ctx.check(countIn(shown, "minecraft:apple") == 5, "the inventory contract shows the moved stacks");
    if (reloaded) ctx.info("chests verified after the reload at " + where(q));
    for (auto const& p : {left, right, old}) emptyAndClear(region, p);
});
