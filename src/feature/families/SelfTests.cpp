// Block families (F11): selftests "families.*" (testPos dx 6100..6199) on the gf_test fixture
// families (tools/families/fixtures/families.json, generated into the gf_test packs).
//
// Every test needs the fixture blocks: a build without them (the generator not applied) logs
// "fixture families missing" and passes without checking; the families e2e scenario applies the
// generator and fails on that line.

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/families/Signs.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::families::detail {
void randomTickAt(BlockSource& region, BlockPos const& pos);
}

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace fam = gf::feature::families;
using Facing  = ScriptModuleMinecraft::ScriptFacing;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool fixturesReady(Context& ctx) {
    if (fam::entryOf("gf_test:family_slab") && gf::game::blockWith("gf_test:family_slab")) return true;
    ctx.info("fixture families missing: the gf_test family blocks are not in this build (tools/families/run.ps1 generates them); nothing checked");
    return false;
}

int st(GlobalPos const& p, char const* name) {
    auto const* b = fam::blockAt(p);
    return b ? fam::intState(*b, name).value_or(-1) : -1;
}
std::string sst(GlobalPos const& p, char const* name) {
    auto const* b = fam::blockAt(p);
    return b ? fam::strState(*b, name) : std::string();
}

SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// Clicks face `face` of `p` with `itemId` (hotbar slot 0; "" = empty hand) at face-local `hit`.
bool click(Context& ctx, GlobalPos const& p, Facing face, Vec3 hit, std::string const& itemId) {
    auto& sp = simPlayer(ctx);
    if (itemId.empty()) {
        sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    } else {
        auto item = gf::game::items::make(itemId, 16);
        if (!item) return false;
        sp.simulateSetItem(*item, true, 0);
    }
    return sp.simulateUseItemInSlotOnBlock(0, gf::game::blockPos(p), face, hit);
}

std::string where(GlobalPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

} // namespace

GF_REGISTER_SELFTEST("families.fixtures", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    int missing = 0, count = 0;
    for (auto const* e : fam::entries()) {
        if (!e->id.starts_with("gf_test:family_")) continue;
        ++count;
        if (!gf::game::blockWith(e->id)) {
            ++missing;
            ctx.check(false, e->id + " is registered in the game");
        }
    }
    ctx.check(count >= 30, "every fixture family is loaded (" + std::to_string(count) + ")");
    ctx.check(fam::entryOf("gf_test:family_door")->family == fam::Family::Door, "families are read by name");
    ctx.check(fam::entryOf("gf_test:family_stairs")->has(fam::Waterloggable), "params.waterlogged adds the waterloggable modifier");
    ctx.check(fam::entryOf("gf_test:family_lightable")->has(fam::Lightable) && fam::entryOf("gf_test:family_lightable")->family == fam::Family::Cube,
              "a shape family and a modifier on one id");
    ctx.info("families fixtures present: " + std::to_string(count) + " (" + std::to_string(missing) + " missing)");
    co_return;
});

GF_REGISTER_SELFTEST("families.slab_doubling", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6101, 5);
    auto p   = ctx.testPos(6101, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(at(p, 1, 0, 0), "gf_test:family_slab", {{"minecraft:vertical_half", "bottom"}}), "bottom slab placed");
    co_await ctx.ticks(1);
    // a second slab clicked onto the top of a bottom slab merges into a double slab
    ctx.check(click(ctx, at(p, 1, 0, 0), Facing::Up, Vec3{0.5f, 0.5f, 0.5f}, "gf_test:family_slab"), "the click went through");
    co_await ctx.ticks(2);
    ctx.check(st(at(p, 1, 0, 0), fam::state::Double) == 1, "the slab is double after a second slab on its top (double=" + std::to_string(st(at(p, 1, 0, 0), fam::state::Double)) + ")");
    // a top slab takes a slab from below
    ctx.require(ctx.setBlock(at(p, 3, 1, 0), "gf_test:family_slab", {{"minecraft:vertical_half", "top"}}), "top slab placed");
    co_await ctx.ticks(1);
    auto& region = *ctx.region();
    ctx.check(fam::mergeSlab(region, gf::game::blockPos(at(p, 3, 1, 0))), "a top slab merges");
    ctx.check(st(at(p, 3, 1, 0), fam::state::Double) == 1, "the top slab is double");
    ctx.check(!fam::mergeSlab(region, gf::game::blockPos(at(p, 3, 1, 0))), "a double slab takes no more");
    (void)box;
});

GF_REGISTER_SELFTEST("families.door_pair", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6105, 5);
    auto p   = ctx.testPos(6105, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto door = at(p, 2, 0, 0);
    // a player places the door on the ground: the lower half, then the upper half on top
    ctx.check(click(ctx, at(door, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_door"), "the click went through");
    co_await ctx.ticks(3);
    ctx.require(ctx.blockAt(door) == "gf_test:family_door", "the lower half is placed (got " + ctx.blockAt(door) + ")");
    ctx.check(ctx.blockAt(at(door, 0, 1, 0)) == "gf_test:family_door", "the upper half is placed above it");
    ctx.check(st(door, fam::state::Upper) == 0 && st(at(door, 0, 1, 0), fam::state::Upper) == 1, "lower and upper halves");
    ctx.check(sst(door, fam::state::Cardinal) == sst(at(door, 0, 1, 0), fam::state::Cardinal), "both halves face the same way");
    ctx.cleanupBlock(at(door, 0, 1, 0));
    // using either half opens both
    ctx.check(fam::useDoor(at(door, 0, 1, 0), nullptr), "the upper half can be used");
    co_await ctx.ticks(2);
    ctx.check(st(door, fam::state::Open) == 1 && st(at(door, 0, 1, 0), fam::state::Open) == 1, "both halves are open");
    ctx.check(click(ctx, door, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on the door");
    co_await ctx.ticks(2);
    ctx.check(st(door, fam::state::Open) == 0 && st(at(door, 0, 1, 0), fam::state::Open) == 0, "a player's click closes both halves");
    // redstone opens it (the lower half's neighbour), and it closes when the power goes
    ctx.require(ctx.setBlock(at(door, 1, 0, 0), "minecraft:redstone_block"), "a redstone block beside the door");
    co_await ctx.ticks(8);
    ctx.check(st(door, fam::state::Open) == 1 && st(door, fam::state::Powered) == 1, "redstone opens the door");
    ctx.setBlock(at(door, 1, 0, 0), "minecraft:air");
    co_await ctx.ticks(8);
    ctx.check(st(door, fam::state::Open) == 0, "the door closes without power");
    // removing the upper half removes the lower one
    fam::clear(at(door, 0, 1, 0));
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(door) != "gf_test:family_door", "the lower half goes with the upper half (got " + ctx.blockAt(door) + ")");
    (void)box;
});

GF_REGISTER_SELFTEST("families.fence_connections", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6110, 5);
    auto p   = ctx.testPos(6110, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto a = at(p, 1, 0, 0), b = at(p, 2, 0, 0), c = at(p, 2, 0, -1);
    ctx.require(ctx.setBlock(a, "gf_test:family_fence"), "fence a");
    ctx.require(ctx.setBlock(b, "gf_test:family_fence"), "fence b");
    co_await ctx.ticks(3);
    ctx.check(st(a, "twilightforest:east") == 1 && st(b, "twilightforest:west") == 1, "two fences connect (a east " + std::to_string(st(a, "twilightforest:east")) + ")");
    ctx.check(st(a, "twilightforest:north") == 0 && st(a, "twilightforest:south") == 0, "no connection to air");
    // a full block connects, leaves don't (Java isExceptionForConnection)
    ctx.require(ctx.setBlock(c, "minecraft:stone"), "stone north of b");
    ctx.require(ctx.setBlock(at(b, 0, 0, 1), "gf_test:family_leaves", {{"twilightforest:persistent", 1}}), "leaves south of b");
    co_await ctx.ticks(3);
    ctx.check(st(b, "twilightforest:north") == 1, "a fence connects to a sturdy block");
    ctx.check(st(b, "twilightforest:south") == 0, "but not to leaves");
    // removing a neighbour disconnects
    ctx.setBlock(a, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(st(b, "twilightforest:west") == 0, "the connection goes with the neighbour");
    // walls and panes follow their neighbours too
    auto w = at(p, 1, 0, 2), w2 = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(w, "gf_test:family_wall") && ctx.setBlock(w2, "gf_test:family_wall"), "two walls");
    auto g = at(p, 3, 0, 1);
    ctx.require(ctx.setBlock(g, "gf_test:family_pane") && ctx.setBlock(at(g, 0, 0, 1), "gf_test:family_pane"), "two panes");
    co_await ctx.ticks(3);
    ctx.check(st(w, "twilightforest:east_wall") >= 1 && st(w2, "twilightforest:west_wall") >= 1, "walls connect");
    ctx.check(st(w, fam::state::Up) == 1, "the end of a wall keeps its post");
    ctx.check(st(g, "twilightforest:south") == 1, "panes connect");
    (void)box;
});

GF_REGISTER_SELFTEST("families.button_redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6115, 5);
    auto p   = ctx.testPos(6115, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto button = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(button, "gf_test:family_button", {{"minecraft:block_face", "up"}}), "a button on the ground");
    ctx.require(ctx.setBlock(at(button, 1, 0, 0), "minecraft:redstone_wire"), "redstone dust beside it");
    co_await ctx.ticks(2);
    auto& region = *ctx.region();
    ctx.check(gf::redstone::power(region, gf::game::blockPos(at(button, 1, 0, 0))) == 0, "no power before the press");
    ctx.check(click(ctx, button, Facing::Up, Vec3{0.5f, 0.1f, 0.5f}, ""), "the click went through");
    co_await ctx.ticks(4);
    ctx.check(st(button, fam::state::Powered) == 1, "the button is pressed");
    auto const* dust   = fam::blockAt(at(button, 1, 0, 0));
    int         signal = dust ? fam::intState(*dust, "redstone_signal").value_or(-1) : -1;
    ctx.check(signal == 15, "the pressed button powers the dust with 15 (got " + std::to_string(signal) + ")");
    // TF's wooden buttons stay pressed for 30 ticks
    co_await ctx.ticks(20);
    ctx.check(st(button, fam::state::Powered) == 1, "still pressed after 20 ticks");
    co_await ctx.ticks(14);
    ctx.check(st(button, fam::state::Powered) == 0, "released after 30 ticks");
    co_await ctx.ticks(4);
    dust   = fam::blockAt(at(button, 1, 0, 0));
    signal = dust ? fam::intState(*dust, "redstone_signal").value_or(-1) : -1;
    ctx.check(signal == 0, "the dust is off again (got " + std::to_string(signal) + ")");
    // a pressure plate is pressed while a player stands on it
    auto plate = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(plate, "gf_test:family_pressure_plate"), "a pressure plate");
    simPlayer(ctx).teleport(Vec3{plate.x + 0.5f, float(plate.y), plate.z + 0.5f}, simPlayer(ctx).getDimensionId());
    co_await ctx.ticks(4);
    ctx.check(st(plate, fam::state::Powered) == 1, "the plate is pressed under the player");
    simPlayer(ctx).teleport(Vec3{plate.x + 3.5f, float(plate.y), plate.z + 0.5f}, simPlayer(ctx).getDimensionId());
    co_await ctx.ticks(24);
    ctx.check(st(plate, fam::state::Powered) == 0, "and released after the player left (20 ticks)");
    (void)box;
});

GF_REGISTER_SELFTEST("families.leaves_decay", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6120, 5);
    auto p   = ctx.testPos(6120, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto log    = at(p, 1, 0, 0);
    auto leaf1  = at(p, 1, 1, 0);
    auto leaf2  = at(p, 2, 1, 0);
    auto keeper = at(p, 3, 1, 0);
    ctx.require(ctx.setBlock(log, "gf_test:family_log", {{"minecraft:block_face", "up"}}), "a log");
    ctx.require(ctx.setBlock(leaf1, "gf_test:family_leaves"), "leaves on the log");
    ctx.require(ctx.setBlock(leaf2, "gf_test:family_leaves"), "leaves beside them");
    ctx.require(ctx.setBlock(keeper, "gf_test:family_leaves", {{"twilightforest:persistent", 1}}), "persistent leaves");
    // Java's distance spreads one scheduled tick at a time
    for (int i = 0; i < 40 && !(st(leaf1, fam::state::Distance) == 1 && st(leaf2, fam::state::Distance) == 2); ++i) co_await ctx.ticks(1);
    ctx.check(st(leaf1, fam::state::Distance) == 1 && st(leaf2, fam::state::Distance) == 2, "the distance to the log (" + std::to_string(st(leaf1, fam::state::Distance)) + ", "
                                                                                                   + std::to_string(st(leaf2, fam::state::Distance)) + ")");
    gf::feature::families::detail::randomTickAt(*ctx.region(), gf::game::blockPos(leaf1));
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(leaf1) == "gf_test:family_leaves", "leaves near a log don't decay");
    // the log goes: the distances climb to 7 (the leaves feed each other on the way, as in Java)
    ctx.setBlock(log, "minecraft:air");
    for (int i = 0; i < 80 && !(st(leaf1, fam::state::Distance) == 7 && st(leaf2, fam::state::Distance) == 7); ++i) co_await ctx.ticks(1);
    ctx.check(st(leaf1, fam::state::Distance) == 7 && st(leaf2, fam::state::Distance) == 7, "no log: distance 7 (" + std::to_string(st(leaf1, fam::state::Distance)) + ", "
                                                                                                + std::to_string(st(leaf2, fam::state::Distance)) + ")");
    gf::feature::families::detail::randomTickAt(*ctx.region(), gf::game::blockPos(leaf1));
    gf::feature::families::detail::randomTickAt(*ctx.region(), gf::game::blockPos(keeper));
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(leaf1) != "gf_test:family_leaves", "the leaves decayed");
    ctx.check(ctx.blockAt(keeper) == "gf_test:family_leaves", "persistent leaves never decay");
    gf::feature::families::detail::randomTickAt(*ctx.region(), gf::game::blockPos(leaf2));
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(leaf2) != "gf_test:family_leaves", "the other leaves decayed too");
    (void)box;
});

GF_REGISTER_SELFTEST("families.sign_a_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6125, 4);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    gf::game::setBlock(region, gf::game::blockPos(p), *gf::game::blockWith("gf_test:family_sign"));
    co_await ctx.ticks(1);
    ctx.require(ctx.blockAt(p) == "gf_test:family_sign", "a sign is placed");
    ctx.check(gf::signs::text(p).size() == 4 && gf::signs::text(p)[0].empty(), "a new sign has four empty lines");
    ctx.check(gf::signs::setText(p, {"Here lies", "a naga", "", "families"}), "setText on a sign");
    ctx.check(gf::signs::setText(p, {"back side"}, false), "setText on the back");
    ctx.check(!gf::signs::setText(at(p, 0, 1, 0), {"x"}), "setText refuses a block that is no sign");
    auto lines = gf::signs::text(p);
    ctx.check(lines.size() == 4 && lines[0] == "Here lies" && lines[3] == "families", "the text reads back");
    ctx.check(gf::signs::text(p, false)[0] == "back side", "the back reads back");
    ctx.info("sign written at " + where(p));
    co_return;
});

GF_REGISTER_SELFTEST("families.sign_b_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6125, 4);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    if (ctx.blockAt(p) != "gf_test:family_sign") {
        ctx.info("no sign written: nothing to read");
        co_return;
    }
    auto lines = gf::signs::text(p);
    ctx.check(lines.size() == 4 && lines[0] == "Here lies" && lines[1] == "a naga" && lines[3] == "families", "the sign text survived the reload");
    ctx.check(gf::signs::text(p, false)[0] == "back side", "the back survived too");
    ctx.info("sign text verified at " + where(p));
    ctx.cleanupBlock(p);
});

GF_REGISTER_SELFTEST("families.double_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6130, 5);
    auto p   = ctx.testPos(6130, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  a = at(p, 1, 0, 0), b = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(a, "gf_test:family_chest", {{"minecraft:cardinal_direction", "north"}}), "chest a facing north");
    co_await ctx.ticks(1);
    ctx.require(ctx.setBlock(b, "gf_test:family_chest"), "chest b east of it");
    // placing b facing north beside a: Java pairs them (b is clockwise of a facing north: a LEFT? no:
    // a's clockwise neighbour is east, so b sees a on its counter-clockwise side: b RIGHT, a LEFT)
    fam::pairChest(region, gf::game::blockPos(b), fam::North, false, std::nullopt);
    co_await ctx.ticks(3);
    ctx.check(sst(b, fam::state::Type) == "right" || sst(b, fam::state::Type) == "left", "the new chest pairs (" + sst(b, fam::state::Type) + ")");
    ctx.check(sst(a, fam::state::Type) != "single" && sst(a, fam::state::Type) != sst(b, fam::state::Type), "the other chest pairs with it (" + sst(a, fam::state::Type) + ")");
    ctx.check(fam::chestPartner(region, gf::game::blockPos(a)).has_value(), "each half finds its partner");
    // a third chest beside a double chest stays single
    auto c = at(p, 3, 0, 0);
    ctx.require(ctx.setBlock(c, "gf_test:family_chest"), "chest c");
    fam::pairChest(region, gf::game::blockPos(c), fam::North, false, std::nullopt);
    co_await ctx.ticks(2);
    ctx.check(sst(c, fam::state::Type) == "single", "a third chest stays single");
    // breaking one half leaves the other single
    ctx.setBlock(b, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(sst(a, fam::state::Type) == "single", "the remaining half is single again");
    (void)box;
});

GF_REGISTER_SELFTEST("families.shapes", [](Context& ctx) -> ll::coro::CoroTask<> {
    // stairs, trapdoor, fence gate, pillar axes, wall pillar and connected texture connections
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6135, 5);
    auto p   = ctx.testPos(6135, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // a trapdoor toggles on use and opens with redstone
    auto t = at(p, 0, 0, -2);
    ctx.require(ctx.setBlock(t, "gf_test:family_trapdoor", {{"minecraft:cardinal_direction", "north"}}), "a trapdoor");
    co_await ctx.ticks(1);
    ctx.check(fam::useTrapdoor(t, nullptr) && st(t, fam::state::Open) == 1, "the trapdoor opens");
    ctx.check(fam::useTrapdoor(t, nullptr) && st(t, fam::state::Open) == 0, "and closes");
    // a fence gate opens away from the player and is lowered between walls
    auto g = at(p, 2, 0, -2);
    ctx.require(ctx.setBlock(g, "gf_test:family_fence_gate", {{"minecraft:cardinal_direction", "north"}}), "a fence gate");
    ctx.require(ctx.setBlock(at(g, 1, 0, 0), "gf_test:family_wall") && ctx.setBlock(at(g, -1, 0, 0), "gf_test:family_wall"), "walls on its sides");
    co_await ctx.ticks(3);
    ctx.check(st(g, fam::state::InWall) == 1, "the gate between walls is lowered");
    ctx.check(fam::useFenceGate(g, nullptr) && st(g, fam::state::Open) == 1, "the gate opens");
    // wall pillars connect along their axis, connected textures to their own kind
    auto w1 = at(p, 0, 0, 0), w2 = at(p, 0, 1, 0);
    ctx.require(ctx.setBlock(w1, "gf_test:family_wall_pillar", {{"minecraft:block_face", "up"}}), "wall pillar");
    ctx.require(ctx.setBlock(w2, "gf_test:family_wall_pillar", {{"minecraft:block_face", "up"}}), "wall pillar above");
    auto c1 = at(p, 2, 0, 0), c2 = at(p, 3, 0, 0);
    ctx.require(ctx.setBlock(c1, "gf_test:family_ct") && ctx.setBlock(c2, "gf_test:family_ct"), "connected texture blocks");
    co_await ctx.ticks(3);
    ctx.check(st(w1, "twilightforest:up") == 1 && st(w2, "twilightforest:down") == 1, "wall pillars connect along the axis");
    ctx.check(st(c1, "twilightforest:east") == 1 && st(c2, "twilightforest:west") == 1 && st(c1, "twilightforest:north") == 0, "connected textures join their own kind");
    // stairs exist with the corner traits
    auto s = at(p, 4, 0, 0);
    ctx.check(ctx.setBlock(s, "gf_test:family_stairs", {{"minecraft:vertical_half", "top"}, {"minecraft:cardinal_direction", "east"}}), "stairs with half and facing");
    co_await ctx.ticks(1);
    ctx.check(sst(s, "minecraft:vertical_half") == "top", "upside-down stairs");
    (void)box;
});

GF_REGISTER_SELFTEST("families.interactions", [](Context& ctx) -> ll::coro::CoroTask<> {
    // log stripping, potted plants, lightable, snowloggable, saplings, climbable
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6140, 5);
    auto p   = ctx.testPos(6140, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto lg = at(p, 0, 0, 0);
    ctx.require(ctx.setBlock(lg, "gf_test:family_log", {{"minecraft:block_face", "east"}}), "a log on its side");
    co_await ctx.ticks(1);
    ctx.check(click(ctx, lg, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:iron_axe"), "an axe click");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(lg) == "gf_test:family_stripped_log", "the axe strips the log (" + ctx.blockAt(lg) + ")");
    ctx.check(sst(lg, "minecraft:block_face") == "east", "the stripped log keeps its axis");
    // potted plants
    auto pot = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(pot, "minecraft:flower_pot"), "an empty flower pot");
    co_await ctx.ticks(1);
    ctx.check(click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, "gf_test:family_sapling"), "a sapling click on the pot");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(pot) == "gf_test:family_potted", "the sapling goes into the pot (" + ctx.blockAt(pot) + ")");
    ctx.check(click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, ""), "an empty hand on the potted sapling");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(pot) == "minecraft:flower_pot", "the plant comes out again");
    // lightable
    auto lamp = at(p, 4, 0, 0);
    ctx.require(ctx.setBlock(lamp, "gf_test:family_lightable"), "a lightable block");
    co_await ctx.ticks(1);
    ctx.check(click(ctx, lamp, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:flint_and_steel"), "flint and steel");
    co_await ctx.ticks(2);
    ctx.check(sst(lamp, fam::state::Lighting) == "normal", "flint and steel lights it (" + sst(lamp, fam::state::Lighting) + ")");
    ctx.check(click(ctx, lamp, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, ""), "an empty hand");
    co_await ctx.ticks(2);
    ctx.check(sst(lamp, fam::state::Lighting) == "none", "an empty hand puts it out");
    // snowloggable
    auto bush = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(bush, "gf_test:family_snowloggable"), "a snowloggable plant");
    co_await ctx.ticks(1);
    ctx.check(click(ctx, bush, Facing::Up, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:snow_layer"), "snow on the plant");
    co_await ctx.ticks(2);
    ctx.check(st(bush, fam::state::Layers) == 1, "a snow layer inside the plant (" + std::to_string(st(bush, fam::state::Layers)) + ")");
    // saplings grow a stage per advance (the tree comes from gf::features)
    auto sap = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(sap, "gf_test:family_sapling"), "a sapling");
    co_await ctx.ticks(1);
    fam::advanceSapling(*ctx.region(), gf::game::blockPos(sap));
    ctx.check(st(sap, fam::state::Stage) == 1, "the sapling's first stage");
    // climbable blocks are climbable for the game's movement
    auto const* climb = gf::game::blockWith("gf_test:family_climbable");
    ctx.check(climb && (static_cast<std::uint64_t>(climb->getBlockType().mProperties) & static_cast<std::uint64_t>(BlockProperty::Climbable)) != 0,
              "the climbable family block type is climbable");
    (void)box;
});

GF_REGISTER_SELFTEST("families.blockevents", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the contract on the F0 fixture block: placed, neighbour change, removed, random tick
    auto p = ctx.testPos(6145, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    static int placed = 0, neighbor = 0, removed = 0, ticked = 0; // handlers stay registered: no captured locals
    static GlobalPos probe;
    placed = neighbor = removed = ticked = 0;
    probe  = p;
    static bool registered = false;
    if (!registered) {
        registered = true;
        auto mine  = [](BlockSource& r, BlockPos const& pos) { return gf::game::globalPos(r, pos) == probe; };
        gf::blockevents::onPlaced("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&, Actor*) { if (mine(r, pos)) ++placed; });
        gf::blockevents::onNeighborChanged("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, BlockPos const&) { if (mine(r, pos)) ++neighbor; });
        gf::blockevents::onRemoved("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&) { if (mine(r, pos)) ++removed; });
        gf::blockevents::onRandomTick("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&, gf::blockevents::RandomSource&) {
            if (mine(r, pos)) ++ticked;
        });
    }
    ctx.require(ctx.setBlock(p, "gf_test:fixture_block"), "the fixture block");
    co_await ctx.ticks(2);
    ctx.check(placed == 1, "onPlaced ran once (" + std::to_string(placed) + ")");
    ctx.setBlock(at(p, 1, 0, 0), "minecraft:stone");
    co_await ctx.ticks(2);
    ctx.check(neighbor >= 1, "onNeighborChanged ran for a neighbour");
    ctx.setBlock(p, "gf_test:fixture_block", {{"gf_test:level", 2}});
    co_await ctx.ticks(2);
    ctx.check(placed == 1 && removed == 0, "a state change is no placement / removal");
    // the random ticks of the chunk reach the block (about once per 1365 ticks at the default
    // rate): only the dispatch is checked here
    gf::feature::families::detail::randomTickAt(*ctx.region(), gf::game::blockPos(p));
    ctx.check(ticked == 1, "onRandomTick dispatches");
    ctx.setBlock(p, "minecraft:air");
    co_await ctx.ticks(2);
    ctx.check(removed == 1, "onRemoved ran once (" + std::to_string(removed) + ")");
    ctx.setBlock(at(p, 1, 0, 0), "minecraft:air");
    probe = GlobalPos{-1, 0, -100000, 0};
});
