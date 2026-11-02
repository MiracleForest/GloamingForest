// Selftests of unit F4 (dimension): `/twilightforest selftest "dimension.*"`, dx 5400..5499 (rule R12).

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string>
#include <vector>

#include "ll/api/Config.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/GameRulesChangedPacket.h"
#include "mc/network/packet/SetTimePacket.h"
#include "mc/network/packet/SyncWorldClocksPacket.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionBrightnessRamp.h"
#include "mc/world/phys/AABB.h"

#include "feature/dimension/AreaLoader.h"
#include "feature/dimension/Clock.h"
#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/Teleporter.h"
#include "feature/dimension/World.h"
#include "feature/ext/dimension/DimensionType.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace gf::feature::dimension {

namespace {

using selftest::Context;
namespace tp = teleporter;

std::string str(BlockPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

/// Restores the dimension config when the test ends, however it ends (and its file when `save`).
struct ConfigGuard {
    DimensionConfig keep = gf::config::get<DimensionConfig>();
    bool            save = false;
    ConfigGuard()                              = default;
    ConfigGuard(ConfigGuard const&)            = delete;
    ConfigGuard& operator=(ConfigGuard const&) = delete;
    ~ConfigGuard() {
        gf::config::mut<DimensionConfig>() = keep;
        if (save) ll::config::saveConfig(keep, gf::config::configDir() / "dimension.json");
    }
};

/// Runs a command as the server in dimension `dim`; the output messages ("" on success).
std::pair<bool, std::string> runCommand(std::string const& command, int dim = 0) {
    auto level = ll::service::getLevel();
    if (!level) return {false, "no level"};
    ServerCommandOrigin origin("gloamingforest-dimension-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(dim));
    auto        out = ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
    std::string messages;
    for (auto const& m : out.mMessages) {
        if (!messages.empty()) messages += "; ";
        messages += m.mMessageId;
        for (auto const& p : m.mParams) messages += " " + p;
    }
    return {out.mSuccessCount > 0, messages};
}

/// Builds a 2x2 water pool at `corner` (lowest x / z) in a flat area: the surface block of the
/// pool turns to water on its ground, the rim keeps its grass and gets short grass on top.
/// Returns false when a block could not be set.
bool buildPool(Context& ctx, core::GlobalPos corner, bool decorate = true, int width = 2, int depth = 2) {
    // (setBlock reports false when the block is already there: read the result back instead)
    auto place = [&](core::GlobalPos const& p, std::string const& id) {
        if (ctx.blockAt(p) != id) ctx.setBlock(p, id);
        if (ctx.blockAt(p) == id) return true;
        ctx.info("could not set " + id + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + " (found "
                 + ctx.blockAt(p) + ")");
        return false;
    };
    bool ok = true;
    for (int dx = -1; dx <= width; ++dx)
        for (int dz = -1; dz <= depth; ++dz) {
            bool inside  = dx >= 0 && dx < width && dz >= 0 && dz < depth;
            bool corner2 = (dx == -1 || dx == width) && (dz == -1 || dz == depth);
            auto ground  = corner.offset(dx, 0, dz); // the flat area's grass layer, dirt below
            ok &= place(ground, inside ? "minecraft:water" : "minecraft:grass_block");
            if (!inside && !corner2 && decorate) ok &= place(ground.offset(0, 1, 0), "minecraft:short_grass");
        }
    return ok;
}

int countPortals(BlockSource& region, BlockPos const& corner, int width = 2, int depth = 2) {
    int n = 0;
    for (int dx = 0; dx < width; ++dx)
        for (int dz = 0; dz < depth; ++dz)
            if (isPortal(region.getBlock(BlockPos{corner.x + dx, corner.y, corner.z + dz}))) ++n;
    return n;
}

/// The item entities within `range` blocks of `at` (ids).
std::set<std::int64_t> itemsNear(BlockSource& region, Vec3 const& at, float range) {
    std::set<std::int64_t> out;
    AABB                   box{at - Vec3(range, range, range), at + Vec3(range, range, range)};
    for (auto* e : region.getEntities(box, 0.0f, ActorType::ItemEntity))
        if (e && e->getEntityTypeId() == ActorType::ItemEntity) out.insert(idOf(*e));
    return out;
}

/// The item entity of `itemId` near `at` that is not in `before` (nullptr: none).
ItemActor* newItemNear(BlockSource& region, Vec3 const& at, std::set<std::int64_t> const& before, std::string const& itemId) {
    AABB box{at - Vec3(8, 8, 8), at + Vec3(8, 8, 8)};
    for (auto* e : region.getEntities(box, 0.0f, ActorType::ItemEntity)) {
        if (!e || e->getEntityTypeId() != ActorType::ItemEntity || before.contains(idOf(*e))) continue;
        auto& item = static_cast<ItemActor&>(*e);
        if (game::items::idOf(item.item()) == itemId) return &item;
    }
    return nullptr;
}

/// Player#drop: the player drops `stack` (thrown ahead, or around it when `randomly`); the new
/// item entity, nullptr when none appeared.
ItemActor* dropFrom(Player& player, ItemStack const& stack, bool randomly) {
    auto* region = game::regionOf(static_cast<int>(player.getDimensionId()));
    if (!region) return nullptr;
    Vec3 at     = player.getFeetPos();
    auto before = itemsNear(*region, at, 8.0f);
    player.drop(stack, randomly);
    return newItemNear(*region, at, before, game::items::idOf(stack));
}

/// Sets the first of `ids` the game knows at `pos` (block ids differ between versions); the id set,
/// "" when none could be.
std::string setFirst(Context& ctx, core::GlobalPos const& pos, std::initializer_list<char const*> ids) {
    for (auto const* id : ids) {
        ctx.setBlock(pos, id);
        if (ctx.blockAt(pos) == id) return id;
    }
    return {};
}

/// A random source that answers nextInt(bound) with the scripted values in turn (clamped below the
/// bound), for the rules that draw from a RandomSource.
class ScriptedRandom final : public core::jrand::RandomSource {
public:
    explicit ScriptedRandom(std::vector<int> values) : mValues(std::move(values)) {}
    void                                                  setSeed(std::int64_t) override {}
    [[nodiscard]] int                                     nextInt() override { return pop(); }
    [[nodiscard]] int                                     nextInt(int bound) override { return std::clamp(pop(), 0, bound - 1); }
    [[nodiscard]] std::int64_t                            nextLong() override { return pop(); }
    [[nodiscard]] bool                                    nextBoolean() override { return pop() != 0; }
    [[nodiscard]] float                                   nextFloat() override { return 0.0f; }
    [[nodiscard]] double                                  nextDouble() override { return 0.0; }
    [[nodiscard]] double                                  nextGaussian() override { return 0.0; }
    [[nodiscard]] std::unique_ptr<RandomSource>           fork() override { return std::make_unique<ScriptedRandom>(mValues); }
    [[nodiscard]] std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }
    [[nodiscard]] std::size_t                             used() const { return mNext; }

private:
    int pop() { return mNext < mValues.size() ? mValues[mNext++] : 0; }
    std::vector<int> mValues;
    std::size_t      mNext = 0;
};

/// The positions makePortalAt touches around a portal whose lowest corner is `p` (for cleanup).
void cleanupPortalSite(Context& ctx, core::GlobalPos p) {
    for (int dx = -1; dx <= 2; ++dx)
        for (int dz = -1; dz <= 2; ++dz)
            for (int dy = -1; dy <= 1; ++dy) ctx.cleanupBlock(p.offset(dx, dy, dz));
}

// ---- the dimension (spike S1) ---------------------------------------------------------------------------------------

// The dimension exists with TF's dimension_type values.
GF_REGISTER_SELFTEST("dimension.registered", [](Context& ctx) {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "twilightforest:twilight_forest is registered");
    int id = static_cast<int>(*tf);
    ctx.info("the Twilight Forest is dimension " + std::to_string(id));
    ctx.check(id > 2, "it is a custom dimension (id > 2)");
    ctx.check(world::isTwilight(*tf), "isTwilight(id)");
    ctx.check(!world::isTwilight(DimensionType(0)), "the overworld is not the Twilight Forest");
    ctx.check(dimensionByName(world::kDimensionId) == id, "found by identifier");
    ctx.check(dimensionName(id) == world::kDimensionId, "identifier of the id");
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto d = level->getOrCreateDimension(*tf).lock();
    ctx.require(d != nullptr, "the dimension is created");
    ctx.check(world::isTwilight(*d), "isTwilight(Dimension)");
    auto h = heightOf(id);
    ctx.require(h.has_value(), "height known");
    ctx.check(h->min == world::kMinY, "min y is -32 (got " + std::to_string(h->min) + ")");
    ctx.check(h->max - h->min == world::kHeight, "height is 288 (got " + std::to_string(h->max - h->min) + ")");
    ctx.check(world::coordinateScale() == 0.125, "coordinate scale 0.125");
    ctx.check(coordinateScaleOf(id) == 0.125 && coordinateScaleOf(0) == 1.0 && coordinateScaleOf(1) == 8.0, "scales per dimension");
    ctx.check(world::fixedTime() == 13000, "fixed time 13000");
    ctx.check(d->mayRespawnViaBed(), "beds work (bed_works)");
    // fixed_time: the dimension's time of day is the one of 13000 whatever the level time
    auto ow = level->getDimension(DimensionType(0)).lock();
    ctx.require(ow != nullptr, "overworld loaded");
    float want = ow->getTimeOfDay(13000, 0.0f);
    for (int t : {0, 6000, 13000, 18000, 23999, 24000 * 7 + 100}) {
        float got = d->getTimeOfDay(t, 0.5f);
        ctx.check(std::abs(got - want) < 1e-6f, "time of day at level time " + std::to_string(t) + " is the one of 13000");
    }
    ctx.check(std::abs(ow->getTimeOfDay(6000, 0.0f) - want) > 1e-3f, "the overworld's time still flows");
});

// A player enters the dimension and blocks can be placed from y -32 to y 255.
GF_REGISTER_SELFTEST("dimension.enter", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "moved into the Twilight Forest");
    auto tf = *world::twilight();
    ctx.check(static_cast<int>(ctx.player().getDimensionId()) == static_cast<int>(tf), "the player is in the Twilight Forest");
    auto at = ctx.testPos(5400);
    for (int y : {world::kMinY, 0, 64, world::kMinY + world::kHeight - 1}) {
        core::GlobalPos p{static_cast<int>(tf), at.x, y, at.z};
        ctx.require(co_await ctx.ensureLoaded(p), "chunk loaded at y " + std::to_string(y));
        ctx.check(ctx.setBlock(p, "minecraft:stone"), "stone set at y " + std::to_string(y));
        ctx.check(ctx.blockAt(p) == "minecraft:stone", "stone reads back at y " + std::to_string(y));
    }
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.check(region->getMinHeight() == world::kMinY, "region min height -32");
    ctx.check(region->getMaxHeight() == world::kMinY + world::kHeight, "region max height 256");
});

// ---- config, gamerules, tags ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.config_defaults", [](Context& ctx) {
    DimensionConfig d;
    ctx.check(!d.newPlayersSpawnInTF && !d.portalForNewPlayer, "dimension settings default off");
    ctx.check(d.originDimension == "minecraft:overworld", "origin dimension");
    ctx.check(!d.allowPortalsInOtherDimensions && d.portalCreationPermission == 0 && !d.disablePortalCreation, "creation defaults");
    ctx.check(d.checkPortalPlacement && d.destructivePortalLightning && d.shouldReturnPortalBeUsable, "portal defaults");
    ctx.check(d.portalUnlockedByAdvancement.empty() && d.maxPortalSize == 64, "lock and size defaults");
});

GF_REGISTER_SELFTEST("dimension.portal_delay", [](Context& ctx) {
    auto& p = ctx.player();
    ctx.check(portalDelay(p) == kDefaultDelay, "survival delay is playersTfPortalDefaultDelay (60), got " + std::to_string(portalDelay(p)));
});

GF_REGISTER_SELFTEST("dimension.tags", [](Context& ctx) {
    ctx.check(blockTagged("twilightforest:portal/fluid", "minecraft:water"), "water is the portal fluid");
    ctx.check(blockTagged("twilightforest:portal/edge", "minecraft:grass_block"), "grass is a portal edge");
    ctx.check(blockTagged("twilightforest:portal/edge", "minecraft:dirt"), "dirt is a portal edge");
    ctx.check(!blockTagged("twilightforest:portal/edge", "minecraft:stone"), "stone is not a portal edge");
    ctx.check(blockTagged("twilightforest:portal/decoration", "minecraft:short_grass"), "short grass decorates");
    ctx.check(blockTagged("twilightforest:portal/decoration", "minecraft:poppy"), "flowers decorate");
    ctx.check(!blockTagged("twilightforest:portal/decoration", "minecraft:stone"), "stone does not decorate");
    ctx.check(itemTagged("twilightforest:portal/activator", "minecraft:diamond"), "a diamond activates");
    ctx.check(!itemTagged("twilightforest:portal/activator", "minecraft:emerald"), "an emerald does not");
    ctx.check(!blockTagMembers("twilightforest:portal/generated_decoration").empty(), "generated decorations");
    // TF's own members of the vanilla tags the portal tags include (BlockTagGenerator)
    ctx.check(blockTagged("twilightforest:portal/edge", "twilightforest:uberous_soil"), "uberous soil is a portal edge (#dirt)");
    ctx.check(blockTagged("twilightforest:portal/decoration", "twilightforest:canopy_sapling"), "TF saplings decorate (#saplings)");
    ctx.check(blockTagged("twilightforest:portal/decoration", "twilightforest:twilight_oak_leaves"), "TF leaves decorate (#leaves)");
    ctx.check(portalBlock(false) && portalBlock(true) && portalBlock(false) != portalBlock(true), "both portal states exist");
});

// ---- TFPortalBlock: lighting --------------------------------------------------------------------------------------------

// tryToCreatePortal: the shapes that light and the ones that don't.
GF_REGISTER_SELFTEST("dimension.portal_light", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5410, 30, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int  y = box.minY - 1; // the grass layer
    auto at = [&](int dx, int dz) { return core::GlobalPos{ctx.dimension(), box.minX + dx, y, box.minZ + dz}; };

    // a good 2x2 pool lights
    auto good = at(2, 2);
    ctx.require(buildPool(ctx, good), "pool built");
    ctx.check(portal::tryActivate(*region, game::blockPos(good)), "a 2x2 pool with a decorated rim lights");
    ctx.check(countPortals(*region, game::blockPos(good)) == 4, "the 4 pool blocks are portal blocks");
    ctx.check(!game::intState(region->getBlock(game::blockPos(good)), kOneWayState).value_or(1), "lit portals are two-way");
    ctx.check(!portal::tryActivate(*region, game::blockPos(good)), "an open portal does not light again");

    // no decoration on one rim block
    auto bare = at(8, 2);
    ctx.require(buildPool(ctx, bare), "pool built");
    ctx.check(ctx.setBlock(bare.offset(-1, 1, 0), "minecraft:air"), "decoration removed");
    ctx.check(!portal::tryActivate(*region, game::blockPos(bare)), "a rim block without nature on top refuses");
    ctx.check(countPortals(*region, game::blockPos(bare)) == 0, "nothing lit");

    // stone in the rim
    auto stone = at(14, 2);
    ctx.require(buildPool(ctx, stone), "pool built");
    ctx.check(ctx.setBlock(stone.offset(2, 0, 0), "minecraft:stone"), "rim block is stone");
    ctx.check(!portal::tryActivate(*region, game::blockPos(stone)), "a stone rim refuses");

    // too small (MIN_PORTAL_SIZE 4): 1x2
    auto small = at(20, 2);
    ctx.require(buildPool(ctx, small, true, 1, 2), "pool built");
    ctx.check(!portal::tryActivate(*region, game::blockPos(small)), "a 2-block pool is too small");

    // larger pools light whole: 3x2
    auto wide = at(2, 10);
    ctx.require(buildPool(ctx, wide, true, 3, 2), "pool built");
    ctx.check(portal::tryActivate(*region, game::blockPos(wide)), "a 3x2 pool lights");
    ctx.check(countPortals(*region, game::blockPos(wide), 3, 2) == 6, "all 6 pool blocks are portal blocks");

    // maxPortalSize
    auto big = at(10, 10);
    ctx.require(buildPool(ctx, big, true, 3, 2), "pool built");
    bool lit = false;
    {
        ConfigGuard guard;
        gf::config::mut<DimensionConfig>().maxPortalSize = 5;
        lit = portal::tryActivate(*region, game::blockPos(big));
    }
    ctx.check(!lit, "a pool larger than maxPortalSize refuses");

    // a one-way portal re-lights (canFormPortal)
    auto oneWay = at(18, 10);
    ctx.require(buildPool(ctx, oneWay), "pool built");
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
            ctx.check(ctx.setBlock(oneWay.offset(dx, 0, dz), kPortalBlock, {{kOneWayState, true}}), "one-way portal set");
    ctx.check(portal::tryActivate(*region, game::blockPos(oneWay)), "a one-way portal pool re-lights");
    ctx.check(!game::intState(region->getBlock(game::blockPos(oneWay)), kOneWayState).value_or(1), "and becomes two-way");
});

// A diamond thrown by a player into a pool lights it (ProgressionEvents#checkForPortalCreation).
GF_REGISTER_SELFTEST("dimension.portal_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5440, 12, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY - 1, box.minZ + 4};
    ctx.require(buildPool(ctx, pool), "pool built");
    auto& p     = ctx.player();
    auto  owner = idOf(p);
    p.teleport(Vec3{pool.x - 1.5f, static_cast<float>(pool.y + 1), pool.z + 1.0f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(2);
    // someone else's diamond does not count
    auto stack = game::items::make("minecraft:diamond", 1);
    ctx.require(stack.has_value(), "diamond");
    auto level = ll::service::getLevel();
    Vec3 center{pool.x + 1.0f, pool.y + 0.6f, pool.z + 1.0f};
    auto* stray = level->getSpawner().spawnItem(*region, *stack, nullptr, center, 0);
    ctx.require(stray != nullptr, "unowned diamond spawned");
    co_await ctx.ticks(45);
    ctx.check(countPortals(*region, game::blockPos(pool)) == 0, "an unowned diamond does not light the pool");
    if (auto* s = level->fetchEntity(ActorUniqueID{idOf(*stray)}, false)) s->remove();
    // a diamond the game spawns with the player as its spawner, but not thrown, does not count
    auto* spawned = level->getSpawner().spawnItem(*region, *stack, &ctx.player(), center, 0);
    ctx.require(spawned != nullptr, "diamond spawned for the player");
    ctx.check(itemOwner(*spawned) == -1, "an item spawned for the player without a throw has no thrower");
    co_await ctx.ticks(45);
    ctx.check(countPortals(*region, game::blockPos(pool)) == 0, "a diamond the player did not throw does not light the pool");
    if (auto* s = level->fetchEntity(ActorUniqueID{idOf(*spawned)}, false)) s->remove();
    // the player's own diamond, thrown (Player#drop), then put in the middle of the pool
    auto* own = dropFrom(ctx.player(), *stack, false);
    ctx.require(own != nullptr, "the player threw a diamond");
    auto ownId = idOf(*own);
    ctx.check(itemOwner(*own) == owner, "the diamond's thrower is the player who threw it");
    own->teleport(center, DimensionType(ctx.dimension()));
    bool lit = false;
    for (int i = 0; i < 60 && !lit; ++i) {
        co_await ctx.ticks(1);
        lit = countPortals(*region, game::blockPos(pool)) == 4;
    }
    ctx.check(lit, "the player's diamond in the pool lights the portal within 3 s");
    ctx.check(level->fetchEntity(ActorUniqueID{ownId}, false) == nullptr, "the diamond was used up");
});

// The portal turns back into water when its rim breaks (TFPortalBlock#neighborChanged).
GF_REGISTER_SELFTEST("dimension.portal_neighbor", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5460, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY - 1, box.minZ + 2};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    portalNeighborChanged(*region, game::blockPos(pool));
    ctx.check(isPortal(region->getBlock(game::blockPos(pool))), "an intact portal stays");
    ctx.check(ctx.setBlock(pool.offset(-1, 0, 0), "minecraft:stone"), "rim broken");
    portalNeighborChanged(*region, game::blockPos(pool));
    ctx.check(game::blockId(region->getBlock(game::blockPos(pool))) == "minecraft:water", "the portal block next to stone is water again");
    ctx.check(isPortal(region->getBlock(game::blockPos(pool.offset(1, 0, 1)))), "the far block stays until its own neighbours change");
});

// ---- TeleporterCache ----------------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.teleporter_cache", [](Context& ctx) {
    std::string dim = "gf_test:cache_dimension";
    tp::cacheAdd(dim, 123, -456, tp::PortalPosition{BlockPos{1, 2, 3}, 77});
    auto got = tp::cacheGet(dim, 123, -456);
    ctx.require(got.has_value(), "cached");
    ctx.check(got->pos == BlockPos(1, 2, 3) && got->lastUpdateTime == 77, "cached value");
    tp::cacheSave();
    tp::cacheLoad();
    got = tp::cacheGet(dim, 123, -456);
    ctx.check(got.has_value() && got->pos == BlockPos(1, 2, 3), "survives save + load");
    tp::cacheRemove(dim, 123, -456);
    ctx.check(!tp::cacheGet(dim, 123, -456).has_value(), "removed");
    tp::cacheSave();
});

// ---- travel -----------------------------------------------------------------------------------------------------------------

// A player stands in a lit portal: after the delay it is in the Twilight Forest next to a return
// portal; standing in that one brings it back next to the first portal.
GF_REGISTER_SELFTEST("dimension.portal_travel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    int   tfId   = static_cast<int>(*tf);
    auto  box    = co_await ctx.flatArea(5480, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY - 1, box.minZ + 3};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    // the Twilight Forest is void until F5's generator: give the destination (x and z times 8) a
    // dirt floor at y 32 so the portal there is an "ideal" one with ground under it
    int const destX = pool.x * 8, destZ = pool.z * 8, floorY = 32;
    ctx.require(co_await ctx.ensureLoaded(core::GlobalPos{tfId, destX, floorY, destZ}), "destination loaded");
    for (int dx = -8; dx <= 8; ++dx)
        for (int dz = -8; dz <= 8; ++dz) ctx.setBlock(core::GlobalPos{tfId, destX + dx, floorY, destZ + dz}, "minecraft:dirt");
    auto& p  = ctx.player();
    auto  id = idOf(p);
    p.teleport(Vec3{pool.x - 1.5f, static_cast<float>(pool.y + 1), pool.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(20);
    auto* pp = playerById(id);
    ctx.require(pp && static_cast<int>(pp->getDimensionId()) == ctx.dimension(), "back at the pool");
    auto dest = portal::destinationFor(*pp);
    ctx.check(dest && dest->dim == tfId, "destinationFor: the Twilight Forest");
    pp->teleport(Vec3{pool.x + 0.5f, static_cast<float>(pool.y), pool.z + 0.5f}, DimensionType(ctx.dimension()));

    // the trip
    bool there = false;
    int  ticks = 0;
    for (; ticks < 400 && !there; ++ticks) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        ctx.require(pl != nullptr, "the player is still there");
        if (ticks == 30) ctx.check(portalTimer(*pl) > 20, "the portal timer runs while inside (" + std::to_string(portalTimer(*pl)) + ")");
        // in the Twilight Forest and moved next to the return portal
        there = static_cast<int>(pl->getDimensionId()) == tfId && game::regionOf(tfId)
             && tp::portalBlocksNear(*game::regionOf(tfId), game::blockPosOf(pl->getFeetPos()), 3).size() >= 4;
    }
    ctx.require(there, "in the Twilight Forest next to a portal after " + std::to_string(ticks) + " ticks");
    ctx.check(ticks >= kDefaultDelay, "not before the 60 tick delay");
    auto* pl       = playerById(id);
    auto* tfRegion = game::regionOf(tfId);
    ctx.require(pl && tfRegion, "player and dimension");
    auto here    = game::blockPosOf(pl->getFeetPos());
    auto portals = tp::portalBlocksNear(*tfRegion, here, 4);
    ctx.check(portals.size() == 4, "a 2x2 return portal (" + std::to_string(portals.size()) + " blocks)");
    BlockPos corner = portals.front();
    for (auto const& b : portals) corner = BlockPos{std::min(corner.x, b.x), b.y, std::min(corner.z, b.z)};
    cleanupPortalSite(ctx, core::GlobalPos{tfId, corner.x, corner.y, corner.z});
    ctx.check(!game::intState(tfRegion->getBlock(corner), kOneWayState).value_or(1), "the return portal is usable (shouldReturnPortalBeUsable)");
    ctx.info("arrived at " + str(here) + ", return portal at " + str(corner));
    // the way back is cached for the return portal's columns
    auto back = tp::cacheGet("minecraft:overworld", corner.x, corner.z);
    ctx.check(back.has_value(), "the way back is cached");

    // back home (after the 10 tick portal cooldown: standing in a portal while it runs restarts it)
    co_await ctx.ticks(15);
    pl = playerById(id);
    ctx.require(pl != nullptr, "the player is still there");
    pl->teleport(Vec3{corner.x + 0.5f, static_cast<float>(corner.y), corner.z + 0.5f}, DimensionType(tfId));
    bool home = false;
    for (ticks = 0; ticks < 400 && !home; ++ticks) {
        co_await ctx.ticks(1);
        pl = playerById(id);
        ctx.require(pl != nullptr, "the player is still there");
        home = static_cast<int>(pl->getDimensionId()) == ctx.dimension();
        if (home) {
            co_await ctx.ticks(5);
            pl   = playerById(id);
            home = pl && std::abs(pl->getFeetPos().x - (pool.x + 1)) < 4 && std::abs(pl->getFeetPos().z - (pool.z + 1)) < 4;
        }
    }
    ctx.check(home, "back next to the first portal");
    ctx.check(corner.y == floorY, "the return portal sits on the floor (y " + std::to_string(corner.y) + ")");
    for (auto const& b : portals) tp::cacheRemove("minecraft:overworld", b.x, b.z);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) tp::cacheRemove(world::kDimensionId, pool.x + dx, pool.z + dz);
});

// newPlayersSpawnInTF (CapabilityEvents#newSpawnInTwilightForest): the player lands in the Twilight
// Forest, spawns there and counts as banished.
GF_REGISTER_SELFTEST("dimension.new_player_spawn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    // the settings go back however the test ends (a later joining player must not be banished)
    ConfigGuard config;
    gf::config::mut<DimensionConfig>().newPlayersSpawnInTF = true;
    gf::config::mut<DimensionConfig>().portalForNewPlayer  = true;
    auto& sp = ctx.spawnPlayer("gf_test_newcomer");
    auto  id = idOf(sp);
    struct Unbanish {
        std::string uuid;
        ~Unbanish() { unbanish(uuid); }
    } unban{sp.getUuid().asString()};
    co_await ctx.ticks(2);
    auto respawnInfo = [](Player const& pl) {
        auto const& rp = *pl.mPlayerRespawnPoint;
        return "spawn block " + str(*rp.mSpawnBlockPos) + ", position " + str(*rp.mPlayerPosition) + ", dimension "
             + std::to_string(rp.mDimension->mValue) + " (MIN " + str(BlockPos::MIN()) + ")";
    };
    if (auto* p = playerById(id)) {
        ctx.info("new player respawn point: " + respawnInfo(*p));
        ctx.check(!hasRespawnPosition(*p), "a new player has no respawn point");
        newSpawnInTwilightForest(*p);
    }
    bool there = false;
    for (int i = 0; i < 300 && !there; ++i) {
        co_await ctx.ticks(1);
        auto* p = playerById(id);
        there   = p && world::isTwilight(p->getDimensionId()) && p->getFeetPos().y < 250;
    }
    ctx.require(there, "the new player is in the Twilight Forest");
    co_await ctx.ticks(5);
    auto* p = playerById(id);
    ctx.require(p != nullptr, "player");
    ctx.info("after the new spawn: " + respawnInfo(*p));
    ctx.check(hasRespawnPosition(*p), "the new spawn is a respawn point");
    ctx.check(p->mPlayerRespawnPoint->mDimension->mValue == static_cast<int>(*tf), "respawns in the Twilight Forest");
    auto portals = tp::portalBlocksNear(*game::regionOf(static_cast<int>(*tf)), game::blockPosOf(p->getFeetPos()), 4);
    ctx.check(portals.size() == 4, "next to a fresh portal (portalForNewPlayer)");
    if (!portals.empty()) {
        ctx.check(game::intState(game::regionOf(static_cast<int>(*tf))->getBlock(portals.front()), kOneWayState).value_or(0) == 1, "a one-way portal (forced entry)");
        BlockPos corner = portals.front();
        for (auto const& b : portals) corner = BlockPos{std::min(corner.x, b.x), b.y, std::min(corner.z, b.z)};
        cleanupPortalSite(ctx, core::GlobalPos{static_cast<int>(*tf), corner.x, corner.y, corner.z});
    }
});

// ---- non-player entities -----------------------------------------------------------------------------------------

// An item dropped into a lit portal goes to the Twilight Forest at once (transition time 0), where
// the teleporter builds a return portal for it; nobody has to be there.
GF_REGISTER_SELFTEST("dimension.item_travel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    int   tfId   = static_cast<int>(*tf);
    auto  box    = co_await ctx.flatArea(5490, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY - 1, box.minZ + 3};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    // the destination (x and z times 8) gets a dirt floor at y 32 while the area loader holds it,
    // without a player there
    int const destX = pool.x * 8, destZ = pool.z * 8, floorY = 32;
    {
        auto hold = co_await loadArea(tfId, BlockPos{destX, floorY, destZ}, 10);
        ctx.require(hold != nullptr, "destination loaded without a player");
        for (int dx = -8; dx <= 8; ++dx)
            for (int dz = -8; dz <= 8; ++dz) ctx.setBlock(core::GlobalPos{tfId, destX + dx, floorY, destZ + dz}, "minecraft:dirt");
    }
    // the test player waits next to the pool, in the overworld
    ctx.player().teleport(Vec3{pool.x - 1.5f, static_cast<float>(pool.y + 1), pool.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(5);
    auto stack = game::items::make("minecraft:stick", 3);
    ctx.require(stack.has_value(), "sticks");
    auto  level = ll::service::getLevel();
    auto* item  = level->getSpawner().spawnItem(*region, *stack, nullptr, Vec3{pool.x + 1.0f, pool.y + 0.3f, pool.z + 1.0f}, 0);
    ctx.require(item != nullptr, "item dropped into the portal");
    auto   itemId = idOf(*item);
    Actor* there  = nullptr;
    int    ticks  = 0;
    for (; ticks < 300 && !there; ++ticks) {
        co_await ctx.ticks(1);
        auto* a = level->fetchEntity(ActorUniqueID{itemId}, false);
        if (a && static_cast<int>(a->getDimensionId()) == tfId) there = a;
        // (or the sticks as another item entity, when the engine gave the moved item a new id)
        if (auto* r = game::regionOf(tfId); !there && r) {
            AABB near{Vec3(destX - 12, floorY - 4, destZ - 12), Vec3(destX + 12, floorY + 8, destZ + 12)};
            for (auto* e : r->getEntities(near, 0.0f, ActorType::ItemEntity)) {
                if (e && e->getEntityTypeId() == ActorType::ItemEntity
                    && game::items::idOf(static_cast<ItemActor*>(e)->item()) == "minecraft:stick") {
                    there = e;
                    ctx.info("the sticks arrived as entity " + std::to_string(idOf(*e)) + " (dropped as " + std::to_string(itemId) + ")");
                    break;
                }
            }
        }
    }
    ctx.require(there != nullptr, "the item is in the Twilight Forest (after " + std::to_string(ticks) + " ticks)");
    ctx.info("the item arrived after " + std::to_string(ticks) + " ticks at " + str(game::blockPosOf(there->getFeetPos())));
    ctx.check(ticks <= 60, "it went through without the players' 60 tick delay");
    auto* tfRegion = game::regionOf(tfId);
    ctx.require(tfRegion != nullptr, "the Twilight Forest region");
    auto portals = tp::portalBlocksNear(*tfRegion, game::blockPosOf(there->getFeetPos()), 4);
    ctx.check(portals.size() == 4, "next to a 2x2 return portal (" + std::to_string(portals.size()) + " blocks)");
    if (!portals.empty()) {
        BlockPos corner = portals.front();
        for (auto const& b : portals) corner = BlockPos{std::min(corner.x, b.x), b.y, std::min(corner.z, b.z)};
        ctx.check(corner.y == floorY, "the return portal sits on the floor (y " + std::to_string(corner.y) + ")");
        cleanupPortalSite(ctx, core::GlobalPos{tfId, corner.x, corner.y, corner.z});
        for (auto const& b : portals) tp::cacheRemove("minecraft:overworld", b.x, b.z);
    }
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) tp::cacheRemove(world::kDimensionId, pool.x + dx, pool.z + dz);
    // it does not come straight back (Entity#getDimensionChangingDelay: 300 ticks for entities)
    there->remove();
});

// ---- the area loader -------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.area_loader", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    int      tfId = static_cast<int>(*tf);
    BlockPos far{-77712, 64, 54321}; // far from every test and player
    auto*    region = game::regionOf(tfId);
    ctx.info(std::string("loaded before: ") + (region && region->hasChunksAt(far, 2, false) ? "yes" : "no"));
    int  held = heldAreas();
    auto hold = co_await loadArea(tfId, far, 18);
    ctx.require(hold != nullptr, "the area loads without a player");
    region = game::regionOf(tfId);
    ctx.check(region && region->hasChunksAt(far, 18, false), "every chunk within 18 blocks is loaded");
    ctx.check(heldAreas() == held + 1, "one area held");
    hold.reset();
    ctx.check(heldAreas() == held, "released with its handle");
});

// ---- BlockHooks: mushrooms next to a portal ------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.mushroom", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5452, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY - 1, box.minZ + 2};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    // on the rim (where the short grass is) and in the corner, both next to portal blocks
    auto rim    = pool.offset(-1, 1, 0);
    auto corner = pool.offset(-1, 1, -1);
    ctx.check(ctx.setBlock(rim, "minecraft:brown_mushroom"), "mushroom on the rim");
    ctx.check(ctx.setBlock(corner, "minecraft:red_mushroom"), "mushroom on the corner");
    ctx.check(portalKeepsMushroom(*region, game::blockPos(rim)), "a portal block is next to the rim");
    ctx.check(portalKeepsMushroom(*region, game::blockPos(corner)), "and diagonal to the corner");
    auto const& rimBlock = region->getBlock(game::blockPos(rim));
    ctx.check(rimBlock.getBlockType().canSurvive(*region, game::blockPos(rim)), "the rim mushroom can survive (in daylight)");
    // away from the portal the vanilla rule decides
    auto away = pool.offset(-1, 1, 5);
    ctx.check(!portalKeepsMushroom(*region, game::blockPos(away)), "no portal next to the far one");
    ctx.check(ctx.setBlock(away, "minecraft:brown_mushroom"), "far mushroom");
    ctx.info(std::string("the far mushroom ") + (region->getBlock(game::blockPos(away)).getBlockType().canSurvive(*region, game::blockPos(away)) ? "can" : "cannot")
             + " survive by the vanilla rule here");
    // neighbour updates around the rim one: it stays
    ctx.check(ctx.setBlock(pool.offset(-2, 1, 0), "minecraft:short_grass"), "neighbour changed");
    co_await ctx.ticks(40);
    ctx.check(ctx.blockAt(rim) == "minecraft:brown_mushroom", "the rim mushroom is still there (" + ctx.blockAt(rim) + ")");
    ctx.check(ctx.blockAt(corner) == "minecraft:red_mushroom", "the corner mushroom is still there (" + ctx.blockAt(corner) + ")");
});

// ---- the portal site keeps its water ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.portal_site_fluids", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5468, 10, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto site = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY - 1, box.minZ + 3};
    auto water = site.offset(0, 3, 1), stone = site.offset(1, 4, 0);
    ctx.check(ctx.setBlock(water, "minecraft:water"), "water above the site");
    ctx.check(ctx.setBlock(stone, "minecraft:stone"), "stone above the site");
    tp::makePortalAt(*region, game::blockPos(site), false);
    cleanupPortalSite(ctx, site);
    ctx.check(countPortals(*region, game::blockPos(site)) == 4, "a 2x2 portal");
    ctx.check(ctx.blockAt(water) == "minecraft:water", "Level#removeBlock leaves the water (" + ctx.blockAt(water) + ")");
    ctx.check(ctx.blockAt(stone) == "minecraft:air", "the stone is cleared (" + ctx.blockAt(stone) + ")");
    co_await ctx.ticks(1);
});

// ---- TFConfig#getPortalLockingAdvancement ------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.portal_lock", [](Context& ctx) {
    ConfigGuard guard;
    guard.save = true; // the check writes the cleared value to config/dimension.json
    ctx.check(!advancementExists("twilightforest:gf_test_no_such_advancement"), "an unknown advancement does not exist");
    ctx.check(!advancementExists("not an advancement"), "an invalid id does not exist");
    auto& cfg      = gf::config::mut<DimensionConfig>();
    bool  shipped  = advancementExists("twilightforest:progress_lich");
    cfg.portalUnlockedByAdvancement = "twilightforest:gf_test_no_such_advancement";
    if (shipped) {
        ctx.check(!portalLockingAdvancement(true).has_value(), "an unknown locking advancement locks nothing");
        ctx.check(cfg.portalUnlockedByAdvancement.empty(), "and is cleared from the config");
        cfg.portalUnlockedByAdvancement = "twilightforest:progress_lich";
        ctx.check(portalLockingAdvancement(true) == std::optional<std::string>("twilightforest:progress_lich"), "a shipped advancement locks portals");
    } else {
        // without TF's advancement data the lock can't be checked: it is kept as configured
        ctx.info("no advancement data shipped yet (data/twilightforest/advancement): the lock is kept unchecked");
        ctx.check(portalLockingAdvancement(true).has_value(), "the configured lock is kept");
        ctx.check(!cfg.portalUnlockedByAdvancement.empty(), "and stays in the config");
    }
});

// ---- the per-player clock of vanilla clients ----------------------------------------------------------------------------------

// The corrections of the three time packets (Clock.cpp), on packets built here.
GF_REGISTER_SELFTEST("dimension.clock", [](Context& ctx) {
    ctx.check(twilightClock(6000) == 13000, "noon of day 0 -> 13000");
    ctx.check(twilightClock(24000 * 5 + 100) == 24000 * 5 + 13000, "the day count is kept");
    ctx.check(timeOfDay(-1) == 23999, "negative times wrap");
    auto id = overworldClockId();
    ctx.require(id != 0, "the overworld world clock is known");
    // SetTime
    auto setTime = MinecraftPackets::createPacket(MinecraftPacketIds::SetTime);
    ctx.require(setTime != nullptr, "SetTime packet");
    static_cast<SetTimePacket&>(*setTime).mTime = 30000;
    ctx.check(timePacketKind(*setTime) == TimePacket::SetTime, "SetTime is a time packet");
    auto undo = fixForTwilight(*setTime);
    ctx.check(static_cast<SetTimePacket&>(*setTime).mTime == 24000 + 13000, "SetTime corrected");
    ctx.require(static_cast<bool>(undo), "with an undo");
    undo();
    ctx.check(static_cast<SetTimePacket&>(*setTime).mTime == 30000, "SetTime restored");
    // SyncWorldClocks (state of the overworld clock and of another clock)
    auto clocks = MinecraftPackets::createPacket(MinecraftPacketIds::SyncWorldClocks);
    ctx.require(clocks != nullptr, "SyncWorldClocks packet");
    auto& state = static_cast<SyncWorldClocksPacket&>(*clocks).mData.get().emplace<SyncWorldClocksPacketPayload::SyncStateData>();
    SyncWorldClockStateData ow{}, other{};
    ow.mClockId     = id;
    ow.mTime        = 6000;
    ow.mIsPaused    = false;
    other.mClockId  = id + 1;
    other.mTime     = 500;
    other.mIsPaused = false;
    state.mClockData->push_back(ow);
    state.mClockData->push_back(other);
    ctx.check(timePacketKind(*clocks) == TimePacket::WorldClocks, "SyncWorldClocks with the overworld clock is a time packet");
    undo = fixForTwilight(*clocks);
    auto const& list = state.mClockData.get();
    ctx.check(list[0].mTime == 13000 && list[0].mIsPaused, "the overworld clock reads 13000, stopped");
    ctx.check(list[1].mTime == 500 && !list[1].mIsPaused, "other clocks are left alone");
    ctx.require(static_cast<bool>(undo), "with an undo");
    undo();
    ctx.check(list[0].mTime == 6000 && !list[0].mIsPaused, "SyncWorldClocks restored");
    // GameRulesChanged (doDaylightCycle)
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto& rules = *level->getGameRules().mGameRules;
    auto  index = static_cast<std::size_t>(GameRules::GameRulesIndex::DoDaylightCycle);
    ctx.require(index < rules.size(), "doDaylightCycle rule");
    auto rulesPacket = MinecraftPackets::createPacket(MinecraftPacketIds::GameRulesChanged);
    ctx.require(rulesPacket != nullptr, "GameRulesChanged packet");
    GameRule rule(rules[index]);
    rule.mValue = GameRule::Value(true);
    static_cast<GameRulesChangedPacket&>(*rulesPacket).mRuleData->mRules->push_back(rule);
    ctx.check(timePacketKind(*rulesPacket) == TimePacket::DaylightRule, "doDaylightCycle is a time packet");
    undo = fixForTwilight(*rulesPacket);
    auto& sent = static_cast<GameRulesChangedPacket&>(*rulesPacket).mRuleData->mRules->front();
    ctx.check(std::get_if<bool>(&sent.mValue.get()) && !*std::get_if<bool>(&sent.mValue.get()), "the clock is stopped");
    ctx.require(static_cast<bool>(undo), "with an undo");
    undo();
    ctx.check(std::get_if<bool>(&sent.mValue.get()) && *std::get_if<bool>(&sent.mValue.get()), "rule restored");
    auto other2 = MinecraftPackets::createPacket(MinecraftPacketIds::Text);
    ctx.check(other2 && timePacketKind(*other2) == TimePacket::None, "other packets are not time packets");
});

// The clock remote clients in the Twilight Forest are sent (what leaves for the network), watched
// for 20 s: every tick ends with the time of day 13000. Run on a server with real players there
// (the client e2e runs it on its host); without one it has nothing to watch.
GF_REGISTER_SELFTEST("dimension.clock_watch", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    std::vector<std::int64_t> ids;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulated() && world::isTwilight(p.getDimensionId()) && clientClock(p)) ids.push_back(idOf(p));
        return true;
    });
    if (ids.empty()) {
        ctx.info("no remote player in the Twilight Forest: nothing to watch");
        co_return;
    }
    for (auto id : ids)
        if (auto* p = playerById(id)) resetClientClockChecks(*p);
    co_await ctx.ticks(400);
    for (auto id : ids) {
        auto* p = playerById(id);
        if (!p) continue;
        auto c = clientClock(*p);
        ctx.require(c.has_value(), "clock of " + p->getRealName());
        ctx.info(
            p->getRealName() + ": " + std::to_string(c->packets) + " time packets in 20 s, time of day " + std::to_string(c->timeOfDay)
            + " (last " + c->lastPacket + "), clock " + (c->clockPaused ? "stopped" : "running") + ", wrong ticks "
            + std::to_string(c->wrongTicks)
        );
        ctx.check(c->wrongTicks == 0, p->getRealName() + "'s client ended " + std::to_string(c->wrongTicks) + " ticks with another time of day (first: " + c->wrongPacket + ")");
        ctx.check(c->timeOfDay == 13000, p->getRealName() + "'s client clock reads 13000");
        ctx.check(c->packets >= 3, p->getRealName() + " was sent its clock again and again");
    }
});

// ---- functions: /execute in the Twilight Forest --------------------------------------------------------------------------------

// Bedrock's /execute in accepts the custom dimension's identifier.
GF_REGISTER_SELFTEST("dimension.execute_in", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    auto& p    = ctx.player();
    auto  id   = idOf(p);
    auto  name = p.getRealName();
    auto [ok, out] = runCommand("execute as \"" + name + "\" at @s in twilightforest:twilight_forest run tp @s ~ ~ ~");
    ctx.check(ok, "execute in twilightforest:twilight_forest runs (" + out + ")");
    bool there = false;
    for (int i = 0; ok && i < 200 && !there; ++i) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        there    = pl && world::isTwilight(pl->getDimensionId());
    }
    ctx.check(there, "the player is in the Twilight Forest");
    auto [ok2, out2] = runCommand("execute as \"" + name + "\" at @s in overworld run tp @s ~ ~ ~");
    ctx.check(ok2, "execute in overworld runs (" + out2 + ")");
    bool back = false;
    for (int i = 0; ok2 && i < 200 && !back; ++i) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        back     = pl && static_cast<int>(pl->getDimensionId()) == 0;
    }
    ctx.check(back, "the player is back in the overworld");
});

// TF's functions tp_twilight / tp_overworld ("execute in <dimension> run tp @p ~ ~ ~"): Java's
// execute in scales X and Z by from.coordinateScale / to.coordinateScale (x8 into the Twilight
// Forest, /8 out of it).
GF_REGISTER_SELFTEST("dimension.functions", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    int tfId = static_cast<int>(*tf);
    // the semantics (CommandSourceStack#withLevel)
    auto in = runTpFunction(0, Vec3{100.5f, 70.0f, -20.25f}, true);
    ctx.require(in.has_value(), "tp_twilight has a destination");
    ctx.check(in->dim == tfId, "tp_twilight goes to the Twilight Forest");
    ctx.check(in->pos.x == 804.0f && in->pos.y == 70.0f && in->pos.z == -162.0f, "overworld 100.5 70 -20.25 is 804 70 -162 there");
    auto out = runTpFunction(tfId, Vec3{804.0f, 70.0f, -162.0f}, false);
    ctx.require(out.has_value(), "tp_overworld has a destination");
    ctx.check(out->dim == 0 && out->pos.x == 100.5f && out->pos.z == -20.25f, "and back: 100.5 70 -20.25 in the overworld");
    auto same = runTpFunction(tfId, Vec3{5.0f, 6.0f, 7.0f}, true);
    ctx.check(same && same->pos.x == 5.0f && same->pos.z == 7.0f, "within the same dimension the position stays");
    auto nether = runTpFunction(1, Vec3{10.0f, 64.0f, 10.0f}, true);
    ctx.check(nether && nether->pos.x == 640.0f && nether->pos.z == 640.0f, "the nether (scale 8) is 64 times farther in the Twilight Forest");

    // the behavior pack function, run by the test player at its feet (Y moved to 70 by `positioned`)
    auto& p    = ctx.player();
    auto  id   = idOf(p);
    auto  name = p.getRealName();
    auto  at   = ctx.testPos(5498);
    // a platform under the arrival spot first (the Twilight Forest is void until F5); loading it
    // may take the test player there
    BlockPos floor{static_cast<int>(std::floor((at.x + 0.5f) * 8.0f)), 69, static_cast<int>(std::floor((at.z + 0.5f) * 8.0f))};
    ctx.require(co_await ctx.ensureLoaded(core::GlobalPos{tfId, floor.x, floor.y, floor.z}), "the arrival spot loaded");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            core::GlobalPos f{tfId, floor.x + dx, floor.y, floor.z + dz};
            ctx.setBlock(f, "minecraft:stone");
            ctx.cleanupBlock(f);
        }
    // then the test player stands at the source spot in the overworld
    ctx.player().teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, DimensionType(ctx.dimension()));
    Player* pl = nullptr;
    for (int i = 0; i < 200; ++i) {
        co_await ctx.ticks(1);
        pl = playerById(id);
        if (pl && static_cast<int>(pl->getDimensionId()) == ctx.dimension() && std::abs(pl->getFeetPos().x - (at.x + 0.5f)) < 0.01f) break;
    }
    ctx.require(pl != nullptr && static_cast<int>(pl->getDimensionId()) == ctx.dimension(), "the test player at the source spot");
    Vec3 source = pl->getFeetPos();
    source.y    = 70.0f;
    auto want   = runTpFunction(ctx.dimension(), source, true);
    ctx.require(want.has_value(), "the expected destination");
    if (want->player != id) {
        ctx.info("another player is nearer to the scaled position: the in-game part is skipped");
        co_return;
    }
    auto [okF, outF] = runCommand("execute as \"" + name + "\" at @s positioned ~ 70 ~ run function twilightforest/dimension/tp_twilight");
    ctx.check(okF, "function tp_twilight runs (" + outF + ")");
    bool there = false;
    for (int i = 0; okF && i < 200 && !there; ++i) {
        co_await ctx.ticks(1);
        pl    = playerById(id);
        there = pl && world::isTwilight(pl->getDimensionId());
    }
    ctx.require(there, "tp_twilight moved the player into the Twilight Forest");
    co_await ctx.ticks(5);
    pl = playerById(id);
    ctx.require(pl != nullptr, "the test player");
    Vec3 got = pl->getFeetPos();
    ctx.info(std::format("from {:.2f} {:.2f} to {:.2f} {:.2f} {:.2f} in the Twilight Forest", source.x, source.z, got.x, got.y, got.z));
    ctx.check(std::abs(got.x - source.x * 8.0f) < 0.01f && std::abs(got.z - source.z * 8.0f) < 0.01f, "X and Z times 8");
    ctx.check(std::abs(got.y - 70.0f) < 1.5f, "Y as the execution position had it (70, on the platform)");
    // and back from where it stands, Y back on the overworld's ground
    Vec3 back = got;
    back.y    = static_cast<float>(at.y);
    auto home = runTpFunction(tfId, back, false);
    ctx.require(home.has_value(), "the expected way back");
    if (home->player != id) {
        ctx.info("another player is nearer to the scaled way back: it is not taken");
        pl->teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, DimensionType(ctx.dimension()));
        co_return;
    }
    auto [okB, outB] = runCommand(std::format("execute as \"{}\" at @s positioned ~ {} ~ run function twilightforest/dimension/tp_overworld", name, at.y));
    ctx.check(okB, "function tp_overworld runs (" + outB + ")");
    bool home2 = false;
    for (int i = 0; okB && i < 200 && !home2; ++i) {
        co_await ctx.ticks(1);
        pl    = playerById(id);
        home2 = pl && static_cast<int>(pl->getDimensionId()) == 0;
    }
    ctx.require(home2, "tp_overworld moved the player back to the overworld");
    co_await ctx.ticks(5);
    pl = playerById(id);
    ctx.require(pl != nullptr, "the test player");
    Vec3 end = pl->getFeetPos();
    ctx.check(std::abs(end.x - source.x) < 0.01f && std::abs(end.z - source.z) < 0.01f, std::format("X and Z divided by 8: back at {:.2f} {:.2f}", end.x, end.z));
});

// ---- the portal block's physical rules ---------------------------------------------------------------------------------

// TFPortalBlock#getFluidState (sugar cane keeps its water), pushReaction BLOCK (pistons can't move
// it) and no map colour (maps look through it).
GF_REGISTER_SELFTEST("dimension.portal_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5401, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int  y  = box.minY - 1; // the grass layer
    auto at = [&](int dx, int dz, int dy = 0) { return core::GlobalPos{ctx.dimension(), box.minX + dx, y + dy, box.minZ + dz}; };

    // sugar cane on the rim of a pool, then the pool is lit
    auto pool = at(1, 1);
    ctx.require(buildPool(ctx, pool), "pool built");
    auto cane   = pool.offset(-1, 1, 0);
    auto caneId = setFirst(ctx, cane, {"minecraft:reeds", "minecraft:sugar_cane"});
    ctx.require(!caneId.empty(), "sugar cane on the rim (found " + ctx.blockAt(cane) + ")");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    ctx.check(portalWatersSugarCane(*region, game::blockPos(cane)), "the portal next to its soil waters it");
    auto const& caneBlock = region->getBlock(game::blockPos(cane));
    ctx.check(caneBlock.getBlockType().mayPlace(*region, game::blockPos(cane)), "the cane may stay (SugarCaneBlock mayPlace)");
    // its neighbour (the short grass on the next rim block) changes: it checks that it can stay
    ctx.setBlock(cane.offset(0, 0, 1), "minecraft:air");
    ctx.setBlock(cane.offset(0, 0, 1), "minecraft:short_grass");
    co_await ctx.ticks(10);
    ctx.check(ctx.blockAt(cane) == caneId, "the cane is still there after a neighbour update (" + ctx.blockAt(cane) + ")");
    // away from water and portals the vanilla rule decides
    auto dry = at(6, 6, 1);
    ctx.check(!portalWatersSugarCane(*region, game::blockPos(dry)), "no portal by a dry spot");
    ctx.check(!caneBlock.getBlockType().mayPlace(*region, game::blockPos(dry)), "sugar cane may not stand on a dry spot");

    // no map colour (MapColor.NONE): the portal is transparent to maps, as air is
    auto const& portalState = region->getBlock(game::blockPos(pool));
    auto        portalColor = portalState.getBlockType().getMapColor(*region, game::blockPos(pool), portalState);
    auto        airPos      = game::blockPos(pool.offset(0, 2, 0));
    auto const& air         = region->getBlock(airPos);
    auto        airColor    = air.getBlockType().getMapColor(*region, airPos, air);
    ctx.info(std::format("map colours: portal {} {} {} {}, air {} {} {} {}", portalColor.r, portalColor.g, portalColor.b, portalColor.a, airColor.r, airColor.g, airColor.b, airColor.a));
    ctx.check(portalColor.a == 0.0f, "the portal has no map colour (alpha 0)");

    // pistons: a stone above a powered piston moves up, a portal block does not
    auto pushTest = [&](int dz, std::string const& id, nlohmann::json const& states) -> core::GlobalPos {
        auto base = at(6, dz);
        ctx.setBlock(base, "minecraft:piston", {{"facing_direction", 1}});
        ctx.setBlock(base.offset(0, 1, 0), id, states);
        return base;
    };
    auto stoneBase = pushTest(1, "minecraft:stone", nlohmann::json::object());
    auto portalBase = pushTest(4, kPortalBlock, {{kOneWayState, false}});
    // the portal block's rim (its neighbour rule keeps a portal ringed by portal/edge blocks)
    for (auto [dx, dz] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) ctx.setBlock(portalBase.offset(dx, 1, dz), "minecraft:dirt");
    for (auto base : {stoneBase, portalBase}) {
        auto [ok, msg] = runCommand(std::format("setblock {} {} {} redstone_block", base.x + 1, base.y, base.z), ctx.dimension());
        ctx.check(ok, "redstone block next to the piston (" + msg + ")");
    }
    co_await ctx.ticks(20);
    ctx.info("after the pistons fired: stone column " + ctx.blockAt(stoneBase.offset(0, 1, 0)) + " / " + ctx.blockAt(stoneBase.offset(0, 2, 0)) + ", portal column "
             + ctx.blockAt(portalBase.offset(0, 1, 0)) + " / " + ctx.blockAt(portalBase.offset(0, 2, 0)));
    ctx.check(ctx.blockAt(stoneBase.offset(0, 2, 0)) == "minecraft:stone", "the control: the piston pushed the stone up");
    ctx.check(ctx.blockAt(portalBase.offset(0, 1, 0)) == kPortalBlock, "the portal block stays where it is");
    ctx.check(ctx.blockAt(portalBase.offset(0, 2, 0)) != kPortalBlock, "and was not pushed up");
});

// ---- Player#drop: only throws name a thrower ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.thrower", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  level  = ll::service::getLevel();
    auto* region = ctx.region();
    ctx.require(level.has_value() && region != nullptr, "level");
    auto at    = ctx.testPos(5499, 2);
    auto stack = game::items::make("minecraft:diamond", 1);
    ctx.require(stack.has_value(), "diamond");
    auto& p  = ctx.player();
    auto  id = idOf(p);
    p.teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(2);
    auto* pl = playerById(id);
    ctx.require(pl != nullptr, "the test player");
    // a throw (drop key, a stack dragged out of a screen: dropAround false) names the thrower
    auto* thrown = dropFrom(*pl, *stack, false);
    ctx.require(thrown != nullptr, "the player threw a diamond");
    ctx.check(itemOwner(*thrown) == id, "a thrown item names its thrower");
    thrown->remove();
    // dropped around the player (Inventory#dropAll: dropAround, no thrower name)
    auto* around = dropFrom(*pl, *stack, true);
    ctx.require(around != nullptr, "the player dropped a diamond around itself");
    ctx.check(itemOwner(*around) == -1, "an item dropped around the player names nobody");
    around->remove();
    // the death drops of a player
    auto& rules     = *level->getGameRules().mGameRules;
    auto  keepIndex = static_cast<std::size_t>(GameRules::GameRulesIndex::KeepInventory);
    bool  keep      = keepIndex < rules.size() && std::get_if<bool>(&rules[keepIndex].mValue.get()) && *std::get_if<bool>(&rules[keepIndex].mValue.get());
    if (keep) {
        ctx.info("keepInventory is on: no death drops to look at");
        co_return;
    }
    auto& dying   = ctx.spawnPlayer("gf_test_dropper");
    auto  dyingId = idOf(dying);
    dying.teleport(Vec3{at.x + 2.5f, static_cast<float>(at.y), at.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(2);
    auto* d = playerById(dyingId);
    ctx.require(d != nullptr, "the dying player");
    ctx.give(*d, *stack);
    Vec3 where  = d->getFeetPos();
    auto before = itemsNear(*region, where, 8.0f);
    d->kill();
    ItemActor* dropped = nullptr;
    for (int i = 0; i < 40 && !dropped; ++i) {
        co_await ctx.ticks(1);
        dropped = newItemNear(*region, where, before, "minecraft:diamond");
    }
    ctx.require(dropped != nullptr, "the dead player dropped its diamond");
    ctx.check(itemOwner(*dropped) == -1, "death drops name nobody (Inventory#dropAll)");
    dropped->remove();
});

// ---- dimension_type values (contract extension feature/ext/dimension/DimensionType.h) ---------------------------------

GF_REGISTER_SELFTEST("dimension.dimension_type", [](Context& ctx) {
    namespace ext = world::ext;
    auto const& t = ext::twilightType();
    ctx.check(t.fixedTime == 13000 && t.coordinateScale == 0.125 && t.minY == -32 && t.height == 288 && t.logicalHeight == 288, "twilight_forest_type: time, scale, height");
    ctx.check(t.ambientLight == 0.01f && t.monsterSpawnBlockLightLimit == 7 && t.monsterSpawnLightMin == 0 && t.monsterSpawnLightMax == 7, "ambient light and monster spawn light");
    ctx.check(t.bedWorks && !t.respawnAnchorWorks && !t.hasRaids && !t.piglinSafe && t.natural && t.hasSkylight && !t.hasCeiling && !t.ultrawarm, "flags");
    auto tf = world::twilight();
    ctx.require(tf.has_value(), "the Twilight Forest exists");
    ctx.check(&ext::dimensionType(static_cast<int>(*tf)) == &t, "the Twilight Forest's id reads twilight_forest_type");
    ctx.check(ext::dimensionType(0).id == "minecraft:overworld" && ext::dimensionType(1).id == "minecraft:the_nether", "vanilla types");
    // DimensionType#brightness
    ctx.check(std::abs(ext::brightness(0.01f, 0) - 0.01f) < 1e-6f, "light 0: the ambient light");
    ctx.check(std::abs(ext::brightness(0.01f, 15) - 1.0f) < 1e-6f, "light 15: 1");
    ctx.check(std::abs(ext::brightness(0.0f, 7) - (7.0f / 15.0f) / (4.0f - 3.0f * 7.0f / 15.0f)) < 1e-6f, "light 7 without ambient light");
    // the Twilight Forest's brightness ramp has it
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto d = level->getOrCreateDimension(*tf).lock();
    ctx.require(d != nullptr, "the dimension");
    auto const& ramp = d->getBrightnessRamp().mBrightnessRamp;
    bool        same = true;
    for (int i = 0; i < 16; ++i) same &= std::abs(ramp[i] - ext::brightness(0.01f, i)) < 1e-6f;
    ctx.check(same, std::format("the dimension's brightness ramp is DimensionType#brightness with 0.01 (light 0: {:.4f})", ramp[0]));
    // Level#updateSkyBrightness at time 13000 (DimensionType#timeOfDay 0.2597), noon and midnight
    float tod = d->getTimeOfDay(level->getTime(), 0.0f);
    ctx.check(std::abs(tod - 0.25965f) < 1e-3f, std::format("time of day {:.5f}", tod));
    ctx.check(ext::skyDarken(tod, 0.0f, 0.0f) == 6 && ext::skyDarken(0.0f, 0.0f, 0.0f) == 0 && ext::skyDarken(0.5f, 0.0f, 0.0f) == 11, "sky darkening 6 at 13000, 0 at noon, 11 at midnight");
    // Monster#isDarkEnoughToSpawn: nextInt(32) against the sky light, the block light limit, then
    // the light test nextInt(8)
    auto dark = [&](ext::DimensionTypeValues const& type, int sky, int block, int darken, std::vector<int> rolls) {
        ScriptedRandom r(std::move(rolls));
        return ext::isDarkEnoughToSpawn(type, sky, block, darken, r);
    };
    ctx.check(dark(t, 0, 0, 6, {0, 0}), "darkness spawns");
    ctx.check(dark(t, 0, 7, 6, {0, 7}), "block light 7 spawns in the Twilight Forest when the test rolls 7");
    ctx.check(!dark(t, 0, 7, 6, {0, 6}), "but not when it rolls 6");
    ctx.check(!dark(t, 0, 8, 6, {0, 7}), "block light 8 never spawns there");
    ctx.check(!dark(ext::dimensionType(0), 0, 1, 6, {0, 7}), "the overworld's block light limit is 0");
    ctx.check(!dark(t, 15, 0, 6, {14, 7}), "a sky light above the nextInt(32) roll refuses");
    ctx.check(dark(t, 13, 0, 6, {31, 7}), "sky light 13 darkened by 6 is 7");
    ctx.check(!dark(t, 14, 0, 6, {31, 7}), "sky light 14 darkened by 6 is 8");
    ctx.check(dark(ext::dimensionType(1), 0, 15, 0, {0, 0}) == false && dark(ext::dimensionType(1), 0, 7, 0, {0, 0}), "the nether: no block light limit, a constant 7");
});

} // namespace

} // namespace gf::feature::dimension
