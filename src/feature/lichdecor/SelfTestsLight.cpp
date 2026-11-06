// U22 (lichdecor): selftests of the candelabra, skull candles, ominous candles, ominous fire and
// brazier (docs/spec/lichdecor.md section 10). dx 2200..2249 (rule R12; 2250..2299 are the
// unit's other tests).

#include <map>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Redstone.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "feature/spawn/Spawn.h"
#include "feature/stats/Stats.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace gf::feature::lichdecor::light::tests {

namespace {

using gf::selftest::Context;
using core::GlobalPos;

BlockPos bp(GlobalPos const& g) { return game::blockPos(g); }
GlobalPos up(GlobalPos const& g, int n = 1) { return g.offset(0, n, 0); }

std::string st(Context& ctx, GlobalPos const& g, char const* name) {
    auto* r = ctx.region();
    return r ? state(r->getBlock(bp(g)), name) : std::string();
}

/// Puts `itemId` (or nothing) into the test player's hand.
void hold(Context& ctx, std::string const& itemId, int count = 1) {
    auto& p = ctx.player();
    if (itemId.empty()) p.setSelectedItem(ItemStack::EMPTY_ITEM());
    else if (auto s = game::items::make(itemId, count)) p.setSelectedItem(*s);
}

/// A right-click of the test player on `g` (face, block-local hit), with what it holds.
ia::Result click(Context& ctx, GlobalPos const& g, int face = 1, ia::Vec hit = {0.5, 1.0, 0.5}, bool sneaking = false) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const&     block = region->getBlock(bp(g));
    auto             bid   = game::blockId(block);
    ItemStack const& held  = p.getSelectedItem();
    std::string      hid   = game::items::idOf(held);
    ia::UseOnContext c{p, held, hid, block, bid, g, face, hit, sneaking, true};
    return ia::dispatchUseOn(c);
}

/// Vanilla placed the default block at `g` from a use-on of `face` at `clicked`: the place hooks.
void placed(Context& ctx, GlobalPos const& g, GlobalPos const& clicked, int face, std::string const& itemId) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return;
    Block const& block = region->getBlock(bp(g));
    auto         bid   = game::blockId(block);
    auto         stack = game::items::make(itemId, 1);
    ia::PlaceContext c{p, itemId, block, bid, g, clicked, face, {0.5, 0.5, 0.5}, false, stack ? &*stack : nullptr};
    ia::dispatchPlace(c);
}

void look(Context& ctx, GlobalPos const& g, float pitch, float yaw) {
    auto& p = ctx.player();
    p.teleportTo(Vec3{g.x + 0.5F, float(g.y), g.z + 2.5F}, true, 0, 0, false);
    const_cast<Vec2&>(p.getRotation()) = Vec2{pitch, yaw};
}

int count(Context& ctx, GlobalPos const& c, std::string const& type, float r) {
    auto* region = ctx.region();
    if (!region) return 0;
    int n = 0;
    for (auto* a : region->getEntities(AABB{Vec3{c.x - r, c.y - r, c.z - r}, Vec3{c.x + 1 + r, c.y + 1 + r, c.z + 1 + r}}, 0.0F))
        if (a && a->isAlive() && a->getTypeName() == type) ++n;
    return n;
}

/// Ominous fire damage dealt, by victim (registered once per process).
std::map<std::int64_t, int>& fireHits() {
    static std::map<std::int64_t, int> m;
    static bool once = [] {
        gf::combat::onDealt(gf::combat::priority::Lowest, [](gf::combat::DamageInfo const& d) {
            if (d.type == kOminousFireDamage) ++fireHits()[d.victim.getOrCreateUniqueID().rawID];
        });
        return true;
    }();
    (void)once;
    return m;
}

} // namespace

GF_REGISTER_SELFTEST("lichdecor.candelabra_slots", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2200, 4);
    (void)box;
    auto g = ctx.testPos(2200, 1);
    ctx.require(ctx.setBlock(g, kCandelabra, {{"twilightforest:facing", "south"}, {"twilightforest:on_wall", false}}), "candelabra set");
    // facing south, standing: slot by hit x (dir = up x south = (1, 0, 0)); not reversed
    hold(ctx, "minecraft:red_candle", 4);
    click(ctx, g, 1, {0.2, 1.0, 0.5});
    ctx.check(st(ctx, g, "has_candle_1") == "true", "a candle at hit x 0.2 fills slot 0");
    click(ctx, g, 1, {0.5, 1.0, 0.5});
    click(ctx, g, 1, {0.9, 1.0, 0.5});
    ctx.check(st(ctx, g, "has_candle_2") == "true" && st(ctx, g, "has_candle_3") == "true", "slots 1 and 2 by hit x 0.5 / 0.9");
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    auto d = dataAt(*r, bp(g));
    ctx.check(d.value("candles", Json::array()).size() == 3 && d["candles"][0] == "minecraft:red_candle", "the block entity keeps the candle ids");
    // light, dim, extinguish
    hold(ctx, "minecraft:flint_and_steel");
    click(ctx, g);
    ctx.check(st(ctx, g, "lighting") == "normal", "flint and steel lights it");
    ctx.check(gf::redstone::analogOutput(*r, bp(g)).value_or(-1) == 12, "comparator NORMAL = 9 + candles");
    hold(ctx, "minecraft:redstone");
    click(ctx, g);
    ctx.check(st(ctx, g, "lighting") == "dim", "redstone dims it");
    ctx.check(gf::redstone::analogOutput(*r, bp(g)).value_or(-1) == 6, "comparator DIM = 3 + candles");
    hold(ctx, "");
    click(ctx, g);
    ctx.check(st(ctx, g, "lighting") == "none", "an empty hand puts it out");
    ctx.check(gf::redstone::analogOutput(*r, bp(g)).value_or(-1) == 3, "comparator NONE = candles");
    // sneaking with an empty hand takes the candle of the slot back
    click(ctx, g, 1, {0.2, 1.0, 0.5}, true);
    ctx.check(st(ctx, g, "has_candle_1") == "false", "a sneaking empty hand takes the candle of slot 0");
    click(ctx, g, 1, {0.5, 1.0, 0.5}, true);
    click(ctx, g, 1, {0.9, 1.0, 0.5}, true);
    ctx.check(candelabraCandles(r->getBlock(bp(g))) == 0, "all candles removed");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.candelabra_support", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2204, 4);
    (void)box;
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    auto base = ctx.testPos(2204, 1);
    // standing on a bottom slab: refused (no top face at y 1), walls around are air
    ctx.require(ctx.setBlock(base, "minecraft:smooth_stone_slab"), "bottom slab");
    auto g = up(base);
    look(ctx, g, 60.0F, 180.0F);
    ctx.require(ctx.setBlock(g, kCandelabra), "candelabra set");
    placed(ctx, g, base, 1, kCandelabra);
    ctx.check(ctx.blockAt(g) != kCandelabra, "a standing candelabra on a bottom slab is refused");
    // standing on a fence post: accepted
    ctx.require(ctx.setBlock(base, "minecraft:oak_fence"), "fence");
    ctx.require(ctx.setBlock(g, kCandelabra), "candelabra set again");
    placed(ctx, g, base, 1, kCandelabra);
    ctx.check(ctx.blockAt(g) == kCandelabra && st(ctx, g, "on_wall") == "false", "a standing candelabra on a fence post is accepted");
    // the support goes: the candelabra stays (no survival check)
    ctx.setBlock(base, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(g) == kCandelabra, "breaking the support leaves the candelabra in place");
    // on a wall: the support is the block north of it, high in the air (no floor)
    auto wallAt = g.offset(1, 2, 0);
    auto north  = wallAt.offset(0, 0, -1);
    ctx.require(ctx.setBlock(north, "minecraft:smooth_stone_slab"), "wall slab");
    ctx.require(ctx.setBlock(wallAt, kCandelabra), "wall candelabra set");
    look(ctx, wallAt, 0.0F, 180.0F); // looking north
    placed(ctx, wallAt, north, 3, kCandelabra);
    ctx.check(ctx.blockAt(wallAt) != kCandelabra, "a wall candelabra against a bottom slab is refused");
    ctx.require(ctx.setBlock(north, "minecraft:oak_fence"), "wall fence");
    ctx.require(ctx.setBlock(wallAt, kCandelabra), "wall candelabra set again");
    look(ctx, wallAt, 0.0F, 180.0F);
    placed(ctx, wallAt, north, 3, kCandelabra);
    ctx.check(ctx.blockAt(wallAt) == kCandelabra && st(ctx, wallAt, "on_wall") == "true" && st(ctx, wallAt, "facing") == "north",
              "a wall candelabra on a fence post is accepted, facing its support");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.candelabra_signal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2210, 4);
    (void)box;
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    auto support = ctx.testPos(2210, 1);
    auto g       = up(support);
    ctx.require(ctx.setBlock(support, "minecraft:stone"), "support");
    ctx.require(ctx.setBlock(g, kCandelabra, {{"twilightforest:has_candle_1", true}, {"twilightforest:has_candle_2", true}, {"twilightforest:lighting", "dim"}}), "candelabra");
    setDataAt(*r, bp(g), Json{{"candles", Json::array({"minecraft:candle", "minecraft:candle", nullptr})}});
    syncCandelabraPower(*r, bp(g));
    ctx.check(gf::redstone::signalFrom(*r, bp(g), bp(support)) == 6, "DIM standing: 3 per candle into the block below");
    ctx.check(gf::redstone::signalFrom(*r, bp(g), bp(up(g))) == 0, "nothing into the block above");
    ctx.check(gf::redstone::signalFrom(*r, bp(g), bp(g.offset(1, 0, 0))) == 0, "nothing into the side");
    // a redstone lamp beside the support lights through it (strongly powered conductor)
    auto lamp = support.offset(1, 0, 0);
    ctx.require(ctx.setBlock(lamp, "minecraft:redstone_lamp"), "lamp");
    co_await ctx.ticks(6);
    ctx.check(ctx.blockAt(lamp) == "minecraft:lit_redstone_lamp", "a redstone lamp beside the support lights (" + ctx.blockAt(lamp) + ")");
    // the comparator table
    r = ctx.region();
    setStates(*r, bp(g), r->getBlock(bp(g)), Json{{"lighting", "ominous"}});
    ctx.check(gf::redstone::analogOutput(*r, bp(g)).value_or(-1) == 8, "comparator OMINOUS = 6 + candles");
    syncCandelabraPower(*r, bp(g));
    co_await ctx.ticks(6);
    ctx.check(ctx.blockAt(lamp) == "minecraft:redstone_lamp", "not DIM: the lamp goes out");
    // a wall candelabra powers only its support behind it
    auto wall = g.offset(0, 0, 1);
    ctx.require(ctx.setBlock(wall, kCandelabra,
                             {{"twilightforest:has_candle_1", true}, {"twilightforest:lighting", "dim"}, {"twilightforest:on_wall", true}, {"twilightforest:facing", "north"}}),
                "wall candelabra");
    r = ctx.region();
    ctx.check(gf::redstone::signalFrom(*r, bp(wall), bp(g)) == 3, "DIM wall: 3 per candle into the support at FACING");
    ctx.check(gf::redstone::signalFrom(*r, bp(wall), bp(wall.offset(0, -1, 0))) == 0, "nothing below a wall candelabra");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.brazier_cycle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2216, 4);
    (void)box;
    auto lower = ctx.testPos(2216, 1);
    auto upper = up(lower);
    ctx.require(ctx.setBlock(lower, kBrazier, {{"twilightforest:half", "lower"}}), "lower half");
    ctx.require(ctx.setBlock(upper, kBrazier, {{"twilightforest:half", "upper"}}), "upper half");
    char const* order[] = {"dim", "half", "bright", "full"};
    for (auto const* want : order) {
        hold(ctx, "minecraft:flint_and_steel");
        click(ctx, upper);
        ctx.check(st(ctx, upper, "light") == want, std::string("flint and steel -> ") + want);
    }
    hold(ctx, "minecraft:fire_charge");
    ctx.check(click(ctx, upper) == ia::Result::Pass && st(ctx, upper, "light") == "full", "no further at full");
    ctx.check(st(ctx, lower, "light") == "off", "the lower half's state is not cycled (as TF)");
    hold(ctx, "minecraft:water_bucket");
    click(ctx, upper);
    ctx.check(st(ctx, upper, "light") == "off", "a water bucket puts it out");
    ctx.check(game::items::idOf(ctx.player().getSelectedItem()) == "minecraft:bucket" || ctx.player().isCreative(), "the bucket is emptied");
    // the halves keep each other
    ctx.setBlock(upper, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(lower) != kBrazier, "the lower half goes with the upper half");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.skull_candle_create", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2220, 4);
    (void)box;
    auto g = ctx.testPos(2220, 1);
    ctx.require(ctx.setBlock(g, "minecraft:skeleton_skull", {{"facing_direction", 1}}), "skull");
    auto& cfg    = gf::config::mut<LichdecorConfig>();
    bool  saved  = cfg.disableSkullCandleCreation;
    long long before = gf::stats::value(ctx.player(), "twilightforest:skull_candles_made");
    // config off: nothing happens
    cfg.disableSkullCandleCreation = true;
    hold(ctx, "minecraft:red_candle", 2);
    click(ctx, g);
    ctx.check(ctx.blockAt(g) == "minecraft:skeleton_skull", "disableSkullCandleCreation keeps the skull");
    cfg.disableSkullCandleCreation = saved;
    // sneaking: nothing
    click(ctx, g, 1, {0.5, 0.5, 0.5}, true);
    ctx.check(ctx.blockAt(g) == "minecraft:skeleton_skull", "sneaking does not make a skull candle");
    hold(ctx, "minecraft:red_candle", 2);
    ctx.check(click(ctx, g) == ia::Result::Consume, "the candle is used on the skull");
    ctx.check(ctx.blockAt(g) == "twilightforest:skeleton_skull_candle", "the skull becomes a skeleton skull candle");
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    ctx.check(dataAt(*r, bp(g)).value("color", -1) == 5, "the candle colour is TF's RED (5)");
    ctx.check(st(ctx, g, "candles") == "1" && st(ctx, g, "lighting") == "none", "one unlit candle");
    ctx.check(gf::stats::value(ctx.player(), "twilightforest:skull_candles_made") == before + 1, "skull_candles_made counts it");
    // the same colour adds one; sneaking with an empty hand takes them back to the skull
    hold(ctx, "minecraft:red_candle", 1);
    click(ctx, g);
    ctx.check(st(ctx, g, "candles") == "2", "a red candle adds one");
    hold(ctx, "");
    click(ctx, g, 1, {0.5, 0.5, 0.5}, true);
    click(ctx, g, 1, {0.5, 0.5, 0.5}, true);
    ctx.check(ctx.blockAt(g) == "minecraft:skeleton_skull", "the last candle taken off restores the vanilla skull");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.ominous_candle_revert", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2224, 4);
    (void)box;
    auto g = ctx.testPos(2224, 1);
    ctx.require(ctx.setBlock(g, "twilightforest:ominous_red_candle", {{"twilightforest:candles", 2}}), "ominous candle");
    hold(ctx, "minecraft:red_candle");
    click(ctx, g);
    ctx.check(st(ctx, g, "candles") == "3", "the matching vanilla candle adds one");
    hold(ctx, "minecraft:blue_candle");
    ctx.check(click(ctx, g) == ia::Result::Pass && st(ctx, g, "candles") == "3", "another colour does not");
    hold(ctx, "");
    click(ctx, g);
    ctx.check(ctx.blockAt(g) == "minecraft:red_candle", "an empty hand turns it back into the vanilla candle");
    ctx.check(st(ctx, g, "candles") == "2" && st(ctx, g, "lit") == "false", "the same count (3), unlit");
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.ominous_fire_conversion", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2228, 4);
    (void)box;
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    auto   g        = ctx.testPos(2228, 2);
    Actor* villager = gf::spawn::spawnMob(*r, "minecraft:villager_v2", Vec3{g.x + 0.5F, float(g.y), g.z + 0.5F}, gf::spawn::SpawnReason::Command);
    ctx.require(villager != nullptr, "villager spawned");
    auto uid = villager->getOrCreateUniqueID().rawID;
    gf::combat::hurt(*villager, 1000.0F, kOminousFireDamage);
    co_await ctx.ticks(10);
    int zombies = count(ctx, g, "minecraft:zombie_villager_v2", 3.0F) + count(ctx, g, "minecraft:zombie_villager", 3.0F);
    ctx.check(zombies == 1, "a villager killed by ominous fire becomes a zombie villager (" + std::to_string(zombies) + ")");
    ActorUniqueID id;
    id.rawID = uid;
    auto level = ll::service::getLevel();
    ctx.check(!level || !level->fetchEntity(id, false), "the villager is gone");
    // cleanup
    if (r = ctx.region(); r)
        for (auto* a : r->getEntities(AABB{Vec3{g.x - 4.0F, g.y - 2.0F, g.z - 4.0F}, Vec3{g.x + 5.0F, g.y + 4.0F, g.z + 5.0F}}, 0.0F))
            if (a && !a->isPlayer()) a->remove();
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.ominous_fire_entity", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2232, 4);
    (void)box;
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    auto& hits = fireHits();
    auto  a    = ctx.testPos(2232, 1);
    auto  b    = ctx.testPos(2234, 1);
    auto  c    = ctx.testPos(2236, 1);
    for (auto const& at : {a, b, c}) {
        ctx.require(ctx.setBlock(at, kOminousFire), "fire");
        // a cage keeps the mobs in the fire, a roof keeps the sun off
        for (auto const& o : {at.offset(1, 0, 0), at.offset(-1, 0, 0), at.offset(0, 0, 1), at.offset(0, 0, -1), at.offset(1, 1, 0), at.offset(-1, 1, 0), at.offset(0, 1, 1),
                              at.offset(0, 1, -1)})
            ctx.setBlock(o, "minecraft:glass");
        ctx.setBlock(at.offset(0, 2, 0), "minecraft:stone");
    }
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(a) == kOminousFire, "the fire survives on grass");
    r          = ctx.region();
    Actor* pig = gf::spawn::spawnMob(*r, "minecraft:pig", Vec3{a.x + 0.5F, float(a.y), a.z + 0.5F}, gf::spawn::SpawnReason::Command);
    Actor* zom = gf::spawn::spawnMob(*r, "minecraft:zombie", Vec3{b.x + 0.5F, float(b.y), b.z + 0.5F}, gf::spawn::SpawnReason::Command);
    ctx.require(pig && zom, "mobs spawned");
    auto pigId = pig->getOrCreateUniqueID().rawID, zomId = zom->getOrCreateUniqueID().rawID;
    hits.erase(pigId);
    hits.erase(zomId);
    auto ingot = game::items::make("minecraft:netherite_ingot", 1);
    ctx.require(ingot.has_value(), "ingot");
    auto* item = r->getLevel().getSpawner().spawnItem(*r, *ingot, nullptr, Vec3{c.x + 0.5F, c.y + 0.1F, c.z + 0.5F}, 0);
    ctx.require(item != nullptr, "item spawned");
    auto itemId = item->getOrCreateUniqueID().rawID;
    co_await ctx.ticks(30);
    auto level = ll::service::getLevel();
    auto find  = [&](std::int64_t uid) -> Actor* {
        ActorUniqueID id;
        id.rawID = uid;
        return level ? level->fetchEntity(id, false) : nullptr;
    };
    ctx.check(hits[pigId] > 0, "a pig in it takes twilightforest:ominous_fire damage");
    if (auto* p = find(pigId)) {
        ctx.check(!p->isOnFire(), "and is never set on fire");
        p->remove();
    }
    ctx.check(hits[zomId] == 0, "a zombie (undead) takes none");
    if (auto* z = find(zomId)) z->remove();
    auto* left = find(itemId);
    ctx.check(!left || !left->isAlive(), "a netherite ingot resting in it is gone after about 20 ticks");
    if (left) left->remove();
    co_return;
});

GF_REGISTER_SELFTEST("lichdecor.ominous_fire_portal", [](Context& ctx) -> ll::coro::CoroTask<> {
    // an obsidian frame (4 wide, 5 tall, inside 2 x 3) along x
    auto build = [&](GlobalPos const& base) {
        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 5; ++y) {
                bool edge = x == 0 || x == 3 || y == 0 || y == 4;
                ctx.setBlock(base.offset(x, y, 0), edge ? "minecraft:obsidian" : "minecraft:air");
            }
    };
    auto box = co_await ctx.flatArea(2240, 6);
    (void)box;
    auto base = ctx.testPos(2240, 2);
    build(base);
    auto inside = base.offset(1, 1, 0);
    ctx.require(ctx.setBlock(inside, kOminousFire), "fire in the frame");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(inside) == "minecraft:portal", "in the Overworld the frame lights a nether portal (" + ctx.blockAt(inside) + ")");
    for (int x = 1; x < 3; ++x)
        for (int y = 1; y < 4; ++y) ctx.setBlock(base.offset(x, y, 0), "minecraft:air");
    if (!co_await ctx.inTwilight()) {
        ctx.info("the Twilight Forest is not available: its half of the test is skipped");
        co_return;
    }
    auto tbox = co_await ctx.flatArea(2240, 6);
    (void)tbox;
    auto tbase = ctx.testPos(2240, 2);
    build(tbase);
    auto tin = tbase.offset(1, 1, 0);
    ctx.require(ctx.setBlock(tin, kOminousFire), "fire in the Twilight frame");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(tin) == kOminousFire, "in the Twilight Forest it does not (" + ctx.blockAt(tin) + ")");
    co_return;
});

} // namespace gf::feature::lichdecor::light::tests
