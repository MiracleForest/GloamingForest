// Selftests of unit F4 (dimension): `/gloamingforest selftest "dimension.*"`, dx 5400..5499 (rule R12).

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

#include "ll/api/Config.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/GameRulesChangedPacket.h"
#include "mc/network/packet/SetTimePacket.h"
#include "mc/network/packet/SyncWorldClocksPacket.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
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
#include "feature/ext/entity/EntityExt.h"
#include "feature/gamerules/GameRules.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::dimension {

namespace {

using selftest::Context;
namespace tp = teleporter;
namespace ge = gf::entity;

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

/// The blocks a test changes outside its flat areas (the generated Gloaming Forest, the ground
/// under a flat area): saved before they change (id, states and the liquid layer, never a Block
/// pointer: rule R16) and put back when the test ends, bottom up, with their chunks loaded first.
/// While `watchSites()` is on, every portal site makePortalAt builds (a trip's return portal) is
/// saved before it is built. A test that ends early (require) has them put back by the destructor.
class TerrainRestore {
public:
    TerrainRestore() = default;
    TerrainRestore(TerrainRestore const&)            = delete;
    TerrainRestore& operator=(TerrainRestore const&) = delete;
    ~TerrainRestore() {
        if (mWatching) tp::setSiteObserver({});
        if (mBlocks.empty()) return;
        ll::coro::keepThis([blocks = std::move(mBlocks)]() -> ll::coro::CoroTask<> {
            int n = co_await putBack(blocks);
            log().info("dimension selftest: put back {} blocks of the terrain after the test ended early", n);
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    }

    /// Saves the block at `pos` (the first save of a position counts).
    void save(BlockSource& region, BlockPos const& pos) {
        int dim = static_cast<int>(region.getDimensionId());
        if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return;
        if (!mSeen.insert({dim, pos.x, pos.y, pos.z}).second) return;
        Saved b{dim, pos, game::blockId(region.getBlock(pos)), statesJson(region.getBlock(pos)), {}, {}};
        Block const& extra = region.getExtraBlock(pos);
        if (!extra.isAir()) {
            b.extraId     = game::blockId(extra);
            b.extraStates = statesJson(extra);
        }
        mBlocks.push_back(std::move(b));
    }
    /// Saves the blocks makePortalAt changes around a portal whose lowest corner is `corner`.
    void saveSite(BlockSource& region, BlockPos const& corner) {
        for (int dy = -1; dy <= 5; ++dy)
            for (int dx = -1; dx <= 2; ++dx)
                for (int dz = -1; dz <= 2; ++dz) save(region, BlockPos{corner.x + dx, corner.y + dy, corner.z + dz});
        mSites.emplace_back(static_cast<int>(region.getDimensionId()), corner);
    }
    /// Saves every site makePortalAt builds from now on (until restore() or the end of the test).
    void watchSites() {
        mWatching = true;
        tp::setSiteObserver([this](BlockSource& region, BlockPos const& corner) { saveSite(region, corner); });
    }
    [[nodiscard]] std::size_t sites() const { return mSites.size(); }
    /// The lowest corners of the portals built in dimension `dim` while watching.
    [[nodiscard]] std::vector<BlockPos> sitesIn(int dim) const {
        std::vector<BlockPos> out;
        for (auto const& [d, corner] : mSites)
            if (d == dim) out.push_back(corner);
        return out;
    }
    /// Puts every saved block back; the number of blocks that had changed.
    ll::coro::CoroTask<int> restore() {
        if (mWatching) tp::setSiteObserver({});
        mWatching   = false;
        auto blocks = std::move(mBlocks);
        mBlocks.clear();
        mSeen.clear();
        int n = co_await putBack(blocks); // (the portal index hears of it through gf::blockevents)
        mSites.clear();
        co_return n;
    }

private:
    struct Saved {
        int            dim = 0;
        BlockPos       pos;
        std::string    id;
        nlohmann::json states;
        std::string    extraId; ///< the liquid layer ("" when empty)
        nlohmann::json extraStates;
    };

    /// game::statesOf (text values) as blockWithStates takes them (bool, int or text).
    static nlohmann::json statesJson(Block const& block) {
        nlohmann::json out = nlohmann::json::object();
        for (auto const& [name, text] : game::statesOf(block)) {
            if (text == "true" || text == "false") out[name] = text == "true";
            else if (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || (text[0] == '-' && text.size() > 1))
                     && text.find_first_not_of("-0123456789") == std::string::npos)
                out[name] = std::stoi(text);
            else out[name] = text;
        }
        return out;
    }

    /// Puts one saved block back (its chunk is loaded); true when it had changed.
    static bool putOne(BlockSource& region, Saved const& b) {
        bool        changed = false;
        auto const* want    = game::blockWithStates(b.id, b.states);
        if (!want) want = game::blockWith(b.id);
        if (want && &region.getBlock(b.pos) != want) {
            game::setBlock(region, b.pos, *want);
            changed = true;
        }
        auto const* extra = b.extraId.empty() ? game::blockWith("minecraft:air") : game::blockWithStates(b.extraId, b.extraStates);
        if (extra && &region.getExtraBlock(b.pos) != extra) region.setExtraBlock(b.pos, *extra, 3);
        return changed;
    }

    static ll::coro::CoroTask<int> putBack(std::vector<Saved> blocks) {
        std::stable_sort(blocks.begin(), blocks.end(), [](Saved const& a, Saved const& b) { return a.pos.y < b.pos.y; });
        // every block whose chunk is still loaded goes back at once, within one tick: a portal never
        // sees half of its frame put back (the portal's neighbour rule, run at the end of the tick
        // through gf::blockevents, would turn it into water that then flows into the terrain)
        int                                                           changed = 0;
        std::map<std::tuple<int, int, int>, std::vector<std::size_t>> byChunk;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            auto const& b      = blocks[i];
            auto*       region = game::regionOf(b.dim);
            if (region && region->hasChunksAt(b.pos, 0, false)) changed += putOne(*region, b) ? 1 : 0;
            else byChunk[{b.dim, b.pos.x >> 4, b.pos.z >> 4}].push_back(i);
        }
        // the rest chunk by chunk, each loaded first
        for (auto const& [key, list] : byChunk) {
            auto [dim, cx, cz] = key;
            auto hold          = co_await loadArea(dim, BlockPos{cx * 16 + 8, 64, cz * 16 + 8}, 0);
            auto* region       = game::regionOf(dim);
            if (!hold || !region) {
                log().warn("dimension selftest: chunk {} {} of dimension {} did not load: {} blocks not put back", cx, cz, dim, list.size());
                continue;
            }
            for (auto i : list) changed += putOne(*region, blocks[i]) ? 1 : 0;
        }
        co_return changed;
    }

    std::vector<Saved>                         mBlocks;
    std::set<std::tuple<int, int, int, int>>   mSeen;
    std::vector<std::pair<int, BlockPos>>      mSites;
    bool                                       mWatching = false;
};

// ---- the dimension (spike S1) ---------------------------------------------------------------------------------------

// The dimension exists with upstream's dimension_type values.
GF_REGISTER_SELFTEST("dimension.registered", [](Context& ctx) {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "gloamingforest:gloaming_forest is registered");
    int id = static_cast<int>(*upsrc);
    ctx.info("the Gloaming Forest is dimension " + std::to_string(id));
    ctx.check(id > 2, "it is a custom dimension (id > 2)");
    ctx.check(world::isGloaming(*upsrc), "isGloaming(id)");
    ctx.check(!world::isGloaming(DimensionType(0)), "the overworld is not the Gloaming Forest");
    ctx.check(dimensionByName(world::kDimensionId) == id, "found by identifier");
    ctx.check(dimensionName(id) == world::kDimensionId, "identifier of the id");
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto d = level->getOrCreateDimension(*upsrc).lock();
    ctx.require(d != nullptr, "the dimension is created");
    ctx.check(world::isGloaming(*d), "isGloaming(Dimension)");
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

// A player enters the dimension and blocks can be placed from y -32 to y 255 (in the generated
// terrain: the blocks it had are put back afterwards).
GF_REGISTER_SELFTEST("dimension.enter", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inGloaming(), "moved into the Gloaming Forest");
    auto upsrc = *world::gloaming();
    ctx.check(static_cast<int>(ctx.player().getDimensionId()) == static_cast<int>(upsrc), "the player is in the Gloaming Forest");
    auto           at = ctx.testPos(5400);
    TerrainRestore terrain;
    auto           hold   = co_await loadArea(static_cast<int>(upsrc), BlockPos{at.x, 0, at.z}, 0);
    auto*          region = ctx.region();
    ctx.require(hold != nullptr && region != nullptr, "the test column loaded");
    auto const* stone = game::blockWith("minecraft:stone");
    ctx.require(stone != nullptr, "stone");
    for (int y : {world::kMinY, 0, 64, world::kMinY + world::kHeight - 1}) {
        BlockPos p{at.x, y, at.z};
        auto     was = game::blockId(region->getBlock(p));
        terrain.save(*region, p);
        game::setBlock(*region, p, *stone);
        ctx.check(game::blockId(region->getBlock(p)) == "minecraft:stone", "stone reads back at y " + std::to_string(y) + " (was " + was + ")");
    }
    ctx.check(region->getMinHeight() == world::kMinY, "region min height -32");
    ctx.check(region->getMaxHeight() == world::kMinY + world::kHeight, "region max height 256");
    int back = co_await terrain.restore();
    ctx.info("put back " + std::to_string(back) + " blocks of the generated terrain");
    region = game::regionOf(static_cast<int>(upsrc));
    ctx.require(region != nullptr, "region");
    ctx.check(game::blockId(region->getBlock(BlockPos{at.x, world::kMinY + world::kHeight - 1, at.z})) != "minecraft:stone", "the terrain is back");
});

// ---- config, gamerules, tags ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.config_defaults", [](Context& ctx) {
    DimensionConfig d;
    ctx.check(!d.newPlayersSpawnInGloaming && !d.portalForNewPlayer, "dimension settings default off");
    ctx.check(d.originDimension == "minecraft:overworld", "origin dimension");
    ctx.check(!d.allowPortalsInOtherDimensions && d.portalCreationPermission == 0 && !d.disablePortalCreation, "creation defaults");
    ctx.check(d.checkPortalPlacement && d.destructivePortalLightning && d.shouldReturnPortalBeUsable, "portal defaults");
    ctx.check(d.portalUnlockedByAdvancement.empty() && d.maxPortalSize == 64, "lock and size defaults");
});

// GloamingPortalBlock#getPortalTransitionTime: the two gamerules of the gamerule store (F10), the
// creative one for invulnerable players (creative, spectator). Set through the contract and through
// `/gloamingforest gamerule`; the rules go back to what they were.
GF_REGISTER_SELFTEST("dimension.portal_delay", [](Context& ctx) {
    struct RestoreRules {
        gamerules::Value def      = gamerules::get(kDefaultDelayRule);
        gamerules::Value creative = gamerules::get(kCreativeDelayRule);
        ~RestoreRules() {
            gamerules::set(kDefaultDelayRule, def);
            gamerules::set(kCreativeDelayRule, creative);
        }
    } restore;
    ctx.check(std::holds_alternative<int>(restore.def) && std::holds_alternative<int>(restore.creative), "both delays are integer gamerules of the store");
    auto& p = static_cast<SimulatedPlayer&>(ctx.player());
    p.setPlayerGameType(GameType::Survival);
    ctx.check(portalDelay(p) == gamerules::getInt(kDefaultDelayRule), "survival: playersGloamingPortalDefaultDelay, got " + std::to_string(portalDelay(p)));
    ctx.info(std::format("the world's delays: {} / {} (upstream's defaults 60 / 1)", gamerules::getInt(kDefaultDelayRule), gamerules::getInt(kCreativeDelayRule)));
    ctx.require(gamerules::set(kDefaultDelayRule, 30), "playersGloamingPortalDefaultDelay set to 30");
    ctx.check(portalDelay(p) == 30, "a survival player's delay follows it (" + std::to_string(portalDelay(p)) + ")");
    auto cmd = selftest::runServerCommand(std::string("gloamingforest gamerule ") + kDefaultDelayRule + " 45");
    ctx.check(cmd.ok, "/gloamingforest gamerule " + std::string(kDefaultDelayRule) + " 45 (" + cmd.messages + ")");
    ctx.check(portalDelay(p) == 45, "and through the command (" + std::to_string(portalDelay(p)) + ")");
    ctx.require(gamerules::set(kCreativeDelayRule, 7), "playersGloamingPortalCreativeDelay set to 7");
    p.setPlayerGameType(GameType::Creative);
    ctx.check(portalDelay(p) == 7, "a creative player's delay is playersGloamingPortalCreativeDelay (" + std::to_string(portalDelay(p)) + ")");
    p.setPlayerGameType(GameType::Spectator);
    ctx.check(portalDelay(p) == 7, "so is a spectator's (" + std::to_string(portalDelay(p)) + ")");
    p.setPlayerGameType(GameType::Adventure);
    ctx.check(portalDelay(p) == 45, "an adventure player's is the default one (" + std::to_string(portalDelay(p)) + ")");
    p.setPlayerGameType(GameType::Survival);
});

GF_REGISTER_SELFTEST("dimension.tags", [](Context& ctx) {
    ctx.check(blockTagged("gloamingforest:portal/fluid", "minecraft:water"), "water is the portal fluid");
    ctx.check(blockTagged("gloamingforest:portal/edge", "minecraft:grass_block"), "grass is a portal edge");
    ctx.check(blockTagged("gloamingforest:portal/edge", "minecraft:dirt"), "dirt is a portal edge");
    ctx.check(!blockTagged("gloamingforest:portal/edge", "minecraft:stone"), "stone is not a portal edge");
    ctx.check(blockTagged("gloamingforest:portal/decoration", "minecraft:short_grass"), "short grass decorates");
    ctx.check(blockTagged("gloamingforest:portal/decoration", "minecraft:poppy"), "flowers decorate");
    ctx.check(!blockTagged("gloamingforest:portal/decoration", "minecraft:stone"), "stone does not decorate");
    ctx.check(itemTagged("gloamingforest:portal/activator", "minecraft:diamond"), "a diamond activates");
    ctx.check(!itemTagged("gloamingforest:portal/activator", "minecraft:emerald"), "an emerald does not");
    ctx.check(!blockTagMembers("gloamingforest:portal/generated_decoration").empty(), "generated decorations");
    // upstream's own members of the vanilla tags the portal tags include (BlockTagGenerator)
    ctx.check(blockTagged("gloamingforest:portal/edge", "gloamingforest:uberous_soil"), "uberous soil is a portal edge (#dirt)");
    ctx.check(blockTagged("gloamingforest:portal/decoration", "gloamingforest:canopy_sapling"), "upstream saplings decorate (#saplings)");
    ctx.check(blockTagged("gloamingforest:portal/decoration", "gloamingforest:gloaming_oak_leaves"), "upstream leaves decorate (#leaves)");
    ctx.check(portalBlock(false) && portalBlock(true) && portalBlock(false) != portalBlock(true), "both portal states exist");
    // the tag store (F9) answers upstream's portal tags itself (F4's own member lists are only a fallback
    // for an empty store), with Java's ids turned into the game's
    for (auto const* t : {"gloamingforest:portal/edge", "gloamingforest:portal/decoration", "gloamingforest:portal/fluid", "gloamingforest:portal/generated_decoration"})
        ctx.check(!tags::members(tags::Registry::Block, t).empty(), std::string("the tag store has #") + t);
    ctx.check(!tags::members(tags::Registry::Item, "gloamingforest:portal/activator").empty(), "the tag store has #gloamingforest:portal/activator");
    auto registered = [](std::initializer_list<char const*> ids) -> std::string {
        for (auto const* id : ids)
            if (game::blockWith(id)) return id;
        return {};
    };
    ctx.check(blockTagged("gloamingforest:portal/edge", "minecraft:farmland"), "farmland is a portal edge");
    if (auto path = registered({"minecraft:dirt_path", "minecraft:grass_path"}); !path.empty()) ctx.check(blockTagged("gloamingforest:portal/edge", path), path + " (minecraft:dirt_path) is a portal edge");
    if (auto cane = registered({"minecraft:sugar_cane", "minecraft:reeds"}); !cane.empty()) ctx.check(blockTagged("gloamingforest:portal/decoration", cane), cane + " (minecraft:sugar_cane) decorates");
    if (auto bush = registered({"minecraft:dead_bush", "minecraft:deadbush"}); !bush.empty()) ctx.check(blockTagged("gloamingforest:portal/decoration", bush), bush + " (minecraft:dead_bush) decorates");
});

// ---- GloamingPortalBlock: lighting --------------------------------------------------------------------------------------------

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

/// The players the progression bus (F10) heard gloamingforest:make_gf_portal for.
std::vector<std::int64_t>& madePortal() {
    static std::vector<std::int64_t> v;
    return v;
}
void listenToMadePortal() {
    static bool const once = [] {
        progression::onTrigger(kMakePortalTrigger, [](Player& p, nlohmann::json const&) {
            if (madePortal().size() < 64) madePortal().push_back(idOf(p));
        });
        return true;
    }();
    (void)once;
}

// A diamond thrown by a player into a pool lights it (ProgressionEvents#checkForPortalCreation) and
// fires upstream's make_gf_portal trigger.
GF_REGISTER_SELFTEST("dimension.portal_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5440, 12, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY - 1, box.minZ + 4};
    ctx.require(buildPool(ctx, pool), "pool built");
    auto& p     = ctx.player();
    auto  owner = idOf(p);
    listenToMadePortal();
    madePortal().clear();
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
    ctx.check(std::count(madePortal().begin(), madePortal().end(), owner) == 1, "the progression bus heard gloamingforest:make_gf_portal for the player once");
});

// GloamingPortalBlock#neighborChanged through the game's own block changes (gf::blockevents, F11): a
// portal whose rim or floor is no longer valid turns back into water, and the water it leaves
// reaches the portal blocks next to it (Java: setBlock(WATER, UPDATE_ALL)), so the whole pool goes.
// Changes that keep the frame valid (another edge block, a block above, a decoration) leave it.
GF_REGISTER_SELFTEST("dimension.portal_neighbor", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5460, 16, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int  dim   = ctx.dimension();
    int  y     = box.minY - 1; // the grass layer
    auto count = [&](core::GlobalPos const& pool, std::string const& id) {
        int n = 0;
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz) n += ctx.blockAt(pool.offset(dx, 0, dz)) == id ? 1 : 0;
        return n;
    };
    auto light = [&](core::GlobalPos const& pool) {
        if (!buildPool(ctx, pool)) return false;
        auto* r = ctx.region();
        return r && portal::tryActivate(*r, game::blockPos(pool));
    };
    // the block changes reach the portal at the end of the tick; a few ticks for the water to spread
    auto settle = [&]() { return ctx.ticks(4); };
    // the player waits out of the lightning's reach (3 blocks around each pool)
    ctx.player().teleport(Vec3{box.minX + 8.5f, static_cast<float>(box.minY), box.minZ + 7.5f}, DimensionType(dim));
    co_await ctx.ticks(1);

    // A: a lit portal stays through its own lighting and through changes that keep its frame
    auto a = core::GlobalPos{dim, box.minX + 2, y, box.minZ + 2};
    ctx.require(light(a), "pool A lit");
    co_await settle();
    ctx.check(count(a, kPortalBlock) == 4, "the lit portal stays a portal (" + std::to_string(count(a, kPortalBlock)) + " of 4)");
    {
        // the portal index hears of the new portal blocks through gf::blockevents' onPlaced
        auto indexed = indexedPortals(dim);
        int  known   = 0;
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz) known += std::count(indexed.begin(), indexed.end(), game::blockPos(a.offset(dx, 0, dz))) > 0 ? 1 : 0;
        ctx.check(known == 4, "the portal index knows the lit portal (" + std::to_string(known) + " of 4)");
    }
    auto rimA = a.offset(-1, 0, 0); // the west rim block the player breaks below
    ctx.check(ctx.setBlock(a.offset(-1, 0, 1), "minecraft:dirt"), "a rim block becomes dirt (still a portal/edge block)");
    ctx.check(ctx.setBlock(a.offset(0, 1, 0), "minecraft:glass"), "glass above a portal block");
    ctx.check(ctx.setBlock(rimA.offset(0, 1, 0), "minecraft:air"), "a rim decoration taken away (decorations matter for lighting only)");
    co_await settle();
    ctx.check(ctx.setBlock(a.offset(0, 1, 0), "minecraft:air"), "the glass taken away again");
    co_await settle();
    ctx.check(count(a, kPortalBlock) == 4, "changes that keep the frame valid leave the portal (" + std::to_string(count(a, kPortalBlock)) + " of 4)");

    // ... then a survival player breaks a rim block by hand: the portal turns back into water, all of it
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player()); // the test player is a simulated one
        sp.setPlayerGameType(GameType::Survival);
        sp.teleport(Vec3{rimA.x - 0.5f, static_cast<float>(y + 1), rimA.z + 0.5f}, DimensionType(dim));
        sp.simulateLookAt(game::blockPos(rimA), sim::LookDuration::Instant);
        sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(2); // a simulated player applies its look on its next tick
    std::string how = static_cast<SimulatedPlayer&>(ctx.player()).simulateDestroyBlock(game::blockPos(rimA), ScriptModuleMinecraft::ScriptFacing::Up)
                        ? "started"
                        : "refused";
    int took = 0;
    for (; took < 120 && ctx.blockAt(rimA) == "minecraft:grass_block"; ++took) co_await ctx.ticks(1);
    static_cast<SimulatedPlayer&>(ctx.player()).simulateStopDestroyingBlock();
    ctx.info("the player's destroy " + how + ": the rim block is " + ctx.blockAt(rimA) + " after " + std::to_string(took) + " ticks");
    if (ctx.blockAt(rimA) == "minecraft:grass_block") {
        ctx.info("the simulated player could not break the rim block: a block change breaks it instead");
        ctx.setBlock(rimA, "minecraft:air");
    }
    co_await settle();
    ctx.check(count(a, "minecraft:water") == 4, "the rim broken by a player: the whole portal is water again (" + std::to_string(count(a, "minecraft:water")) + " of 4)");
    if (auto* r = ctx.region()) {
        // the grass block's drop
        AABB near{Vec3(rimA.x - 3.0f, y - 1.0f, rimA.z - 3.0f), Vec3(rimA.x + 4.0f, y + 4.0f, rimA.z + 4.0f)};
        for (auto* e : r->getEntities(near, 0.0f, ActorType::ItemEntity))
            if (e && e->getEntityTypeId() == ActorType::ItemEntity) e->remove();
    }
    ctx.player().teleport(Vec3{box.minX + 8.5f, static_cast<float>(box.minY), box.minZ + 7.5f}, DimensionType(dim));
    co_await ctx.ticks(1);

    // B: stone in the rim
    auto b = core::GlobalPos{dim, box.minX + 7, y, box.minZ + 2};
    ctx.require(light(b), "pool B lit");
    co_await settle();
    ctx.require(count(b, kPortalBlock) == 4, "pool B is a portal");
    ctx.check(ctx.setBlock(b.offset(2, 0, 1), "minecraft:stone"), "a rim block becomes stone");
    co_await settle();
    ctx.check(count(b, "minecraft:water") == 4, "stone in the rim: the whole portal is water again (" + std::to_string(count(b, "minecraft:water")) + " of 4)");

    // C: the floor under one portal block goes (a pool built on the grass layer, so that its floor
    // is in the flat area)
    auto c = core::GlobalPos{dim, box.minX + 12, y + 1, box.minZ + 2};
    ctx.require(light(c), "pool C lit");
    co_await settle();
    ctx.require(count(c, kPortalBlock) == 4, "pool C is a portal");
    ctx.check(ctx.setBlock(c.offset(1, -1, 1), "minecraft:air"), "the floor under a portal block taken away");
    co_await settle();
    ctx.check(count(c, "minecraft:water") == 4, "no floor: the whole portal is water again (" + std::to_string(count(c, "minecraft:water")) + " of 4)");
    // (the index follows: none of the three pools is indexed as portal blocks any more)
    auto indexed = indexedPortals(dim);
    int  stale   = 0;
    for (auto const& pool : {a, b, c})
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz)
                stale += std::count(indexed.begin(), indexed.end(), game::blockPos(pool.offset(dx, 0, dz))) > 0 ? 1 : 0;
    ctx.check(stale == 0, "the portal index forgot the blocks that turned into water (" + std::to_string(stale) + " left)");
});

// GloamingPortalBlock#neighborChanged reads the floor and the four sides with Level#getBlockState, which
// loads a chunk that is not loaded. Two portal blocks on the east border of a loaded area (their east
// sides in a chunk that is not loaded) hear of a change: each check waits for that chunk, has it
// loaded and runs again. The one whose west side became stone turns into water; the other one,
// whose frame is whole, stays (upstream's rule on the block the east chunk holds). Under the surface of
// the flat world: the water has nowhere to flow, and every changed block is put back afterwards.
GF_REGISTER_SELFTEST("dimension.portal_neighbor_unloaded", [](Context& ctx) -> ll::coro::CoroTask<> {
    int  dim  = ctx.dimension();
    auto base = ctx.testPos(5466);
    // 4096 blocks east of the test rows, far from every player and test area: one chunk is loaded
    int  cx   = (base.x + 4096) >> 4;
    int  cz   = base.z >> 4;
    auto hold = co_await loadArea(dim, BlockPos{cx * 16 + 8, 0, cz * 16 + 8}, 0);
    ctx.require(hold != nullptr, "a far chunk loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    // the east border of the loaded area there
    int border = cx;
    while (border < cx + 8 && region->hasChunksAt(BlockPos{(border + 1) * 16, 0, cz * 16 + 8}, 0, false)) ++border;
    ctx.require(border < cx + 8, "a chunk east of the far area is not loaded");
    int x   = border * 16 + 15; // the east side is in the next chunk
    int top = region->getAboveTopSolidBlock(x, cz * 16 + 8, false, false);
    int y   = top - 2; // the flat world's dirt under its grass
    ctx.require(y - 1 >= region->getMinHeight(), "ground deep enough under the far area (surface " + std::to_string(top) + ")");
    BlockPos whole{x, y, cz * 16 + 4}, broken{x, y, cz * 16 + 10};
    auto     at = [&](BlockPos const& p) { return core::GlobalPos{dim, p.x, p.y, p.z}; };
    for (auto const& p : {whole, broken}) {
        // the next chunk holds the same ground at this height (a flat world): solid, an edge block
        ctx.require(blockTagged("gloamingforest:portal/edge", ctx.blockAt(at(p))), "dirt under the far area at " + str(p) + " (" + ctx.blockAt(at(p)) + ")");
    }
    auto const* portal = portalBlock(false);
    ctx.require(portal != nullptr, "the portal block");
    TerrainRestore terrain;
    auto put = [&](BlockPos const& p, Block const* block) {
        if (!block) return false;
        terrain.save(*region, p);
        game::setBlock(*region, p, *block);
        return &region->getBlock(p) == block;
    };
    auto side = [](BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; };
    auto const* dirt  = game::blockWith("minecraft:dirt");
    auto const* stone = game::blockWith("minecraft:stone");
    // a frame on the loaded side (floor, north, south, west), then the portal blocks
    for (auto const& p : {whole, broken})
        for (auto const& n : {side(p, 0, -1, 0), side(p, 0, 0, -1), side(p, 0, 0, 1), side(p, -1, 0, 0)}) ctx.require(put(n, dirt), "dirt at " + str(n));
    ctx.require(put(whole, portal) && put(broken, portal), "two portal blocks on the border");
    for (auto const& p : {whole, broken}) ctx.require(!region->hasChunksAt(side(p, 1, 0, 0), 0, false), "the east side of " + str(p) + " is in a chunk that is not loaded");
    // the changes: stone west of one, a block above the other (its frame stays whole)
    int waited = portalChecksWaited();
    ctx.require(put(side(broken, -1, 0, 0), stone), "stone west of the broken one");
    ctx.require(put(side(whole, 0, 1, 0), stone), "stone above the whole one");
    co_await ctx.ticks(2); // the block changes reach the portal blocks at the end of the tick
    ctx.check(portalChecksWaited() - waited >= 2, "both checks waited for the east chunk (" + std::to_string(portalChecksWaited() - waited) + ")");
    int took = 0;
    for (; took < 300 && portalChecksWaiting() > 0; ++took) co_await ctx.ticks(1);
    ctx.check(portalChecksWaiting() == 0, "the checks ran once the east chunk loaded (" + std::to_string(took) + " ticks)");
    // the east sides as the checks read them (the chunk held for the reading)
    auto holdEast = co_await loadArea(dim, side(whole, 1, 0, 0), 0);
    ctx.require(holdEast != nullptr, "the east chunk loaded");
    auto eastWhole = ctx.blockAt(at(side(whole, 1, 0, 0)));
    auto eastBroken = ctx.blockAt(at(side(broken, 1, 0, 0)));
    ctx.info("after " + std::to_string(took) + " ticks: the east sides are " + eastWhole + " / " + eastBroken + "; the portal blocks are " + ctx.blockAt(at(whole)) + " / "
             + ctx.blockAt(at(broken)));
    ctx.check(ctx.blockAt(at(broken)) == "minecraft:water", "the broken frame: the portal block turned into water (" + ctx.blockAt(at(broken)) + ")");
    bool edge = blockTagged("gloamingforest:portal/edge", eastWhole);
    ctx.check(
        ctx.blockAt(at(whole)) == (edge ? std::string(kPortalBlock) : std::string("minecraft:water")),
        std::string("the whole frame (east side ") + eastWhole + "): " + (edge ? "the portal block stays" : "upstream's rule turns it into water") + " (" + ctx.blockAt(at(whole)) + ")"
    );
    int n = co_await terrain.restore();
    ctx.info("put back " + std::to_string(n) + " blocks");
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

// A player stands in a lit portal: after the delay it is in the Gloaming Forest next to a return
// portal; standing in that one brings it back next to the first portal.
GF_REGISTER_SELFTEST("dimension.portal_travel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int   gfId   = static_cast<int>(*upsrc);
    auto  box    = co_await ctx.flatArea(5480, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY - 1, box.minZ + 3};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    // the return portal is built into the generated Gloaming Forest: put back afterwards
    TerrainRestore terrain;
    terrain.watchSites();
    auto& p  = ctx.player();
    auto  id = idOf(p);
    p.teleport(Vec3{pool.x - 1.5f, static_cast<float>(pool.y + 1), pool.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(20);
    auto* pp = playerById(id);
    ctx.require(pp && static_cast<int>(pp->getDimensionId()) == ctx.dimension(), "back at the pool");
    auto dest = portal::destinationFor(*pp);
    ctx.check(dest && dest->dim == gfId, "destinationFor: the Gloaming Forest");
    pp->teleport(Vec3{pool.x + 0.5f, static_cast<float>(pool.y), pool.z + 0.5f}, DimensionType(ctx.dimension()));

    // the trip
    bool there = false;
    int  ticks = 0;
    for (; ticks < 400 && !there; ++ticks) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        ctx.require(pl != nullptr, "the player is still there");
        if (ticks == 30) ctx.check(portalTimer(*pl) > 20, "the portal timer runs while inside (" + std::to_string(portalTimer(*pl)) + ")");
        // in the Gloaming Forest and moved next to the return portal
        there = static_cast<int>(pl->getDimensionId()) == gfId && game::regionOf(gfId)
             && tp::portalBlocksNear(*game::regionOf(gfId), game::blockPosOf(pl->getFeetPos()), 3).size() >= 4;
    }
    ctx.require(there, "in the Gloaming Forest next to a portal after " + std::to_string(ticks) + " ticks");
    ctx.check(ticks >= kDefaultDelay, "not before the 60 tick delay");
    auto* pl       = playerById(id);
    auto* gloamingRegion = game::regionOf(gfId);
    ctx.require(pl && gloamingRegion, "player and dimension");
    // PortalProcessor: the process stays after the trip (portalTime 61) and decays from here on
    auto arrived = portalProcessOf(*pl);
    ctx.check(arrived.hasProcess && arrived.portalTime > 0 && !arrived.travelling,
              "the portal process is kept after the trip (portalTime " + std::to_string(arrived.portalTime) + ")");
    auto here    = game::blockPosOf(pl->getFeetPos());
    auto portals = tp::portalBlocksNear(*gloamingRegion, here, 4);
    ctx.check(portals.size() == 4, "a 2x2 return portal (" + std::to_string(portals.size()) + " blocks)");
    BlockPos corner = portals.front();
    for (auto const& b : portals) corner = BlockPos{std::min(corner.x, b.x), b.y, std::min(corner.z, b.z)};
    ctx.check(terrain.sites() == 1, "the site of the return portal was saved before it was built");
    ctx.check(!game::intState(gloamingRegion->getBlock(corner), kOneWayState).value_or(1), "the return portal is usable (shouldReturnPortalBeUsable)");
    ctx.info("arrived at " + str(here) + ", return portal at " + str(corner));
    // the way back is cached for the return portal's columns
    auto back = tp::cacheGet("minecraft:overworld", corner.x, corner.z);
    ctx.check(back.has_value(), "the way back is cached");
    ctx.check(!ctx.blockAt(core::GlobalPos{gfId, corner.x, corner.y - 1, corner.z}).ends_with(":air"), "the return portal stands on the ground (y " + std::to_string(corner.y) + ", " + ctx.blockAt(core::GlobalPos{gfId, corner.x, corner.y - 1, corner.z}) + " below)");

    // back home as soon as the 10 tick cooldown is over (standing in a portal while it runs
    // restarts it): the kept process goes on from where it decayed to, so the trip comes sooner
    // than 60 ticks (upstream: transition + 1 - what is left)
    PortalProcessView left;
    for (int i = 0; i < 40; ++i) {
        pl = playerById(id);
        ctx.require(pl != nullptr, "the player is still there");
        left = portalProcessOf(*pl);
        if (left.cooldown <= 0) break;
        co_await ctx.ticks(1);
    }
    pl = playerById(id);
    ctx.require(pl != nullptr, "the player is still there");
    ctx.check(left.cooldown <= 0, "the portal cooldown ran out");
    ctx.info("stepping into the return portal with portalTime " + std::to_string(left.hasProcess ? left.portalTime : 0));
    pl->teleport(Vec3{corner.x + 0.5f, static_cast<float>(corner.y), corner.z + 0.5f}, DimensionType(gfId));
    int  leaveTick = -1;
    bool home      = false;
    for (ticks = 0; ticks < 400 && !home; ++ticks) {
        co_await ctx.ticks(1);
        pl = playerById(id);
        ctx.require(pl != nullptr, "the player is still there");
        if (leaveTick < 0 && portalProcessOf(*pl).travelling) leaveTick = ticks;
        home = static_cast<int>(pl->getDimensionId()) == ctx.dimension();
        if (home) {
            co_await ctx.ticks(5);
            pl   = playerById(id);
            home = pl && std::abs(pl->getFeetPos().x - (pool.x + 1)) < 4 && std::abs(pl->getFeetPos().z - (pool.z + 1)) < 4;
        }
    }
    ctx.check(home, "back next to the first portal");
    if (left.hasProcess && left.portalTime > 4) {
        // the first tick in decays it once more (4), then portalTime counts up from there until
        // it reaches 60: the trip leaves 65 - left ticks later (a fresh process: 61)
        int want = kDefaultDelay + 5 - left.portalTime;
        ctx.check(leaveTick >= 0 && std::abs(leaveTick - want) <= 3 && leaveTick < kDefaultDelay,
                  "the way back left after " + std::to_string(leaveTick) + " ticks (upstream: " + std::to_string(want) + ", the process went on)");
    } else {
        ctx.info("the process had run out before the player stepped in: the way back took the full delay");
        ctx.check(leaveTick >= kDefaultDelay, "the way back left after " + std::to_string(leaveTick) + " ticks");
    }
    for (auto const& b : portals) tp::cacheRemove("minecraft:overworld", b.x, b.z);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) tp::cacheRemove(world::kDimensionId, pool.x + dx, pool.z + dz);
    int put = co_await terrain.restore();
    ctx.info("put back " + std::to_string(put) + " blocks of the generated terrain at the return portal");
    gloamingRegion = game::regionOf(gfId);
    ctx.check(gloamingRegion && tp::portalBlocksNear(*gloamingRegion, corner, 2).empty(), "the return portal is gone, the terrain is back");
});

// newPlayersSpawnInGloaming (CapabilityEvents#newSpawnInGloamingForest): the player lands in the Gloaming
// Forest, spawns there and counts as banished.
GF_REGISTER_SELFTEST("dimension.new_player_spawn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    // the settings go back however the test ends (a later joining player must not be banished)
    ConfigGuard config;
    gf::config::mut<DimensionConfig>().newPlayersSpawnInGloaming = true;
    gf::config::mut<DimensionConfig>().portalForNewPlayer  = true;
    // the portal is built into the generated Gloaming Forest: put back afterwards
    TerrainRestore terrain;
    terrain.watchSites();
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
        newSpawnInGloamingForest(*p);
    }
    bool there = false;
    for (int i = 0; i < 300 && !there; ++i) {
        co_await ctx.ticks(1);
        auto* p = playerById(id);
        there   = p && world::isGloaming(p->getDimensionId()) && p->getFeetPos().y < 250;
    }
    ctx.require(there, "the new player is in the Gloaming Forest");
    co_await ctx.ticks(5);
    auto* p = playerById(id);
    ctx.require(p != nullptr, "player");
    ctx.info("after the new spawn: " + respawnInfo(*p));
    ctx.check(hasRespawnPosition(*p), "the new spawn is a respawn point");
    ctx.check(p->mPlayerRespawnPoint->mDimension->mValue == static_cast<int>(*upsrc), "respawns in the Gloaming Forest");
    auto portals = tp::portalBlocksNear(*game::regionOf(static_cast<int>(*upsrc)), game::blockPosOf(p->getFeetPos()), 4);
    ctx.check(portals.size() == 4, "next to a fresh portal (portalForNewPlayer)");
    if (!portals.empty()) {
        ctx.check(game::intState(game::regionOf(static_cast<int>(*upsrc))->getBlock(portals.front()), kOneWayState).value_or(0) == 1, "a one-way portal (forced entry)");
    }
    ctx.check(terrain.sites() == 1, "the portal's site was saved before it was built");
    int put = co_await terrain.restore();
    ctx.info("put back " + std::to_string(put) + " blocks of the generated terrain at the new player's portal");
});

// ---- non-player entities -----------------------------------------------------------------------------------------

// An item dropped into a lit portal goes to the Gloaming Forest at once (transition time 0), where
// the teleporter builds a return portal for it; nobody has to be there.
GF_REGISTER_SELFTEST("dimension.item_travel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int   gfId   = static_cast<int>(*upsrc);
    auto  box    = co_await ctx.flatArea(5490, 8, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY - 1, box.minZ + 3};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    // the return portal is built into the generated Gloaming Forest: put back afterwards
    TerrainRestore terrain;
    terrain.watchSites();
    // the destination (x and z times 8, generated by the Gloaming Forest's generator), with nobody there
    int const destX = pool.x * 8, destZ = pool.z * 8;
    // where to look for the sticks: around the scaled destination, and around the portal the
    // teleporter chose for the pool (its cache) or built (the watched sites). GloamingTeleporter#
    // moveToSafeCoords moves an arrival out of landmark chunks (scanIntoSafeBiomes), which can be
    // hundreds of blocks from the scaled destination.
    auto searchAreas = [&]() {
        std::vector<AABB> out{AABB{Vec3(destX - 24, world::kMinY, destZ - 24), Vec3(destX + 24, world::kMinY + world::kHeight, destZ + 24)}};
        auto around = [&](BlockPos const& c) {
            out.emplace_back(Vec3(c.x - 8, world::kMinY, c.z - 8), Vec3(c.x + 10, world::kMinY + world::kHeight, c.z + 10));
        };
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz)
                if (auto cached = tp::cacheGet(world::kDimensionId, pool.x + dx, pool.z + dz)) around(cached->pos);
        for (auto const& corner : terrain.sitesIn(gfId)) around(corner);
        return out;
    };
    // the test player waits next to the pool, in the overworld
    ctx.player().teleport(Vec3{pool.x - 1.5f, static_cast<float>(pool.y + 1), pool.z + 0.5f}, DimensionType(ctx.dimension()));
    co_await ctx.ticks(5);
    auto stack = game::items::make("minecraft:stick", 3);
    ctx.require(stack.has_value(), "sticks");
    auto  level = ll::service::getLevel();
    auto* item  = level->getSpawner().spawnItem(*region, *stack, nullptr, Vec3{pool.x + 1.0f, pool.y + 0.3f, pool.z + 1.0f}, 0);
    ctx.require(item != nullptr, "item dropped into the portal");
    auto   itemId = idOf(*item);
    Actor* there   = nullptr;
    int    ticks   = 0;
    int    started = -1; // the tick its trip started (the destination still has to load then)
    for (; ticks < 300 && !there; ++ticks) {
        co_await ctx.ticks(1);
        auto* a = level->fetchEntity(ActorUniqueID{itemId}, false);
        if (started < 0 && a && portalProcessOf(*a).travelling) started = ticks;
        if (a && static_cast<int>(a->getDimensionId()) == gfId) there = a;
        // (or the sticks as another item entity, when the engine gave the moved item a new id)
        if (auto* r = game::regionOf(gfId); !there && r) {
            for (auto const& near : searchAreas()) {
                for (auto* e : r->getEntities(near, 0.0f, ActorType::ItemEntity)) {
                    if (e && e->getEntityTypeId() == ActorType::ItemEntity
                        && game::items::idOf(static_cast<ItemActor*>(e)->item()) == "minecraft:stick") {
                        there = e;
                        ctx.info("the sticks arrived as entity " + std::to_string(idOf(*e)) + " (dropped as " + std::to_string(itemId) + ")");
                        break;
                    }
                }
                if (there) break;
            }
        }
    }
    ctx.require(there != nullptr, "the item is in the Gloaming Forest (after " + std::to_string(ticks) + " ticks)");
    ctx.info("the item arrived after " + std::to_string(ticks) + " ticks at " + str(game::blockPosOf(there->getFeetPos())));
    // the trip starts at once; loading the destination (twice when GloamingTeleporter#moveToSafeCoords
    // moves it out of landmark chunks) takes the engine's time, which is no portal delay
    ctx.info("its trip started after " + std::to_string(started) + " ticks");
    ctx.check(started >= 0 && started < 60, "it went through without the players' 60 tick delay");
    auto* gloamingRegion = game::regionOf(gfId);
    ctx.require(gloamingRegion != nullptr, "the Gloaming Forest region");
    auto portals = tp::portalBlocksNear(*gloamingRegion, game::blockPosOf(there->getFeetPos()), 4);
    ctx.check(portals.size() == 4, "next to a 2x2 return portal (" + std::to_string(portals.size()) + " blocks)");
    if (!portals.empty()) {
        BlockPos corner = portals.front();
        for (auto const& b : portals) corner = BlockPos{std::min(corner.x, b.x), b.y, std::min(corner.z, b.z)};
        ctx.check(!ctx.blockAt(core::GlobalPos{gfId, corner.x, corner.y - 1, corner.z}).ends_with(":air"), "the return portal stands on the ground (y " + std::to_string(corner.y) + ", " + ctx.blockAt(core::GlobalPos{gfId, corner.x, corner.y - 1, corner.z}) + " below)");
        for (auto const& b : portals) tp::cacheRemove("minecraft:overworld", b.x, b.z);
    }
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) tp::cacheRemove(world::kDimensionId, pool.x + dx, pool.z + dz);
    // it does not come straight back (Entity#getDimensionChangingDelay: 300 ticks for entities)
    there->remove();
    int put = co_await terrain.restore();
    ctx.info("put back " + std::to_string(put) + " blocks of the generated terrain at the item's return portal");
});

// ---- the area loader -------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.area_loader", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int      gfId = static_cast<int>(*upsrc);
    BlockPos far{-77712, 64, 54321}; // far from every test and player
    auto*    region = game::regionOf(gfId);
    ctx.info(std::string("loaded before: ") + (region && region->hasChunksAt(far, 2, false) ? "yes" : "no"));
    ctx.info("the engine's ticking areas now: " + loadReport(gfId, far, 0));
    int  held = heldAreas();
    auto hold = co_await loadArea(gfId, far, 18);
    ctx.require(hold != nullptr, "the area loads without a player");
    region = game::regionOf(gfId);
    ctx.check(region && region->hasChunksAt(far, 18, false), "every chunk within 18 blocks is loaded");
    ctx.check(heldAreas() == held + 1, "one area held");
    hold.reset();
    ctx.check(heldAreas() == held, "released with its handle");
});

// An area released halfway through its loading (teleport_riders: the ground of a column) and a
// larger area over the same chunks asked for in the same tick (the trip's destination). The race
// is forced: each fresh, far column is asked for without waiting (requestArea) and released one
// tick later, long before its loading is over. The loader keeps a released area until its own
// loading is over (removed at once, it would strand the chunks it was bringing in: W0d, docs/spikes
// S1.md), so every destination loads and no wait meets stranded chunks.
GF_REGISTER_SELFTEST("dimension.area_handover", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int gfId     = static_cast<int>(*upsrc);
    int stranded = strandedWaits();
    int raced = 0, loadedCount = 0;
    for (int i = 0; i < 3; ++i) {
        BlockPos column{-61000 + i * 512 + 7, 0, 47000 + 9};
        auto     first = requestArea(gfId, column, 0);
        ctx.require(first != nullptr, "column " + std::to_string(i) + " asked for");
        co_await ctx.ticks(1);
        int  inPipeline = 0, missing = 0;
        auto states     = chunkStates(gfId, column, 0);
        for (int s : states) {
            if (s >= 1 && s <= 10) ++inPipeline;
            if (s < 1) ++missing;
        }
        int const settlingBefore = settlingAreas();
        first.reset();
        bool const settling = settlingAreas() == settlingBefore + 1;
        if (settling) ++raced;
        ctx.info(std::format(
            "column {}: released after 1 tick with {} of its {} chunks (and ring) in the pipeline, {} not there yet: {}",
            i,
            inPipeline,
            states.size(),
            missing,
            settling ? "it finishes its loading before it goes" : "its loading was over"
        ));
        auto second = co_await loadArea(gfId, column, 18);
        if (second) ++loadedCount;
        ctx.check(second != nullptr, std::format("column {}: its destination loaded", i));
    }
    ctx.info(std::format("{} of 3 hand-overs released an area that was still loading; {} destinations loaded", raced, loadedCount));
    ctx.check(raced >= 1, "the race was exercised (an area released while it was still loading)");
    ctx.check(strandedWaits() == stranded, "no wait met stranded chunks");
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
    // (the dirt floor reaches below the flat area's ground layer: put back after the test)
    TerrainRestore terrain;
    terrain.saveSite(*region, game::blockPos(site));
    tp::makePortalAt(*region, game::blockPos(site), false);
    ctx.check(countPortals(*region, game::blockPos(site)) == 4, "a 2x2 portal");
    ctx.check(ctx.blockAt(water) == "minecraft:water", "Level#removeBlock leaves the water (" + ctx.blockAt(water) + ")");
    ctx.check(ctx.blockAt(stone) == "minecraft:air", "the stone is cleared (" + ctx.blockAt(stone) + ")");
    co_await ctx.ticks(1);
    co_await terrain.restore();
});

// ---- GloamingConfig#getPortalLockingAdvancement ------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("dimension.portal_lock", [](Context& ctx) {
    ConfigGuard guard;
    guard.save = true; // the check writes the cleared value to config/dimension.json
    ctx.check(!advancementExists("gloamingforest:gf_test_no_such_advancement"), "an unknown advancement does not exist");
    ctx.check(!advancementExists("not an advancement"), "an invalid id does not exist");
    auto& cfg      = gf::config::mut<DimensionConfig>();
    bool  shipped  = advancementExists("gloamingforest:progress_lich");
    cfg.portalUnlockedByAdvancement = "gloamingforest:gf_test_no_such_advancement";
    if (shipped) {
        ctx.check(!portalLockingAdvancement(true).has_value(), "an unknown locking advancement locks nothing");
        ctx.check(cfg.portalUnlockedByAdvancement.empty(), "and is cleared from the config");
        cfg.portalUnlockedByAdvancement = "gloamingforest:progress_lich";
        ctx.check(portalLockingAdvancement(true) == std::optional<std::string>("gloamingforest:progress_lich"), "a shipped advancement locks portals");
    } else {
        // without upstream's advancement data the lock can't be checked: it is kept as configured
        ctx.info("no advancement data shipped yet (data/gloamingforest/advancement): the lock is kept unchecked");
        ctx.check(portalLockingAdvancement(true).has_value(), "the configured lock is kept");
        ctx.check(!cfg.portalUnlockedByAdvancement.empty(), "and stays in the config");
    }
    // a vanilla advancement is valid in upstream (PlayerHelper.getAdvancement): kept, never cleared
    ctx.check(isVanillaAdvancement("minecraft:story/enter_the_nether") && !isVanillaAdvancement("minecraft:Not Valid"), "vanilla ids");
    cfg.portalUnlockedByAdvancement = "minecraft:story/enter_the_nether";
    ctx.check(portalLockingAdvancement(true) == std::optional<std::string>("minecraft:story/enter_the_nether"), "a vanilla advancement locks portals");
    ctx.check(cfg.portalUnlockedByAdvancement == "minecraft:story/enter_the_nether", "and stays in the config");

    // the lock against the progression store (F10): GloamingPortalBlock#entityInside turns a survival
    // player without the advancement away ("unworthy"), lets one with it through; creative and
    // spectator players are never held back
    if (!shipped) return;
    std::string const adv = "gloamingforest:progress_lich";
    cfg.portalUnlockedByAdvancement = adv;
    auto& p   = static_cast<SimulatedPlayer&>(ctx.player());
    bool  had = progression::has(p, adv);
    struct Restore {
        SimulatedPlayer* p;
        std::string      adv;
        bool             had;
        ~Restore() {
            p->setPlayerGameType(GameType::Survival);
            if (had) progression::grant(*p, adv);
            else progression::revoke(*p, adv);
        }
    } restore{&p, adv, had};
    p.setPlayerGameType(GameType::Survival);
    progression::revoke(p, adv);
    ctx.check(!progression::has(p, adv) && unworthy(p), "without " + adv + " a survival player is unworthy");
    progression::grant(p, adv);
    ctx.check(progression::has(p, adv) && !unworthy(p), "with it the portal lets the player through");
    progression::revoke(p, adv);
    p.setPlayerGameType(GameType::Creative);
    ctx.check(!unworthy(p), "a creative player without it is never held back");
    p.setPlayerGameType(GameType::Spectator);
    ctx.check(!unworthy(p), "nor is a spectator");
});

// ---- the per-player clock of vanilla clients ----------------------------------------------------------------------------------

// The corrections of the three time packets (Clock.cpp), on packets built here.
GF_REGISTER_SELFTEST("dimension.clock", [](Context& ctx) {
    ctx.check(gloamingClock(6000) == 13000, "noon of day 0 -> 13000");
    ctx.check(gloamingClock(24000 * 5 + 100) == 24000 * 5 + 13000, "the day count is kept");
    ctx.check(timeOfDay(-1) == 23999, "negative times wrap");
    auto id = overworldClockId();
    ctx.require(id != 0, "the overworld world clock is known");
    // SetTime
    auto setTime = MinecraftPackets::createPacket(MinecraftPacketIds::SetTime);
    ctx.require(setTime != nullptr, "SetTime packet");
    static_cast<SetTimePacket&>(*setTime).mTime = 30000;
    ctx.check(timePacketKind(*setTime) == TimePacket::SetTime, "SetTime is a time packet");
    auto undo = fixForGloaming(*setTime);
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
    undo = fixForGloaming(*clocks);
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
    undo = fixForGloaming(*rulesPacket);
    auto& sent = static_cast<GameRulesChangedPacket&>(*rulesPacket).mRuleData->mRules->front();
    ctx.check(std::get_if<bool>(&sent.mValue.get()) && !*std::get_if<bool>(&sent.mValue.get()), "the clock is stopped");
    ctx.require(static_cast<bool>(undo), "with an undo");
    undo();
    ctx.check(std::get_if<bool>(&sent.mValue.get()) && *std::get_if<bool>(&sent.mValue.get()), "rule restored");
    auto other2 = MinecraftPackets::createPacket(MinecraftPacketIds::Text);
    ctx.check(other2 && timePacketKind(*other2) == TimePacket::None, "other packets are not time packets");
});

// The clock remote clients in the Gloaming Forest are sent (what leaves for the network), watched
// for 20 s: every tick ends with the time of day 13000. Run on a server with real players there
// (the client e2e runs it on its host); without one it has nothing to watch.
GF_REGISTER_SELFTEST("dimension.clock_watch", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    std::vector<std::int64_t> ids;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulated() && world::isGloaming(p.getDimensionId()) && clientClock(p)) ids.push_back(idOf(p));
        return true;
    });
    if (ids.empty()) {
        ctx.info("no remote player in the Gloaming Forest: nothing to watch");
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

// ---- functions: /execute in the Gloaming Forest --------------------------------------------------------------------------------

// Bedrock's /execute in accepts the custom dimension's identifier.
GF_REGISTER_SELFTEST("dimension.execute_in", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    auto& p    = ctx.player();
    auto  id   = idOf(p);
    auto  name = p.getRealName();
    auto toGloaming = selftest::runServerCommand("execute as \"" + name + "\" at @s in gloamingforest:gloaming_forest run tp @s ~ ~ ~");
    ctx.check(toGloaming.ok, "execute in gloamingforest:gloaming_forest runs (" + toGloaming.messages + ")");
    bool there = false;
    for (int i = 0; toGloaming.ok && i < 200 && !there; ++i) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        there    = pl && world::isGloaming(pl->getDimensionId());
    }
    ctx.check(there, "the player is in the Gloaming Forest");
    auto toOw = selftest::runServerCommand("execute as \"" + name + "\" at @s in overworld run tp @s ~ ~ ~");
    ctx.check(toOw.ok, "execute in overworld runs (" + toOw.messages + ")");
    bool back = false;
    for (int i = 0; toOw.ok && i < 200 && !back; ++i) {
        co_await ctx.ticks(1);
        auto* pl = playerById(id);
        back     = pl && static_cast<int>(pl->getDimensionId()) == 0;
    }
    ctx.check(back, "the player is back in the overworld");
});

// upstream's functions tp_gloaming / tp_overworld ("execute in <dimension> run tp @p ~ ~ ~"): Java's
// execute in scales X and Z by from.coordinateScale / to.coordinateScale (x8 into the Gloaming
// Forest, /8 out of it).
GF_REGISTER_SELFTEST("dimension.functions", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int gfId = static_cast<int>(*upsrc);
    // the semantics (CommandSourceStack#withLevel)
    auto in = runTpFunction(0, Vec3{100.5f, 70.0f, -20.25f}, true);
    ctx.require(in.has_value(), "tp_gloaming has a destination");
    ctx.check(in->dim == gfId, "tp_gloaming goes to the Gloaming Forest");
    ctx.check(in->pos.x == 804.0f && in->pos.y == 70.0f && in->pos.z == -162.0f, "overworld 100.5 70 -20.25 is 804 70 -162 there");
    auto out = runTpFunction(gfId, Vec3{804.0f, 70.0f, -162.0f}, false);
    ctx.require(out.has_value(), "tp_overworld has a destination");
    ctx.check(out->dim == 0 && out->pos.x == 100.5f && out->pos.z == -20.25f, "and back: 100.5 70 -20.25 in the overworld");
    auto same = runTpFunction(gfId, Vec3{5.0f, 6.0f, 7.0f}, true);
    ctx.check(same && same->pos.x == 5.0f && same->pos.z == 7.0f, "within the same dimension the position stays");
    auto nether = runTpFunction(1, Vec3{10.0f, 64.0f, 10.0f}, true);
    ctx.check(nether && nether->pos.x == 640.0f && nether->pos.z == 640.0f, "the nether (scale 8) is 64 times farther in the Gloaming Forest");

    // the behavior pack function, run by the test player at its feet, with Y moved by `positioned`
    // to the generated ground of the arrival column in the Gloaming Forest (loaded without a
    // player), so the player lands on the terrain
    auto& p    = ctx.player();
    auto  id   = idOf(p);
    auto  name = p.getRealName();
    auto  at   = ctx.testPos(5498);
    BlockPos column{static_cast<int>(std::floor((at.x + 0.5f) * 8.0f)), 0, static_cast<int>(std::floor((at.z + 0.5f) * 8.0f))};
    auto     columnHold = co_await loadArea(gfId, column, 2);
    ctx.require(columnHold != nullptr && game::regionOf(gfId) != nullptr, "the arrival column loaded");
    int ground = tp::heightmap(*game::regionOf(gfId), column.x, column.z);
    ctx.info("the arrival column's ground is at y " + std::to_string(ground));
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
    source.y    = static_cast<float>(ground);
    auto want   = runTpFunction(ctx.dimension(), source, true);
    ctx.require(want.has_value(), "the expected destination");
    if (want->player != id) {
        ctx.info("another player is nearer to the scaled position: the in-game part is skipped");
        co_return;
    }
    auto fnGloaming = selftest::runServerCommand(std::format("execute as \"{}\" at @s positioned ~ {} ~ run function gloamingforest/dimension/tp_gloaming", name, ground));
    ctx.check(fnGloaming.ok, "function tp_gloaming runs (" + fnGloaming.messages + ")");
    bool there = false;
    for (int i = 0; fnGloaming.ok && i < 200 && !there; ++i) {
        co_await ctx.ticks(1);
        pl    = playerById(id);
        there = pl && world::isGloaming(pl->getDimensionId());
    }
    ctx.require(there, "tp_gloaming moved the player into the Gloaming Forest");
    // the function places the player exactly once its destination is loaded around it
    Vec3 got;
    for (int i = 0; i < 100; ++i) {
        co_await ctx.ticks(1);
        pl = playerById(id);
        ctx.require(pl != nullptr, "the test player");
        got = pl->getFeetPos();
        if (i >= 5 && std::abs(got.y - static_cast<float>(ground)) < 0.01f) break;
    }
    ctx.info(std::format("from {:.2f} {:.2f} to {:.2f} {:.2f} {:.2f} in the Gloaming Forest", source.x, source.z, got.x, got.y, got.z));
    ctx.check(std::abs(got.x - source.x * 8.0f) < 0.01f && std::abs(got.z - source.z * 8.0f) < 0.01f, "X and Z times 8");
    ctx.check(std::abs(got.y - static_cast<float>(ground)) < 0.5f, std::format("Y as the execution position had it ({}, on the ground)", ground));
    // and back from where it stands, Y back on the overworld's ground
    Vec3 back = got;
    back.y    = static_cast<float>(at.y);
    auto home = runTpFunction(gfId, back, false);
    ctx.require(home.has_value(), "the expected way back");
    if (home->player != id) {
        ctx.info("another player is nearer to the scaled way back: it is not taken");
        pl->teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, DimensionType(ctx.dimension()));
        co_return;
    }
    auto fnOw = selftest::runServerCommand(std::format("execute as \"{}\" at @s positioned ~ {} ~ run function gloamingforest/dimension/tp_overworld", name, at.y));
    ctx.check(fnOw.ok, "function tp_overworld runs (" + fnOw.messages + ")");
    bool home2 = false;
    for (int i = 0; fnOw.ok && i < 200 && !home2; ++i) {
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

// GloamingPortalBlock#getFluidState (sugar cane keeps its water), pushReaction BLOCK (pistons can't move
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
        auto cmd = selftest::runServerCommand(std::format("setblock {} {} {} redstone_block", base.x + 1, base.y, base.z), ctx.dimension());
        ctx.check(cmd.ok, "redstone block next to the piston (" + cmd.messages + ")");
    }
    co_await ctx.ticks(20);
    ctx.info("after the pistons fired: stone column " + ctx.blockAt(stoneBase.offset(0, 1, 0)) + " / " + ctx.blockAt(stoneBase.offset(0, 2, 0)) + ", portal column "
             + ctx.blockAt(portalBase.offset(0, 1, 0)) + " / " + ctx.blockAt(portalBase.offset(0, 2, 0)));
    ctx.check(ctx.blockAt(stoneBase.offset(0, 2, 0)) == "minecraft:stone", "the control: the piston pushed the stone up");
    ctx.check(ctx.blockAt(portalBase.offset(0, 1, 0)) == kPortalBlock, "the portal block stays where it is");
    ctx.check(ctx.blockAt(portalBase.offset(0, 2, 0)) != kPortalBlock, "and was not pushed up");
});

// Explosions (GloamingPortalBlock#getFluidState): Java's ExplosionDamageCalculator takes the larger of the
// block's resistance (upstream's 0) and its fluid's (water, 100), so an explosion never destroys a portal
// block, though the block itself keeps 0. Two deterministic cases (the explosion's rays: each starts
// with radius x 0.7..1.3, loses 0.225 per 0.3-block step and (resistance + 0.3) x 0.3 per block):
//  - the rim holds: a power-0.75 explosion 0.25 above the middle of the pool breaks the glass on
//    the pool (its first step) and reaches every portal block (one step down: any ray keeps at
//    least 0.525 - 0.405 there, which breaks a block of resistance 0), but never the grass rim
//    (four steps away: at most 0.975 - 1.17 left), so the portal stays a portal;
//  - the rim breaks: a power-1.5 explosion 0.25 above a rim block breaks it (one step down, at
//    least 1.05 - 0.585 left) and reaches the portal block beside it; the portal blocks resist, and
//    upstream's neighbour rule then turns the whole portal back into water.
GF_REGISTER_SELFTEST("dimension.portal_explosion", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5430, 10, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto const* open   = portalBlock(false);
    auto const* oneWay = portalBlock(true);
    ctx.require(open && oneWay, "both portal states");
    auto gloamingZero = [&] { return open->mDirectData->mExplosionResistance == 0.0f && oneWay->mDirectData->mExplosionResistance == 0.0f; };
    ctx.check(gloamingZero(), "the block keeps upstream's explosion resistance 0 (strength(-1.0F))");

    // a lit 2x2 portal in the middle of the area (the craters stay inside it)
    int  y    = box.minY - 1; // the grass layer
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 4, y, box.minZ + 4};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*ctx.region(), game::blockPos(pool)), "lit");
    co_await ctx.ticks(4);
    auto count = [&](std::string const& id) {
        int n = 0;
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz) n += ctx.blockAt(pool.offset(dx, 0, dz)) == id ? 1 : 0;
        return n;
    };
    ctx.require(count(kPortalBlock) == 4, "the pool is a portal");
    // the rim: the eight edge blocks beside the pool
    std::vector<core::GlobalPos> rim;
    for (int i = 0; i < 2; ++i) {
        rim.push_back(pool.offset(-1, 0, i));
        rim.push_back(pool.offset(2, 0, i));
        rim.push_back(pool.offset(i, 0, -1));
        rim.push_back(pool.offset(i, 0, 2));
    }
    auto rimBroken = [&] {
        std::string broken;
        for (auto const& p : rim)
            if (!blockTagged("gloamingforest:portal/edge", ctx.blockAt(p))) broken += (broken.empty() ? "" : ", ") + ctx.blockAt(p) + " at " + str(game::blockPos(p));
        return broken;
    };
    auto explode = [&](Vec3 const& center, float power) {
        auto* r = ctx.region();
        return r && level->explode(*r, nullptr, center, power, false, true, std::numeric_limits<float>::max(), false);
    };

    // the rim holds: two explosions in the same tick (a TNT chain), each opens and closes the window
    auto glass = pool.offset(0, 1, 0);
    ctx.require(ctx.setBlock(glass, "minecraft:glass"), "glass on the pool");
    Vec3 middle{pool.x + 1.0f, pool.y + 1.25f, pool.z + 1.0f};
    for (int i = 0; i < 2; ++i) {
        explode(middle, 0.75f);
        ctx.check(gloamingZero() && portalExplosionDepth() == 0, "upstream's 0 again after explosion " + std::to_string(i + 1));
    }
    co_await ctx.ticks(4); // block changes reach the portal at the end of the tick
    auto broken = rimBroken();
    ctx.info("after the explosions over the pool: " + std::to_string(count(kPortalBlock)) + " portal blocks, glass " + ctx.blockAt(glass) + ", rim "
             + (broken.empty() ? "intact" : "broken (" + broken + ")"));
    ctx.check(ctx.blockAt(glass) != "minecraft:glass", "the explosions broke the glass on the pool (" + ctx.blockAt(glass) + ")");
    ctx.check(broken.empty(), "the explosions did not reach the rim (" + broken + ")");
    ctx.check(count(kPortalBlock) == 4, "the portal blocks resisted the explosions: the portal stays (" + std::to_string(count(kPortalBlock)) + " of 4)");

    // the rim breaks: an explosion over the west rim block (its decoration taken away first)
    auto west = pool.offset(-1, 0, 0);
    ctx.setBlock(west.offset(0, 1, 0), "minecraft:air");
    explode(Vec3{west.x + 0.5f, west.y + 1.25f, west.z + 0.5f}, 1.5f);
    ctx.check(gloamingZero() && portalExplosionDepth() == 0, "upstream's 0 again after the explosion over the rim");
    co_await ctx.ticks(4); // (the water flows into the crater only after 5 ticks)
    int portals = count(kPortalBlock), water = count("minecraft:water");
    ctx.info("after the explosion over the rim: " + std::to_string(portals) + " portal blocks, " + std::to_string(water) + " water, rim " + rimBroken());
    ctx.check(!blockTagged("gloamingforest:portal/edge", ctx.blockAt(west)), "the explosion broke the rim block under it (" + ctx.blockAt(west) + ")");
    ctx.check(portals + water == 4, "the explosion destroyed none of the portal blocks (" + std::to_string(portals) + " portal, " + std::to_string(water) + " water)");
    ctx.check(water == 4, "the rim broke: upstream's neighbour rule turned the whole portal back into water (" + std::to_string(water) + " of 4)");
    ctx.check(gloamingZero(), "and the portal holds upstream's 0 again after the explosions");
    if (auto* r = ctx.region()) {
        // the drops of the broken blocks
        AABB area{Vec3(static_cast<float>(box.minX), y - 1.0f, static_cast<float>(box.minZ)), Vec3(box.maxX + 1.0f, y + 6.0f, box.maxZ + 1.0f)};
        for (auto* e : r->getEntities(area, 0.0f, ActorType::ItemEntity))
            if (e && e->getEntityTypeId() == ActorType::ItemEntity) e->remove();
    }
});

// The portal's explosion window (Explosion::explode): the water's resistance from the start of the
// first explosion in flight to the end of the last one, whether they nest (an explosion set off by
// another one) or overlap on two threads, and upstream's 0 again afterwards, also after an explosion that
// throws. (Driven by hand: the game's explosions open it the same way, see dimension.portal_block.)
GF_REGISTER_SELFTEST("dimension.explosion_window", [](Context& ctx) {
    auto const* open   = portalBlock(false);
    auto const* oneWay = portalBlock(true);
    ctx.require(open && oneWay, "both portal states");
    float water = portalWaterResistance();
    ctx.require(water > 0.0f, "the water's explosion resistance is known (" + std::to_string(water) + ")");
    ctx.require(portalExplosionDepth() == 0, "no explosion in flight");
    auto holds = [&](float v) { return open->mDirectData->mExplosionResistance == v && oneWay->mDirectData->mExplosionResistance == v; };
    ctx.require(holds(0.0f), "upstream's 0 outside explosions");
    // (only checks below, never a require: every window opened here is closed again)
    beginPortalExplosion();
    ctx.check(holds(water), std::format("during an explosion both states hold the water's {}", water));
    beginPortalExplosion(); // a nested explosion
    endPortalExplosion();
    ctx.check(holds(water), "a nested explosion ending first leaves the outer one the water's resistance");
    std::thread([] { beginPortalExplosion(); }).join(); // one on another thread starts
    endPortalExplosion();                                // the first one ends
    ctx.check(holds(water), "an explosion still running on another thread keeps the water's resistance");
    std::thread([] { endPortalExplosion(); }).join();
    ctx.check(holds(0.0f) && portalExplosionDepth() == 0, "upstream's 0 again once the last explosion ended");
    try {
        PortalExplosionScope scope;
        throw std::runtime_error("an explosion that throws");
    } catch (std::exception const&) {}
    ctx.check(holds(0.0f) && portalExplosionDepth() == 0, "upstream's 0 again after an explosion that threw");
    endPortalExplosion(); // an unbalanced end does not go below zero
    ctx.check(portalExplosionDepth() == 0 && holds(0.0f), "an end without a start changes nothing");
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
    auto const& t = ext::gloamingType();
    ctx.check(t.fixedTime == 13000 && t.coordinateScale == 0.125 && t.minY == -32 && t.height == 288 && t.logicalHeight == 288, "gloaming_forest_type: time, scale, height");
    ctx.check(t.ambientLight == 0.01f && t.monsterSpawnBlockLightLimit == 7 && t.monsterSpawnLightMin == 0 && t.monsterSpawnLightMax == 7, "ambient light and monster spawn light");
    ctx.check(t.bedWorks && !t.respawnAnchorWorks && !t.hasRaids && !t.piglinSafe && t.natural && t.hasSkylight && !t.hasCeiling && !t.ultrawarm, "flags");
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    ctx.check(&ext::dimensionType(static_cast<int>(*upsrc)) == &t, "the Gloaming Forest's id reads gloaming_forest_type");
    ctx.check(ext::dimensionType(0).id == "minecraft:overworld" && ext::dimensionType(1).id == "minecraft:the_nether", "vanilla types");
    // DimensionType#brightness
    ctx.check(std::abs(ext::brightness(0.01f, 0) - 0.01f) < 1e-6f, "light 0: the ambient light");
    ctx.check(std::abs(ext::brightness(0.01f, 15) - 1.0f) < 1e-6f, "light 15: 1");
    ctx.check(std::abs(ext::brightness(0.0f, 7) - (7.0f / 15.0f) / (4.0f - 3.0f * 7.0f / 15.0f)) < 1e-6f, "light 7 without ambient light");
    // the Gloaming Forest's brightness ramp has it
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto d = level->getOrCreateDimension(*upsrc).lock();
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
    ctx.check(dark(t, 0, 7, 6, {0, 7}), "block light 7 spawns in the Gloaming Forest when the test rolls 7");
    ctx.check(!dark(t, 0, 7, 6, {0, 6}), "but not when it rolls 6");
    ctx.check(!dark(t, 0, 8, 6, {0, 7}), "block light 8 never spawns there");
    ctx.check(!dark(ext::dimensionType(0), 0, 1, 6, {0, 7}), "the overworld's block light limit is 0");
    ctx.check(!dark(t, 15, 0, 6, {14, 7}), "a sky light above the nextInt(32) roll refuses");
    ctx.check(dark(t, 13, 0, 6, {31, 7}), "sky light 13 darkened by 6 is 7");
    ctx.check(!dark(t, 14, 0, 6, {31, 7}), "sky light 14 darkened by 6 is 8");
    ctx.check(dark(ext::dimensionType(1), 0, 15, 0, {0, 0}) == false && dark(ext::dimensionType(1), 0, 7, 0, {0, 0}), "the nether: no block light limit, a constant 7");
});

// ---- GloamingPortalBlock#getFluidState: what the portal's water does ----------------------------------------------

// The portal is level-1 water: an entity in it lands without fall damage, its fire goes out,
// water-sensitive mobs take drowning damage there, and farmland within 4 blocks stays moist.
GF_REGISTER_SELFTEST("dimension.portal_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(5420, 16, 24);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int  dim  = ctx.dimension();
    auto pool = core::GlobalPos{dim, box.minX + 3, box.minY - 1, box.minZ + 3};
    ctx.require(buildPool(ctx, pool), "pool built");
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    co_await ctx.ticks(2); // the portal index reads the pool (the water rules run on it)

    Vec3 inPool{pool.x + 1.0f, static_cast<float>(pool.y), pool.z + 1.0f};
    Vec3 onGrass{box.minX + 12.5f, static_cast<float>(box.minY), box.minZ + 12.5f};
    std::vector<std::int64_t> spawned;
    struct Despawn {
        std::vector<std::int64_t>& ids;
        ~Despawn() {
            if (auto level = ll::service::getLevel())
                for (auto id : ids)
                    if (auto* a = level->fetchEntity(ActorUniqueID{id}, false)) a->remove();
        }
    } despawn{spawned};
    auto spawn = [&](char const* type, Vec3 const& at) -> std::int64_t {
        auto* r = game::regionOf(dim);
        if (!r) return 0;
        ActorUniqueID uid{};
        auto*         a = CommandUtils::spawnEntityAt(*r, at, ActorDefinitionIdentifier(type), uid, nullptr);
        if (!a) return 0;
        // on portal cooldown: in the pool it stays (its cooldown starts again each tick inside)
        setPortalCooldown(*a, 1000);
        spawned.push_back(idOf(*a));
        return idOf(*a);
    };
    auto actor = [&](std::int64_t id) -> Actor* { return ll::service::getLevel()->fetchEntity(ActorUniqueID{id}, false); };

    // where the water is: on the pool's floor in it, on the rim not
    auto probe = spawn("minecraft:pig", inPool);
    ctx.require(probe != 0, "pig spawned");
    ctx.check(actor(probe) && inPortalWater(*actor(probe), *region), "a pig on the pool's floor is in the portal's water");
    if (auto* a = actor(probe)) {
        a->teleport(Vec3{pool.x - 0.5f, static_cast<float>(pool.y + 1), pool.z + 0.5f}, DimensionType(dim));
        ctx.check(!inPortalWater(*a, *region), "a pig on the rim is not");
        a->remove();
    }

    // fall damage: a pig dropped 8 blocks onto grass is hurt, one dropped into the pool is not
    auto grassPig = spawn("minecraft:pig", Vec3{onGrass.x, onGrass.y + 8.0f, onGrass.z});
    auto poolPig  = spawn("minecraft:pig", Vec3{inPool.x, inPool.y + 8.0f, inPool.z});
    ctx.require(grassPig != 0 && poolPig != 0, "pigs spawned");
    int grassFull = actor(grassPig)->getHealth(), poolFull = actor(poolPig)->getHealth();
    int grassMin = grassFull, poolMin = poolFull;
    for (int i = 0; i < 50; ++i) {
        co_await ctx.ticks(1);
        auto* g = actor(grassPig);
        auto* q = actor(poolPig);
        grassMin = std::min(grassMin, g ? g->getHealth() : 0);
        poolMin  = std::min(poolMin, q ? q->getHealth() : 0);
    }
    ctx.info(std::format("pigs after an 8 block fall: on grass {} of {}, into the portal {} of {}", grassMin, grassFull, poolMin, poolFull));
    ctx.check(grassMin < grassFull, "the control: a pig falling 8 blocks onto grass is hurt");
    ctx.check(poolMin == poolFull, "a pig falling 8 blocks into the portal's water lands unhurt");

    // the same for a survival player
    auto& faller   = ctx.spawnPlayer("gf_test_faller");
    auto  fallerId = idOf(faller);
    faller.setPlayerGameType(GameType::Survival);
    setPortalCooldown(faller, 1000);
    auto fall = [&](Vec3 const& onto) {
        if (auto* f = playerById(fallerId)) f->teleport(Vec3{onto.x, onto.y + 8.0f, onto.z}, DimensionType(dim));
    };
    fall(inPool);
    int full = 20, intoPool = 20, ontoGrass = 20;
    for (int i = 0; i < 50; ++i) {
        co_await ctx.ticks(1);
        if (auto* f = playerById(fallerId)) intoPool = std::min(intoPool, f->getHealth());
    }
    fall(onGrass);
    for (int i = 0; i < 50; ++i) {
        co_await ctx.ticks(1);
        if (auto* f = playerById(fallerId)) ontoGrass = std::min(ontoGrass, f->getHealth());
    }
    ctx.info(std::format("a survival player after an 8 block fall: into the portal {} of {}, onto grass {}", intoPool, full, ontoGrass));
    ctx.check(intoPool == full, "a survival player jumping 8 blocks into the portal's water lands unhurt");
    if (ontoGrass < full) ctx.info("(the control: the same fall onto grass hurts)");
    else ctx.info("the simulated player took no fall damage on grass either: the player part only shows no damage");

    // fire: a burning pig in the pool is put out, one on the grass keeps burning
    auto wetPig = spawn("minecraft:pig", inPool);
    auto dryPig = spawn("minecraft:pig", onGrass);
    ctx.require(wetPig != 0 && dryPig != 0, "pigs spawned");
    actor(wetPig)->setOnFire(8, true);
    actor(dryPig)->setOnFire(8, true);
    co_await ctx.ticks(3);
    ctx.check(actor(wetPig) && !actor(wetPig)->isOnFire(), "a burning pig in the portal's water is put out");
    ctx.check(actor(dryPig) && actor(dryPig)->isOnFire(), "the control: a burning pig on grass keeps burning");

    // water-sensitive mobs (Entity#isSensitiveToWater): a snow golem in the pool takes drowning damage
    auto golem = spawn("minecraft:snow_golem", inPool);
    ctx.require(golem != 0, "snow golem spawned");
    int golemFull = actor(golem)->getHealth(), golemMin = golemFull;
    for (int i = 0; i < 30; ++i) {
        co_await ctx.ticks(1);
        auto* g  = actor(golem);
        golemMin = std::min(golemMin, g ? g->getHealth() : 0);
    }
    ctx.check(golemMin < golemFull, std::format("a snow golem in the portal's water is hurt ({} of {})", golemMin, golemFull));

    // farmland 3 blocks from the pool is made fully moist by a random tick; far from it the game's
    // rule dries it
    region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto nearFarm = pool.offset(-3, 0, 1);
    auto farFarm  = core::GlobalPos{dim, box.minX + 13, pool.y, box.minZ + 13};
    ctx.require(ctx.setBlock(nearFarm, "minecraft:farmland", {{"moisturized_amount", 2}}), "farmland near the pool");
    ctx.require(ctx.setBlock(farFarm, "minecraft:farmland", {{"moisturized_amount", 5}}), "farmland far from it");
    ctx.check(portalWatersFarmland(*region, game::blockPos(nearFarm)), "the portal waters the farmland 3 blocks away");
    ctx.check(!portalWatersFarmland(*region, game::blockPos(farFarm)), "but not the far one");
    auto& random = level->getRandom();
    for (auto const& f : {nearFarm, farFarm}) region->getBlock(game::blockPos(f)).randomTick(*region, game::blockPos(f), random);
    auto moisture = [&](core::GlobalPos const& f) { return game::intState(region->getBlock(game::blockPos(f)), "moisturized_amount").value_or(-1); };
    ctx.check(moisture(nearFarm) == 7, "the farmland near the portal is fully moist after a random tick (" + std::to_string(moisture(nearFarm)) + ")");
    auto* weather = region->getDimension().mWeather.get();
    bool  raining = weather && weather->isRainingAt(*region, game::blockPos(farFarm.offset(0, 1, 0)));
    if (raining) ctx.info("it rains on the far farmland: the control is skipped");
    else ctx.check(moisture(farFarm) == 4, "the control: the far farmland dries by one (" + std::to_string(moisture(farFarm)) + ")");
});

// ---- gf::world::teleport ------------------------------------------------------------------------------------------

std::vector<std::tuple<std::int64_t, int, int>>& heardTeleports() {
    static std::vector<std::tuple<std::int64_t, int, int>> v;
    return v;
}
void listenToTeleports() {
    static bool const once = [] {
        portal::onTeleported([](Player& p, DimensionType from, DimensionType to) {
            auto& v = heardTeleports();
            if (v.size() > 64) v.erase(v.begin());
            v.emplace_back(idOf(p), static_cast<int>(from), static_cast<int>(to));
        });
        return true;
    }();
    (void)once;
}
bool heard(std::int64_t id, int from, int to) {
    auto const& v = heardTeleports();
    return std::find(v.begin(), v.end(), std::make_tuple(id, from, to)) != v.end();
}

// gf::world::teleport moves a player with its riders (its shoulder parrots on Bedrock): within a
// dimension at once, into another one once the destination has loaded (the player waits where it
// is meanwhile), and tells portal::onTeleported.
GF_REGISTER_SELFTEST("dimension.teleport_riders", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    listenToTeleports();
    heardTeleports().clear();
    int  gfId = static_cast<int>(*upsrc);
    int  home = ctx.dimension();
    auto at   = ctx.testPos(5470, 2);
    auto id   = idOf(ctx.player());
    Vec3 start{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f};
    ctx.player().teleport(start, DimensionType(home));
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    auto* pl     = playerById(id);
    ctx.require(region && pl, "region and player");
    std::vector<std::int64_t> parrots;
    struct Despawn {
        std::vector<std::int64_t>& ids;
        ~Despawn() {
            if (auto level = ll::service::getLevel())
                for (auto i : ids)
                    if (auto* a = level->fetchEntity(ActorUniqueID{i}, false)) a->remove();
        }
    } despawn{parrots};
    ActorUniqueID uid{};
    auto*         parrot = CommandUtils::spawnEntityAt(*region, start, ActorDefinitionIdentifier("minecraft:parrot"), uid, nullptr);
    ctx.require(parrot != nullptr, "parrot spawned");
    parrots.push_back(idOf(*parrot));
    parrot->startRiding(*pl, true);
    co_await ctx.ticks(2);
    auto riderOf = [&](Player& player) -> Actor* {
        auto* r = player.getFirstPassenger();
        if (r) parrots.push_back(idOf(*r));
        return r && r->getTypeName() == "minecraft:parrot" ? r : nullptr;
    };
    pl             = playerById(id);
    bool withRider = pl && riderOf(*pl);
    if (!withRider) ctx.info("the player could not carry the parrot: only the player's moves are checked");

    // within the dimension: at once
    Vec3 near{start.x + 3.0f, start.y, start.z};
    ctx.require(world::teleport(*pl, DimensionType(home), near, 90.0f), "teleport within the dimension");
    pl = playerById(id);
    ctx.require(pl != nullptr, "player");
    ctx.check(std::abs(pl->getFeetPos().x - near.x) < 0.01f && std::abs(pl->getFeetPos().z - near.z) < 0.01f, "the player moved at once");
    ctx.check(heard(id, home, home), "portal::onTeleported heard it");
    co_await ctx.ticks(2);
    pl = playerById(id);
    if (withRider) ctx.check(pl && riderOf(*pl), "the parrot is still on its shoulder");

    // into the Gloaming Forest, onto the generated ground of a fresh column
    BlockPos column{(at.x + 3) * 8 + 4, 0, at.z * 8 + 4};
    int      ground = 0;
    {
        auto hold = co_await loadArea(gfId, column, 0);
        ctx.require(hold != nullptr && game::regionOf(gfId), "the column loaded");
        ground = tp::heightmap(*game::regionOf(gfId), column.x, column.z);
    }
    Vec3 dest{column.x + 0.5f, static_cast<float>(ground), column.z + 0.5f};
    pl = playerById(id);
    ctx.require(pl != nullptr, "player");
    ctx.info("before the trip: " + loadReport(gfId, game::blockPosOf(dest), 18));
    ctx.require(world::teleport(*pl, *upsrc, dest, 0.0f), "the teleport into the Gloaming Forest started");
    ctx.check(static_cast<int>(pl->getDimensionId()) == home, "the player waits at home while the destination loads");
    bool there = false;
    int  ticks = 0;
    for (; ticks < 300 && !there; ++ticks) {
        co_await ctx.ticks(1);
        pl    = playerById(id);
        there = pl && static_cast<int>(pl->getDimensionId()) == gfId && heard(id, home, gfId);
    }
    if (!there) ctx.info("not there after 300 ticks: " + loadReport(gfId, game::blockPosOf(dest), 18));
    ctx.require(there, "the player arrived in the Gloaming Forest (portal::onTeleported)");
    co_await ctx.ticks(10);
    pl = playerById(id);
    ctx.require(pl != nullptr, "player");
    ctx.info(std::format("arrived after {} ticks at {:.2f} {:.2f} {:.2f} (asked {:.2f} {} {:.2f})", ticks, pl->getFeetPos().x, pl->getFeetPos().y, pl->getFeetPos().z, dest.x, ground, dest.z));
    ctx.check(std::abs(pl->getFeetPos().x - dest.x) < 0.5f && std::abs(pl->getFeetPos().z - dest.z) < 0.5f && std::abs(pl->getFeetPos().y - dest.y) < 1.5f, "at the asked spot");
    // the parrot came along. Whether it stays on the shoulder after the dimension change depends
    // on the engine finishing it, which a simulated player (no client to acknowledge the change)
    // never does: the trip puts it back on for 3 s, and here it is put on once more
    bool withRiderThere = false;
    if (withRider) {
        Actor* came = nullptr;
        if (auto* gloamingr = game::regionOf(gfId)) {
            AABB around{pl->getFeetPos() - Vec3(4, 4, 4), pl->getFeetPos() + Vec3(4, 4, 4)};
            for (auto* e : gloamingr->getEntities(around, 0.0f)) {
                if (!e || e->getTypeName() != "minecraft:parrot") continue;
                parrots.push_back(idOf(*e));
                came = e;
            }
        }
        ctx.check(came != nullptr, "the parrot came along into the Gloaming Forest");
        if (came && came->getVehicle() != pl) {
            came->startRiding(*pl, true);
            co_await ctx.ticks(2);
            pl = playerById(id);
            ctx.require(pl != nullptr, "player");
        }
        withRiderThere = riderOf(*pl) != nullptr;
        if (withRiderThere) ctx.info("the parrot is on its shoulder in the Gloaming Forest");
        else ctx.info("the simulated player holds no rider after its dimension change (the engine never finishes it without a client)");
    }

    // and home
    ctx.require(world::teleport(*pl, DimensionType(home), start, 0.0f), "the teleport home started");
    bool back = false;
    for (int i = 0; i < 300 && !back; ++i) {
        co_await ctx.ticks(1);
        pl   = playerById(id);
        back = pl && static_cast<int>(pl->getDimensionId()) == home && heard(id, gfId, home);
    }
    ctx.check(back, "the player is home again");
    co_await ctx.ticks(10);
    pl = playerById(id);
    if (withRiderThere && pl) {
        bool home2 = riderOf(*pl) != nullptr;
        if (!home2) ctx.info("the parrot did not stay on after the dimension change home (no client to finish it)");
        if (auto* r = game::regionOf(home)) {
            bool near = false;
            AABB around{pl->getFeetPos() - Vec3(4, 4, 4), pl->getFeetPos() + Vec3(4, 4, 4)};
            for (auto* e : r->getEntities(around, 0.0f))
                if (e && e->getTypeName() == "minecraft:parrot") {
                    parrots.push_back(idOf(*e));
                    near = true;
                }
            ctx.check(near, "the parrot came home with it");
        }
    }
});

// ---- ridden actors in the portal -------------------------------------------------------------------------------------

// GloamingPortalBlock#entityInside and Entity#handlePortal ask Entity#canUsePortal(false) of the actor
// itself first: BaseTFBoss (and upstream's parts, and the types F8 marks with ext::setCanUsePortal) answer
// false whether or not something rides them, so an upstream boss carrying a rider stays out of the portal:
// the fixture boss (it has no seat, so its rider keeps sliding off and is put back on) and a boat of
// a type marked the same way, which holds its pig. Vanilla's answer has no rule against riders, nor
// has the engine's: an unmarked boat with a pig in it travels, and changeDimension takes the pig
// along.
GF_REGISTER_SELFTEST("dimension.portal_ridden", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto upsrc = world::gloaming();
    ctx.require(upsrc.has_value(), "the Gloaming Forest exists");
    int   gfId   = static_cast<int>(*upsrc);
    auto  box    = co_await ctx.flatArea(5410, 12, 8);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int  dim  = ctx.dimension();
    auto pool = core::GlobalPos{dim, box.minX + 4, box.minY - 1, box.minZ + 4};
    ctx.require(buildPool(ctx, pool), "pool built");
    TerrainRestore terrain; // the boat's return portal is built into the generated Gloaming Forest
    terrain.watchSites();
    std::vector<std::int64_t> spawned;
    struct Cleanup {
        std::vector<std::int64_t>& ids;
        ~Cleanup() {
            ge::ext::setCanUsePortal("gf_test:boss", true);
            ge::ext::setCanUsePortal("minecraft:boat", true);
            watchPortalDecisions(0);
            if (auto level = ll::service::getLevel())
                for (auto id : ids)
                    if (auto* a = level->fetchEntity(ActorUniqueID{id}, false)) a->remove();
        }
    } cleanup{spawned};
    auto actor = [](std::int64_t id) -> Actor* {
        auto level = ll::service::getLevel();
        return level ? level->fetchEntity(ActorUniqueID{id}, false) : nullptr;
    };
    auto spawn = [&](char const* type, Vec3 const& at) -> Actor* {
        auto* r = game::regionOf(dim);
        if (!r) return nullptr;
        ActorUniqueID uid{};
        auto*         a = CommandUtils::spawnEntityAt(*r, at, ActorDefinitionIdentifier(type), uid, nullptr);
        if (a) spawned.push_back(idOf(*a));
        return a;
    };
    auto inPortal = [&](Actor& a) {
        a.teleport(Vec3{pool.x + 1.0f, pool.y + 0.2f, pool.z + 1.0f}, DimensionType(dim));
    };
    // a rider that sits on `vehicle` (a forced ride: the fixture boss has no seats of its own)
    auto mount = [&](Actor& vehicle, Vec3 const& at) -> std::int64_t {
        for (auto const* type : {"minecraft:pig", "minecraft:chicken", "minecraft:parrot"}) {
            auto* rider = spawn(type, at);
            if (!rider) continue;
            if (rider->startRiding(vehicle, true) && rider->getVehicle() == &vehicle) return idOf(*rider);
            ctx.info(std::string(type) + " could not ride " + vehicle.getTypeName());
            rider->remove();
        }
        return 0;
    };

    // the fixture boss stands for an upstream boss: Entity#canUsePortal false (BaseTFBoss), which F8 answers
    // through canChangeDimensionsUsingPortal for the types marked with ext::setCanUsePortal(false)
    ge::ext::setCanUsePortal("gf_test:boss", false);
    Vec3  beside{box.minX + 9.5f, static_cast<float>(box.minY), box.minZ + 9.5f};
    auto* boss = spawn("gf_test:boss", beside);
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto bossId  = idOf(*boss);
    auto riderId = mount(*boss, beside);
    ctx.require(riderId != 0, "something rides the fixture boss");
    boss = actor(bossId);
    ctx.require(boss != nullptr, "the boss is there");
    ctx.check(boss->hasPassenger(), "the boss carries its rider");
    ctx.check(!canUsePortal(*boss), "Entity#canUsePortal(false) of the ridden boss is false (its own answer first)");
    ge::ext::setCanUsePortal("gf_test:boss", true);
    ctx.check(canUsePortal(*boss), "the same mob unmarked (a plain mob) may use portals with its rider");
    ge::ext::setCanUsePortal("gf_test:boss", false);

    // in a lit portal it stays where it is, rider and all (asked every tick, never a process)
    ctx.require(portal::tryActivate(*region, game::blockPos(pool)), "lit");
    co_await ctx.ticks(10); // the lightning is gone
    auto touches = [&](Actor& a) { // Entity#checkInsideBlocks: a portal block in its box
        auto* r = game::regionOf(dim);
        if (!r) return false;
        AABB box = a.getAABB();
        for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x)); ++x)
            for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y)); ++y)
                for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z)); ++z)
                    if (isPortal(r->getBlock(BlockPos{x, y, z}))) return true;
        return false;
    };
    struct Stay {
        bool            stayed = true, process = false, asked = true;
        int             inside = 0, ridden = 0;
        PortalDecisions portal; ///< what the portal's own ticks decided (entityInside, handlePortal)
    };
    // 40 ticks in the portal; the rider is put back on whenever it got off
    auto stayInPortal = [&](std::int64_t vehicleId, std::int64_t riderId) -> ll::coro::CoroTask<Stay> {
        Stay out;
        struct Unwatch {
            ~Unwatch() { watchPortalDecisions(0); }
        } unwatch;
        watchPortalDecisions(vehicleId);
        if (auto* v = actor(vehicleId)) inPortal(*v);
        for (int i = 0; i < 40; ++i) {
            auto* v = actor(vehicleId);
            if (auto* r = actor(riderId); r && v && r->getVehicle() != v) r->startRiding(*v, true);
            co_await ctx.ticks(1);
            v = actor(vehicleId);
            if (!v || static_cast<int>(v->getDimensionId()) != dim) {
                out.stayed = false;
                break;
            }
            out.process |= portalProcessOf(*v).hasProcess || portalProcessOf(*v).travelling;
            if (!touches(*v)) continue;
            ++out.inside;
            if (!v->hasPassenger()) continue;
            ++out.ridden;
            out.asked &= !canUsePortal(*v);
        }
        out.portal = portalDecisions();
        co_return out;
    };
    auto decided = [](PortalDecisions const& d) {
        return std::format("the portal asked {} times, {} while ridden, refused {} / {} of those", d.asked, d.askedRidden, d.refused, d.refusedRidden);
    };
    auto stay = co_await stayInPortal(bossId, riderId);
    ctx.check(stay.stayed, "the ridden upstream boss stays out of the portal for 2 s");
    ctx.check(!stay.process, "it never starts a portal process");
    ctx.check(stay.inside >= 30, std::format("it stood in the portal ({} of 40 ticks)", stay.inside));
    ctx.check(stay.ridden >= 1 && stay.asked, std::format("with its rider on it refused the portal ({} of 40 ticks)", stay.ridden));
    ctx.check(
        stay.portal.askedRidden >= 1 && stay.portal.refusedRidden == stay.portal.askedRidden && stay.portal.refused == stay.portal.asked,
        "the portal's own ticks refused the ridden boss every time (" + decided(stay.portal) + ")"
    );
    if (auto* b = actor(bossId)) b->remove();
    if (auto* r = actor(riderId)) r->remove();

    // a type marked the same way that holds its rider: a boat with a pig in it stays too
    ge::ext::setCanUsePortal("minecraft:boat", false);
    Vec3  shore{box.minX + 9.5f, static_cast<float>(box.minY), box.minZ + 2.5f};
    auto* marked = spawn("minecraft:boat", shore);
    ctx.require(marked != nullptr, "a boat spawns");
    auto markedId    = idOf(*marked);
    auto markedPigId = mount(*marked, shore);
    ctx.require(markedPigId != 0, "a pig sits in the boat");
    marked = actor(markedId);
    ctx.require(marked != nullptr, "the boat is there");
    ctx.check(!canUsePortal(*marked), "Entity#canUsePortal(false) of the marked boat with its rider is false");
    stay = co_await stayInPortal(markedId, markedPigId);
    ctx.check(stay.stayed && !stay.process, "the marked boat stays out of the portal for 2 s, pig and all");
    ctx.check(stay.inside >= 30 && stay.ridden >= 30 && stay.asked, std::format("in the portal {} and with its pig {} of 40 ticks", stay.inside, stay.ridden));
    ctx.check(
        stay.portal.askedRidden >= 30 && stay.portal.refusedRidden == stay.portal.askedRidden,
        "the portal's own ticks refused the marked boat with its pig every time (" + decided(stay.portal) + ")"
    );
    if (auto* b = actor(markedId)) b->remove();
    if (auto* r = actor(markedPigId)) r->remove();
    ge::ext::setCanUsePortal("minecraft:boat", true);

    // an unmarked boat with a pig in it: the vehicle may go, so it does, and its rider comes along
    auto* boat = spawn("minecraft:boat", shore);
    ctx.require(boat != nullptr, "a boat spawns");
    auto boatId = idOf(*boat);
    auto pigId  = mount(*boat, shore);
    ctx.require(pigId != 0, "a pig sits in the boat");
    boat = actor(boatId);
    ctx.require(boat != nullptr && boat->hasPassenger(), "the boat carries the pig");
    ctx.check(canUsePortal(*boat), "Entity#canUsePortal(false) of the boat with its rider is true (no rule against riders)");
    if (auto* pig = actor(pigId)) ctx.check(!canUsePortal(*pig), "its rider is not asked (a passenger never uses a portal itself)");
    int const destX = pool.x * 8, destZ = pool.z * 8;
    // where to look: around the scaled destination, and around the portal the teleporter chose for
    // the pool (its cache) or built (the watched sites). GloamingTeleporter#moveToSafeCoords moves an
    // arrival out of landmark chunks (scanIntoSafeBiomes), which can be hundreds of blocks away.
    auto searchAreas = [&]() {
        std::vector<AABB> out{AABB{Vec3(destX - 24, world::kMinY, destZ - 24), Vec3(destX + 24, world::kMinY + world::kHeight, destZ + 24)}};
        auto around = [&](BlockPos const& c) {
            out.emplace_back(Vec3(c.x - 12, world::kMinY, c.z - 12), Vec3(c.x + 14, world::kMinY + world::kHeight, c.z + 14));
        };
        for (int dx = 0; dx < 2; ++dx)
            for (int dz = 0; dz < 2; ++dz)
                if (auto cached = tp::cacheGet(world::kDimensionId, pool.x + dx, pool.z + dz)) around(cached->pos);
        for (auto const& corner : terrain.sitesIn(gfId)) around(corner);
        return out;
    };
    auto boatsAndPigs = [&](auto&& fn) {
        auto* r = game::regionOf(gfId);
        if (!r) return;
        std::set<std::int64_t> seen;
        for (auto const& area : searchAreas())
            for (auto* e : r->getEntities(area, 0.0f))
                if (e && (e->getTypeName() == "minecraft:boat" || e->getTypeName() == "minecraft:pig") && seen.insert(idOf(*e)).second) fn(*e);
    };
    // what is there already: a rider that slid off a vehicle above into the portal went on its
    // own (an unridden pig may use portals), and is no part of this trip
    std::set<std::int64_t> before;
    boatsAndPigs([&](Actor& e) {
        before.insert(idOf(e));
        spawned.push_back(idOf(e));
    });
    if (!before.empty()) ctx.info(std::to_string(before.size()) + " boat(s) / pig(s) of the earlier parts went through on their own");
    watchPortalDecisions(boatId);
    inPortal(*boat);
    if (auto* p = actor(pigId); p && p->getVehicle() != actor(boatId)) p->startRiding(*actor(boatId), true);
    Actor*                  boatThere = nullptr;
    Actor*                  pigThere  = nullptr;
    std::optional<BlockPos> arrival;
    int                     ticks = 0;
    for (; ticks < 300 && !(boatThere && pigThere); ++ticks) {
        co_await ctx.ticks(1);
        boatThere = pigThere = nullptr;
        // the same entities (when the engine kept their ids), or new ones near the arrival
        if (auto* b = actor(boatId); b && static_cast<int>(b->getDimensionId()) == gfId) boatThere = b;
        if (auto* p = actor(pigId); p && static_cast<int>(p->getDimensionId()) == gfId) pigThere = p;
        boatsAndPigs([&](Actor& e) {
            if (before.contains(idOf(e))) return;
            if (!boatThere && e.getTypeName() == "minecraft:boat") boatThere = &e;
            else if (!pigThere && e.getTypeName() == "minecraft:pig") pigThere = &e;
        });
    }
    auto boatDecisions = portalDecisions();
    watchPortalDecisions(0);
    ctx.check(
        boatDecisions.askedRidden >= 1 && boatDecisions.refused == 0,
        "the portal's own ticks let the boat with its pig go (" + decided(boatDecisions) + ")"
    );
    if (boatThere) {
        spawned.push_back(idOf(*boatThere));
        arrival = game::blockPosOf(boatThere->getFeetPos());
    }
    if (pigThere) spawned.push_back(idOf(*pigThere));
    ctx.check(boatThere != nullptr, "the boat is in the Gloaming Forest (after " + std::to_string(ticks) + " ticks)");
    ctx.check(pigThere != nullptr, "the pig came along");
    if (boatThere && pigThere) {
        co_await ctx.ticks(5);
        auto* b = actor(spawned[spawned.size() - 2]);
        auto* p = actor(spawned.back());
        ctx.check(b && p && p->getVehicle() == b, "and sits in the boat again");
    }
    for (auto* e : {actor(boatId), actor(pigId)})
        ctx.check(e == nullptr || static_cast<int>(e->getDimensionId()) != dim, "nothing of the trip stayed at home");
    // the teleporter cache entries of the trip
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) tp::cacheRemove(world::kDimensionId, pool.x + dx, pool.z + dz);
    if (auto* gloamingr = game::regionOf(gfId); gloamingr && arrival)
        for (auto const& b : tp::portalBlocksNear(*gloamingr, *arrival, 4)) tp::cacheRemove("minecraft:overworld", b.x, b.z);
    for (auto id : spawned)
        if (auto* a = actor(id)) a->remove();
    int put = co_await terrain.restore();
    ctx.info("put back " + std::to_string(put) + " blocks of the generated terrain at the boat's return portal");
});

} // namespace

} // namespace gf::feature::dimension
