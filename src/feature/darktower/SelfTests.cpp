// U26 (darktower) selftests (rule R12: darktower.*, dx 2600..2699, dz 0..4): the tower devices
// (vanishing chain, reappearing cycle, locked door and tower key, carminite builder bridges and
// their expiry chain, antibuilder, reactor, slider), the ghast trap downing a scripted Ur-Ghast
// (activate_ghast_trap, tantrum off, trap damage), the Ur-Ghast phases and the mobs' attributes.

#include <algorithm>
#include <cmath>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/darktower/Devices.h"
#include "feature/darktower/Runtime.h"
#include "feature/darktower/UrGhast.h"
#include "feature/darktower/Util.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::darktower::tests {

using gf::selftest::Context;
namespace ia = gf::interactions;

namespace {

BlockPos bp(core::GlobalPos const& g) { return game::blockPos(g); }

bool boolAt(Context& ctx, core::GlobalPos const& g, char const* state) {
    auto* r = ctx.region();
    return r && boolStateAt(*r, bp(g), state);
}

/// A right-click of the test player on `g` with `itemId` in the hand (empty: an empty hand).
ia::Result click(Context& ctx, core::GlobalPos const& g, std::string const& itemId = {}) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const& block = region->getBlock(bp(g));
    auto         bid   = game::blockId(block);
    ItemStack const& held = p.getSelectedItem();
    std::string      hid  = itemId;
    ia::UseOnContext c{p, held, hid, block, bid, g, 1, {0.5, 1.0, 0.5}, false, true};
    return ia::dispatchUseOn(c);
}

/// Moves the test player next to `g`, looking with (pitch, yaw).
void placePlayer(Context& ctx, core::GlobalPos const& g, float dx, float dz, float pitch = 0.0f, float yaw = 0.0f) {
    auto& p = ctx.player();
    p.teleportTo(Vec3{g.x + dx, float(g.y), g.z + dz}, true, 0, 0, false);
    const_cast<Vec2&>(p.getRotation()) = Vec2{pitch, yaw};
}

int countAround(Context& ctx, core::GlobalPos const& c, std::string const& type, float r) {
    auto* region = ctx.region();
    if (!region) return 0;
    int n = 0;
    for (auto* a : region->getEntities(AABB{Vec3{c.x - r, c.y - r, c.z - r}, Vec3{c.x + 1 + r, c.y + 1 + r, c.z + 1 + r}}, 0.0f))
        if (a && a->isAlive() && typeOf(*a) == type) ++n;
    return n;
}

void removeAround(Context& ctx, core::GlobalPos const& c, std::string const& type, float r) {
    auto* region = ctx.region();
    if (!region) return;
    for (auto* a : region->getEntities(AABB{Vec3{c.x - r, c.y - r, c.z - r}, Vec3{c.x + 1 + r, c.y + 1 + r, c.z + 1 + r}}, 0.0f))
        if (a && typeOf(*a) == type) a->remove();
}

/// The mob tests need a non-peaceful world (the boss kit turns a boss back into its spawner on
/// peaceful, and peaceful removes hostile mobs).
struct DifficultyScope {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    DifficultyScope() {
        if (auto* l = level()) {
            previous = l->getDifficulty();
            l->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
        }
    }
    ~DifficultyScope() {
        if (auto* l = level()) l->setDifficulty(previous);
    }
};

/// Explosions of the mobs' fireballs and the reactor must not dig into the shared test world:
/// mobGriefing and doFireTick are off while a mob test runs (restored afterwards).
struct GriefScope {
    bool griefing = true, fireTick = true;
    static bool get(GameRules::GameRulesIndex index) {
        auto* l = level();
        if (!l) return true;
        GameRuleId id;
        id.mValue = static_cast<int>(index);
        return l->getGameRules().getBool(id, true);
    }
    GriefScope() {
        griefing = get(GameRules::GameRulesIndex::MobGriefing);
        fireTick = get(GameRules::GameRulesIndex::DoFireTick);
        (void)selftest::runServerCommand("gamerule mobgriefing false");
        (void)selftest::runServerCommand("gamerule dofiretick false");
    }
    ~GriefScope() {
        (void)selftest::runServerCommand(std::string("gamerule mobgriefing ") + (griefing ? "true" : "false"));
        (void)selftest::runServerCommand(std::string("gamerule dofiretick ") + (fireTick ? "true" : "false"));
    }
};

} // namespace

// vanishing blocks: a click starts a chain reaction through the connected family (2-6 ticks each)
GF_REGISTER_SELFTEST("darktower.vanishing_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2600, 12);
    (void)box;
    auto a = ctx.testPos(2602, 1).offset(0, 1, 0);
    for (int i = 0; i < 4; ++i) ctx.require(ctx.setBlock(a.offset(i, 0, 0), id::VanishingBlock), "vanishing block placed");
    placePlayer(ctx, a, -2.0f, 0.5f);
    co_await ctx.ticks(2);
    ctx.check(click(ctx, a) == ia::Result::Consume, "a click on an inactive vanishing block is taken");
    ctx.check(boolAt(ctx, a, "gloamingforest:active"), "the clicked block is active");
    co_await ctx.ticks(7);
    ctx.check(ctx.blockAt(a) == "minecraft:air", "the clicked block vanished within 2..6 ticks");
    co_await ctx.ticks(25);
    bool all = true;
    for (int i = 0; i < 4; ++i) all &= ctx.blockAt(a.offset(i, 0, 0)) == "minecraft:air";
    ctx.check(all, "the chain reaction removed every connected vanishing block");
});

// reappearing blocks: vanish 2-6 ticks after activation, warn 80 ticks later, reappear after 15
GF_REGISTER_SELFTEST("darktower.reappearing_cycle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2615, 8);
    (void)box;
    auto a = ctx.testPos(2617, 1).offset(0, 1, 0);
    ctx.require(ctx.setBlock(a, id::ReappearingBlock), "reappearing block placed");
    placePlayer(ctx, a, -2.0f, 0.5f);
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    devices::activateVanishing(*region, bp(a));
    co_await ctx.ticks(7);
    ctx.check(boolAt(ctx, a, "gloamingforest:vanished") && !boolAt(ctx, a, "gloamingforest:active"), "vanished within 2..6 ticks");
    co_await ctx.ticks(70);
    ctx.check(boolAt(ctx, a, "gloamingforest:vanished") && !boolAt(ctx, a, "gloamingforest:active"), "still vanished before 80 ticks");
    co_await ctx.ticks(13);
    ctx.check(boolAt(ctx, a, "gloamingforest:vanished") && boolAt(ctx, a, "gloamingforest:active"), "the 15-tick warning after 80 ticks");
    co_await ctx.ticks(16);
    ctx.check(ctx.blockAt(a) == id::ReappearingBlock && !boolAt(ctx, a, "gloamingforest:vanished") && !boolAt(ctx, a, "gloamingforest:active"),
              "the block reappeared");
});

// a locked door clicks until a tower key unlocks it; then it vanishes
GF_REGISTER_SELFTEST("darktower.locked_door", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2625, 8);
    (void)box;
    auto a = ctx.testPos(2627, 1).offset(0, 1, 0);
    ctx.require(ctx.setBlock(a, id::UnbreakableVanish), "door block placed");
    ctx.require(ctx.setBlock(a.offset(1, 0, 0), id::LockedVanish, {{"gloamingforest:locked", true}}), "lock placed");
    placePlayer(ctx, a, -2.0f, 0.5f);
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(devices::areBlocksLocked(*region, bp(a)), "the connected door is locked");
    click(ctx, a);
    co_await ctx.ticks(10);
    ctx.check(ctx.blockAt(a) == id::UnbreakableVanish && !boolAt(ctx, a, "gloamingforest:active"), "a locked door refuses (it only clicks)");
    // the key
    auto& p = ctx.player();
    static_cast<Player&>(p).setPlayerGameType(GameType::Survival);
    int slot = ctx.give(id::TowerKey, 2);
    ctx.require(slot >= 0, "tower key given");
    static_cast<Player&>(ctx.player()).setSelectedSlot(slot);
    co_await ctx.ticks(1);
    auto lock = a.offset(1, 0, 0);
    ctx.check(click(ctx, lock, id::TowerKey) == ia::Result::Consume, "the key is used on the lock");
    ctx.check(!boolAt(ctx, lock, "gloamingforest:locked"), "the lock is unlocked");
    ctx.check(game::items::countOf(ctx.player().getSelectedItem()) == 1, "one key was consumed");
    ctx.check(!devices::areBlocksLocked(*region, bp(a)), "the door is no longer locked");
    click(ctx, a);
    co_await ctx.ticks(30);
    ctx.check(ctx.blockAt(a) == "minecraft:air" && ctx.blockAt(lock) == "minecraft:air", "the unlocked door vanishes, lock included");
});

// carminite builder: a powered builder builds up to 17 blocks along the player's look, then times out
GF_REGISTER_SELFTEST("darktower.builder_bridge", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2635, 30);
    (void)box;
    auto b = ctx.testPos(2636, 2).offset(0, 3, 0);
    ctx.require(ctx.setBlock(b, id::Builder), "builder placed");
    // the player looks east (yaw -90), level
    placePlayer(ctx, b, 0.5f, 1.5f, 0.0f, -90.0f);
    co_await ctx.ticks(2);
    ctx.require(ctx.setBlock(b.offset(0, 1, 0), "minecraft:redstone_block"), "power placed");
    co_await ctx.ticks(6);
    ctx.check(ctx.blockAt(b) == id::Builder, "the builder is still there");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(textState(region->getBlock(bp(b)), "gloamingforest:state") == "builder_active", "the powered builder is active");
    // 17 blocks at one per 10 ticks
    for (int t = 0; t < 40; ++t) {
        placePlayer(ctx, b, 0.5f, 1.5f, 0.0f, -90.0f);
        co_await ctx.ticks(5);
    }
    int built = 0;
    for (int i = 1; i <= 20; ++i)
        if (ctx.blockAt(b.offset(i, 0, 0)) == id::BuiltBlock) ++built;
    ctx.check(built == 17, "17 built blocks along the look (got " + std::to_string(built) + ")");
    ctx.check(ctx.blockAt(b.offset(18, 0, 0)) == "minecraft:air", "the 18th position stays empty");
    // power off: the built blocks expire in a chain
    ctx.cleanupBlock(b.offset(0, 1, 0));
    remove(*region, bp(b.offset(0, 1, 0)));
    co_await ctx.ticks(10);
    ctx.check(textState(region->getBlock(bp(b)), "gloamingforest:state") != "builder_active", "the unpowered builder is no longer active");
    co_await ctx.ticks(17 * 10 + 40);
    int left = 0;
    for (int i = 1; i <= 20; ++i)
        if (ctx.blockAt(b.offset(i, 0, 0)) == id::BuiltBlock) ++left;
    ctx.check(left == 0, "every built block expired in a chain (left " + std::to_string(left) + ")");
    for (int i = 1; i <= 20; ++i) ctx.cleanupBlock(b.offset(i, 0, 0));
});

// antibuilder: refills a broken block with an antibuilt block and destroys a placed one
GF_REGISTER_SELFTEST("darktower.antibuilder", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2670, 14);
    (void)box;
    auto c = ctx.testPos(2676, 2).offset(0, 5, 0);
    for (int dx = -2; dx <= 2; ++dx) ctx.require(ctx.setBlock(c.offset(dx, -2, 0), "minecraft:stone"), "stone placed");
    ctx.require(ctx.setBlock(c, id::Antibuilder), "antibuilder placed");
    placePlayer(ctx, c, 0.5f, 3.5f);
    co_await ctx.ticks(25);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(runtime::tracked(c), "the antibuilder ticks");
    auto const* s = devices::antibuilderState(c);
    ctx.require(s && !s->snapshot.empty(), "the antibuilder captured its 9x9x9 snapshot");
    ctx.check(s->slowScan, "it starts in slow scan");
    // break a motion-blocking block: refilled with an antibuilt block
    remove(*region, bp(c.offset(1, -2, 0)));
    // place a block into snapshot air: destroyed
    ctx.require(ctx.setBlock(c.offset(2, 1, 0), "minecraft:oak_planks"), "a block placed into snapshot air");
    bool refilled = false, destroyed = false;
    for (int t = 0; t < 120 && !(refilled && destroyed); ++t) {
        co_await ctx.ticks(5);
        placePlayer(ctx, c, 0.5f, 3.5f);
        refilled |= ctx.blockAt(c.offset(1, -2, 0)) == id::Antibuilt;
        destroyed |= ctx.blockAt(c.offset(2, 1, 0)) == "minecraft:air";
    }
    ctx.check(refilled, "the broken stone came back as an antibuilt block");
    ctx.check(destroyed, "the placed planks were destroyed");
    s = devices::antibuilderState(c);
    ctx.check(s && !s->slowScan, "with an antibuilt block in range the antibuilder stays in fast scan");
    ctx.cleanupBlock(c.offset(1, -2, 0));
    removeAround(ctx, c, "minecraft:item", 6.0f);
});

// carminite reactor: six redstone blocks fire it up; fake gold/diamond at tick 5; it explodes at 350
GF_REGISTER_SELFTEST("darktower.reactor", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    GriefScope noGrief;
    auto box = co_await ctx.flatArea(2645, 24, 40);
    (void)box;
    auto c = ctx.testPos(2657, 2).offset(0, 18, 0);
    ctx.require(ctx.setBlock(c, id::Reactor), "reactor placed");
    placePlayer(ctx, c.offset(0, -18, 0), 0.5f, 0.5f);
    for (int f = 0; f < 6; ++f) {
        auto o = core::darktower::offsetOf(static_cast<core::darktower::Facing>(f));
        ctx.setBlock(c.offset(o[0], o[1], o[2]), "minecraft:redstone_block");
    }
    co_await ctx.ticks(3);
    ctx.check(boolAt(ctx, c, "gloamingforest:active"), "the reactor fired up");
    co_await ctx.ticks(8);
    ctx.check(ctx.blockAt(c.offset(0, 1, 0)) == id::FakeDiamond && ctx.blockAt(c.offset(1, 1, 0)) == id::FakeGold
                  && ctx.blockAt(c.offset(1, 0, 1)) == id::FakeGold,
              "the shell turned into fake diamond / gold");
    co_await ctx.ticks(350);
    ctx.check(ctx.blockAt(c) != id::Reactor, "the reactor is gone after 350 ticks");
    int ghastlings = countAround(ctx, c, id::Ghastling, 12.0f);
    ctx.check(ghastlings >= 6, "six carminite ghastlings appeared (" + std::to_string(ghastlings) + ")");
    removeAround(ctx, c, id::Ghastling, 16.0f);
    removeAround(ctx, c, "minecraft:item", 16.0f);
});

// slider: spawns its moving block when a player is near; the block damages 5
GF_REGISTER_SELFTEST("darktower.slider", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2685, 12);
    (void)box;
    auto s = ctx.testPos(2687, 2).offset(0, 1, 0);
    ctx.require(ctx.setBlock(s, id::Slider, {{"gloamingforest:axis", "y"}, {"gloamingforest:delay", 0}}), "slider placed");
    ctx.require(ctx.setBlock(s.offset(-1, 0, 0), "minecraft:stone"), "a wall behind it");
    placePlayer(ctx, s, 6.5f, 0.5f);
    bool spawned = false;
    for (int t = 0; t < 100 && !spawned; t += 5) {
        co_await ctx.ticks(5);
        spawned = countAround(ctx, s, id::SlideEntity, 4.0f) > 0;
    }
    ctx.check(spawned, "the sliding trap launched its block within 80 ticks");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(s) != id::Slider, "the moving block took the slider out of the wall");
    co_await ctx.ticks(120);
    ctx.check(countAround(ctx, s, id::SlideEntity, 8.0f) == 0, "the moving block landed again");
    removeAround(ctx, s, id::SlideEntity, 12.0f);
    removeAround(ctx, s, "minecraft:item", 12.0f);
    for (int dx = -1; dx <= 8; ++dx) ctx.cleanupBlock(s.offset(dx, 0, 0));
});

// the scripted fight: three dying ghastlings charge the trap, power fires it and it downs the
// Ur-Ghast (tantrum off, trapped, pulled, activate_ghast_trap for the players around)
GF_REGISTER_SELFTEST("darktower.ghast_trap_fight", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    GriefScope noGrief;
    auto box = co_await ctx.flatArea(2600, 30, 48);
    (void)box;
    // inside the flat area (z 0..29): the trap tower's walls reach 5 blocks to each side
    auto trap = ctx.testPos(2615, 2).offset(0, 1, 8);
    ctx.require(ctx.setBlock(trap, id::GhastTrap), "ghast trap placed");
    co_await ctx.ticks(2); // the block events dispatch on the next tick
    ctx.check(devices::ghastTrapIndexed(trap.dim, bp(trap)), "the ghast trap is in the POI index");
    // DarkTowerBossTrapComponent's open-topped tower: 11 wide (the trap at (5, 1, 5)), 9 high
    for (int y = 0; y < 9; ++y)
        for (int d = -5; d <= 5; ++d) {
            (void)ctx.setBlock(trap.offset(d, y, -5), "minecraft:stone");
            (void)ctx.setBlock(trap.offset(d, y, 5), "minecraft:stone");
            (void)ctx.setBlock(trap.offset(-5, y, d), "minecraft:stone");
            (void)ctx.setBlock(trap.offset(5, y, d), "minecraft:stone");
        }
    placePlayer(ctx, trap, 3.5f, 0.5f);
    static_cast<Player&>(ctx.player()).setPlayerGameType(GameType::Creative); // no fireballs at the test player
    static int  fired      = 0;
    static bool listening  = false;
    fired                  = 0;
    if (!listening) {
        listening = true;
        progression::onTrigger("gloamingforest:activate_ghast_trap", [](Player&, nlohmann::json const&) { ++fired; });
    }
    co_await ctx.ticks(3);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(runtime::tracked(trap), "the trap ticks");
    // three ghastlings die near the trap
    for (int i = 0; i < 3; ++i) {
        auto* g = spawnMob(*region, id::Ghastling, Vec3{trap.x + 2.5f + i, float(trap.y + 4), trap.z + 0.5f});
        ctx.require(g != nullptr, "a ghastling spawned");
        g->kill();
    }
    co_await ctx.ticks(5);
    ctx.check(devices::trapDyingGhasts(trap) >= 3, "the trap recorded 3 dying ghastlings (" + std::to_string(devices::trapDyingGhasts(trap)) + ")");
    // the Ur-Ghast above the trap, in a tantrum
    auto* ur = spawnMob(*region, id::UrGhast, Vec3{trap.x + 0.5f, float(trap.y + 18), trap.z + 0.5f});
    ctx.require(ur != nullptr, "the Ur-Ghast spawned");
    auto urId = uidOf(*ur);
    ghasts::setTantrum(*ur, true);
    removeAround(ctx, trap, id::Ghastling, 30.0f); // the minions the tantrum summons at the trap (not part of this script)
    ctx.check(ghasts::inTantrum(*ur), "the Ur-Ghast is in a tantrum");
    float before = static_cast<float>(ur->getHealth());
    // power the trap
    ctx.require(ctx.setBlock(trap.offset(0, 0, 1), "minecraft:redstone_block"), "power placed");
    co_await ctx.ticks(4);
    ctx.check(boolAt(ctx, trap, "gloamingforest:active"), "the charged, powered trap is active");
    ctx.check(fired >= 1, "activate_ghast_trap fired for the player within 6 blocks");
    co_await ctx.ticks(60);
    auto* again = actorById(urId);
    ctx.require(again != nullptr, "the Ur-Ghast is still there");
    ctx.check(!ghasts::inTantrum(*again), "the trap ended the tantrum");
    ctx.check(ghasts::trappedTicks(*again) > 0, "the Ur-Ghast is held in the trap");
    ctx.check(!ghasts::solid(*again), "the trap turned the Ur-Ghast's block collision off (noPhysics)");
    auto fp = again->getFeetPos();
    // pulled to trap + (0.5, 2.5, 0.5) through the tower's rim: its feet are inside the tower's box
    ctx.check(fp.y < trap.y + 9 && std::abs(fp.x - (trap.x + 0.5f)) < 4.5f && std::abs(fp.z - (trap.z + 0.5f)) < 4.5f,
              "the trap pulled the 14-wide Ur-Ghast down into the trap tower (feet " + std::to_string(fp.x - trap.x) + ", " + std::to_string(fp.y - trap.y) + ", " +
                  std::to_string(fp.z - trap.z) + " from the trap)");
    ctx.check(static_cast<float>(again->getHealth()) < before, "the trap damaged the Ur-Ghast (7 per hit)");
    co_await ctx.ticks(70);
    ctx.check(!boolAt(ctx, trap, "gloamingforest:active"), "the trap spun down after 120 ticks");
    if (auto* a = actorById(urId)) a->remove();
    removeAround(ctx, trap, id::Ghastling, 30.0f);
    ctx.require(ctx.setBlock(trap, "minecraft:air"), "trap removed");
    co_await ctx.ticks(2);
    ctx.check(!devices::ghastTrapIndexed(trap.dim, bp(trap)), "the removed trap left the POI index");
});

// Ur-Ghast phases: 18 damage starts a tantrum (the constructor's setInTantrum(false) resets upstream's field
// initializer of 10 to 18), which divides damage by 10; 18 more ends it
GF_REGISTER_SELFTEST("darktower.urghast_phases", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    GriefScope noGrief;
    auto box = co_await ctx.flatArea(2620, 24, 32);
    (void)box;
    auto at = ctx.testPos(2632, 2).offset(0, 6, 0);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    placePlayer(ctx, at, 12.5f, 0.5f);
    static_cast<Player&>(ctx.player()).setPlayerGameType(GameType::Creative);
    auto* ur = spawnMob(*region, id::UrGhast, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(ur != nullptr, "the Ur-Ghast spawned");
    auto id = uidOf(*ur);
    auto alive = [&]() -> Actor& {
        auto* a = actorById(id);
        ctx.require(a != nullptr, "the Ur-Ghast is still there");
        return *a;
    };
    co_await ctx.ticks(2);
    ctx.check(!ghasts::inTantrum(alive()), "it starts calm");
    ctx.check(ghasts::damageUntilNextPhase(alive()) == 18.0f, "the first tantrum is 18 damage away");
    combat::hurt(alive(), 11.0f, "minecraft:player_attack", &ctx.player());
    co_await ctx.ticks(12);
    ctx.check(!ghasts::inTantrum(alive()), "11 damage is not enough yet");
    combat::hurt(alive(), 8.0f, "minecraft:player_attack", &ctx.player());
    co_await ctx.ticks(12);
    ctx.check(ghasts::inTantrum(alive()), "18 damage started the tantrum");
    int h0 = alive().getHealth();
    combat::hurt(alive(), 30.0f, "minecraft:player_attack", &ctx.player());
    co_await ctx.ticks(12);
    int h1 = alive().getHealth();
    ctx.check(h0 - h1 <= 3 && h0 - h1 >= 2, "in tantrum damage is divided by 10 (lost " + std::to_string(h0 - h1) + ")");
    ctx.check(!ghasts::inTantrum(alive()) || ghasts::damageUntilNextPhase(alive()) < 18.0f, "the tantrum counts toward the next switch");
    ctx.check(combat::hurt(alive(), 5.0f, "minecraft:in_fire") == false, "fire does not hurt the Ur-Ghast");
    alive().remove();
    removeAround(ctx, at, id::Ghastling, 30.0f);
});

} // namespace gf::feature::darktower::tests
