// U6 flora: selftests (flora.*, dx 600..699; rows 5 blocks apart, dz 0..4).
//
// The interactions are the real handlers (gf::interactions::dispatchUseOn with the test player's
// held stack); growth runs TF's random tick with a scripted random source; the features place on a
// flat area through a world generation region over the live world.

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/features/Features.h"
#include "feature/flora/Flora.h"
#include "feature/progression/Progression.h"
#include "feature/stats/Stats.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::flora {

namespace {

using selftest::Context;
namespace ia = interactions;

BlockPos bp(GlobalPos const& p) { return game::blockPos(p); }

/// A random source whose ints come from a script (then 0), floats 0 (Java's growth checks pass).
class ScriptedRandom final : public RandomSource {
public:
    explicit ScriptedRandom(std::deque<int> ints = {}) : mInts(std::move(ints)) {}
    void         setSeed(std::int64_t) override {}
    int          nextInt() override { return next(); }
    int          nextInt(int bound) override { return bound <= 0 ? 0 : std::clamp(next(), 0, bound - 1); }
    std::int64_t nextLong() override { return 0; }
    bool         nextBoolean() override { return false; }
    float        nextFloat() override { return 0.f; }
    double       nextDouble() override { return 0.0; }
    double       nextGaussian() override { return 0.0; }
    std::unique_ptr<RandomSource> fork() override { return std::make_unique<ScriptedRandom>(); }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }

private:
    int next() {
        if (mInts.empty()) return 0;
        int v = mInts.front();
        mInts.pop_front();
        return v;
    }
    std::deque<int> mInts;
};

/// The test player (survival) right-clicks `g` with `count` of `itemId` (empty: an empty hand).
ia::Result click(Context& ctx, GlobalPos const& g, std::string const& itemId = {}, int count = 1, bool sneaking = false, double hitY = 0.5, int face = 1) {
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    if (itemId.empty()) p.setSelectedItem(ItemStack::EMPTY_ITEM());
    else if (auto s = game::items::make(itemId, count)) p.setSelectedItem(*s);
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const&     block = region->getBlock(bp(g));
    std::string      bid   = block.getTypeName();
    ItemStack const& held  = p.getSelectedItem();
    std::string      hid   = game::items::idOf(held);
    ia::UseOnContext c{p, held, hid, block, bid, g, face, {0.5, hitY, 0.5}, sneaking, true};
    return ia::dispatchUseOn(c);
}

/// How many items of `itemId` lie as item entities within 2 blocks of `g`.
int itemsNear(Context& ctx, GlobalPos const& g, std::string const& itemId) {
    auto* region = ctx.region();
    if (!region) return 0;
    AABB box{Vec3{g.x - 2.f, g.y - 2.f, g.z - 2.f}, Vec3{g.x + 3.f, g.y + 3.f, g.z + 3.f}};
    int  n = 0;
    for (Actor* a : region->fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
        if (!a || !a->hasType(ActorType::ItemEntity)) continue;
        auto const& s = static_cast<ItemActor*>(a)->item();
        if (game::items::idOf(s) == itemId) n += game::items::countOf(s);
    }
    return n;
}

void clearItemsNear(Context& ctx, GlobalPos const& g) {
    auto* region = ctx.region();
    if (!region) return;
    AABB box{Vec3{g.x - 3.f, g.y - 3.f, g.z - 3.f}, Vec3{g.x + 4.f, g.y + 4.f, g.z + 4.f}};
    for (Actor* a : region->fetchEntities(static_cast<Actor const*>(nullptr), box, false, false))
        if (a && a->hasType(ActorType::ItemEntity)) a->remove();
}

int inventoryCount(Player& p, std::string const& itemId) {
    int n = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (game::items::idOf(s) == itemId) n += game::items::countOf(s);
    }
    return n;
}

Block const* at(Context& ctx, GlobalPos const& g) {
    auto* r = ctx.region();
    return r ? &r->getBlock(bp(g)) : nullptr;
}

int stateAt(Context& ctx, GlobalPos const& g, std::string const& name) {
    auto const* b = at(ctx, g);
    return b ? intState(*b, name, -1) : -1;
}

/// A world generation region over the live world (Java's WorldGenRegion for a test placement).
class LiveRegion final : public worldgen::Region {
public:
    LiveRegion(BlockSource& region, BlockPos const& at, std::string biome) : mRegion(region), mCx(at.x >> 4), mCz(at.z >> 4), mBiome(std::move(biome)) {}
    int          centerChunkX() const override { return mCx; }
    int          centerChunkZ() const override { return mCz; }
    int          minY() const override { return mRegion.getMinHeight(); }
    int          maxY() const override { return mRegion.getMaxHeight(); }
    bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY() || !mRegion.hasChunksAt(pos, 0, false)) return false;
        ++placed;
        return game::setBlock(mRegion, pos, block);
    }
    int height(worldgen::Heightmap, int x, int z) const override {
        for (int y = maxY() - 1; y >= minY(); --y)
            if (!mRegion.getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
        return minY();
    }
    std::string  biome(BlockPos const&) const override { return mBiome; }
    std::int64_t seed() const override { return 0; }
    int          dimension() const override { return -1; } // no landmark lookups in tests
    int          placed = 0;

private:
    BlockSource& mRegion;
    int          mCx, mCz;
    std::string  mBiome;
};

/// The blocks of `id` in a box.
int countBlocks(BlockSource& region, BlockPos const& lo, BlockPos const& hi, std::string const& id) {
    int n = 0;
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z)
                if (region.getBlock(BlockPos{x, y, z}).getTypeName() == id) ++n;
    return n;
}

} // namespace

// ---- bushes ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("flora.bush_growth", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(600, 5);
    auto  p      = ctx.testPos(600, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    // a raspberry bush on grass survives and grows with daylight (1/20 per random tick)
    ctx.require(ctx.setBlock(p, "twilightforest:raspberry_bush", {{st::Age, 0}}), "raspberry bush placed");
    auto const* bush = at(ctx, p);
    ctx.check(bushes::canSurvive(*region, bp(p), *bush), "the bush survives on grass");
    ctx.check(bushes::canGrowAt(*region, bp(p), *bush), "the bush can grow in daylight");
    ScriptedRandom grow{{0, 1}}; // nextInt(20) == 0: age + 1; then the upward check fails
    bushes::randomTick(*region, bp(p), grow);
    ctx.check(stateAt(ctx, p, st::Age) == 1, "a successful growth roll ages the bush (age " + std::to_string(stateAt(ctx, p, st::Age)) + ")");
    ScriptedRandom noGrow{{5, 5}};
    bushes::randomTick(*region, bp(p), noGrow);
    ctx.check(stateAt(ctx, p, st::Age) == 1, "a failed roll (nextInt(20) != 0) leaves it");
    // grown bushes grow upwards (1/20, then 1/3) while the column below is short
    ctx.setBlock(p, "twilightforest:raspberry_bush", {{st::Age, 2}});
    ScriptedRandom up{{0, 0, 0}};
    bushes::randomTick(*region, bp(p), up);
    ctx.check(ctx.blockAt(p.offset(0, 1, 0)) == "twilightforest:raspberry_bush", "a grown bush puts a new bush above it");
    ctx.check(stateAt(ctx, p.offset(0, 1, 0), st::Age) == 0, "the new bush is young");
    ctx.cleanupBlock(p.offset(0, 1, 0));
    // a bush on a mature bush survives (TFBushBlock#canSurvive), not on stone
    ctx.check(bushes::canSurvive(*region, bp(p.offset(0, 1, 0)), *game::blockWith("twilightforest:raspberry_bush")), "a bush stacks on a grown bush");
    ctx.setBlock(p.offset(1, -1, 0), "minecraft:stone");
    ctx.check(!bushes::canSurvive(*region, bp(p.offset(1, 0, 0)), *game::blockWith("twilightforest:raspberry_bush")), "a berry bush does not survive on stone");
    // dark tower bushes need netherrack, oreberries stone and the dark (< 13 light)
    ctx.setBlock(p.offset(2, -1, 0), "minecraft:netherrack");
    ctx.check(bushes::canSurvive(*region, bp(p.offset(2, 0, 0)), *game::blockWith("twilightforest:skyberry_bush")), "a skyberry bush survives on netherrack");
    ctx.check(!bushes::canSurvive(*region, bp(p.offset(1, 0, 0)), *game::blockWith("twilightforest:skyberry_bush")), "a skyberry bush does not survive on stone");
    ctx.check(!bushes::canSurvive(*region, bp(p.offset(1, 0, 0)), *game::blockWith("twilightforest:iron_oreberry")), "an iron oreberry does not survive in daylight");
    ctx.check(bushes::canSurvive(*region, bp(p.offset(1, 0, 0)), *game::blockWith("twilightforest:essence_oreberry")), "an essence oreberry ignores the light");
    ctx.check(bushes::berryTable("twilightforest:iron_oreberry") == "twilightforest:blocks/iron_oreberry_bush_berries"
                  && bushes::berryTable("twilightforest:essence_oreberry") == "twilightforest:blocks/essence_berry_bush_berries"
                  && bushes::berryTable("twilightforest:raspberry_bush") == "twilightforest:blocks/raspberry_bush_berries",
              "berry loot tables");
    co_return;
});

GF_REGISTER_SELFTEST("flora.bush_harvest", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(610, 5);
    auto p = ctx.testPos(610, 2);
    ctx.require(ctx.setBlock(p, "twilightforest:raspberry_bush", {{st::Age, 3}}), "ripe raspberry bush placed");
    auto& player = ctx.player();
    player.getInventory().removeAllItems();
    ctx.check(click(ctx, p) == ia::Result::Consume, "an empty hand picks a ripe bush");
    ctx.check(stateAt(ctx, p, st::Age) == 2, "the picked bush is age 2");
    ctx.check(inventoryCount(ctx.player(), "twilightforest:raspberry") >= 1, "the raspberries went into the inventory");
    // bone meal: a young bush grows to age 1..2 and the meal is used
    ctx.setBlock(p.offset(1, 0, 0), "twilightforest:blueberry_bush", {{st::Age, 0}});
    click(ctx, p.offset(1, 0, 0), "minecraft:bone_meal", 2);
    int age = stateAt(ctx, p.offset(1, 0, 0), st::Age);
    ctx.check(age >= 1 && age <= 2, "bone meal grows a young bush to age 1..2 (age " + std::to_string(age) + ")");
    ctx.check(game::items::countOf(ctx.player().getSelectedItem()) == 1, "one bone meal used");
    // oreberries take no bone meal
    ctx.setBlock(p.offset(2, -1, 0), "minecraft:stone");
    ctx.setBlock(p.offset(2, 0, 0), "twilightforest:iron_oreberry", {{st::Age, 0}});
    click(ctx, p.offset(2, 0, 0), "minecraft:bone_meal", 2);
    ctx.check(stateAt(ctx, p.offset(2, 0, 0), st::Age) == 0, "an oreberry bush ignores bone meal");
    co_return;
});

GF_REGISTER_SELFTEST("flora.bush_special", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(620, 5);
    auto p = ctx.testPos(620, 2);
    // a dark tower bush dies (with its drops) when nylium comes below it
    ctx.setBlock(p.offset(0, -1, 0), "minecraft:netherrack");
    ctx.require(ctx.setBlock(p, "twilightforest:stingberry_bush", {{st::Age, 2}}), "stingberry bush placed");
    ctx.setBlock(p.offset(0, -1, 0), "minecraft:crimson_nylium");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(p) != "twilightforest:stingberry_bush", "the stingberry bush died on nylium");
    // an oreberry bush hurts what stands in it (1 damage of twilightforest:oreberry)
    auto q = p.offset(2, 0, 0);
    ctx.setBlock(q.offset(0, -1, 0), "minecraft:stone");
    ctx.setBlock(q, "twilightforest:essence_oreberry", {{st::Age, 0}});
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    float before = static_cast<float>(player.getHealth());
    player.teleport(Vec3{q.x + 0.5f, q.y + 0.1f, q.z + 0.5f}, player.getDimensionId());
    co_await ctx.ticks(3);
    float after = static_cast<float>(ctx.player().getHealth());
    ctx.check(after < before, "the oreberry bush hurt the player (" + std::to_string(before) + " -> " + std::to_string(after) + ")");
    co_return;
});

// ---- plants ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("flora.torchberries", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(630, 5);
    auto p   = ctx.testPos(630, 2).offset(0, 2, 0);
    ctx.setBlock(p.offset(0, 1, 0), "minecraft:dirt");
    ctx.require(ctx.setBlock(p, "twilightforest:torchberry_plant", {{st::Berries, true}}), "torchberry plant placed");
    clearItemsNear(ctx, p);
    auto before = stats::value(ctx.player(), "twilightforest:torchberries_harvested");
    ctx.check(click(ctx, p) == ia::Result::Consume, "a plant with berries is harvested");
    ctx.check(stateAt(ctx, p, st::Berries) == 0, "the plant has no berries left");
    ctx.check(stats::value(ctx.player(), "twilightforest:torchberries_harvested") == before + 1, "stat torchberries_harvested + 1");
    co_await ctx.ticks(1);
    ctx.check(itemsNear(ctx, p, "twilightforest:torchberries") == 1, "one torchberries item dropped");
    // bone meal brings the berries back
    click(ctx, p, "minecraft:bone_meal", 1);
    ctx.check(stateAt(ctx, p, st::Berries) == 1, "bone meal grows torchberries");
    // the plant hangs: without the dirt it pops
    ctx.setBlock(p.offset(0, 1, 0), "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(p) != "twilightforest:torchberry_plant", "a torchberry plant without support pops");
    clearItemsNear(ctx, p);
    co_return;
});

GF_REGISTER_SELFTEST("flora.roots", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(640, 5);
    auto p = ctx.testPos(640, 2).offset(0, 3, 0);
    ctx.setBlock(p.offset(0, 1, 0), "twilightforest:root");
    ctx.require(ctx.setBlock(p, "twilightforest:root_strand"), "root strand placed");
    click(ctx, p, "minecraft:bone_meal", 1);
    ctx.check(ctx.blockAt(p.offset(0, -1, 0)) == "twilightforest:root_strand", "bone meal grows the strand down");
    ctx.cleanupBlock(p.offset(0, -1, 0));
    // the liveroot block strips into roots with an axe and drops a liveroot
    auto q = ctx.testPos(640, 2).offset(2, 0, 0);
    ctx.setBlock(q, "twilightforest:liveroot_block");
    clearItemsNear(ctx, q);
    ctx.check(click(ctx, q, "minecraft:iron_axe", 1) == ia::Result::Consume, "an axe strips the liveroot block");
    ctx.check(ctx.blockAt(q) == "twilightforest:root", "the liveroot block became roots");
    co_await ctx.ticks(1);
    ctx.check(itemsNear(ctx, q, "twilightforest:liveroot") == 1, "one liveroot dropped");
    clearItemsNear(ctx, q);
    co_return;
});

GF_REGISTER_SELFTEST("flora.fallen_leaves", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(650, 5);
    auto p = ctx.testPos(650, 2);
    ctx.require(ctx.setBlock(p, "twilightforest:fallen_leaves", {{st::Layers, 1}}), "fallen leaves placed");
    {
        auto& player = ctx.player();
        player.setPlayerGameType(GameType::Survival);
        player.setSelectedItem(*game::items::make(ids::FallenLeaves, 4));
        auto*            region = ctx.region();
        Block const&     block  = region->getBlock(bp(p));
        std::string      bid    = block.getTypeName();
        ItemStack const& held   = player.getSelectedItem();
        std::string      hid    = ids::FallenLeaves;
        // vanilla passes the use-on (the item places nothing itself): the unused use-on places
        ia::UseOnContext c{player, held, hid, block, bid, p, 1, {0.5, 0.1, 0.5}, false, true};
        ctx.check(useFallenLeavesOn(c) == ia::Result::Consume, "the fallen leaves item takes the click");
    }
    ctx.check(stateAt(ctx, p, st::Layers) == 2, "fallen leaves used on the top add a layer");
    ctx.check(game::items::countOf(ctx.player().getSelectedItem()) == 3, "one fallen leaves item used");
    // unsupported plants pop
    auto q = p.offset(2, 0, 0);
    ctx.setBlock(q, "twilightforest:mayapple");

    ctx.setBlock(q.offset(0, -1, 0), "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(q) != "twilightforest:mayapple", "a mayapple without ground pops");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(!survives(*region, bp(q), *game::blockWith("twilightforest:mushgloom")).value_or(true), "a mushgloom needs a sturdy block");
    ctx.check(survives(*region, bp(p.offset(1, 0, 0)), *game::blockWith("twilightforest:moss_patch")).value_or(false), "a moss patch stays on grass");
    clearItemsNear(ctx, q);
    co_return;
});

GF_REGISTER_SELFTEST("flora.huge_lily_pad", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(660, 5);
    auto p = ctx.testPos(660, 1);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) ctx.setBlock(p.offset(dx, -1, dz), "minecraft:water");
    char const* pieces[4] = {"nw", "ne", "sw", "se"};
    int         i         = 0;
    for (int dz = 0; dz < 2; ++dz)
        for (int dx = 0; dx < 2; ++dx) ctx.setBlock(p.offset(dx, 0, dz), "twilightforest:huge_lily_pad", {{st::Facing, "north"}, {st::Piece, pieces[i++]}});
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(p.offset(1, 0, 1)) == "twilightforest:huge_lily_pad", "the pad stays on water");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    destroyBlock(*region, bp(p.offset(1, 0, 1)), false);
    co_await ctx.ticks(3);
    int left = 0;
    for (int dz = 0; dz < 2; ++dz)
        for (int dx = 0; dx < 2; ++dx) left += ctx.blockAt(p.offset(dx, 0, dz)) == "twilightforest:huge_lily_pad";
    ctx.check(left == 0, "breaking one piece breaks the whole pad (" + std::to_string(left) + " left)");
    co_return;
});

// ---- critters ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("flora.squish_bug", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(670, 5);
    auto p = ctx.testPos(670, 2);
    ctx.require(ctx.setBlock(p, "twilightforest:firefly", {{st::Facing, "up"}}), "firefly placed");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    static int triggers = 0;
    static bool listening = false;
    if (!listening) {
        listening = true;
        progression::onTrigger("twilightforest:kill_bug", [](Player&, Json const& data) {
            if (data.value("block", std::string{}) == "twilightforest:firefly") ++triggers;
        });
    }
    int  before = triggers;
    auto stat   = stats::value(ctx.player(), "twilightforest:bugs_squished");
    clearItemsNear(ctx, p);
    ctx.check(critters::squish(*region, bp(p), nullptr, &ctx.player()), "a projectile squishes the firefly");
    ctx.check(ctx.blockAt(p) != "twilightforest:firefly", "the firefly is gone");
    ctx.check(stats::value(ctx.player(), "twilightforest:bugs_squished") == stat + 1, "stat bugs_squished + 1");
    ctx.check(triggers == before + 1, "trigger kill_bug fired with the firefly");
    co_await ctx.ticks(1);
    ctx.check(itemsNear(ctx, p, "minecraft:glowstone_dust") == 1, "the squish loot (glowstone dust) dropped");
    clearItemsNear(ctx, p);
    // a real arrow falling into a cicada squishes it
    auto q = p.offset(2, 0, 0);
    ctx.setBlock(q, "twilightforest:cicada", {{st::Facing, "up"}});
    auto r = selftest::runServerCommand("summon arrow " + std::to_string(q.x + 0.5) + " " + std::to_string(q.y + 1.5) + " " + std::to_string(q.z + 0.5), ctx.dimension());
    ctx.check(r.ok, "an arrow summoned: " + r.messages);
    for (int i = 0; i < 40 && ctx.blockAt(q) == "twilightforest:cicada"; ++i) co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(q) != "twilightforest:cicada", "the falling arrow squished the cicada");
    clearItemsNear(ctx, q);
    (void)selftest::runServerCommand("kill @e[type=arrow,x=" + std::to_string(q.x) + ",y=" + std::to_string(q.y) + ",z=" + std::to_string(q.z) + ",r=4]", ctx.dimension());
    co_return;
});

GF_REGISTER_SELFTEST("flora.critter_support", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(680, 5);
    auto  p      = ctx.testPos(680, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    // on the floor: facing up; against a wall: facing away from it; nothing to hold: no placement
    ctx.check(critters::placementFacing(*region, bp(p), Up, nullptr) == std::optional<int>(Up), "a click on the ground faces up");
    ctx.setBlock(p.offset(1, 1, 0), "minecraft:stone");
    ctx.check(critters::placementFacing(*region, bp(p.offset(0, 1, 0)), West, nullptr) == std::optional<int>(West), "a click on a wall's west face faces west");
    ctx.check(!critters::placementFacing(*region, bp(p.offset(0, 2, 2)), Up, nullptr).has_value(), "no support, no placement");
    // a moonworm on dirt pops when the dirt goes
    ctx.setBlock(p.offset(2, 0, 0), "minecraft:dirt");
    ctx.setBlock(p.offset(2, 1, 0), "twilightforest:moonworm", {{st::Facing, "up"}});
    ctx.setBlock(p.offset(2, 0, 0), "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(p.offset(2, 1, 0)) != "twilightforest:moonworm", "a moonworm without support pops");
    clearItemsNear(ctx, p.offset(2, 1, 0));
    co_return;
});

// ---- jars ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("flora.jars", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(690, 5);
    auto p = ctx.testPos(690, 2);
    // an empty mason jar catches a firefly
    ctx.require(ctx.setBlock(p, "twilightforest:firefly", {{st::Facing, "up"}}), "firefly placed");
    ctx.player().getInventory().removeAllItems();
    ctx.check(click(ctx, p, "twilightforest:mason_jar", 2) == ia::Result::Consume, "a mason jar catches the firefly");
    ctx.check(ctx.blockAt(p) != "twilightforest:firefly", "the firefly went into the jar");
    ctx.check(inventoryCount(ctx.player(), "twilightforest:firefly_jar") == 1, "the player got a firefly jar");
    // sneaking with empty hands releases a jarred firefly (the firefly and an empty jar drop)
    auto q = p.offset(1, 0, 0);
    ctx.setBlock(q, "twilightforest:firefly_jar");
    clearItemsNear(ctx, q);
    ctx.check(click(ctx, q, {}, 1, true) == ia::Result::Consume, "sneaking releases the firefly");
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(q) != "twilightforest:firefly_jar", "the jar is gone");
    ctx.check(itemsNear(ctx, q, "twilightforest:firefly") >= 1 && itemsNear(ctx, q, "twilightforest:mason_jar") >= 1, "a firefly and a mason jar dropped");
    clearItemsNear(ctx, q);
    // the mason jar keeps a stack: insert, comparator, take out; a log on the top swaps the lid
    auto m = p.offset(2, 0, 0);
    ctx.setBlock(m, "twilightforest:mason_jar");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    click(ctx, m, "minecraft:dirt", 10);
    ItemStack stored = jars::contents(*region, bp(m));
    ctx.check(game::items::idOf(stored) == "minecraft:dirt" && game::items::countOf(stored) == 10, "the jar holds 10 dirt");
    ctx.check(game::items::isEmpty(ctx.player().getSelectedItem()), "the hand is empty after the insert");
    ctx.check(jars::analog(stored) == 3, "comparator output 3 for 10 / 64 (" + std::to_string(jars::analog(stored)) + ")");
    ctx.check(redstone::analogOutput(*region, bp(m)) == std::optional<int>(3), "the comparator reads the jar");
    click(ctx, m);
    ctx.check(game::items::countOf(ctx.player().getSelectedItem()) == 10, "an empty hand takes the stack back");
    ctx.check(game::items::isEmpty(jars::contents(*region, bp(m))), "the jar is empty again");
    ctx.check(jars::lidAt(*region, bp(m)) == "twilightforest:twilight_oak_log", "the default lid is twilight oak");
    click(ctx, m, "minecraft:oak_log", 1, false, 0.95);
    ctx.check(jars::lidAt(*region, bp(m)) == "minecraft:oak_log", "an oak log on the top swaps the lid");
    ctx.check(jars::isLid("minecraft:stripped_bamboo_block") && !jars::isLid("minecraft:stone"), "the registered lids");
    // the jar emits the light of a stored block item (the jar_light state) and keeps its lid and contents
    click(ctx, m, "minecraft:glowstone", 2);
    ctx.check(stateAt(ctx, m, "twilightforest:jar_light") == 15, "a jar of glowstone emits light 15 (" + std::to_string(stateAt(ctx, m, "twilightforest:jar_light")) + ")");
    ctx.check(jars::lidAt(*region, bp(m)) == "minecraft:oak_log" && game::items::countOf(jars::contents(*region, bp(m))) == 2, "the light state keeps the lid and the stack");
    click(ctx, m);
    ctx.check(stateAt(ctx, m, "twilightforest:jar_light") == 0, "the emptied jar is dark again");
    // the firefly particle spawner takes fireflies (radius + 1)
    auto s = p.offset(3, 0, 0);
    ctx.setBlock(s, "twilightforest:firefly_particle_spawner", {{st::Radius, 1}});
    click(ctx, s, "twilightforest:firefly", 1);
    ctx.check(stateAt(ctx, s, st::Radius) == 2, "a firefly widens the spawner's radius");
    click(ctx, s, {}, 1, true);
    ctx.check(stateAt(ctx, s, st::Radius) == 1, "sneaking narrows it again");
    clearItemsNear(ctx, s);
    co_return;
});

// ---- features --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("flora.features", [](Context& ctx) -> ll::coro::CoroTask<> {
    // 13 x 13 from dx 686 (into the next rows, restored afterwards): the piles reach 5 blocks out
    auto  box    = co_await ctx.flatArea(686, 13, 16);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto  o = ctx.testPos(692, 6);
    // a berry bush feature on grass places bushes (seeded, Java's draw order)
    {
        LiveRegion               live(*region, bp(o), "twilightforest:twilight_forest");
        core::jrand::LegacyRandom random(12345);
        Json feature{{"type", "twilightforest:berry_bush"},
                     {"config", {{"bush", {{"Name", "twilightforest:blueberry_bush"}, {"Properties", {{"age", "0"}, {"layers", "0"}}}}},
                                 {"can_generate_snowy", true}, {"generates_on", "twilightforest:tf_berry_bushes_survive"}}}};
        bool placed = features::placeJson(feature, live, bp(o), random);
        int  bushes = countBlocks(*region, bp(o.offset(-2, -2, -2)), bp(o.offset(2, 2, 2)), "twilightforest:blueberry_bush");
        ctx.check(placed && bushes > 0, "berry_bush placed " + std::to_string(bushes) + " blueberry bushes");
        ctx.info("berry_bush: " + std::to_string(bushes) + " bushes, " + std::to_string(live.placed) + " writes");
    }
    // fallen leaves pile up on grass
    {
        LiveRegion               live(*region, bp(o), "twilightforest:twilight_forest");
        core::jrand::LegacyRandom random(7);
        bool placed = features::placeJson(Json{{"type", "twilightforest:fallen_leaves"}, {"config", Json::object()}}, live, bp(o.offset(-3, 0, 0)), random);
        int  leaves = countBlocks(*region, bp(o.offset(-6, -2, -5)), bp(o.offset(5, 3, 5)), ids::FallenLeaves);
        ctx.check(placed && leaves > 0, "fallen_leaves piled leaves on grass");
        ctx.info("fallen_leaves: " + std::to_string(leaves) + " piles");
    }
    // root strands hang under a dirt ceiling (underground_plants)
    {
        auto c = o.offset(1, 4, 1);
        ctx.setBlock(c, "minecraft:dirt");
        LiveRegion               live(*region, bp(o), "twilightforest:twilight_forest");
        ScriptedRandom           random{{1, 1, 1}};
        Json                     feature{{"type", "twilightforest:underground_plants"}, {"config", {{"state", {{"Name", "twilightforest:root_strand"}}}}}};
        bool                     placed = features::placeJson(feature, live, bp(c.offset(0, -1, 0)), random);
        ctx.check(placed && ctx.blockAt(c.offset(0, -1, 0)) == "twilightforest:root_strand", "underground_plants hangs a root strand under dirt");
        ctx.cleanupBlock(c.offset(0, -1, 0));
    }
    (void)box;
    co_return;
});

} // namespace gf::feature::flora
