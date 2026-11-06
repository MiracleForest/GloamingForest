// U30 (finalcastle): selftests of the blocks part (rule R12: finalcastle.*, dx 3000..3049 for the
// blocks, dz 0..4): castle doors (use, redstone, chain, reappearing, no collision while vanished),
// force fields (placement connections, the three-connection limit, updateShape only adding,
// unbreakable), thorns (damage, the immune items, burst for survival players, creative breaking,
// connections), burnt thorns dissolving, the thorn rose's survival, the cinder furnace (smelting, the
// speed and output multipliers of cinder logs, charring logs) and the thorns feature (grows, and not
// above sea level + 150).
#include <algorithm>
#include <cmath>
#include <vector>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/features/Features.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "feature/inventory/Inventory.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::finalcastle::blocks::tests {

using gf::selftest::Context;
namespace ia    = gf::interactions;
namespace items = gf::game::items;

namespace {

BlockPos bp(core::GlobalPos const& g) { return game::blockPos(g); }

bool flag(Context& ctx, core::GlobalPos const& g, char const* state) {
    auto* r = ctx.region();
    return r && boolStateAt(*r, bp(g), state);
}

std::string text(Context& ctx, core::GlobalPos const& g, char const* state) {
    auto* r = ctx.region();
    return r ? textState(r->getBlock(bp(g)), state) : std::string{};
}

/// A right-click of the test player on `g`.
ia::Result click(Context& ctx, core::GlobalPos const& g) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const&     block = region->getBlock(bp(g));
    auto             bid   = game::blockId(block);
    ItemStack const& held  = p.getSelectedItem();
    std::string      hid;
    ia::UseOnContext c{p, held, hid, block, bid, g, 1, {0.5, 1.0, 0.5}, false, true};
    return ia::dispatchUseOn(c);
}

/// The test player breaking `g` (GameMode::destroyBlock's dispatch).
ia::Result breakBlock(Context& ctx, core::GlobalPos const& g) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const&     block = region->getBlock(bp(g));
    auto             bid   = game::blockId(block);
    ItemStack const& held  = p.getSelectedItem();
    std::string      hid;
    ia::DestroyContext c{p, held, hid, block, bid, g, 1, false};
    return ia::dispatchDestroy(c);
}

/// The test player's placement of the block now at `g`, clicked on face `face` of `clicked`.
void placed(Context& ctx, core::GlobalPos const& g, core::GlobalPos const& clicked, int face, bool sneaking = false) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return;
    Block const& block = region->getBlock(bp(g));
    auto         bid   = game::blockId(block);
    ia::PlaceContext c{p, bid, block, bid, g, clicked, face, {0.5, 0.5, 0.5}, sneaking};
    ia::dispatchPlace(c);
}

bool hasCollision(Context& ctx, core::GlobalPos const& g) {
    auto* r = ctx.region();
    if (!r) return false;
    Block const&      b = r->getBlock(bp(g));
    std::vector<AABB> boxes;
    b.getBlockType().addCollisionShapes(b, *r, bp(g), nullptr, boxes, {});
    return !boxes.empty();
}

Actor* spawn(Context& ctx, std::string const& type, core::GlobalPos const& g) {
    auto* r = ctx.region();
    auto* l = level();
    if (!r || !l) return nullptr;
    return l->getSpawner().spawnMob(*r, ActorDefinitionIdentifier(type), nullptr, Vec3{g.x + 0.5f, float(g.y), g.z + 0.5f}, false, false, false);
}

Actor* dropItem(Context& ctx, std::string const& itemId, core::GlobalPos const& g) {
    auto* r = ctx.region();
    auto* l = level();
    auto  s = items::make(itemId);
    if (!r || !l || !s) return nullptr;
    return l->getSpawner().spawnItem(*r, *s, nullptr, Vec3{g.x + 0.5f, g.y + 0.3f, g.z + 0.5f}, 0);
}

Actor* byId(std::int64_t uid) {
    auto* l = level();
    if (!l) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return l->fetchEntity(id, false);
}

/// The thorns damage dealt so far (victim unique id), recorded through the combat contract.
std::vector<std::pair<std::int64_t, float>>& thornHits() {
    static std::vector<std::pair<std::int64_t, float>> v;
    static bool registered = false;
    if (!registered) {
        registered = true;
        combat::onDealt(combat::priority::Lowest, [](combat::DamageInfo const& d) {
            if (d.type == "twilightforest:thorns") thornHits().emplace_back(d.victim.getOrCreateUniqueID().rawID, d.amount);
        });
    }
    return v;
}

/// Java's WorldGenRegion over the live world, for placing the feature in a test.
class LiveRegion final : public worldgen::Region {
public:
    LiveRegion(BlockSource& region, BlockPos const& at) : mRegion(region), mCx(at.x >> 4), mCz(at.z >> 4) {}
    int          centerChunkX() const override { return mCx; }
    int          centerChunkZ() const override { return mCz; }
    int          minY() const override { return mRegion.getMinHeight(); }
    int          maxY() const override { return mRegion.getMaxHeight(); }
    bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override { return game::setBlock(mRegion, pos, block); }
    int          height(worldgen::Heightmap, int x, int z) const override {
        for (int y = maxY() - 1; y > minY(); --y)
            if (!mRegion.getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
        return minY();
    }
    std::string  biome(BlockPos const&) const override { return "twilightforest:thornlands"; }
    std::int64_t seed() const override { return 0; }
    int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

private:
    BlockSource& mRegion;
    int          mCx, mCz;
};

} // namespace

// a used door goes active, vanishes 2..6 ticks later (no collision), passes it on to its neighbours
// and comes back 80 ticks later (active again, then solid)
GF_REGISTER_SELFTEST("finalcastle.door_use_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3000, 5, 8);
    (void)box;
    auto a = ctx.testPos(3001, 2);
    for (int i = 0; i < 3; ++i) ctx.require(ctx.setBlock(a.offset(i, 0, 0), id::YellowDoor), "door placed");
    ctx.require(ctx.setBlock(a.offset(0, 1, 0), id::BlueDoor), "a door of another colour above");
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    co_await ctx.ticks(2);
    ctx.check(hasCollision(ctx, a), "a solid door has collision");
    ctx.check(click(ctx, a) == ia::Result::Consume, "a click on a solid door is taken");
    ctx.check(flag(ctx, a, "twilightforest:active"), "the clicked door is active");
    ctx.check(click(ctx, a) == ia::Result::Pass, "a click on an active door fails (the item would be used)");
    int vanishedAt = -1;
    for (int t = 1; t <= 8 && vanishedAt < 0; ++t) {
        co_await ctx.ticks(1);
        if (flag(ctx, a, "twilightforest:vanish")) vanishedAt = t;
    }
    ctx.check(vanishedAt >= 2 && vanishedAt <= 7, "the door vanishes 2..6 ticks after the click (" + std::to_string(vanishedAt) + ")");
    ctx.check(!flag(ctx, a, "twilightforest:active"), "a vanished door is inactive");
    ctx.check(!hasCollision(ctx, a), "a vanished door has no collision");
    co_await ctx.ticks(20);
    ctx.check(flag(ctx, a.offset(1, 0, 0), "twilightforest:vanish"), "the neighbour door vanished too (chain)");
    ctx.check(flag(ctx, a.offset(2, 0, 0), "twilightforest:vanish"), "the door two blocks away vanished too");
    ctx.check(flag(ctx, a.offset(0, 1, 0), "twilightforest:vanish"), "doors of other colours join the chain");
    // 80 ticks after vanishing: active again (still vanished), then solid 2..6 ticks later
    co_await ctx.ticks(80 - 20 - 1);
    ctx.check(flag(ctx, a, "twilightforest:vanish"), "still vanished before 80 ticks");
    co_await ctx.ticks(9);
    ctx.check(!flag(ctx, a, "twilightforest:vanish"), "the door reappeared within 80 + 2..6 ticks");
    ctx.check(hasCollision(ctx, a), "the reappeared door is solid");
    co_await ctx.ticks(30);
    for (int i = 0; i < 3; ++i) {
        ctx.check(!flag(ctx, a.offset(i, 0, 0), "twilightforest:vanish") && !flag(ctx, a.offset(i, 0, 0), "twilightforest:active"),
                  "door " + std::to_string(i) + " is solid and inactive again");
    }
});

// a redstone signal from a block that is not a door activates it
GF_REGISTER_SELFTEST("finalcastle.door_redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(3006, 2);
    ctx.require(ctx.setBlock(a, id::PinkDoor), "door placed");
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    co_await ctx.ticks(2);
    ctx.require(ctx.setBlock(a.offset(1, 0, 0), "minecraft:redstone_block"), "redstone block placed beside it");
    int vanishedAt = -1;
    for (int t = 1; t <= 10 && vanishedAt < 0; ++t) {
        co_await ctx.ticks(1);
        if (flag(ctx, a, "twilightforest:vanish")) vanishedAt = t;
    }
    ctx.check(vanishedAt > 0, "a powered door vanishes (" + std::to_string(vanishedAt) + " ticks)");
});

// ForceFieldBlock#getStateForPlacement / canConnectTo / updateShape, and unbreakable
GF_REGISTER_SELFTEST("finalcastle.force_field", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(3010, 1);
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    // placed on top of a stone block (clicked face up): down towards the stone, up (not sneaking)
    ctx.require(ctx.setBlock(a, "minecraft:stone"), "stone");
    auto f = a.offset(0, 1, 0);
    ctx.require(ctx.setBlock(f, id::VioletField), "force field");
    r = ctx.region();
    auto on = fieldPlacement(*r, bp(f), Up, false);
    ctx.check(on[Down] && on[Up] && !on[North] && !on[South] && !on[West] && !on[East], "placed on stone: down and up only");
    auto sneak = fieldPlacement(*r, bp(f), Up, true);
    ctx.check(sneak[Down] && !sneak[Up], "sneaking leaves the clicked direction off");
    placed(ctx, f, a, Up);
    ctx.check(flag(ctx, f, "twilightforest:down") && flag(ctx, f, "twilightforest:up") && !flag(ctx, f, "twilightforest:north"), "the placement handler sets the connections");
    // updateShape only adds: a sturdy neighbour connects, removing it keeps the connection
    ctx.require(ctx.setBlock(f.offset(1, 0, 0), "minecraft:stone"), "stone east");
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, f, "twilightforest:east"), "a sturdy neighbour connects");
    ctx.require(ctx.setBlock(f.offset(1, 0, 0), "minecraft:air"), "stone removed");
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, f, "twilightforest:east"), "the connection stays (updateShape never clears one)");
    // leaves are an exception for connection
    ctx.require(ctx.setBlock(f.offset(-1, 0, 0), "minecraft:oak_leaves"), "leaves west");
    co_await ctx.ticks(3);
    ctx.check(!flag(ctx, f, "twilightforest:west"), "leaves do not connect");
    // three connections already (down, up, east): a further sturdy neighbour is refused, a field of
    // the same colour is not
    ctx.require(ctx.setBlock(f.offset(0, 0, -1), "minecraft:stone"), "stone north");
    co_await ctx.ticks(3);
    ctx.check(!flag(ctx, f, "twilightforest:north"), "three connections refuse a fourth sturdy neighbour");
    ctx.require(ctx.setBlock(f.offset(0, 0, 1), id::VioletField), "a field south");
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, f, "twilightforest:south"), "a field of the same colour always connects");
    r = ctx.region();
    // unbreakable: not destructible by mining
    ctx.check(r->getBlock(bp(f)).getBlockType().getDestroySpeed() < 0.0f, "force fields are unbreakable");
});

// brown / green thorns hurt for 4 (thorns), spare the immune items, burst for survival players
GF_REGISTER_SELFTEST("finalcastle.thorns_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3015, 5, 6);
    (void)box;
    auto a = ctx.testPos(3016, 2);
    ctx.require(ctx.setBlock(a, id::BrownThorns, {{"twilightforest:axis", "y"}}), "thorns");
    ctx.require(ctx.setBlock(a.offset(2, 0, 0), id::GreenThorns, {{"twilightforest:axis", "y"}}), "green thorns");
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    (void)thornHits();
    thornHits().clear();
    auto* pig = spawn(ctx, "minecraft:pig", a);
    ctx.require(pig != nullptr, "pig spawned in the thorns");
    auto pigId = pig->getOrCreateUniqueID().rawID;
    auto* rose = dropItem(ctx, id::ThornRose, a.offset(2, 0, 0));
    auto* dirt = dropItem(ctx, "minecraft:dirt", a.offset(2, 0, 0));
    ctx.require(rose && dirt, "items dropped into the green thorns");
    auto roseId = rose->getOrCreateUniqueID().rawID;
    auto dirtId = dirt->getOrCreateUniqueID().rawID;
    co_await ctx.ticks(10);
    bool pigHit = std::any_of(thornHits().begin(), thornHits().end(), [&](auto const& h) { return h.first == pigId && std::abs(h.second - 4.0f) < 0.01f; });
    ctx.check(pigHit, "the pig took 4 thorns damage");
    // ItemEntity#hurt: 5 health, so two touches destroy an item that is not immune
    auto* roseNow = byId(roseId);
    auto* dirtNow = byId(dirtId);
    ctx.check(roseNow && roseNow->isAlive(), "a thorn rose item is immune to thorns");
    ctx.check(!dirtNow || !dirtNow->isAlive(), "other items are destroyed by thorns");
    if (auto* p = byId(pigId)) p->remove();
    if (auto* i = byId(roseId)) i->remove();
    if (auto* i = byId(dirtId)) i->remove();
});

GF_REGISTER_SELFTEST("finalcastle.thorns_burst", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3020, 5, 8);
    (void)box;
    auto a = ctx.testPos(3022, 2).offset(0, 3, 0);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    ctx.player().setPlayerGameType(GameType::Survival);
    int greens = 0;
    for (int attempt = 0; attempt < 8 && greens == 0; ++attempt) {
        ctx.require(ctx.setBlock(a, id::BrownThorns, {{"twilightforest:axis", "y"}}), "thorns");
        ctx.check(breakBlock(ctx, a) == ia::Result::Deny, "a survival player cannot break thorns");
        co_await ctx.ticks(2);
        ctx.check(ctx.blockAt(a) == id::BrownThorns, "the thorns stay");
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy)
                for (int dz = -2; dz <= 2; ++dz) {
                    auto p = a.offset(dx, dy, dz);
                    if (ctx.blockAt(p) != id::GreenThorns) continue;
                    ++greens;
                    // the green thorns lie along their direction
                    int axis = dx != 0 ? 0 : (dy != 0 ? 1 : 2);
                    ctx.check(text(ctx, p, "twilightforest:axis") == axisName(axis), "burst thorns take the axis of their direction");
                    ctx.cleanupBlock(p);
                }
    }
    ctx.check(greens > 0, "breaking thorns grows a burst of green thorns (" + std::to_string(greens) + ")");
    ctx.player().setPlayerGameType(GameType::Creative);
    ctx.check(breakBlock(ctx, a) == ia::Result::Pass, "a creative player breaks thorns normally");
    ctx.player().setPlayerGameType(GameType::Survival);
});

// thorns connect to thorns, the rose, thorn leaves and weathered deadrock; air keeps the connection
GF_REGISTER_SELFTEST("finalcastle.thorns_connect", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(3026, 2).offset(0, 2, 0);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    ctx.require(ctx.setBlock(a.offset(0, 0, -1), id::ThornLeaves), "leaves north");
    ctx.require(ctx.setBlock(a.offset(-1, 0, 0), "minecraft:stone"), "stone west");
    ctx.require(ctx.setBlock(a, id::BrownThorns), "thorns");
    placed(ctx, a, a.offset(0, -1, 0), Up);
    ctx.check(text(ctx, a, "twilightforest:axis") == "y", "placed on a top face: axis y");
    ctx.check(flag(ctx, a, "twilightforest:north"), "connects to thorn leaves");
    ctx.check(!flag(ctx, a, "twilightforest:west"), "does not connect to stone");
    ctx.require(ctx.setBlock(a.offset(1, 0, 0), id::GreenThorns), "green thorns east");
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, a, "twilightforest:east"), "a thorns neighbour connects (updateShape)");
    ctx.require(ctx.setBlock(a.offset(1, 0, 0), "minecraft:air"), "green thorns removed");
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, a, "twilightforest:east"), "an air neighbour keeps the connection");
    ctx.require(ctx.setBlock(a.offset(1, 0, 0), "minecraft:stone"), "stone east");
    co_await ctx.ticks(3);
    ctx.check(!flag(ctx, a, "twilightforest:east"), "a stone neighbour clears it");
});

// burnt thorns dissolve when a living entity walks into them
GF_REGISTER_SELFTEST("finalcastle.burnt_thorns", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(3030, 2);
    ctx.require(ctx.setBlock(a, id::BurntThorns, {{"twilightforest:axis", "y"}}), "burnt thorns");
    ctx.require(ctx.setBlock(a.offset(2, 0, 0), id::BurntThorns, {{"twilightforest:axis", "y"}}), "burnt thorns (item test)");
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto* item = dropItem(ctx, "minecraft:dirt", a.offset(2, 0, 0));
    auto* pig  = spawn(ctx, "minecraft:pig", a);
    ctx.require(pig != nullptr, "pig spawned in the burnt thorns");
    auto pigId  = pig->getOrCreateUniqueID().rawID;
    auto itemId = item ? item->getOrCreateUniqueID().rawID : 0;
    co_await ctx.ticks(5);
    ctx.check(ctx.blockAt(a) == "minecraft:air", "burnt thorns dissolve under a living entity");
    ctx.check(ctx.blockAt(a.offset(2, 0, 0)) == id::BurntThorns, "an item does not dissolve them");
    if (auto* p = byId(pigId)) p->remove();
    if (auto* i = byId(itemId)) i->remove();
});

// ThornRoseBlock#canSurvive
GF_REGISTER_SELFTEST("finalcastle.thorn_rose", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(3034, 2).offset(0, 2, 0);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    ctx.require(ctx.setBlock(a, "minecraft:stone"), "stone");
    auto rose = a.offset(0, 1, 0);
    ctx.require(ctx.setBlock(rose, id::ThornRose, {{"twilightforest:facing", "up"}}), "rose on the stone");
    auto* r = ctx.region();
    ctx.check(roseCanSurvive(*r, bp(rose), Up), "a rose survives on a sturdy face behind it");
    ctx.check(!roseCanSurvive(*r, bp(rose), North), "facing north it has air behind it");
    ctx.require(ctx.setBlock(rose.offset(0, 0, 1), id::BrownThorns), "thorns south of it");
    r = ctx.region();
    ctx.check(roseCanSurvive(*r, bp(rose), North), "facing north it survives on the thorns behind it");
    ctx.require(ctx.setBlock(a, "minecraft:air"), "stone removed");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(rose) == "minecraft:air", "the rose breaks without its support");
});

// CinderFurnaceBlockEntity: smelting, cinder log multipliers, charring logs
GF_REGISTER_SELFTEST("finalcastle.cinder_furnace", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3038, 5, 6);
    (void)box;
    auto f = ctx.testPos(3040, 2).offset(0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(f), "test area loaded");
    ctx.require(ctx.setBlock(f, id::CinderFurnace), "furnace placed");
    auto* r = ctx.region();
    ctx.check(click(ctx, f) == ia::Result::Consume, "a click on the furnace is taken (it opens nothing)");
    auto h = inventory::find(*r, bp(f));
    ctx.require(h && h->get && h->set && h->slots == 3, "the furnace has three slots");
    auto coal = items::make("minecraft:coal", 4);
    auto logs = items::make("minecraft:oak_log", 8);
    ctx.require(coal && logs, "items");
    ctx.info("coal burns " + std::to_string(static_cast<int>(FurnaceBlockActor::getBurnDuration(*coal, 200.0f))) + " ticks");
    // 20 cinder logs around: speed x10, output x2 (and up to x3)
    int n = 0;
    for (int dx = -1; dx <= 1 && n < 20; ++dx)
        for (int dy = -1; dy <= 1 && n < 20; ++dy)
            for (int dz = -1; dz <= 1 && n < 20; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                ctx.require(ctx.setBlock(f.offset(dx, dy, dz), id::CinderLog), "cinder log");
                ++n;
            }
    r = ctx.region();
    ctx.check(countCinderLogs(*r, bp(f)) == 20, "20 cinder logs counted around the furnace");
    int m2 = cinderMultiplier(*r, bp(f), 2);
    ctx.check(m2 == 10, "speed multiplier with 20 logs: 10 (" + std::to_string(m2) + ")");
    int m10 = cinderMultiplier(*r, bp(f), 10);
    ctx.check(m10 == 2, "output multiplier with 20 logs: 2 (" + std::to_string(m10) + ")");
    h->set(f, 0, *logs);
    h->set(f, 1, *coal);
    co_await ctx.ticks(3);
    ctx.check(flag(ctx, f, "twilightforest:lit"), "the furnace lights");
    auto st = furnaceState(f);
    ctx.check(st.litTime > 0 && st.cookingProgress >= 10, "it burns and cooks 10 per tick (" + std::to_string(st.cookingProgress) + ")");
    co_await ctx.ticks(25);
    r = ctx.region();
    h = inventory::find(*r, bp(f));
    ctx.require(h.has_value(), "handler");
    ItemStack out = h->get(f, 2);
    ctx.check(items::idOf(out) == "minecraft:charcoal", "logs smelt into charcoal (" + items::idOf(out) + ")");
    ctx.check(items::countOf(out) >= 2, "the output is multiplied (" + std::to_string(items::countOf(out)) + ")");
    ctx.check(items::countOf(h->get(f, 0)) < 8, "the input is used");
    h->set(f, 0, items::empty());
    h->set(f, 1, items::empty());
    h->set(f, 2, items::empty());
});

GF_REGISTER_SELFTEST("finalcastle.cinder_charring", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3043, 5, 6);
    (void)box;
    auto f = ctx.testPos(3045, 2).offset(0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(f), "test area loaded");
    ctx.require(ctx.setBlock(f, id::CinderFurnace), "furnace placed");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                ctx.require(ctx.setBlock(f.offset(dx, dy, dz), "minecraft:oak_log"), "oak log");
            }
    auto* r = ctx.region();
    auto  h = inventory::find(*r, bp(f));
    ctx.require(h.has_value(), "handler");
    auto ore  = items::make("minecraft:raw_iron", 16);
    auto coal = items::make("minecraft:coal", 2);
    ctx.require(ore && coal, "items");
    h->set(f, 0, *ore);
    h->set(f, 1, *coal);
    co_await ctx.ticks(60);
    r = ctx.region();
    int cinder = countCinderLogs(*r, bp(f));
    ctx.check(cinder > 0, "a burning cinder furnace chars the logs around it (" + std::to_string(cinder) + ")");
    h = inventory::find(*r, bp(f));
    if (h) {
        for (int i = 0; i < 3; ++i) h->set(f, i, items::empty());
    }
});

// ThornFeature: grows thorns (the stalk goes up), not above sea level + 150
GF_REGISTER_SELFTEST("finalcastle.thorn_feature", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(3030, 16, 48);
    auto origin = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY, box.minZ + 8};
    ctx.require(co_await ctx.ensureLoaded(origin), "test area loaded");
    auto* r = ctx.region();
    ctx.require(r != nullptr, "region");
    LiveRegion        live(*r, bp(origin));
    core::jrand::LegacyRandom rand(1234);
    int placedAny = 0;
    for (int attempt = 0; attempt < 4 && placedAny == 0; ++attempt) {
        ctx.check(features::place("twilightforest:thorns", live, bp(origin), rand), "the configured feature places");
        for (int x = box.minX; x <= box.maxX; ++x)
            for (int y = box.minY; y <= box.maxY; ++y)
                for (int z = box.minZ; z <= box.maxZ; ++z)
                    if (r->getBlock(BlockPos{x, y, z}).getTypeName() == id::BrownThorns) ++placedAny;
    }
    ctx.check(placedAny >= 2, "brown thorns grew (" + std::to_string(placedAny) + ")");
    ctx.check(ctx.blockAt(origin) == id::BrownThorns && text(ctx, origin, "twilightforest:axis") == "y", "the first stalk goes up from the origin");
    // above the cloud line nothing grows
    auto high = core::GlobalPos{ctx.dimension(), origin.x, 200, origin.z};
    LiveRegion highRegion(*r, bp(high));
    (void)features::place("twilightforest:thorns", highRegion, bp(high), rand);
    ctx.check(ctx.blockAt(high) == "minecraft:air", "no thorns above sea level + 150");
});

} // namespace gf::feature::finalcastle::blocks::tests
