// U3 trees: selftests (trees.*, testPos dx 300..399, rule R12).
//
//   trees.features_a / trees.features_b   every owned tree / mushroom configured feature places
//   trees.golden_*                        golden volume hashes of seeded canopy, mega canopy, darkwood
//                                         and mangrove trees (rule R19 style review once)
//   trees.sapling_growers                 each tree sapling grows its tree from bone meal (the real click)
//   trees.mega_2x2                        four canopy saplings grow the mega canopy tree
//   trees.core_toggle_held_item           a core toggles with a block in hand, nothing is placed
//   trees.core_disabled_config            a core disabled by config stays off; the message names it
//   trees.time_core_ticks                 the tree of time ticks a crop
//   trees.transformation_core_biome       the heart of transformation turns a column into enchanted forest
//   trees.mining_core_magnet              the minewood core pulls an ore vein toward it
//   trees.sorting_core_moves_item         the sortingwood engine moves an item into the chest holding more
//   trees.snow_under_trees                snow under leaves
#include <cmath>
#include <map>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "core/jrand/JRand.h"
#include "feature/features/Features.h"
#include "feature/trees/Trees.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace gf::feature::trees {

namespace {

using gf::core::BlockBox;
using gf::core::GlobalPos;
using gf::selftest::Context;

/// A worldgen Region over the live world (Java's WorldGenRegion for a test placement).
class LiveRegion final : public gf::worldgen::Region {
public:
    LiveRegion(BlockSource& region, BlockPos const& at) : mRegion(region), mCx(at.x >> 4), mCz(at.z >> 4) {}
    int          centerChunkX() const override { return mCx; }
    int          centerChunkZ() const override { return mCz; }
    int          minY() const override { return mRegion.getMinHeight(); }
    int          maxY() const override { return mRegion.getMaxHeight(); }
    bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY() || !mRegion.hasChunksAt(pos, 0, false)) return false;
        return game::setBlock(mRegion, pos, block);
    }
    int height(gf::worldgen::Heightmap type, int x, int z) const override {
        bool leaves = type != gf::worldgen::Heightmap::MotionBlockingNoLeaves;
        for (int y = maxY() - 1; y >= minY(); --y) {
            Block const& b = mRegion.getBlock(BlockPos{x, y, z});
            if (b.isAir()) continue;
            if (type == gf::worldgen::Heightmap::MotionBlocking || type == gf::worldgen::Heightmap::MotionBlockingNoLeaves) {
                if (b.getTypeName() == "minecraft:snow_layer") continue;
            }
            if (!leaves && b.getTypeName().find("leaves") != std::string::npos) continue;
            return y + 1;
        }
        return minY();
    }
    std::string  biome(BlockPos const&) const override { return "minecraft:plains"; }
    std::int64_t seed() const override { return 0; }
    int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

private:
    BlockSource& mRegion;
    int          mCx, mCz;
};

BlockPos bp(GlobalPos const& p) { return game::blockPos(p); }

/// The vanilla huge mushrooms as Minecraft 1.21.1 configures them (huge_brown_mushroom /
/// huge_red_mushroom: the features data has no configured feature of these ids).
nlohmann::json vanillaMushroom(bool red) {
    auto face = [](bool up, bool down) {
        return nlohmann::json{{"down", down ? "true" : "false"}, {"east", "true"}, {"north", "true"}, {"south", "true"}, {"up", up ? "true" : "false"}, {"west", "true"}};
    };
    nlohmann::json cap  = nlohmann::json::object();
    cap["type"]         = "minecraft:simple_state_provider";
    cap["state"]        = {{"Name", red ? "minecraft:red_mushroom_block" : "minecraft:brown_mushroom_block"}, {"Properties", face(true, false)}};
    nlohmann::json stem = nlohmann::json::object();
    stem["type"]        = "minecraft:simple_state_provider";
    stem["state"]       = {{"Name", "minecraft:mushroom_stem"}, {"Properties", face(false, false)}};
    nlohmann::json config = nlohmann::json::object();
    config["cap_provider"]   = cap;
    config["stem_provider"]  = stem;
    config["foliage_radius"] = red ? 2 : 3;
    nlohmann::json out       = nlohmann::json::object();
    out["type"]              = red ? "minecraft:huge_red_mushroom" : "minecraft:huge_brown_mushroom";
    out["config"]            = config;
    return out;
}

bool placeSeeded(Context& ctx, std::string const& id, BlockPos const& pos, std::int64_t seed) {
    auto* region = ctx.region();
    if (!region) return false;
    LiveRegion                live(*region, pos);
    core::jrand::LegacyRandom random(seed);
    if (id == "minecraft:huge_brown_mushroom" || id == "minecraft:huge_red_mushroom") return gf::features::placeJson(vanillaMushroom(id == "minecraft:huge_red_mushroom"), live, pos, random);
    return gf::features::place(id, live, pos, random);
}

/// The tree of time checks 4 blocks under its base for #minecraft:features_cannot_replace (the flat
/// world's bedrock is there): a 3x4x3 dirt pedestal topped with grass lifts it; returns the new base.
BlockPos pedestal(BlockSource& region, BlockPos const& pos) {
    auto const* dirt  = game::blockWith("minecraft:dirt");
    auto const* grass = game::blockWith("minecraft:grass_block");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = 0; dy < 4; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                if (auto const* b = dy == 3 ? grass : dirt) game::setBlock(region, BlockPos{pos.x + dx, pos.y + dy, pos.z + dz}, *b);
    return BlockPos{pos.x, pos.y + 4, pos.z};
}

int countBlocks(BlockSource& region, BlockBox const& box, std::string_view needle) {
    int n = 0;
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int y = box.minY; y <= box.maxY; ++y)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                if (region.getBlock(BlockPos{x, y, z}).getTypeName().find(needle) != std::string::npos) ++n;
    return n;
}

/// Places every feature of `ids` on a 4 x 4 grid of 36-block cells in one flat area.
ll::coro::CoroTask<> placeAll(Context& ctx, int dx, std::vector<std::string> const& ids) {
    auto box = co_await ctx.flatArea(dx, 144, 72);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        int      cx  = static_cast<int>(i % 4), cz = static_cast<int>(i / 4);
        BlockPos pos{box.minX + 18 + cx * 36, box.minY, box.minZ + 18 + cz * 36};
        co_await ctx.ensureLoaded(game::globalPos(ctx.dimension(), pos));
        if (ids[i].ends_with("time_tree") && ctx.region()) pos = pedestal(*ctx.region(), pos);
        bool ok = placeSeeded(ctx, ids[i], pos, 1234 + static_cast<std::int64_t>(i));
        ctx.check(ok, ids[i] + " placed");
        if (ok) ctx.info(ids[i] + " placed");
    }
}

std::string tree(char const* path) { return std::string("twilightforest:tree/") + path; }

} // namespace

GF_REGISTER_SELFTEST("trees.features_a", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 300, {tree("twilight_oak_tree"), tree("large_twilight_oak_tree"), tree("swampy_oak_tree"), tree("canopy_tree"),
                                 tree("firefly_canopy_tree"), tree("dead_canopy_tree"), tree("mega_canopy_tree"), tree("forest_mega_oak_tree"),
                                 tree("savannah_mega_oak_tree"), tree("mangrove_tree"), tree("darkwood_tree"), tree("homegrown_darkwood_tree"),
                                 tree("darkwood_lantern_tree"), tree("dark_forest_oak_tree"), tree("dark_forest_birch_tree"), tree("dark_oak_bush")});
});

GF_REGISTER_SELFTEST("trees.features_b", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 310, {tree("oak_bush"), tree("rainbow_oak"), tree("large_rainbow_oak"), tree("time_tree"), tree("transformation_tree"),
                                 tree("mining_tree"), tree("sorting_tree"), tree("snowy_spruce_tree"), tree("mega_spruce_tree"), tree("large_winter_tree"),
                                 tree("smaller_jungle_tree"), tree("vanilla_oak_tree"), tree("vanilla_birch_tree"), "twilightforest:mushroom/brown_canopy_mushroom",
                                 "twilightforest:mushroom/canopy_red_vanilla_mushroom", "twilightforest:mushroom/canopy_red_smooth_mushroom"});
});

GF_REGISTER_SELFTEST("trees.features_c", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 320, {"twilightforest:mushroom/canopy_red_spheroid_mushroom", "twilightforest:mushroom/canopy_red_flat_mushroom", "minecraft:huge_brown_mushroom",
                                 "minecraft:huge_red_mushroom"});
});

namespace {

/// Golden volume hashes of seeded trees (filled from the first reviewed run; empty: record only).
ll::coro::CoroTask<> golden(Context& ctx, int dx, std::string const& id, std::string const& want) {
    auto     box = co_await ctx.flatArea(dx, 48, 48);
    BlockPos pos{box.minX + 24, box.minY, box.minZ + 24};
    co_await ctx.ensureLoaded(game::globalPos(ctx.dimension(), pos));
    ctx.require(placeSeeded(ctx, id, pos, 20240611), id + " placed");
    auto hash = ctx.snapshotVolume(box);
    ctx.info(id + " volume hash " + hash);
    if (!want.empty()) ctx.check(hash == want, id + " matches its golden hash " + want + " (got " + hash + ")");
}

} // namespace

GF_REGISTER_SELFTEST("trees.golden_canopy_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 330, tree("canopy_tree"), "bcb7ce14451f3461"); });
GF_REGISTER_SELFTEST("trees.golden_mega_canopy_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 335, tree("mega_canopy_tree"), "f8ad57ebe7d2135f"); });
GF_REGISTER_SELFTEST("trees.golden_darkwood_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 340, tree("darkwood_tree"), "4b02f8878de8e514"); });
GF_REGISTER_SELFTEST("trees.golden_mangrove_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 345, tree("mangrove_tree"), "cf0c7c46e51a5599"); });

namespace {

SimulatedPlayer& sim(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// The test player right-clicks the top face of `p` with `itemId` (16 of it; "" = empty hand), as
/// the network handler runs a click (GameMode::useItemOn, a first event).
ll::coro::CoroTask<> click(Context& ctx, GlobalPos p, std::string itemId, bool sneak = false) {
    auto& sp = sim(ctx);
    sp.teleport(Vec3{p.x + 0.5f, p.y + 1.0f, p.z - 1.5f}, sp.getDimensionId());
    sp.simulateLookAt(bp(p), sim::LookDuration::Instant);
    if (itemId.empty()) sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    else if (auto item = game::items::make(itemId, 16)) sp.simulateSetItem(*item, true, 0);
    sp.setSneaking(sneak);
    co_await ctx.ticks(8);
    auto&    s2   = sim(ctx);
    BlockPos b    = bp(p);
    auto&    held = const_cast<ItemStack&>(s2.getSelectedItem());
    s2.mGameMode->useItemOn(held, b, 1, Vec3{b.x + 0.5f, b.y + 1.0f, b.z + 0.5f}, HandSlot::Mainhand, nullptr, true);
    s2.setSneaking(false);
    co_await ctx.ticks(2);
}

bool activeAt(Context& ctx, GlobalPos const& p) {
    auto* region = ctx.region();
    if (!region) return false;
    auto states = game::statesOf(region->getBlock(bp(p)));
    auto it     = states.find("twilightforest:active");
    return it != states.end() && (it->second == "true" || it->second == "1");
}

struct RangeGuard {
    TreesConfig saved = gf::config::get<TreesConfig>();
    ~RangeGuard() { gf::config::mut<TreesConfig>() = saved; }
};

} // namespace

GF_REGISTER_SELFTEST("trees.sapling_growers", [](Context& ctx) -> ll::coro::CoroTask<> {
    char const* saplings[] = {"twilight_oak_sapling", "canopy_sapling", "mangrove_sapling", "darkwood_sapling", "time_sapling",
                              "transformation_sapling", "mining_sapling", "sorting_sapling", "rainbow_oak_sapling"};
    auto box = co_await ctx.flatArea(350, 144, 60);
    for (int i = 0; i < 9; ++i) {
        GlobalPos p = game::globalPos(ctx.dimension(), BlockPos{box.minX + 24 + (i % 3) * 48, box.minY, box.minZ + 24 + (i / 3) * 48});
        std::string id = std::string("twilightforest:") + saplings[i];
        if (id == "twilightforest:time_sapling" && ctx.region()) p = game::globalPos(ctx.dimension(), pedestal(*ctx.region(), bp(p)));
        ctx.require(ctx.setBlock(p, id), id + " planted");
        bool grown = false;
        for (int tries = 0; tries < 40 && !grown; ++tries) {
            co_await click(ctx, p, "minecraft:bone_meal");
            grown = ctx.blockAt(p) != id;
        }
        ctx.check(grown, id + " grew its tree on bone meal (now " + ctx.blockAt(p) + ")");
        if (grown) ctx.info(id + " grew into " + ctx.blockAt(p));
    }
});

GF_REGISTER_SELFTEST("trees.mega_2x2", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto      box = co_await ctx.flatArea(360, 64, 64);
    GlobalPos p   = game::globalPos(ctx.dimension(), BlockPos{box.minX + 30, box.minY, box.minZ + 30});
    for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) ctx.require(ctx.setBlock(p.offset(dx, 0, dz), "twilightforest:canopy_sapling"), "sapling planted");
    bool grown = false;
    for (int tries = 0; tries < 40 && !grown; ++tries) {
        co_await click(ctx, p, "minecraft:bone_meal");
        grown = ctx.blockAt(p) != "twilightforest:canopy_sapling";
    }
    ctx.require(grown, "the 2x2 canopy saplings grew");
    int trunk = 0;
    for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
        for (int y = 0; y < 20; ++y)
            if (ctx.blockAt(p.offset(dx, y, dz)) == "twilightforest:canopy_log") ++trunk;
    ctx.check(trunk >= 70, "a 2x2 mega canopy trunk (" + std::to_string(trunk) + " logs in the lowest 20 layers)");
});

GF_REGISTER_SELFTEST("trees.core_toggle_held_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    GlobalPos core = ctx.testPos(370, 2);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}}), "a timewood clock");
    ctx.cleanupBlock(core.offset(0, 1, 0));
    co_await click(ctx, core, "minecraft:dirt");
    ctx.check(activeAt(ctx, core), "using the core with dirt in hand activates it");
    ctx.check(ctx.blockAt(core.offset(0, 1, 0)) != "minecraft:dirt", "no dirt was placed on the core");
    ctx.check(game::items::countOf(sim(ctx).getSelectedItem()) == 16, "the held dirt was not used");
    co_await click(ctx, core, "");
    ctx.check(!activeAt(ctx, core), "using it again with an empty hand deactivates it");
    co_await click(ctx, core, "minecraft:dirt", true);
    ctx.check(!activeAt(ctx, core), "sneaking with an item skips the core");
    ctx.check(ctx.blockAt(core.offset(0, 1, 0)) == "minecraft:dirt", "sneaking with dirt places the dirt");
});

GF_REGISTER_SELFTEST("trees.core_disabled_config", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().timeCoreRange = 0;
    GlobalPos core = ctx.testPos(372, 2);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}, {"twilightforest:active", true}}), "an active timewood clock");
    co_await click(ctx, core, "");
    ctx.check(!activeAt(ctx, core), "a disabled core turns off when used");
    auto text = coreDisabledMessage(ctx.player(), Core::Time);
    ctx.check(text.find("Timewood Clock") != std::string::npos && text.find("disabled via config") != std::string::npos, "the message names the core: " + text);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(performCoreEffect(Core::Time, *region, bp(core)) == 0, "a disabled core does nothing");
});

GF_REGISTER_SELFTEST("trees.time_core_ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().timeCoreRange = 1;
    GlobalPos core = ctx.testPos(374, 2);
    GlobalPos soil = core.offset(1, 0, 0), crop = core.offset(1, 1, 0);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}}), "a timewood clock");
    ctx.require(ctx.setBlock(soil, "minecraft:farmland", {{"moisturized_amount", 7}}), "farmland");
    ctx.require(ctx.setBlock(crop, "minecraft:wheat", {{"growth", 0}}), "wheat");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    int grown = 0, ticked = 0;
    for (int cycle = 0; cycle < 40 && grown == 0; ++cycle) {
        ticked += performCoreEffect(Core::Time, *region, bp(core));
        grown = game::intState(region->getBlock(bp(crop)), "growth").value_or(0);
        co_await ctx.ticks(1);
    }
    ctx.check(ticked > 0, "the core ticked blocks (" + std::to_string(ticked) + ")");
    ctx.check(grown > 0, "the wheat grew to " + std::to_string(grown));
});

GF_REGISTER_SELFTEST("trees.transformation_core_biome", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().transformationCoreRange = 3;
    GlobalPos core   = ctx.testPos(376, 2);
    auto*     region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.require(ctx.setBlock(core, "twilightforest:transformation_log_core", {{"twilightforest:axis", "y"}}), "a heart of transformation");
    if (!region->getLevel().getBiomeRegistry().lookupByName("twilightforest:enchanted_forest")) {
        ctx.info("no twilightforest:enchanted_forest biome registered: nothing to transform");
        co_return;
    }
    // the biomes of the columns within reach, put back afterwards
    BlockPos                                         c = bp(core);
    std::map<std::pair<int, int>, Biome const*>       saved;
    for (int dx = -4; dx <= 4; ++dx)
        for (int dz = -4; dz <= 4; ++dz) saved[{c.x + dx, c.z + dz}] = &region->getBiome(BlockPos{c.x + dx, c.y, c.z + dz});
    int changed = 0;
    for (int i = 0; i < 20 && changed == 0; ++i) changed = performCoreEffect(Core::Transformation, *region, c);
    int enchanted = 0;
    for (auto const& [xz, b] : saved) {
        auto const& now  = region->getBiome(BlockPos{xz.first, c.y, xz.second});
        auto const& name = now.mHash->getString();
        if (name.find("enchanted_forest") != std::string::npos) ++enchanted;
    }
    for (auto const& [xz, b] : saved)
        if (auto* chunk = region->getChunkAt(BlockPos{xz.first, c.y, xz.second}))
            chunk->_setBiome(*b, ChunkBlockPos{static_cast<uchar>(xz.first & 15), ChunkLocalHeight{0}, static_cast<uchar>(xz.second & 15)}, true);
    ctx.check(changed == 1, "the core transformed a column");
    ctx.check(enchanted >= 16, "a 4x4 column became enchanted forest (" + std::to_string(enchanted) + " blocks)");
});

GF_REGISTER_SELFTEST("trees.mining_core_magnet", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().miningCoreRange = 2;
    auto     box  = co_await ctx.flatArea(380, 9, 12);
    BlockPos c{box.minX + 4, box.minY + 4, box.minZ + 4};
    GlobalPos core = game::globalPos(ctx.dimension(), c);
    // stone around the core, iron ore on the shell two blocks out
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz) {
                int d = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
                if (d == 0) continue;
                ctx.setBlock(core.offset(dx, dy, dz), d == 1 ? "minecraft:stone" : "minecraft:iron_ore");
            }
    ctx.require(ctx.setBlock(core, "twilightforest:mining_log_core", {{"twilightforest:axis", "y"}}), "a minewood core");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    int moved = 0;
    for (int i = 0; i < 60 && moved == 0; ++i) moved = performCoreEffect(Core::Mining, *region, c);
    ctx.check(moved > 0, "the core pulled ore (" + std::to_string(moved) + " blocks)");
    int innerOre = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                if (ctx.blockAt(core.offset(dx, dy, dz)) == "minecraft:iron_ore") ++innerOre;
    ctx.check(innerOre > 0, "ore moved next to the core (" + std::to_string(innerOre) + ")");
});

namespace {
Container* chestAt(BlockSource& region, BlockPos const& p) {
    auto* actor = region.getBlockEntity(p);
    auto* main  = actor ? actor->_getMainComponent() : nullptr;
    return main ? main->getContainer() : nullptr;
}
int sticks(Container* c) {
    int n = 0;
    if (!c) return -1;
    for (int i = 0; i < c->getContainerSize(); ++i)
        if (game::items::idOf(c->getItem(i)) == "minecraft:stick") n += game::items::countOf(c->getItem(i));
    return n;
}
} // namespace

GF_REGISTER_SELFTEST("trees.sorting_core_moves_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().sortingCoreRange = 4;
    GlobalPos core = ctx.testPos(386, 2);
    GlobalPos in = core.offset(1, 0, 0), out = core.offset(4, 0, 0);
    ctx.require(ctx.setBlock(core, "twilightforest:sorting_log_core", {{"twilightforest:axis", "y"}}), "a sortingwood engine");
    ctx.require(ctx.setBlock(in, "minecraft:chest"), "an input chest");
    ctx.require(ctx.setBlock(out, "minecraft:chest"), "an output chest");
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto* ci = chestAt(*region, bp(in));
    auto* co = chestAt(*region, bp(out));
    ctx.require(ci && co, "both chests have containers");
    if (auto s = game::items::make("minecraft:stick", 3)) ci->setItem(0, *s);
    if (auto s = game::items::make("minecraft:stick", 1)) co->setItem(5, *s);
    int sorted = performCoreEffect(Core::Sorting, *region, bp(core));
    ctx.check(sorted == 1, "one item sorted per input and cycle (" + std::to_string(sorted) + ")");
    ctx.check(sticks(chestAt(*region, bp(in))) == 2 && sticks(chestAt(*region, bp(out))) == 2, "a stick moved into the chest holding sticks");
    if (auto* c = chestAt(*region, bp(in))) c->removeAllItems();
    if (auto* c = chestAt(*region, bp(out))) c->removeAllItems();
});

GF_REGISTER_SELFTEST("trees.snow_under_trees", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto      box  = co_await ctx.flatArea(390, 16, 12);
    GlobalPos leaf = game::globalPos(ctx.dimension(), BlockPos{box.minX + 5, box.minY + 5, box.minZ + 5});
    ctx.require(ctx.setBlock(leaf, "minecraft:spruce_leaves", {{"persistent_bit", true}}), "leaves above the ground");
    BlockPos origin{box.minX, box.minY, box.minZ};
    ctx.require(placeSeeded(ctx, "twilightforest:snow_under_trees", origin, 1), "snow_under_trees placed");
    ctx.check(ctx.blockAt(game::globalPos(ctx.dimension(), BlockPos{box.minX + 5, box.minY, box.minZ + 5})) == "minecraft:snow_layer", "snow under the leaves");
    ctx.check(ctx.blockAt(game::globalPos(ctx.dimension(), BlockPos{box.minX + 7, box.minY, box.minZ + 5})) == "minecraft:air", "no snow in the open");
});

} // namespace gf::feature::trees

#ifdef GF_CLIENT
namespace gf::feature::trees {

namespace {
std::vector<std::string> const& showcaseTrees() {
    static std::vector<std::string> const ids{
        tree("twilight_oak_tree"), tree("large_twilight_oak_tree"), tree("swampy_oak_tree"), tree("canopy_tree"), tree("firefly_canopy_tree"),
        tree("dead_canopy_tree"), tree("mega_canopy_tree"), tree("forest_mega_oak_tree"), tree("savannah_mega_oak_tree"), tree("mangrove_tree"),
        tree("darkwood_tree"), tree("homegrown_darkwood_tree"), tree("darkwood_lantern_tree"), tree("dark_forest_oak_tree"), tree("dark_forest_birch_tree"),
        tree("dark_oak_bush"), tree("oak_bush"), tree("rainbow_oak"), tree("large_rainbow_oak"), tree("time_tree"), tree("transformation_tree"),
        tree("mining_tree"), tree("sorting_tree"), tree("snowy_spruce_tree"), tree("mega_spruce_tree"), tree("large_winter_tree"),
        tree("smaller_jungle_tree"), "twilightforest:mushroom/brown_canopy_mushroom", "twilightforest:mushroom/canopy_red_vanilla_mushroom",
        "twilightforest:mushroom/canopy_red_smooth_mushroom", "twilightforest:mushroom/canopy_red_spheroid_mushroom",
        "twilightforest:mushroom/canopy_red_flat_mushroom",
    };
    return ids;
}
} // namespace

// Client e2e only (never in a server 'full' run): every tree, mushroom and block of the unit, left
// standing for the screenshots of tools/e2e/client/scenarios/trees.ps1 (40 blocks apart along x,
// 250 blocks east of the test rows; the blocks in a row 40 blocks north of the trees).
GF_REGISTER_SELFTEST("trees.showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    GlobalPos base   = ctx.testPos(399).offset(250, 0, 0);
    auto*     region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto const& ids = showcaseTrees();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        GlobalPos p = base.offset(static_cast<int>(i) * 40, 0, 0);
        co_await ctx.ensureLoaded(p);
        region = ctx.region();
        ctx.require(region != nullptr, "a region");
        // a column of grass on dirt and air above, whatever the flat world has
        for (int dx = -20; dx < 20; ++dx)
            for (int dz = -20; dz < 20; ++dz) {
                BlockPos b{p.x + dx, p.y - 1, p.z + dz};
                if (auto const* g = game::blockWith("minecraft:grass_block")) game::setBlock(*region, b, *g);
                for (int dy = 0; dy < 50; ++dy)
                    if (auto const* a = game::blockWith("minecraft:air")) game::setBlock(*region, BlockPos{b.x, p.y + dy, b.z}, *a);
            }
        BlockPos at = bp(p);
        if (ids[i].ends_with("time_tree")) at = pedestal(*region, at);
        bool ok = placeSeeded(ctx, ids[i], at, 4242 + static_cast<std::int64_t>(i));
        ctx.check(ok, ids[i] + " placed");
        ctx.info("trees showcase " + std::to_string(i) + " " + ids[i] + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
    }
    // the blocks: leaves, saplings, pots, cores (two of them active), the mangrove root
    char const* blocks[] = {"twilight_oak_leaves", "canopy_leaves", "mangrove_leaves", "dark_leaves", "hardened_dark_leaves", "rainbow_oak_leaves",
                            "time_leaves", "transformation_leaves", "mining_leaves", "sorting_leaves", "twilight_oak_sapling", "canopy_sapling",
                            "mangrove_sapling", "darkwood_sapling", "hollow_oak_sapling", "time_sapling", "transformation_sapling", "mining_sapling",
                            "sorting_sapling", "rainbow_oak_sapling", "potted_twilight_oak_sapling", "potted_canopy_sapling", "potted_mangrove_sapling",
                            "potted_darkwood_sapling", "potted_hollow_oak_sapling", "potted_time_sapling", "potted_transformation_sapling",
                            "potted_mining_sapling", "potted_sorting_sapling", "potted_rainbow_oak_sapling", "time_log_core", "transformation_log_core",
                            "mining_log_core", "sorting_log_core", "mangrove_root"};
    GlobalPos row = base.offset(0, 0, -40);
    co_await ctx.ensureLoaded(row);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    for (int i = 0; i < 35; ++i) {
        GlobalPos p  = row.offset(i * 2, 0, 0);
        std::string id = std::string("twilightforest:") + blocks[i];
        if (auto const* g = game::blockWith("minecraft:grass_block")) game::setBlock(*region, bp(p.offset(0, -1, 0)), *g);
        nlohmann::json states = nlohmann::json::object();
        if (id.ends_with("_log_core")) states = {{"twilightforest:axis", "y"}, {"twilightforest:active", i % 2 == 0}};
        if (id.ends_with("_leaves") && id != "twilightforest:hardened_dark_leaves") states = {{"twilightforest:persistent", true}};
        auto const* b = game::blockWithStates(id, states);
        ctx.check(b != nullptr, id + " exists");
        if (b) game::setBlock(*region, bp(p), *b);
    }
    ctx.info("trees showcase blocks at " + std::to_string(row.x) + " " + std::to_string(row.y) + " " + std::to_string(row.z));
});

} // namespace gf::feature::trees
#endif
