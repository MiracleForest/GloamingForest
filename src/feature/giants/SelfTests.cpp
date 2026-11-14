// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// Selftests of U29 (rule R12: "giants.*", testPos dx 2900..2999; rule R19: the cloud castle's
// golden hash). docs/spec/giants.md section 11.
#include <algorithm>
#include <climits>
#include <functional>
#include <map>
#include <unordered_map>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/phys/AABB.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"

#include "core/VolumeHash.h"
#include "core/giants/Giants.h"
#include "core/jrand/JRand.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/giants/W0Internals.h"
#include "feature/giants/Giants.h"
#include "feature/giants/Structure.h"
#include "feature/structure/Region.h"
#include "feature/loot/Loot.h"
#include "feature/progression/Progression.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::giants::tests {

namespace {

using selftest::Context;
namespace cg = core::giants;
namespace fe = gf::feature::entity;
namespace ia = gf::interactions;

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

/// The e2e world is peaceful: monsters do not attack there.
struct DifficultyScope {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    explicit DifficultyScope(SharedTypes::Legacy::Difficulty d) {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(d);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

std::vector<ItemActor*> itemsIn(BlockSource& region, core::BlockBox const& b) {
    std::vector<ItemActor*> out;
    AABB box{Vec3{b.minX - 2.f, b.minY - 2.f, b.minZ - 2.f}, Vec3{b.maxX + 3.f, b.maxY + 3.f, b.maxZ + 3.f}};
    for (Actor* a : region.getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") out.push_back(static_cast<ItemActor*>(a));
    return out;
}

std::map<std::string, int> countItems(BlockSource& region, core::BlockBox const& b, bool remove = true) {
    std::map<std::string, int> n;
    for (auto* item : itemsIn(region, b)) {
        n[game::items::idOf(item->item())] += game::items::countOf(item->item());
        if (remove) item->remove();
    }
    return n;
}

/// An aligned 4 x 4 x 4 cell above `p` (the cell whose minimum corner is the first aligned point at
/// or above p + (0, 1, 0)).
core::BlockBox cellAbove(core::GlobalPos const& p) {
    int x = (p.x + 3) & ~3, y = (p.y + 4) & ~3, z = (p.z + 3) & ~3;
    return {x, y, z, x + 3, y + 3, z + 3};
}

void clearBox(Context& ctx, core::BlockBox const& b) {
    auto* region = ctx.region();
    if (!region) return;
    auto const* air = game::blockWith("minecraft:air");
    for (int x = b.minX; x <= b.maxX; ++x)
        for (int y = b.minY; y <= b.maxY; ++y)
            for (int z = b.minZ; z <= b.maxZ; ++z)
                if (air) game::setBlock(*region, BlockPos{x, y, z}, *air);
}

/// The giant miners / armored giants near a position, removed.
void discardGiants(core::GlobalPos const& p, float r) {
    auto* region = game::regionOf(p.dim);
    if (!region) return;
    for (auto* a : fe::actorsIn(*region, p.dim, AABB{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + r, p.y + r, p.z + r}}))
        if (a && (a->getTypeName() == id::GiantMiner || a->getTypeName() == id::ArmoredGiant)) a->remove();
}

} // namespace

// ---- magic beans and the beanstalk ---------------------------------------------------------------

GF_REGISTER_SELFTEST("giants.beans_trigger", [](Context& ctx) -> ll::coro::CoroTask<> {
    static int  calls      = 0;
    static bool registered = false;
    if (!registered) {
        registered = true;
        gf::progression::onTrigger("minecraft:item_used_on_block", [](Player&, nlohmann::json const& data) {
            if (data.dump().find(id::MagicBeans) != std::string::npos) calls++;
        });
    }
    calls     = 0;
    auto box  = co_await ctx.flatArea(2900, 5, 8);
    auto soil = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 2};
    ctx.require(ctx.setBlock(soil, id::UberousSoil), "uberous soil");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto  beans = game::items::make(id::MagicBeans, 3);
    ctx.require(beans.has_value(), "magic beans exist");
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    ItemStack   held = *beans;
    std::string itemId(id::MagicBeans), blockId(id::UberousSoil);
    ia::UseOnContext use{player, held, itemId, region->getBlock(game::blockPos(soil)), blockId, soil, 1, {0.5, 1.0, 0.5}, false, true};
    auto r = ia::dispatchUseOn(use);
    ctx.check(r == ia::Result::Consume, "the beans' use-on is consumed (SUCCESS)");
    ctx.check(game::items::countOf(held) == 2, "one bean used (" + std::to_string(game::items::countOf(held)) + " left)");
    auto above = at(soil, 0, 1, 0);
    ctx.check(ctx.blockAt(above) == id::BeanstalkGrower, "a beanstalk grower above the soil (" + ctx.blockAt(above) + ")");
    co_await ctx.ticks(3);
    ctx.check(calls == 1, "exactly one minecraft:item_used_on_block for the use (" + std::to_string(calls) + ")");
    // a second click on soil with the grower above: PASS
    ia::UseOnContext again{ctx.player(), held, itemId, region->getBlock(game::blockPos(soil)), blockId, soil, 1, {0.5, 1.0, 0.5}, false, true};
    ctx.check(ia::dispatchUseOn(again) == ia::Result::Pass, "no air above: the beans pass");
    // stop the grower before it rumbles into the next rows
    ctx.setBlock(above, "minecraft:air");
    ctx.player().setPlayerGameType(GameType::Creative);
});

GF_REGISTER_SELFTEST("giants.beanstalk_growth", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box  = co_await ctx.flatArea(2905, 48, 8);
    auto soil = core::GlobalPos{ctx.dimension(), box.minX + 24, box.minY, box.minZ + 24};
    ctx.require(ctx.setBlock(soil, id::UberousSoil), "uberous soil");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto grower = at(soil, 0, 1, 0);
    startGrower(*region, game::blockPos(grower));
    int  maxY = cg::beanstalkMaxY(grower.y);
    ctx.check(ctx.blockAt(grower) == id::BeanstalkGrower, "the grower is placed");
    // the module ticks the grower each server tick; extra ticks here speed it up (same steps)
    bool finished = false;
    for (int i = 0; i < 80 && !finished; ++i) {
        for (int k = 0; k < 10; ++k) tickGrowers();
        co_await ctx.ticks(1);
        finished = ctx.blockAt(grower) == id::HugeStalk;
    }
    ctx.check(finished, "the grower turned into a huge stalk when done");
    // the spiral: stalk on every layer, leaves on the way, nothing above maxY + 2
    int  stalkLayers = 0, leaves = 0, highest = INT_MIN;
    core::BlockBox around{grower.x - 24, grower.y, grower.z - 24, grower.x + 24, maxY + 3, grower.z + 24};
    for (int y = around.minY; y <= around.maxY; ++y) {
        bool layer = false;
        for (int x = around.minX; x <= around.maxX; ++x)
            for (int z = around.minZ; z <= around.maxZ; ++z) {
                auto bid = game::blockId(region->getBlock(BlockPos{x, y, z}));
                if (bid == id::HugeStalk) {
                    layer   = true;
                    highest = std::max(highest, y);
                } else if (bid == id::BeanstalkLeaves) leaves++;
            }
        if (layer) stalkLayers++;
    }
    ctx.check(stalkLayers >= maxY - grower.y - 5, "stalk on the layers up to max(y + 100, 175) (" + std::to_string(stalkLayers) + " layers)");
    ctx.check(highest < maxY + 3 && highest >= maxY - 5, "the stalk tops out near " + std::to_string(maxY) + " (" + std::to_string(highest) + ")");
    ctx.check(leaves > 0, "leaf blobs on the way (" + std::to_string(leaves) + " leaves)");
    // leave nothing behind outside the flat area's restore volume
    for (int y = around.minY; y <= around.maxY; ++y)
        for (int x = around.minX; x <= around.maxX; ++x)
            for (int z = around.minZ; z <= around.maxZ; ++z) {
                BlockPos p{x, y, z};
                auto     bid = game::blockId(region->getBlock(p));
                if (bid == id::HugeStalk || bid == id::BeanstalkLeaves) game::clearBlock(*region, p);
            }
});

GF_REGISTER_SELFTEST("giants.uberous_soil", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box  = co_await ctx.flatArea(2955, 5, 8);
    auto soil = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY, box.minZ + 1};
    ctx.require(ctx.setBlock(soil, id::UberousSoil), "uberous soil");
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    // a crop above: farmland (moisture 7) and fifteen bone meals the next tick
    ctx.require(ctx.setBlock(at(soil, 0, 1, 0), "minecraft:wheat"), "wheat above");
    uberousNeighborChanged(*region, game::blockPos(soil), game::blockPos(at(soil, 0, 1, 0)));
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(soil) == "minecraft:farmland", "the soil became farmland (" + ctx.blockAt(soil) + ")");
    ctx.check(game::intState(region->getBlock(game::blockPos(soil)), "moisturized_amount").value_or(-1) == 7, "moisture 7");
    auto const& wheat = region->getBlock(game::blockPos(at(soil, 0, 1, 0)));
    ctx.check(game::blockId(wheat) == "minecraft:wheat" && game::intState(wheat, "growth").value_or(0) == 7,
              "the crop got its bone meals (growth " + std::to_string(game::intState(wheat, "growth").value_or(-1)) + ")");
    // a solid block above turns it into dirt
    auto soil2 = at(soil, 2, 0, 0);
    ctx.require(ctx.setBlock(soil2, id::UberousSoil), "uberous soil 2");
    ctx.require(ctx.setBlock(at(soil2, 0, 1, 0), "minecraft:stone"), "stone above");
    uberousNeighborChanged(*region, game::blockPos(soil2), game::blockPos(at(soil2, 0, 1, 0)));
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(soil2) == "minecraft:dirt", "under a solid block the soil turns to dirt (" + ctx.blockAt(soil2) + ")");
    // bone meal spreads it to a dirt neighbour
    auto soil3 = at(soil, 0, 0, 2);
    ctx.require(ctx.setBlock(soil3, id::UberousSoil), "uberous soil 3");
    ctx.require(ctx.setBlock(at(soil3, 1, 0, 0), "minecraft:dirt"), "dirt beside");
    ctx.check(uberousBonemealTarget(*region, game::blockPos(soil3)), "a valid bone meal target next to dirt");
    core::jrand::LegacyRandom r(5);
    for (int i = 0; i < 4; ++i) uberousPerformBonemeal(*region, game::blockPos(soil3), r);
    int spread = 0;
    for (auto const& [dx, dz] : std::vector<std::pair<int, int>>{{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        if (ctx.blockAt(at(soil3, dx, 0, dz)) == id::UberousSoil) spread++;
    ctx.check(spread >= 1, "bone meal spread the soil (" + std::to_string(spread) + ")");
});

// ---- giant blocks and the giant pickaxe ------------------------------------------------------------

GF_REGISTER_SELFTEST("giants.giant_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(2920, 12, 12);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto cell = cellAbove(core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 2});
    BlockPos origin{cell.minX, cell.minY, cell.minZ};
    fillGiantCell(*region, origin, id::GiantCobblestone);
    int tiled = 0;
    for (int x = cell.minX; x <= cell.maxX; ++x)
        for (int y = cell.minY; y <= cell.maxY; ++y)
            for (int z = cell.minZ; z <= cell.maxZ; ++z) {
                auto const& b = region->getBlock(BlockPos{x, y, z});
                if (game::blockId(b) == id::GiantCobblestone && tilingOf(b) == cg::cellState(x, y, z)) tiled++;
            }
    ctx.check(tiled == 64, "the cell is filled with 64 tiled blocks (" + std::to_string(tiled) + ")");
    ctx.check(giantCellFilled(*region, origin, id::GiantCobblestone), "isVolumeFilled");
    countItems(*region, cell);
    // breaking one removes all 64, with one drop
    BlockPos one{cell.minX + 1, cell.minY + 2, cell.minZ + 3};
    region->getLevel().destroyBlock(*region, one, true, BlockChangeContext{});
    co_await ctx.ticks(4);
    int left = 0;
    for (int x = cell.minX; x <= cell.maxX; ++x)
        for (int y = cell.minY; y <= cell.maxY; ++y)
            for (int z = cell.minZ; z <= cell.maxZ; ++z)
                if (game::blockId(region->getBlock(BlockPos{x, y, z})) == id::GiantCobblestone) left++;
    ctx.check(left == 0, "the whole giant block is gone (" + std::to_string(left) + " left)");
    auto drops = countItems(*region, cell);
    ctx.check(drops[id::GiantCobblestone] == 1 && drops.size() == 1, "one giant cobblestone dropped (" + std::to_string(drops[id::GiantCobblestone]) + ")");
    // a giant block placed without its tiling state gets it
    auto lone = core::GlobalPos{ctx.dimension(), cell.minX + 5, cell.minY, cell.minZ + 1};
    ctx.require(ctx.setBlock(lone, id::GiantLog), "a lone giant log");
    co_await ctx.ticks(3);
    ctx.check(tilingOf(region->getBlock(game::blockPos(lone))) == cg::cellState(lone.x, lone.y, lone.z),
              "world-placed giant blocks get their tiling state");
    clearBox(ctx, cell);
});

GF_REGISTER_SELFTEST("giants.giant_pickaxe", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(2935, 16, 12);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    // destroy speed: x 64 on giant blocks, x 64 x 64 on giant obsidian
    auto pick = game::items::make(id::GiantPickaxe);
    ctx.require(pick.has_value(), "a giant pickaxe");
    ctx.check(giantPickDestroySpeed(id::GiantCobblestone, 4.0f) == 256.0f && giantPickDestroySpeed(id::GiantObsidian, 4.0f) == 16384.0f
                  && giantPickDestroySpeed("minecraft:stone", 4.0f) == 4.0f,
              "GiantPickItem#getDestroySpeed");
    if (auto const* gc = game::blockWith(id::GiantCobblestone); gc && pick->mItem)
        ctx.check(pick->mItem->getDestroySpeed(*pick, *gc) == 256.0f, "the item's destroy speed on giant cobblestone is 256");

    auto runCell = [&](int dx, bool mixed) -> ll::coro::CoroTask<std::map<std::string, int>> {
        auto cell = cellAbove(core::GlobalPos{ctx.dimension(), box.minX + dx, box.minY, box.minZ + 2});
        for (int x = cell.minX; x <= cell.maxX; ++x)
            for (int y = cell.minY; y <= cell.maxY; ++y)
                for (int z = cell.minZ; z <= cell.maxZ; ++z)
                    if (auto const* c = game::blockWith("minecraft:cobblestone")) game::setBlock(*region, BlockPos{x, y, z}, *c);
        if (mixed)
            if (auto const* s = game::blockWith("minecraft:stone")) game::setBlock(*region, BlockPos{cell.maxX, cell.maxY, cell.maxZ}, *s);
        countItems(*region, cell);
        BlockPos target{cell.minX, cell.minY, cell.minZ};
        {
            auto& sp = static_cast<SimulatedPlayer&>(ctx.player()); // the test player is a simulated one
            sp.setPlayerGameType(GameType::Survival);
            sp.teleport(Vec3{cell.minX - 1.5f, static_cast<float>(cell.minY), cell.minZ + 0.5f}, DimensionType(ctx.dimension()));
            sp.simulateLookAt(target, sim::LookDuration::Instant);
            sp.simulateSetItem(*pick, true, 0);
        }
        co_await ctx.ticks(2); // a simulated player applies its look on its next tick
        static_cast<SimulatedPlayer&>(ctx.player()).simulateDestroyBlock(target, ScriptModuleMinecraft::ScriptFacing::West);
        for (int t = 0; t < 120 && game::blockId(region->getBlock(target)) == "minecraft:cobblestone"; ++t) co_await ctx.ticks(1);
        static_cast<SimulatedPlayer&>(ctx.player()).simulateStopDestroyingBlock();
        co_await ctx.ticks(5);
        auto n = countItems(*region, cell);
        int  left = 0;
        for (int x = cell.minX; x <= cell.maxX; ++x)
            for (int y = cell.minY; y <= cell.maxY; ++y)
                for (int z = cell.minZ; z <= cell.maxZ; ++z)
                    if (game::blockId(region->getBlock(BlockPos{x, y, z})) == "minecraft:cobblestone") left++;
        n["left"] = left;
        clearBox(ctx, cell);
        co_return n;
    };
    auto full = co_await runCell(1, false);
    ctx.check(full["left"] == 0, "the giant pickaxe mined the whole 4 x 4 x 4 cell (" + std::to_string(full["left"]) + " left)");
    ctx.check(full[id::GiantCobblestone] == 1 && full["minecraft:cobblestone"] == 0,
              "a full cell of cobblestone drops one giant cobblestone (" + std::to_string(full[id::GiantCobblestone]) + " giant, " + std::to_string(full["minecraft:cobblestone"]) + " cobblestone)");
    auto mixed = co_await runCell(8, true);
    ctx.check(mixed["left"] == 0 && mixed[id::GiantCobblestone] == 0 && mixed["minecraft:cobblestone"] == 63,
              "a mixed cell drops its blocks as they are (" + std::to_string(mixed["minecraft:cobblestone"]) + " cobblestone)");
    ctx.player().setCarriedItem(ItemStack::EMPTY_ITEM());
    ctx.player().setPlayerGameType(GameType::Creative);
});

// ---- clouds ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("giants.cloud_rain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(2960, 5, 16);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto ground   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY - 1, box.minZ + 1};
    auto cauldron = at(ground, 0, 1, 0);
    ctx.require(ctx.setBlock(cauldron, "minecraft:cauldron"), "a cauldron");
    ctx.require(ctx.setBlock(at(ground, 0, 10, 0), id::RainyCloud), "a rainy cloud above it");
    ctx.require(ctx.setBlock(at(ground, 2, 10, 0), id::RainyCloud), "a second rainy cloud");
    ctx.check(cloudPrecipitation(*region, game::blockPos(at(ground, 0, 10, 0)), id::RainyCloud) == Precipitation::Rain, "the rainy cloud rains");
    ctx.check(cloudPrecipitation(*region, game::blockPos(at(ground, 0, 10, 0)), id::WispyCloud) == Precipitation::None, "a wispy cloud never rains");
    // random ticks: rain fills the cauldron (5 % a tick)
    core::jrand::LegacyRandom r(11);
    int fill = 0;
    for (int i = 0; i < 400 && fill == 0; ++i) {
        cloudRandomTick(*region, game::blockPos(at(ground, 0, 10, 0)), id::RainyCloud, r);
        fill = game::intState(region->getBlock(game::blockPos(cauldron)), "fill_level").value_or(0);
    }
    ctx.check(fill > 0, "rain under the rainy cloud fills the cauldron (fill level " + std::to_string(fill) + ")");
    // isRainingAt: under the second cloud, not under a roof
    ctx.check(rainingFromCloudAt(*region, game::blockPos(at(ground, 2, 1, 0))), "isRainingAt under a rainy cloud");
    ctx.require(ctx.setBlock(at(ground, 2, 5, 0), "minecraft:stone"), "a roof");
    ctx.check(!rainingFromCloudAt(*region, game::blockPos(at(ground, 2, 1, 0))), "not under a roof");
    ctx.check(!rainingFromCloudAt(*region, game::blockPos(at(ground, 4, 1, 0))), "not beside the cloud");
});

GF_REGISTER_SELFTEST("giants.cloud_snow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(2966, 5, 16);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto ground = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY - 1, box.minZ + 2};
    ctx.require(ctx.setBlock(at(ground, 0, 10, 0), id::SnowyCloud), "a snowy cloud");
    core::jrand::LegacyRandom r(3);
    cloudRandomTick(*region, game::blockPos(at(ground, 0, 10, 0)), id::SnowyCloud, r);
    ctx.check(ctx.blockAt(at(ground, 0, 1, 0)) == "minecraft:snow_layer", "snow under the snowy cloud (" + ctx.blockAt(at(ground, 0, 1, 0)) + ")");
    cloudRandomTick(*region, game::blockPos(at(ground, 0, 10, 0)), id::SnowyCloud, r);
    ctx.check(game::intState(region->getBlock(game::blockPos(at(ground, 0, 1, 0))), "height").value_or(-1) == 0,
              "one layer (snowAccumulationHeight 1)");
});

// BlockHooks#isRainingAt makes an entity under a raining cloud wet: a burning pig goes out, a snow
// golem (isSensitiveToWater) takes drown damage.
GF_REGISTER_SELFTEST("giants.cloud_wetness", [](Context& ctx) -> ll::coro::CoroTask<> {
    static int  drownHits  = 0;
    static bool registered = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(0, [](gf::combat::DamageInfo const& d) {
            if (d.type == "minecraft:drown" && d.victim.getTypeName() == "minecraft:snow_golem") drownHits++;
        });
    }
    drownHits   = 0;
    auto  box    = co_await ctx.flatArea(2996, 4, 12);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto ground = core::GlobalPos{ctx.dimension(), box.minX, box.minY - 1, box.minZ};
    // a roof of rainy clouds over the whole 4 x 4 area (the mobs may wander a little)
    for (int dx = 0; dx < 4; ++dx)
        for (int dz = 0; dz < 4; ++dz) ctx.require(ctx.setBlock(at(ground, dx, 8, dz), id::RainyCloud), "rainy clouds");
    auto* pig   = gf::spawn::spawnMob(*region, "minecraft:pig", Vec3{ground.x + 1.0f, float(ground.y + 1), ground.z + 1.0f}, gf::spawn::SpawnReason::Command);
    auto* golem = gf::spawn::spawnMob(*region, "minecraft:snow_golem", Vec3{ground.x + 3.0f, float(ground.y + 1), ground.z + 3.0f}, gf::spawn::SpawnReason::Command);
    ctx.require(pig && golem, "a pig and a snow golem under the clouds");
    auto pigUid = fe::uidOf(*pig), golemUid = fe::uidOf(*golem);
    pig->setOnFire(10, true);
    co_await ctx.ticks(12);
    auto* p = fe::actorById(pigUid);
    auto* g = fe::actorById(golemUid);
    ctx.check(p && !p->isOnFire(), "the rain from the cloud put the pig's fire out");
    ctx.check(drownHits > 0, "the snow golem takes drown damage in the cloud's rain (" + std::to_string(drownHits) + " hits)");
    if (p) p->remove();
    if (g) g->remove();
});

GF_REGISTER_SELFTEST("giants.cloud_fall", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box   = co_await ctx.flatArea(2972, 5, 8);
    auto cloud = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 2};
    ctx.require(ctx.setBlock(cloud, id::FluffyCloud), "a fluffy cloud");
    ctx.check(cloudFallDamage(10.0f) == 1.0f && cloudFallDamage(17.0f) == 2.0f && cloudFallDamage(0.0f) == 0.0f, "fall damage x 0.1, rounded up");
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    p.teleportTo(Vec3{cloud.x + 0.5f, cloud.y + 1.0f, cloud.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(3);
    auto& q      = ctx.player();
    int   before = q.getHealth();
    gf::combat::hurt(q, 10.0f, "minecraft:fall");
    co_await ctx.ticks(1);
    int lost = before - ctx.player().getHealth();
    ctx.check(lost == 1, "a 10 damage fall onto a cloud deals 1 (" + std::to_string(lost) + ")");
    ctx.player().setPlayerGameType(GameType::Creative);
});

// ---- giants ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("giants.giant_mobs", [](Context& ctx) -> ll::coro::CoroTask<> {
    static int  antHits    = 0;
    static bool registered = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(0, [](gf::combat::DamageInfo const& d) {
            if (d.type == "gloamingforest:ant" && d.attacker && (d.attacker->getTypeName() == id::GiantMiner || d.attacker->getTypeName() == id::ArmoredGiant)) antHits++;
        });
    }
    antHits = 0;
    DifficultyScope normal(SharedTypes::Legacy::Difficulty::Normal);
    auto  box    = co_await ctx.flatArea(2980, 16, 12);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto pos = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY, box.minZ + 8};
    auto* miner = gf::spawn::spawnMob(*region, id::GiantMiner, Vec3{pos.x + .5f, float(pos.y), pos.z + .5f}, gf::spawn::SpawnReason::Command);
    auto* armored = gf::spawn::spawnMob(*region, id::ArmoredGiant, Vec3{pos.x + 4.5f, float(pos.y), pos.z + .5f}, gf::spawn::SpawnReason::Command);
    ctx.require(miner && armored, "both giants spawn");
    auto mUid = fe::uidOf(*miner), aUid = fe::uidOf(*armored);
    co_await ctx.ticks(4);
    auto* m = fe::actorById(mUid);
    auto* a = fe::actorById(aUid);
    ctx.require(m && a, "both giants are alive");
    ctx.check(m->getMaxHealth() == 80 && a->getMaxHealth() == 80, "max health 80");
    auto const& mb = m->getAABB();
    ctx.check(std::abs((mb.max.x - mb.min.x) - 2.4f) < 0.01f && std::abs((mb.max.y - mb.min.y) - 7.2f) < 0.01f, "2.4 x 7.2");
    auto* mm = fe::asMob(*m);
    auto* am = fe::asMob(*a);
    ctx.require(mm && am, "mobs");
    using Slot = SharedTypes::Legacy::EquipmentSlot;
    ctx.check(game::items::idOf(mm->getItemSlot(Slot::Mainhand)) == id::GiantPickaxe, "the giant miner holds a giant pickaxe");
    ctx.check(game::items::idOf(am->getItemSlot(Slot::Mainhand)) == id::GiantSword, "the armored giant holds a giant sword");
    ctx.check(game::items::idOf(am->getItemSlot(Slot::Head)) == "minecraft:iron_helmet" && game::items::idOf(am->getItemSlot(Slot::Feet)) == "minecraft:iron_boots",
              "and wears iron armour");
    ctx.check(giantAttackDamage(*m) == 11.0f && giantAttackDamage(*a) == 13.0f, "attack damage 2 + 9 (pickaxe) / 2 + 11 (sword)");
    // loot: the tool, only for a player kill
    gf::loot::LootContext lc;
    lc.region     = region;
    lc.thisEntity = m;
    ctx.check(gf::loot::roll("gloamingforest:entities/giant_miner", lc).empty(), "no loot without a player");
    lc.player   = &ctx.player();
    lc.attacker = &ctx.player();
    auto drops  = gf::loot::roll("gloamingforest:entities/giant_miner", lc);
    ctx.check(drops.size() == 1 && game::items::idOf(drops.front()) == id::GiantPickaxe, "a player kill drops the giant pickaxe");
    lc.thisEntity = a;
    auto drops2   = gf::loot::roll("gloamingforest:entities/armored_giant", lc);
    ctx.check(drops2.size() == 1 && game::items::idOf(drops2.front()) == id::GiantSword, "the armored giant drops its sword");
    // the melee is gloamingforest:ant (GiantMiner#doHurtTarget): a giant's mob attack becomes it
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    p.teleportTo(Vec3{pos.x + 2.5f, float(pos.y), pos.z + 2.5f}, true, 0, 1, false);
    co_await ctx.ticks(2);
    if (auto* g = fe::actorById(mUid)) gf::combat::hurt(ctx.player(), 2.0f, "minecraft:mob_attack", g, g);
    // dealt inside the handler, as properlyApplyCustomDamageSource does within doHurtTarget
    ctx.check(antHits == 1, "a giant's melee is gloamingforest:ant damage, at once (" + std::to_string(antHits) + ")");
    co_await ctx.ticks(2);
    // and the AI uses it (the player may be invulnerable for a moment after the first hit)
    antHits = 0;
    for (int i = 0; i < 30 && antHits == 0; ++i) {
        if (auto* g = fe::actorById(mUid)) g->setTarget(&ctx.player());
        co_await ctx.ticks(5);
    }
    ctx.info(antHits > 0 ? "the giant miner's AI hit the player with ant damage" : "the giant miner's AI did not reach the player in 150 ticks");
    ctx.player().setPlayerGameType(GameType::Creative);
    discardGiants(pos, 24);
});

// ---- the cloud castle ---------------------------------------------------------------------------------

namespace {

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region (air everywhere).
class MemoryRegion final : public worldgen::Region {
public:
    MemoryRegion() { structure::resetRegionRandom(this); }
    ~MemoryRegion() override { structure::resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        return it != mBlocks.end() ? *it->second : structure::blocks::air();
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        mBlocks[key(pos.x, pos.y, pos.z)] = &block;
        return true;
    }
    [[nodiscard]] int          height(worldgen::Heightmap, int, int) const override { return 0; }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return "gloamingforest:highlands"; }
    [[nodiscard]] std::int64_t seed() const override { return kSeed; }
    [[nodiscard]] int          dimension() const override { return -1; } // scratch: no block entity work

    [[nodiscard]] std::string hash() const {
        std::vector<std::pair<std::tuple<int, int, int>, Block const*>> sorted;
        int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
        for (auto const& [k, blk] : mBlocks) {
            auto [x, y, z] = unkey(k);
            minX = std::min(minX, x), minY = std::min(minY, y), minZ = std::min(minZ, z);
            sorted.push_back({{y, z, x}, blk});
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& l, auto const& r) { return l.first < r.first; });
        core::VolumeHasher h;
        for (auto const& [pos, blk] : sorted) {
            auto [y, z, x] = pos;
            h.add(x - minX, y - minY, z - minZ, blk->getTypeName(), game::statesOf(*blk));
        }
        return h.hex();
    }
    [[nodiscard]] std::size_t count(std::string const& id) const {
        std::size_t n = 0;
        for (auto const& [k, blk] : mBlocks)
            if (game::blockId(*blk) == id) n++;
        return n;
    }

private:
    static long long key(int x, int y, int z) {
        return (static_cast<long long>(x + (1 << 23)) << 40) | (static_cast<long long>(y + (1 << 19)) << 20) | static_cast<long long>(z + (1 << 19));
    }
    static std::tuple<int, int, int> unkey(long long k) {
        return {static_cast<int>(k >> 40) - (1 << 23), static_cast<int>((k >> 20) & 0xfffff) - (1 << 19), static_cast<int>(k & 0xfffff) - (1 << 19)};
    }
    std::unordered_map<long long, Block const*> mBlocks;
};

/// The golden of the cloud castle (start chunk (3, -2), y 0, the e2e seed), recorded in W2 audit round 1
/// (the screenshot review of a placed castle is the client scenario giants.ps1); "" records it.
constexpr char kCastleHash[] = "18f7c25dab5940c5";

} // namespace

GF_REGISTER_SELFTEST("giants.cloud_castle_hash", [](Context& ctx) {
    // the castle's first piece in the form CloudCastleComponent(1, x, 168, z) saves
    auto tag = cloudCastleTag(51, 170, -29);
    auto bb  = structure::boxFromJson(tag["BB"]);
    ctx.check(bb.minX == 40 && bb.minY == 164 && bb.minZ == -40 && bb.maxX == 104 && bb.maxY == 180 && bb.maxZ == 24, "the castle's box: rounded to 4, (-8, -4, -8) + 64 x 16 x 64");
    if (!cloudCastleAvailable()) {
        ctx.info("the cloud castle piece types (U27: gloamingforest:TFClCa) are not registered: the golden hash waits for them");
        return;
    }
    int const cx = 3, cz = -2;
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    structure::bases::PieceList pieces;
    ctx.require(layoutGiantHouse(context, cx * 16, 0, cz * 16, pieces), "the giant house lays out");
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    ctx.check(types["gloamingforest:TFClCa"] == 1 && types["gloamingforest:TFCloud"] == 1 && types["gloamingforest:TFClTr"] == 2,
              "a castle, its cloud and two cloud trees");
    ctx.check(pieces.front()->bbox.minY == 164, "the castle's box starts at y 164 (castle floor at 168)");
    MemoryRegion mem;
    structure::BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    for (auto& p : pieces) {
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, BlockPos{0, 0, 0});
    }
    ctx.check(mem.count(id::GiantCobblestone) > 0 && mem.count(id::GiantLog) > 0 && mem.count(id::FluffyCloud) > 0 && mem.count(id::GiantLeaves) > 0,
              "giant cobblestone walls, a giant log roof, fluffy clouds and giant leaves");
    auto hash = mem.hash();
    if (std::string(kCastleHash).empty()) ctx.info("GOLDEN giants.cloud_castle_hash = " + hash);
    else ctx.check(hash == kCastleHash, "golden hash: expected " + std::string(kCastleHash) + ", got " + hash);
});

// ---- the client scenario's showcase (tools/e2e/client/scenarios/giants.ps1) ----------------------------

GF_REGISTER_SELFTEST("giants.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    Player* real = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) real = &p;
            return real == nullptr;
        }));
    if (!real) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    auto   viewerUid = fe::uidOf(*real);
    auto   f   = real->getFeetPos();
    auto   v   = real->getViewVector(1.0f);
    int    dim = static_cast<int>(real->getDimensionId());
    double len = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
    double fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
    auto   spot = [&](double ahead, double side, int up = 0) {
        return core::GlobalPos{dim, static_cast<int>(std::floor(f.x + fx * ahead - fz * side)), static_cast<int>(std::floor(f.y)) + up,
                               static_cast<int>(std::floor(f.z + fz * ahead + fx * side))};
    };
    auto* region = game::regionOf(dim);
    ctx.require(region != nullptr, "the player's dimension");
    std::vector<core::GlobalPos> placed;
    auto put = [&](core::GlobalPos const& p, char const* blockId) {
        if (auto const* b = game::blockWith(blockId)) {
            game::setBlock(*region, game::blockPos(p), *b);
            placed.push_back(p);
        }
    };
    // four giant blocks side by side, 10 blocks ahead (the tiled textures)
    char const* giantsIds[] = {id::GiantCobblestone, id::GiantLog, id::GiantLeaves, id::GiantObsidian};
    for (int i = 0; i < 4; ++i) {
        auto     s    = spot(10.0, -8.0 + i * 5.0);
        BlockPos cell = {s.x & ~3, s.y, s.z & ~3};
        fillGiantCell(*region, cell, giantsIds[i]);
        auto o = cg::cellOrigin(cell.x, cell.y, cell.z);
        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 4; ++y)
                for (int z = 0; z < 4; ++z) placed.push_back({dim, o[0] + x, o[1] + y, o[2] + z});
    }
    co_await ctx.ticks(20);
    ctx.info("blocks ready");
    co_await ctx.ticks(60);
    for (auto const& p : placed) game::clearBlock(*region, game::blockPos(p));
    placed.clear();
    // the four clouds in a row, then a rainy and a snowy cloud raining down 6 blocks ahead
    char const* cloudIds[] = {id::WispyCloud, id::FluffyCloud, id::RainyCloud, id::SnowyCloud};
    for (int i = 0; i < 4; ++i) put(spot(6.0, -3.0 + i * 2.0, 0), cloudIds[i]);
    for (int dx = -2; dx <= 2; ++dx) {
        put(spot(9.0, -3.0 + dx, 6), id::RainyCloud);
        put(spot(9.0, 3.0 + dx, 6), id::SnowyCloud);
    }
    co_await ctx.ticks(60);
    ctx.info("clouds ready");
    co_await ctx.ticks(80);
    for (auto const& p : placed) game::clearBlock(*region, game::blockPos(p));
    placed.clear();
    // the giants (the viewer's skin with the client mod, the default skin on a vanilla client)
    auto g1 = spot(12.0, -3.0), g2 = spot(12.0, 3.0);
    auto* miner   = gf::spawn::spawnMob(*region, id::GiantMiner, Vec3{g1.x + .5f, float(g1.y), g1.z + .5f}, gf::spawn::SpawnReason::Command);
    auto* armored = gf::spawn::spawnMob(*region, id::ArmoredGiant, Vec3{g2.x + .5f, float(g2.y), g2.z + .5f}, gf::spawn::SpawnReason::Command);
    ctx.check(miner && armored, "the giants spawn");
    std::int64_t m = miner ? fe::uidOf(*miner) : 0, a = armored ? fe::uidOf(*armored) : 0;
    co_await ctx.ticks(40);
    ctx.info("giants ready");
    co_await ctx.ticks(60);
    if (auto* x = fe::actorById(m)) x->remove();
    if (auto* x = fe::actorById(a)) x->remove();
    // a beanstalk from uberous soil 14 blocks ahead
    auto soil = spot(14.0, 0.0, -1);
    put(soil, id::UberousSoil);
    startGrower(*region, game::blockPos(at(soil, 0, 1, 0)));
    co_await ctx.ticks(60);
    ctx.info("rumble ready");
    for (int i = 0; i < 60 && ctx.blockAt(at(soil, 0, 1, 0)) != id::HugeStalk; ++i) {
        for (int k = 0; k < 6; ++k) tickGrowers();
        co_await ctx.ticks(1);
    }
    co_await ctx.ticks(20);
    ctx.info("beanstalk ready");
    co_await ctx.ticks(60);
    // clean up the stalk and its leaves
    int top = cg::beanstalkMaxY(soil.y + 1) + 3;
    for (int y = soil.y; y <= top; ++y)
        for (int x = soil.x - 24; x <= soil.x + 24; ++x)
            for (int z = soil.z - 24; z <= soil.z + 24; ++z) {
                BlockPos p{x, y, z};
                auto     bid = game::blockId(region->getBlock(p));
                if (bid == id::HugeStalk || bid == id::BeanstalkLeaves) game::clearBlock(*region, p);
            }
    for (auto const& p : placed) game::clearBlock(*region, game::blockPos(p));
    placed.clear();
    // the cloud castle (rule R19's one-time review of giants.cloud_castle_hash): giant_house laid out
    // from about 8 blocks ahead (loaded chunks), the castle floor at y 168; the viewer goes up to its
    // height 16 blocks back and returns afterwards
    if (!cloudCastleAvailable()) {
        ctx.info("castle skipped: U27's cloud castle piece types are not registered");
        co_return;
    }
    int sx = static_cast<int>(std::floor(f.x + fx * 40.0)) - 24, sz = static_cast<int>(std::floor(f.z + fz * 40.0)) - 24;
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, sx >> 4, sz >> 4);
    structure::bases::PieceList pieces;
    ctx.require(layoutGiantHouse(context, sx, 0, sz, pieces), "the giant house lays out");
    // what was not air in the pieces' boxes before (terrain, builds) is left alone by the cleanup
    std::vector<BlockPos> wasAir;
    for (auto const& piece : pieces) {
        auto const& b = piece->bbox;
        for (int y = b.minY; y <= b.maxY; ++y)
            for (int x = b.minX; x <= b.maxX; ++x)
                for (int z = b.minZ; z <= b.maxZ; ++z)
                    if (region->getBlock(BlockPos{x, y, z}).isAir()) wasAir.push_back(BlockPos{x, y, z});
    }
    {
        structure::BlockSourceRegion world(*region, BlockPos{sx, 168, sz});
        structure::BlockBox          all{-30000000, -64, -30000000, 30000000, 319, 30000000};
        for (auto& piece : pieces) {
            core::jrand::LegacyRandom r(0);
            piece->postProcess(world, r, all, BlockPos{0, 0, 0});
        }
    }
    // the viewer stands on a barrier at the castle's height (no fall, whatever the game mode)
    BlockPos stand{static_cast<int>(std::floor(f.x - fx * 16.0)), 175, static_cast<int>(std::floor(f.z - fz * 16.0))};
    bool     barrier = region->getBlock(stand).isAir();
    if (barrier)
        if (auto const* b = game::blockWith("minecraft:barrier")) game::setBlock(*region, stand, *b);
    if (auto* viewer = fe::playerById(viewerUid)) {
        float yaw = static_cast<float>(std::atan2(-fx, fz) * 180.0 / 3.14159265358979);
        viewer->teleport(Vec3{stand.x + 0.5f, 176.0f, stand.z + 0.5f}, viewer->getDimensionId(), Vec2{5.0f, yaw});
    }
    co_await ctx.ticks(60);
    ctx.info("castle ready");
    co_await ctx.ticks(60);
    if (auto* viewer = fe::playerById(viewerUid)) viewer->teleport(Vec3{f.x, f.y, f.z}, viewer->getDimensionId());
    for (auto const& p : wasAir)
        if (!region->getBlock(p).isAir()) game::clearBlock(*region, p);
    if (barrier) game::clearBlock(*region, stand);
});

} // namespace gf::feature::giants::tests
