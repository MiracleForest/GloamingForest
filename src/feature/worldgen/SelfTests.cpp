// Selftests of the generator pipeline, the feature interpreter and the spawn engine (F5, prefix
// "worldgen.", testPos dx 5500..5599). The determinism goldens are the same on the server and the
// client build: the fixture feature and the in-memory pipeline must hash identically on both.
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <thread>
#include <variant>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/JRand.h"
#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "feature/dimension/World.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/ext/worldgen/Spawning.h"
#include "feature/features/Internal.h"
#include "feature/gamerules/GameRules.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Internal.h"
#include "feature/spawn/Spawn.h"
#include "feature/worldgen/Internal.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

#ifdef GF_CLIENT
#include "client/Client.h"
#include "mc/client/player/LocalPlayer.h"
#endif

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
namespace wg   = gf::worldgen;
namespace kit  = gf::features::kit;
namespace jr   = gf::core::jrand;

/// The custom probe biome of spike S2 (a behavior pack biome the worldgen e2e scenario installs
/// into the test pack before the world loads; never shipped).
constexpr char kProbeBiome[] = "gf_test:worldgen_probe";

/// The probe biome in `registry` (nullptr: not installed).
Biome const* probeBiome(BiomeRegistry const& registry) {
    if (auto const* b = registry.lookupByName(std::string(kProbeBiome))) return b;
    return registry.lookupByName(std::string("worldgen_probe"));
}

std::string hex(std::uint64_t v) {
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << v;
    return o.str();
}

/// A flat in-memory world: bedrock, stone, dirt, grass at y 0..3 (top at y 3) over 3x3 chunks.
std::unique_ptr<wg::detail::MemoryWorld> flatMemoryWorld(std::int64_t seed) {
    auto        w       = std::make_unique<wg::detail::MemoryWorld>(-32, 256, seed);
    auto const* bedrock = gf::game::blockWith("minecraft:bedrock");
    auto const* stone   = gf::game::blockWith("minecraft:stone");
    auto const* dirt    = gf::game::blockWith("minecraft:dirt");
    auto const* grass   = gf::game::blockWith("minecraft:grass_block");
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) {
            auto& c = wg::detail::asChunk(w->chunk(cx, cz));
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    c.setBlock(x, 0, z, *bedrock);
                    c.setBlock(x, 1, z, *stone);
                    c.setBlock(x, 2, z, *dirt);
                    c.setBlock(x, 3, z, *grass);
                }
        }
    return w;
}

std::uint64_t worldHash(wg::detail::MemoryWorld const& w) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) h = (h ^ w.hash(cx, cz)) * 0x100000001b3ull;
    return h;
}

int countBlocks(wg::detail::MemoryWorld const& w, std::string const& id) {
    int n = 0;
    for (int x = -16; x < 32; ++x)
        for (int z = -16; z < 32; ++z)
            for (int y = 0; y < 40; ++y)
                if (w.getBlock({x, y, z}).getTypeName() == id) ++n;
    return n;
}

} // namespace

// The Java-parity core runs the same in the game as in gtest (tests/worldgen goldens).
GF_REGISTER_SELFTEST("worldgen.java_parity", [](Context& ctx) {
    jr::LegacyRandom l(0);
    ctx.check(l.nextInt() == -1155484576, "java.util.Random(0).nextInt()");
    jr::Xoroshiro128pp x(42);
    ctx.check(x.nextInt() == 1774573855, "XoroshiroRandomSource(42).nextInt()");
    jr::WorldgenRandom w(std::make_unique<jr::Xoroshiro128pp>(0));
    ctx.check(w.setDecorationSeed(0, 160, -320) == -438838478978501856LL, "WorldgenRandom#setDecorationSeed");
    ctx.check(jr::mthGetSeed(1, 2, 3) == -33674130277896LL, "Mth#getSeed");
    jr::Xoroshiro128pp xs(0);
    gf::core::noise::ImprovedNoise n(xs);
    ctx.check(std::bit_cast<std::int64_t>(n.noise(0.5, 1.25, -3.75)) == -4620161362573904924LL, "ImprovedNoise sample");
});

// gf_test:fixture_column (block_column of 3 fixture blocks, count / in_square / heightmap)
// placed with a fixed seed: the same blocks on the server and the client build.
GF_REGISTER_SELFTEST("worldgen.fixture_feature", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(kit::placedJson("gf_test:fixture_column") != nullptr, "the fixture placed feature is loaded");
    auto world = flatMemoryWorld(12345);
    auto region = world->region(0, 0, 1);
    jr::WorldgenRandom random(std::make_unique<jr::Xoroshiro128pp>(0));
    random.setDecorationSeed(12345, 0, 0);
    random.setFeatureSeed(random.setDecorationSeed(12345, 0, 0), 0, 9);
    ctx.require(kit::placePlaced("gf_test:fixture_column", *region, {0, -32, 0}, random), "the fixture feature placed");
    int placed = countBlocks(*world, "gf_test:fixture_block");
    ctx.check(placed == 3, "three fixture blocks (got " + std::to_string(placed) + ")");
    auto h = hex(worldHash(*world));
    ctx.info("fixture feature hash " + h);
    ctx.check(h == "f3df82a014e6dc38", "the fixture placement matches the golden hash on every build (" + h + ")");

    // the same feature in the game world through a BlockSource region
    auto box = co_await ctx.flatArea(5500, 32);
    auto* bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    int cx = (box.minX + 15) >> 4, cz = (box.minZ + 15) >> 4;
    wg::detail::SourceRegionAdapter view(*bs, cx, cz, 0, box.minY - 1, box.maxY + 1, 12345, ctx.dimension());
    jr::WorldgenRandom random2(std::make_unique<jr::Xoroshiro128pp>(0));
    random2.setFeatureSeed(random2.setDecorationSeed(12345, cx * 16, cz * 16), 0, 9);
    ctx.require(kit::placePlaced("gf_test:fixture_column", view, {cx * 16, box.minY, cz * 16}, random2), "placed in the world");
    ctx.check(view.writes() == 3, "three blocks set in the world (" + std::to_string(view.writes()) + ")");
    int found = 0;
    for (int x = cx * 16; x < cx * 16 + 16; ++x)
        for (int z = cz * 16; z < cz * 16 + 16; ++z)
            for (int y = box.minY; y <= box.maxY; ++y)
                if (gf::game::blockIdAt(*bs, {x, y, z}) == "gf_test:fixture_block") ++found;
    ctx.check(found == 3, "three fixture blocks in the world");
});

// The vanilla features TF's biomes use, from their embedded definitions.
GF_REGISTER_SELFTEST("worldgen.vanilla_features", [](Context& ctx) {
    auto world  = flatMemoryWorld(777);
    auto region = world->region(0, 0, 1);
    jr::WorldgenRandom random(std::make_unique<jr::Xoroshiro128pp>(0));
    random.setDecorationSeed(777, 0, 0);
    for (char const* id : {"minecraft:patch_grass", "minecraft:patch_tall_grass", "minecraft:forest_rock", "minecraft:patch_pumpkin", "minecraft:flower_forest_flowers"}) {
        bool any = false;
        for (int i = 0; i < 8 && !any; ++i) any = kit::placeConfigured(kit::placedJson(id) ? (*kit::placedJson(id))["feature"] : JsonT(id), *region, {4 + i, 4, 6}, random);
        ctx.check(any, std::string(id) + " places something");

    }
    ctx.check(countBlocks(*world, "minecraft:short_grass") > 0, "grass placed");
    ctx.check(countBlocks(*world, "minecraft:mossy_cobblestone") > 0, "a forest rock placed");
    auto h = hex(worldHash(*world));
    ctx.info("vanilla features hash " + h);
    ctx.check(h == "172b5e4895dfc424", "the vanilla features match the golden hash on every build (" + h + ")");
});

// A vanilla tree configuration (straight trunk, blob foliage, vines) placed twice identically.
GF_REGISTER_SELFTEST("worldgen.tree", [](Context& ctx) {
    JsonT tree = JsonT::parse(R"({"type":"minecraft:tree","config":{
        "trunk_provider":{"type":"minecraft:simple_state_provider","state":{"Name":"minecraft:oak_log","Properties":{"axis":"y"}}},
        "foliage_provider":{"type":"minecraft:simple_state_provider","state":{"Name":"minecraft:oak_leaves","Properties":{"persistent":"false","distance":"7"}}},
        "dirt_provider":{"type":"minecraft:simple_state_provider","state":{"Name":"minecraft:dirt"}},
        "trunk_placer":{"type":"minecraft:straight_trunk_placer","base_height":4,"height_rand_a":2,"height_rand_b":0},
        "foliage_placer":{"type":"minecraft:blob_foliage_placer","radius":2,"offset":0,"height":3},
        "minimum_size":{"type":"minecraft:two_layers_feature_size","limit":1,"lower_size":0,"upper_size":1},
        "decorators":[{"type":"minecraft:leave_vine","probability":0.25}],
        "ignore_vines":true,"force_dirt":false}})");
    std::string first;
    for (int run = 0; run < 2; ++run) {
        auto world  = flatMemoryWorld(99);
        auto region = world->region(0, 0, 1);
        jr::WorldgenRandom random(std::make_unique<jr::Xoroshiro128pp>(0));
        random.setDecorationSeed(99, 0, 0);
        ctx.require(gf::features::placeJson(tree, *region, {8, 4, 8}, random), "the tree placed");
        int logs = countBlocks(*world, "minecraft:oak_log"), leaves = countBlocks(*world, "minecraft:oak_leaves");
        ctx.check(logs >= 4 && logs <= 6, "4..6 logs (" + std::to_string(logs) + ")");
        ctx.check(leaves > 20, "a blob of leaves (" + std::to_string(leaves) + ")");
        auto h = hex(worldHash(*world));
        if (run == 0) first = h;
        else ctx.check(h == first, "the same tree for the same seed");
    }
    ctx.info("tree hash " + first);
    ctx.check(first == "e7cec96b2660fb67", "the tree matches the golden hash on every build (" + first + ")");
});

// LargeWriter queues blocks of other chunks and applies them once the chunk is loaded.
GF_REGISTER_SELFTEST("worldgen.large_writer", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5510, 8);
    auto pos = ctx.testPos(5512, 2);
    pos.y    = box.minY + 1;
    auto const* block = gf::game::blockWith("gf_test:fixture_block");
    ctx.require(block != nullptr, "fixture block");
    std::size_t before = gf::features::LargeWriter::pending(ctx.dimension());
    gf::features::LargeWriter::setBlock(ctx.dimension(), gf::game::blockPos(pos), *block);
    ctx.check(gf::features::LargeWriter::pending(ctx.dimension()) == before + 1, "the block is queued");
    ctx.cleanupBlock(pos);
    // the drain runs every 20 ticks; wait for it so no queued block outlives the flat area
    for (int i = 0; i < 30 && gf::features::LargeWriter::pending(ctx.dimension()) > before; ++i) co_await ctx.ticks(10);
    ctx.check(ctx.blockAt(pos) == "gf_test:fixture_block", "the queued block is set in the loaded chunk");
    ctx.check(gf::features::LargeWriter::pending(ctx.dimension()) == before, "the queue is drained");
});

// Biomes are written per block column and read back through the chunk adapter.
GF_REGISTER_SELFTEST("worldgen.biomes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5520, 8);
    auto* bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    auto* lc = bs->getChunkAt(BlockPos{box.minX, box.minY, box.minZ});
    ctx.require(lc != nullptr, "test chunk");
    auto&                              dim = bs->getDimension();
    wg::detail::LevelChunkAdapter      chunk(*lc, dim.getBiomeRegistry(), bs->getMinHeight(), bs->getMaxHeight());
    int                                lx = box.minX & 15, lz = box.minZ & 15;
    std::string                        original = chunk.biome(lx, box.minY, lz);
    std::string                        other    = original == "minecraft:desert" ? "minecraft:plains" : "minecraft:desert";
    struct Restore { // the live chunk gets its biome back even when a check aborts the test
        wg::detail::LevelChunkAdapter& c;
        int                            x, z;
        std::string                    biome;
        ~Restore() { c.setBiomeColumn(x, z, biome); }
    } restore{chunk, lx, lz, original};
    chunk.setBiomeColumn(lx, lz, other);
    ctx.check(chunk.biome(lx, box.minY, lz) == other, "the column reads back " + other);
    ctx.check(chunk.biome(lx, bs->getMaxHeight() - 1, lz) == other, "the whole column is set");
    ctx.check(chunk.biome(lx, bs->getMinHeight(), lz) == other && chunk.biome(lx, (bs->getMinHeight() + bs->getMaxHeight()) / 2, lz) == other,
              "the bottom and the middle of the column too");
    ctx.check(chunk.biome((lx + 1) & 15, box.minY, lz) == original, "the next column keeps its biome");
    chunk.setBiome(lx, box.minY + 4, lz, original);
    ctx.check(chunk.biome(lx, box.minY + 4, lz) == original, "a single block reads back");
    chunk.setBiomeColumn(lx, lz, original);
    ctx.check(chunk.biome(lx, box.minY, lz) == original, "restored");

    // the generator's own writes: a biome source that varies with height (TF's underground
    // biomes) over the whole chunk, through setQuartColumn (each block column takes the most
    // frequent biome in one call, the other quarts per block), read back at both ends of every
    // quart of every column, timed, then the chunk restored block column by block column
    int  minY = bs->getMinHeight(), maxY = bs->getMaxHeight();
    int  qMin = minY >> 2, qMax = (maxY - 1) >> 2;
    auto layered = [&](int qx, int qz, int qy) {
        if (qy < 0) return std::string("minecraft:desert");
        return ((qx + qz + qy) % 7 == 0) ? std::string("minecraft:forest") : original;
    };
    std::vector<std::vector<std::string>> saved(256);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int qy = qMin; qy <= qMax; ++qy) saved[static_cast<std::size_t>(x * 16 + z)].push_back(chunk.biome(x, qy * 4, z));
    struct RestoreChunk {
        wg::detail::LevelChunkAdapter&          c;
        std::vector<std::vector<std::string>>&  saved;
        int                                     qMin;
        ~RestoreChunk() {
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    auto const& col = saved[static_cast<std::size_t>(x * 16 + z)];
                    for (std::size_t i = 0; i < col.size(); ++i)
                        if (c.biome(x, (qMin + static_cast<int>(i)) * 4, z) != col[i])
                            for (int y = (qMin + static_cast<int>(i)) * 4; y < (qMin + static_cast<int>(i)) * 4 + 4; ++y) c.setBiome(x, y, z, col[i]);
                }
        }
    } restoreChunk{chunk, saved, qMin};
    auto start = std::chrono::steady_clock::now();
    for (int qx = 0; qx < 4; ++qx)
        for (int qz = 0; qz < 4; ++qz) {
            std::vector<std::string> ids;
            for (int qy = qMin; qy <= qMax; ++qy) ids.push_back(layered(qx, qz, qy));
            chunk.setQuartColumn(qx, qz, qMin, ids);
        }
    double ms   = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    bool   same = true;
    for (int x = 0; x < 16 && same; ++x)
        for (int z = 0; z < 16 && same; ++z)
            for (int qy = qMin; qy <= qMax && same; ++qy) {
                auto want = layered(x >> 2, z >> 2, qy);
                if (chunk.biome(x, qy * 4, z) != want || chunk.biome(x, std::min(qy * 4 + 3, maxY - 1), z) != want) same = false;
            }
    ctx.check(same, "a height-varying biome layout reads back at every quart of every column");
    std::ostringstream o;
    o << "worldgen biomes: height-varying biomes written into a live chunk in " << std::fixed << std::setprecision(3) << ms << " ms";
    gf::log().info("{}", o.str());
    ctx.info(o.str());
});

// The whole pipeline on 200 chunks in memory (timing budget, determinism).
GF_REGISTER_SELFTEST("worldgen.timing", [](Context& ctx) {
    std::uint64_t h1 = 0, h2 = 0;
    double        ms = wg::ext::runInMemory(20241102, -10, -5, 20, 200, &h1);
    double        ms2 = wg::ext::runInMemory(20241102, 0, 0, 5, 10, &h2);
    std::uint64_t h3  = 0;
    (void)wg::ext::runInMemory(20241102, 0, 0, 5, 10, &h3);
    ctx.check(h2 == h3, "the pipeline is deterministic for a seed");
    int    budget = gf::config::get<wg::WorldgenConfig>().chunkBudgetMs;
    double per    = ms / 200.0;
    std::ostringstream o;
    o << "worldgen timing: 200 chunks in " << std::fixed << std::setprecision(1) << ms << " ms (" << std::setprecision(2) << per
      << " ms/chunk, budget " << budget << " ms/chunk, 10 more in " << ms2 << " ms), hash " << hex(h1);
    gf::log().info("{}", o.str());
    ctx.info(o.str());
    ctx.check(per <= budget, "within the per-chunk budget");

    // the same with a biome source that varies with height (TF's underground biomes below the
    // terrain): every quart column is non-uniform
    auto p        = *wg::detail::providers();
    p.biomeSource = [](int qx, int qy, int qz) {
        if (qy < 0) return std::string("minecraft:desert");
        return ((qx + qz) & 4) ? std::string("minecraft:plains") : std::string("minecraft:forest");
    };
    ++p.biomeSourceGeneration;
    std::uint64_t hv = 0;
    double        mv = wg::detail::runInMemory(p, 20241102, -10, -5, 20, 200, &hv);
    std::ostringstream v;
    v << "worldgen timing: 200 chunks with a height-varying biome source in " << std::fixed << std::setprecision(1) << mv << " ms ("
      << std::setprecision(2) << mv / 200.0 << " ms/chunk)";
    gf::log().info("{}", v.str());
    ctx.info(v.str());
    ctx.check(mv / 200.0 <= budget, "within the per-chunk budget with height-varying biomes");

    auto s = wg::ext::stats();
    if (s.chunksGenerated) {
        std::ostringstream g;
        g << "in-game generator: " << s.chunksGenerated << " chunks generated (" << std::setprecision(2) << s.generateMs / s.chunksGenerated
          << " ms each, biome writes " << s.biomeWriteMs / s.chunksGenerated << " ms), " << s.chunksDecorated << " decorated ("
          << (s.chunksDecorated ? s.decorateMs / s.chunksDecorated : 0.0) << " ms each)";
        ctx.info(g.str());
    }
});

// In the Twilight Forest (when F4's dimension is registered): new chunks come from this generator,
// with the biomes of the biome source (the custom probe biome when the e2e scenario installed it).
// Without the dimension the test logs a distinct line the worldgen scenario checks.
GF_REGISTER_SELFTEST("worldgen.generator", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!gf::world::twilight()) {
        gf::log().info("worldgen generator: skipped, no Twilight Forest dimension (F4)");
        ctx.info("the Twilight Forest dimension is not registered (F4): in-dimension generation not exercised");
        co_return;
    }
    ctx.require(co_await ctx.inTwilight(), "in the Twilight Forest");
    ctx.check(wg::ext::generatorActive(), "the Twilight Forest generator is installed");
    // the probe biome (if installed) as the biome source of the chunks generated now
    auto*       bs       = ctx.region();
    ctx.require(bs != nullptr, "test region");
    auto const* probe    = probeBiome(bs->getDimension().getBiomeRegistry());
    std::string probeId  = probe ? wg::detail::biomeIdOf(*probe) : std::string();
    auto        previous = wg::detail::providers()->biomeSource;
    if (probe) wg::setBiomeSource([probeId](int, int, int) { return probeId; });
    struct Restore {
        wg::BiomeSourceFn fn;
        bool              active;
        ~Restore() {
            if (active) wg::setBiomeSource(std::move(fn));
        }
    } restore{previous, probe != nullptr};
    auto before = wg::ext::stats();
    auto far    = ctx.testPos(5530);
    far.x += 4096;
    far.z += 4096;
    ctx.check(co_await ctx.ensureLoaded(far), "far chunks load");
    co_await ctx.ticks(40);
    auto after = wg::ext::stats();
    ctx.check(after.chunksGenerated > before.chunksGenerated, "new chunks generated by the pipeline");
    auto generated = after.chunksGenerated - before.chunksGenerated, decorated = after.chunksDecorated - before.chunksDecorated;
    std::ostringstream o;
    o << "worldgen generator: in-dimension run: " << generated << " chunks generated (" << std::fixed << std::setprecision(3)
      << (generated ? (after.generateMs - before.generateMs) / static_cast<double>(generated) : 0.0) << " ms each), " << decorated
      << " decorated (" << (decorated ? (after.decorateMs - before.decorateMs) / static_cast<double>(decorated) : 0.0) << " ms each)";
    gf::log().info("{}", o.str());
    ctx.info(o.str());
    if (probe) {
        bs = ctx.region();
        ctx.require(bs != nullptr, "test region");
        auto here = wg::detail::biomeIdOf(bs->getBiome(gf::game::blockPos(far)));
        ctx.check(here == probeId, "the generated chunk carries the custom biome " + probeId + " (" + here + ")");
        gf::log().info("worldgen generator: generated chunk biome {}", here);
    }
});

namespace {

/// Runs a command as the console in the test's dimension; the success count.
int runCommand(Context& ctx, std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    ServerCommandOrigin origin("gloamingforest-worldgen", level->asServer(), CommandPermissionLevel::Owner, DimensionType(ctx.dimension()));
    auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
    return out.mSuccessCount;
}

/// A gamerule's value as command text.
std::string ruleText(GameRules::GameRulesIndex index) {
    auto level = ll::service::getLevel();
    if (!level) return {};
    auto const& rules = *level->getGameRules().mGameRules;
    auto        i     = static_cast<std::size_t>(index);
    if (i >= rules.size()) return {};
    auto const& v = rules[i].mValue.get();
    if (auto const* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (auto const* n = std::get_if<int>(&v)) return std::to_string(*n);
    return {};
}

} // namespace

// The dev functions: written into the behavior pack, loaded by the world and run.
GF_REGISTER_SELFTEST("worldgen.functions", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto dir = gf::Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_BP" / "functions" / "twilightforest";
    ctx.check(std::filesystem::exists(dir / "dev_new_world.mcfunction"), "dev_new_world written");
    ctx.check(std::filesystem::exists(dir / "dev_powerwash_chunk.mcfunction"), "dev_powerwash_chunk written");

    // dev_powerwash_chunk: grass, dirt, stone, bedrock, gravel, andesite, diorite and granite from
    // y -32 to 15 within 10 blocks of the caller become air; other blocks stay
    auto box = co_await ctx.flatArea(5550, 24);
    int  x = box.minX + 11, z = box.minZ + 11;
    char const* cleared[] = {"minecraft:grass_block", "minecraft:dirt", "minecraft:stone", "minecraft:bedrock",
                             "minecraft:gravel",      "minecraft:andesite", "minecraft:diorite", "minecraft:granite"};
    gf::core::GlobalPos at = ctx.testPos(5550);
    for (int i = 0; i < 8; ++i) {
        at = {at.dim, x + (i % 2) * 9, -20 + i, z + (i / 2 % 2) * 9};
        ctx.require(ctx.setBlock(at, cleared[i]), std::string("placed ") + cleared[i]);
    }
    gf::core::GlobalPos keep{at.dim, x, -10, z}, high{at.dim, x, 16, z};
    ctx.require(ctx.setBlock(keep, "minecraft:cobblestone") && ctx.setBlock(high, "minecraft:stone"), "control blocks placed");
    int ok = runCommand(ctx, "execute positioned " + std::to_string(x) + " -20 " + std::to_string(z) + " run function twilightforest/dev_powerwash_chunk");
    ctx.check(ok > 0, "/function twilightforest/dev_powerwash_chunk ran");
    co_await ctx.ticks(2);
    for (int i = 0; i < 8; ++i) {
        gf::core::GlobalPos p{at.dim, x + (i % 2) * 9, -20 + i, z + (i / 2 % 2) * 9};
        ctx.check(ctx.blockAt(p) == "minecraft:air", std::string(cleared[i]) + " cleared (" + ctx.blockAt(p) + ")");
    }
    ctx.check(ctx.blockAt(keep) == "minecraft:cobblestone", "cobblestone kept");
    ctx.check(ctx.blockAt(high) == "minecraft:stone", "stone above y 15 kept");

    // dev_new_world: the gamerules, then the scriptevent (tfEnforcedProgression, the teleport)
    using I = GameRules::GameRulesIndex;
    std::pair<I, char const*> rules[] = {
        {I::DoDaylightCycle, "dodaylightcycle"}, {I::DoEntityDrops, "doentitydrops"}, {I::DoMobLoot, "domobloot"},
        {I::DoMobSpawning, "domobspawning"},     {I::DoTileDrops, "dotiledrops"},     {I::DoWeatherCycle, "doweathercycle"},
        {I::KeepInventory, "keepinventory"},     {I::RandomTickSpeed, "randomtickspeed"},
    };
    std::vector<std::pair<std::string, std::string>> saved;
    for (auto const& [index, name] : rules) saved.emplace_back(name, ruleText(index));
    // dev_new_world's scriptevent also turns TF's tfEnforcedProgression off (gf::gamerules): the
    // tests that run after this one (landmark protection, progression) need it back
    auto progression = gf::gamerules::get("tfEnforcedProgression");
    struct RestoreRules { // the shared test world gets its rules back even when a check aborts
        Context&                                          c;
        std::vector<std::pair<std::string, std::string>>& rules;
        gf::gamerules::Value                              progression;
        ~RestoreRules() {
            for (auto const& [name, value] : rules)
                if (!value.empty()) runCommand(c, "gamerule " + name + " " + value);
            (void)gf::gamerules::set("tfEnforcedProgression", progression);
        }
    } restoreRules{ctx, saved, progression};
    int before = gf::worldgen::detail::devNewWorldRuns();
    ctx.check(runCommand(ctx, "function twilightforest/dev_new_world") > 0, "/function twilightforest/dev_new_world ran");
    co_await ctx.ticks(2);
    std::string expected[] = {"false", "false", "false", "false", "false", "false", "true", "0"};
    for (std::size_t i = 0; i < std::size(rules); ++i)
        ctx.check(ruleText(rules[i].first) == expected[i], std::string(rules[i].second) + " = " + expected[i] + " (" + ruleText(rules[i].first) + ")");
    ctx.check(gf::worldgen::detail::devNewWorldRuns() == before + 1, "the function's scriptevent reached the mod (tfEnforcedProgression, teleport)");

    // run by a player in the overworld: `execute in twilightforest:twilight_forest run teleport ~ 128 ~`
    // (x / z times 8, the overworld's coordinate scale over the Twilight Forest's 0.125)
    if (auto twilight = gf::world::twilight()) {
        auto&       player = ctx.player();
        std::string name   = player.getRealName();
        Vec3        from   = player.getFeetPos();
        ctx.check(runCommand(ctx, "execute as @a[name=\"" + name + "\"] at @s run function twilightforest/dev_new_world") > 0, "dev_new_world run by a player");
        // the first tick in the dimension (the player falls from y 128 afterwards)
        bool arrived = false;
        Vec3 to{};
        for (int i = 0; i < 200 && !arrived; ++i) {
            co_await ctx.ticks(1);
            auto& moved = ctx.player();
            if (moved.getDimensionId() == *twilight) {
                arrived = true;
                to      = moved.getFeetPos();
            }
        }
        ctx.check(arrived, "the player is in the Twilight Forest");
        ctx.check(arrived && std::abs(to.y - 128.0F) < 2.0F && std::abs(to.x - from.x * 8.0F) < 1.0F && std::abs(to.z - from.z * 8.0F) < 1.0F,
                  "at x * 8, y 128, z * 8 (" + std::to_string(to.x) + " " + std::to_string(to.y) + " " + std::to_string(to.z) + ")");
        gf::log().info("worldgen functions: dev_new_world sent {} to the Twilight Forest at {:.1f} {:.1f} {:.1f}", name, to.x, to.y, to.z);
    }
    (void)gf::gamerules::set("tfEnforcedProgression", progression);
    ctx.check(gf::gamerules::get("tfEnforcedProgression") == progression, "tfEnforcedProgression is restored for the tests that follow");
    gf::log().info("worldgen functions: dev_powerwash_chunk and dev_new_world ran");
});

// ChunkGenerator#applyCarvers: every source chunk within 8 chunks gives its biome's carvers, each
// seeded with setLargeFeatureSeed(seed + index, sourceX, sourceZ), even when the chunk being
// carved has a biome without carvers.
GF_REGISTER_SELFTEST("worldgen.carvers", [](Context& ctx) {
    constexpr std::int64_t seed = 20261102;
    wg::detail::Providers  p;
    p.biomeSource = [](int qx, int, int qz) {
        int sx = qx >> 2, sz = qz >> 2; // the chunk of the quart
        return ((sx + sz) & 1) == 0 ? std::string("gf_test:carver_biome") : std::string("gf_test:plain_biome");
    };
    p.biomeSourceGeneration = 0x7e570000;
    struct Call {
        int         sx, sz, index;
        std::string id;
        float       first;
        int         mask;
    };
    std::vector<Call> calls;
    auto              record = [&](wg::Chunk&, wg::Region&, wg::RandomSource& r) {
        auto* c = wg::ext::carverContext();
        if (!c) return;
        calls.push_back({c->sourceChunkX, c->sourceChunkZ, c->index, c->configuredId, r.nextFloat(), c->mask ? c->mask->count() : -1});
        if (c->mask) c->mask->set(0, 0, 0);
    };
    p.carvers["gf_test:carver_one"] = record;
    p.carvers["gf_test:carver_two"] = record;
    auto lists                      = [](std::string const& biome) {
        return biome == "gf_test:carver_biome" ? std::vector<std::string>{"gf_test:carver_one", "gf_test:not_registered", "gf_test:carver_two"}
                                                                    : std::vector<std::string>{};
    };
    wg::detail::MemoryWorld world(-32, 64, seed);
    int                     cx = 3, cz = -2; // (3 + -2) is odd: the chunk's own biome has no carvers
    auto&                   chunk  = wg::detail::asChunk(world.chunk(cx, cz));
    auto                    region = world.region(cx, cz, 0);
    wg::detail::applyCarvers(p, chunk, *region, seed, lists);
    ctx.check(wg::ext::carverContext() == nullptr, "no carver context outside a carver call");
    int sources = 0;
    for (int j = -8; j <= 8; ++j)
        for (int k = -8; k <= 8; ++k)
            if (((cx + j + cz + k) & 1) == 0) ++sources;
    ctx.check(static_cast<int>(calls.size()) == sources * 2, "two carvers per carver-biome source chunk (" + std::to_string(calls.size()) + " calls, "
                                                                   + std::to_string(sources) + " sources)");
    bool seeds = true, ranges = true, indices = true;
    for (auto const& c : calls) {
        if (std::abs(c.sx - cx) > 8 || std::abs(c.sz - cz) > 8 || ((c.sx + c.sz) & 1) != 0) ranges = false;
        if ((c.id == "gf_test:carver_one" && c.index != 0) || (c.id == "gf_test:carver_two" && c.index != 2)) indices = false;
        jr::WorldgenRandom expect(std::make_unique<jr::LegacyRandom>(0));
        expect.setLargeFeatureSeed(seed + c.index, c.sx, c.sz);
        if (expect.nextFloat() != c.first) seeds = false;
    }
    ctx.check(ranges, "every source chunk is a carver-biome chunk within 8 chunks");
    ctx.check(indices, "the carver index is its position in the biome's list (an unregistered carver still counts)");
    ctx.check(seeds, "each call is seeded with setLargeFeatureSeed(seed + index, sourceX, sourceZ)");
    ctx.check(!calls.empty() && calls.back().mask == 1, "one carving mask is shared by the calls for the chunk");
    wg::detail::dropCarvingMask(-1, cx, cz); // kept for a decoration that does not come here
    gf::log().info("worldgen carvers: {} carver calls from {} source chunks", calls.size(), sources);
});

// The spawn rules of TF's landmarks: controlled spawns with the piece spawn indices, the hollow
// hill dome, the structure spawn overrides, and concurrent spawn table lookups.
GF_REGISTER_SELFTEST("worldgen.spawn_rules", [](Context& ctx) {
    namespace sd = gf::spawn::detail;
    gf::landmark::Start start;
    start.key        = "twilightforest:small_hollow_hill";
    start.box        = {0, 0, 0, 20, 10, 20};
    start.pieceBoxes = {start.box};
    start.generated  = true;
    gf::landmark::Descriptor d;
    d.key              = start.key;
    d.controlledSpawns = JsonT::parse(R"({"ambient_spawns":[],"water_spawns":[],"labelled_monster_spawns":{
        "0":[{"type":"minecraft:spider","weight":10,"minCount":4,"maxCount":4}],
        "1":[{"type":"minecraft:zombie","weight":10,"minCount":1,"maxCount":2}]}})");
    d.spawnOverrides   = JsonT::parse(R"({"monster":{"bounding_box":"full","spawns":[]},"creature":{"bounding_box":"piece","spawns":[]}})");
    auto center        = sd::controlledSpawnsOf(start, d, BlockPos{10, 1, 10}, "monster");
    ctx.check(center && center->size() == 1 && (*center)[0].entityId == "minecraft:spider", "monsters inside the dome: list 0");
    ctx.check(!sd::controlledSpawnsOf(start, d, BlockPos{0, 9, 0}, "monster"), "outside the dome: the hill does not decide");
    auto over = sd::spawnOverridesOf(start, d, BlockPos{0, 9, 0}, "monster");
    ctx.check(over && over->empty(), "outside the dome but inside the box: the empty spawn override (no monsters)");
    auto ambient = sd::controlledSpawnsOf(start, d, BlockPos{0, 9, 0}, "ambient");
    ctx.check(ambient && ambient->empty(), "ambient: the hill's empty ambient list");
    ctx.check(sd::hollowHillCanSpawnMob(BlockPos{10, 0, 10}, start.box) && !sd::hollowHillCanSpawnMob(BlockPos{0, 0, 0}, start.box), "HollowHillStructure#canSpawnMob");

    // piece spawn indices: the highest index of the pieces containing the position, -1 none
    gf::landmark::Start tower = start;
    tower.key                 = "gf_test:towers";
    tower.pieceBoxes          = {{0, 0, 0, 10, 10, 10}, {0, 5, 0, 10, 20, 10}, {30, 0, 30, 40, 10, 40}};
    tower.data                = JsonT{{"spawn_indices", JsonT::array({0, 1, nullptr})}};
    ctx.check(sd::spawnIndexAt(tower, BlockPos{5, 2, 5}) == 0, "lower piece: index 0");
    ctx.check(sd::spawnIndexAt(tower, BlockPos{5, 7, 5}) == 1, "overlap: the highest index (1)");
    ctx.check(sd::spawnIndexAt(tower, BlockPos{35, 2, 35}) == -1, "a piece without an index: none");
    gf::landmark::Descriptor td = d;
    td.key                      = tower.key;
    auto upper                  = sd::controlledSpawnsOf(tower, td, BlockPos{5, 15, 5}, "monster");
    ctx.check(upper && upper->size() == 1 && (*upper)[0].entityId == "minecraft:zombie", "index 1 picks labelled list 1");
    gf::spawn::ext::registerSpawnIndex("gf_test:towers", [](gf::landmark::Start const&, BlockPos const&) { return -1; });
    ctx.check(!sd::controlledSpawnsOf(tower, td, BlockPos{5, 15, 5}, "monster"), "a registered index lookup wins (-1: no list)");
    gf::spawn::ext::registerSpawnIndex("gf_test:towers", {}); // an empty function: back to the default lookup
    ctx.check(sd::spawnIndexAt(tower, BlockPos{5, 7, 5}) == 1, "the default lookup again");

    // tableOf from several threads while tables are registered (no data race, the same result)
    std::atomic<int>         mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i) {
                auto table = sd::tableOf("gf_test:race_biome_" + std::to_string((i + t) % 16));
                if (!table.byCategory.empty() && table.byCategory.begin()->first != "monster") ++mismatches;
            }
        });
    for (int i = 0; i < 16; ++i) {
        gf::spawn::Table table;
        table.byCategory["monster"] = {{"minecraft:zombie", 1, 1, 1}};
        gf::spawn::setBiomeSpawns("gf_test:race_biome_" + std::to_string(i), table);
    }
    for (auto& t : threads) t.join();
    ctx.check(mismatches.load() == 0, "concurrent spawn table lookups");
    ctx.check(sd::tableOf("gf_test:race_biome_3").byCategory.contains("monster"), "a registered table replaces the imported one");
});

// Custom biomes in chunks (spike S2): the probe biome gf_test:worldgen_probe, a behavior pack biome
// the worldgen e2e scenario installs into the test pack, is written into a live chunk and read
// back. The client scenario then checks what reaches a client, with controls:
// - painted chunks: when a player is tagged gf_worldgen_biome_show, 5 x 5 chunks 3000 blocks east
//   of it are painted with the probe and 5 x 5 chunks 3000 blocks south with minecraft:desert (a
//   vanilla biome with its own grass and water colours, painted the same way), both kept loaded by
//   a ticking area until worldgen.custom_biome_restore; the client has never received them.
// - generated chunks (with F4's dimension): worldgen.generator_view makes the generator write the
//   probe (or, tagged gf_worldgen_gen_desert, minecraft:desert) into fresh Twilight Forest chunks
//   and sends the tagged player there.
// worldgen.custom_biome_view logs the biome the server and the client's own level hold under the
// tagged player. worldgen.custom_biome_map renders a server map over probe, plains and desert
// columns and logs their map colours.
namespace {
struct Painted {
    int                                            dim = 0;
    std::vector<std::tuple<int, int, std::string>> columns; ///< x, z, original biome
    bool                                           tickingArea = false;
};
Painted& painted() {
    static Painted p;
    return p;
}

/// Paints the 5 x 5 chunks around block (x, z) with `biomeId` (whole columns), remembering the
/// originals; the number of columns painted.
int paintChunks(BlockSource& region, int x, int y, int z, std::string const& biomeId, Painted& out) {
    int pcx = x >> 4, pcz = z >> 4, n = 0;
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            auto* c = region.getChunk(pcx + dx, pcz + dz);
            if (!c) continue;
            wg::detail::LevelChunkAdapter a(*c, region.getDimension().getBiomeRegistry(), region.getMinHeight(), region.getMaxHeight());
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz) {
                    out.columns.emplace_back((pcx + dx) * 16 + lx, (pcz + dz) * 16 + lz, a.biome(lx, y, lz));
                    a.setBiomeColumn(lx, lz, biomeId);
                    ++n;
                }
        }
    return n;
}

/// The player tagged `tag` (nullptr: none).
Player* taggedPlayer(char const* tag) {
    Player* found = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer([&](Player& p) {
            if (p.hasTag(tag)) found = &p;
            return found == nullptr;
        });
    return found;
}

/// The generator view experiment's state: the biome source before it, the player's way back.
struct GeneratorView {
    bool              active = false;
    wg::BiomeSourceFn previous;
    Vec3              back{};
};
GeneratorView& generatorView() {
    static GeneratorView v;
    return v;
}
} // namespace

GF_REGISTER_SELFTEST("worldgen.custom_biome", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5560, 8);
    auto* bs  = ctx.region();
    ctx.require(bs != nullptr, "test region");
    auto&       registry = bs->getDimension().getBiomeRegistry();
    auto const* probe    = probeBiome(registry);
    if (!probe) {
        gf::log().info("worldgen custom biome: probe {} not installed", kProbeBiome);
        ctx.info(std::string("the probe biome ") + kProbeBiome + " is not installed (the worldgen e2e scenario installs it): nothing to write");
        co_return;
    }
    std::string probeId = wg::detail::biomeIdOf(*probe);
    {
        auto* lc = bs->getChunkAt(BlockPos{box.minX, box.minY, box.minZ});
        ctx.require(lc != nullptr, "test chunk");
        wg::detail::LevelChunkAdapter chunk(*lc, registry, bs->getMinHeight(), bs->getMaxHeight());
        int                           lx = box.minX & 15, lz = box.minZ & 15;
        std::string                   original = chunk.biome(lx, box.minY, lz);
        struct Restore { // restored this tick, also when a check aborts
            wg::detail::LevelChunkAdapter& c;
            int                            x, z;
            std::string                    biome;
            ~Restore() { c.setBiomeColumn(x, z, biome); }
        } restore{chunk, lx, lz, original};
        chunk.setBiomeColumn(lx, lz, probeId);
        ctx.check(chunk.biome(lx, box.minY, lz) == probeId, "the chunk reads back the custom biome " + probeId);
        auto viaSource = wg::detail::biomeIdOf(bs->getBiome(BlockPos{box.minX, box.minY + 1, box.minZ}));
        ctx.check(viaSource == probeId, "the block source sees the custom biome (" + viaSource + ")");
        gf::log().info("worldgen custom biome: {} written and read back (was {})", probeId, original);
    }

    // client view: paint chunks the client has never received, near a tagged player
    Player* shown = taggedPlayer("gf_worldgen_biome_show");
    if (!shown) co_return;
    std::string shownName = shown->getRealName();
    int         dim       = static_cast<int>(shown->getDimensionId());
    auto        feet      = shown->getFeetPos();
    int         fx = gf::core::mth::floor(feet.x), fy = gf::core::mth::floor(feet.y), fz = gf::core::mth::floor(feet.z);
    auto&       out = painted();
    out             = {dim, {}, false};
    struct Area {
        int         x, z;
        std::string biome;
        char const* where;
    };
    Area areas[] = {{fx + 3000, fz, probeId, "east"}, {fx, fz + 3000, "minecraft:desert", "south"}};
    for (auto const& a : areas) {
        // loaded by the test's simulated player, then kept loaded by a ticking area until the
        // restore test (the painted chunks must not be reloaded from disk before the client comes)
        ctx.require(co_await ctx.ensureLoaded({dim, a.x, fy, a.z}), std::string("the chunks 3000 blocks ") + a.where + " load on the server");
        auto* region = gf::game::regionOf(dim);
        ctx.require(region != nullptr, "the tagged player's region");
        int n = paintChunks(*region, a.x, fy, a.z, a.biome, out);
        runCommand(ctx, "tickingarea add circle " + std::to_string(a.x) + " " + std::to_string(fy) + " " + std::to_string(a.z) + " 2 gf_worldgen_paint_" + a.where);
        out.tickingArea = true;
        auto now = wg::detail::biomeIdOf(region->getBiome(BlockPos{a.x, fy, a.z}));
        ctx.check(now == a.biome, std::string("the server holds ") + a.biome + " 3000 blocks " + a.where + " (" + now + ")");
        gf::log().info("worldgen custom biome: painted {} columns 3000 blocks {} of {} with {}", n, a.where, shownName, a.biome);
    }
});

// What the server and the client's own level hold under the tagged player (client scenario).
GF_REGISTER_SELFTEST("worldgen.custom_biome_view", [](Context& ctx) {
    Player* shown = taggedPlayer("gf_worldgen_biome_show");
    if (!shown) {
        ctx.info("no player tagged gf_worldgen_biome_show");
        return;
    }
    auto        feet = shown->getFeetPos();
    BlockPos    at{gf::core::mth::floor(feet.x), gf::core::mth::floor(feet.y), gf::core::mth::floor(feet.z)};
    std::string server = wg::detail::biomeIdOf(shown->getDimensionBlockSource().getBiome(at));
    std::string client = "(no client level)";
    bool        known  = false;
#ifdef GF_CLIENT
    if (auto const* local = gf::client::currentPlayer()) {
        auto        lf     = local->getFeetPos();
        BlockPos    lat{gf::core::mth::floor(lf.x), gf::core::mth::floor(lf.y), gf::core::mth::floor(lf.z)};
        auto const& source = local->getDimensionBlockSourceConst();
        client             = wg::detail::biomeIdOf(source.getBiome(lat));
        known              = probeBiome(source.getDimensionConst().getBiomeRegistry()) != nullptr;
    }
#endif
    gf::log().info(
        "worldgen custom biome: at {} {} {} in dimension {} the server has {}, the client level has {} under its player (probe in the client registry: {})",
        at.x,
        at.y,
        at.z,
        static_cast<int>(shown->getDimensionId()),
        server,
        client,
        known
    );
    ctx.info("server " + server + ", client " + client);
});

GF_REGISTER_SELFTEST("worldgen.custom_biome_restore", [](Context& ctx) {
    auto& p = painted();
    if (p.tickingArea) {
        runCommand(ctx, "tickingarea remove gf_worldgen_paint_east");
        runCommand(ctx, "tickingarea remove gf_worldgen_paint_south");
        p.tickingArea = false;
    }
    if (p.columns.empty()) {
        ctx.info("nothing painted");
        return;
    }
    auto* region = gf::game::regionOf(p.dim);
    ctx.require(region != nullptr, "the painted region");
    int restored = 0;
    for (auto const& [x, z, biome] : p.columns) {
        auto* c = region->getChunk(x >> 4, z >> 4);
        if (!c) continue;
        wg::detail::LevelChunkAdapter a(*c, region->getDimension().getBiomeRegistry(), region->getMinHeight(), region->getMaxHeight());
        a.setBiomeColumn(x & 15, z & 15, biome);
        ++restored;
    }
    gf::log().info("worldgen custom biome: restored {} of {} columns", restored, p.columns.size());
    p.columns.clear();
});

// The generator path (with F4's dimension): new Twilight Forest chunks get the probe (or, for a
// player tagged gf_worldgen_gen_desert, minecraft:desert) from the biome source, and the tagged
// player is sent where nothing was generated yet. worldgen.generator_view_restore undoes it.
GF_REGISTER_SELFTEST("worldgen.generator_view", [](Context& ctx) {
    auto twilight = gf::world::twilight();
    if (!twilight) {
        gf::log().info("worldgen generator view: skipped, no Twilight Forest dimension (F4)");
        ctx.info("the Twilight Forest dimension is not registered (F4)");
        return;
    }
    Player* shown = taggedPlayer("gf_worldgen_biome_show");
    if (!shown) {
        ctx.info("no player tagged gf_worldgen_biome_show");
        return;
    }
    auto* region = gf::game::regionOf(static_cast<int>(*twilight));
    auto& reg    = region ? region->getDimension().getBiomeRegistry() : shown->getDimensionBlockSource().getDimension().getBiomeRegistry();
    bool  desert = shown->hasTag("gf_worldgen_gen_desert");
    std::string biome;
    if (desert) biome = "minecraft:desert";
    else if (auto const* probe = probeBiome(reg)) biome = wg::detail::biomeIdOf(*probe);
    ctx.require(!biome.empty(), "the probe biome is installed");
    auto& v = generatorView();
    if (!v.active) {
        v.previous = wg::detail::providers()->biomeSource;
        v.back     = shown->getFeetPos();
        v.active   = true;
    }
    wg::setBiomeSource([biome](int, int, int) { return biome; });
    float at = desert ? 40000.5F : 20000.5F; // fresh chunks, far from the trips before
    ctx.check(gf::world::teleport(*shown, *twilight, Vec3{at, 3.0F, at}, 0.0F), "the player is sent into new Twilight Forest chunks");
    gf::log().info("worldgen generator view: the biome source gives {}; {} sent to the Twilight Forest at {:.1f} 3 {:.1f}", biome, shown->getRealName(), at, at);
});

GF_REGISTER_SELFTEST("worldgen.generator_view_restore", [](Context& ctx) {
    auto& v = generatorView();
    if (!v.active) {
        ctx.info("nothing to restore");
        return;
    }
    wg::setBiomeSource(v.previous);
    v.active = false;
    if (Player* shown = taggedPlayer("gf_worldgen_biome_show")) {
        shown->removeTag("gf_worldgen_gen_desert");
        (void)gf::world::teleport(*shown, DimensionType(0), v.back, 0.0F);
    }
    gf::log().info("worldgen generator view: the biome source is restored");
});

// Map colours (spike S2): a server map rendered over columns painted with the probe (its behavior
// pack biome sets minecraft:map_tints), left as they are, and painted minecraft:desert. Logs the
// map colour of each; the probe's must be its own.
GF_REGISTER_SELFTEST("worldgen.custom_biome_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5580, 8);
    auto* bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    auto const* probe = probeBiome(bs->getDimension().getBiomeRegistry());
    if (!probe) {
        ctx.info(std::string("the probe biome ") + kProbeBiome + " is not installed: no map");
        co_return;
    }
    std::string probeId = wg::detail::biomeIdOf(*probe);
    int         dim     = ctx.dimension();
    auto        level   = ll::service::getLevel();
    // a map far from everything else; Bedrock centers it on its 128-block grid
    gf::core::GlobalPos far{dim, box.minX + 6000, box.minY, box.minZ};
    ctx.require(co_await ctx.ensureLoaded(far), "the map area loads");
    auto&    map    = level->createMapSavedData(level->getNewUniqueID(), BlockPos{far.x, far.y, far.z}, DimensionType(dim), 0);
    BlockPos origin = *map.mOrigin;
    if (std::abs(origin.x - far.x) > 16 || std::abs(origin.z - far.z) > 16)
        ctx.require(co_await ctx.ensureLoaded({dim, origin.x, box.minY, origin.z}), "the map's center loads");
    bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    // chunk columns -2..-1 of the map's center: the probe, 0: as they are, 1..2: minecraft:desert
    int     cx = origin.x >> 4, cz = origin.z >> 4;
    Painted saved{dim, {}, false};
    struct RestoreMap {
        Painted& p;
        ~RestoreMap() {
            if (auto* region = gf::game::regionOf(p.dim))
                for (auto const& [x, z, biome] : p.columns)
                    if (auto* c = region->getChunk(x >> 4, z >> 4)) {
                        wg::detail::LevelChunkAdapter a(*c, region->getDimension().getBiomeRegistry(), region->getMinHeight(), region->getMaxHeight());
                        a.setBiomeColumn(x & 15, z & 15, biome);
                    }
        }
    } restoreMap{saved};
    int painted = 0;
    auto paintColumns = [&](int chunkX, std::string const& id) {
        for (int dz = -1; dz <= 1; ++dz)
            if (auto* c = bs->getChunk(chunkX, cz + dz)) {
                wg::detail::LevelChunkAdapter a(*c, bs->getDimension().getBiomeRegistry(), bs->getMinHeight(), bs->getMaxHeight());
                for (int lx = 0; lx < 16; ++lx)
                    for (int lz = 0; lz < 16; ++lz) {
                        saved.columns.emplace_back(chunkX * 16 + lx, (cz + dz) * 16 + lz, a.biome(lx, box.minY, lz));
                        a.setBiomeColumn(lx, lz, id);
                        ++painted;
                    }
            }
    };
    paintColumns(cx - 1, probeId);
    paintColumns(cx + 1, "minecraft:desert");
    ctx.check(painted == 2 * 3 * 256, "the map's chunks are loaded and painted (" + std::to_string(painted) + " columns)");
    // a lapis marker on the ground fixes the map's pixel order
    int mx = origin.x + 3, mz = origin.z + 5;
    int ground = gf::spawn::detail::worldSurfaceY(*bs, mx, mz);
    ctx.require(ctx.setBlock({dim, mx, ground + 1, mz}, "minecraft:lapis_block"), "the marker");

    // MapItem::sampleMapData renders the whole map (128 x 128 texels around its center, texel
    // (i, j) = column (x - 64 + i, z - 64 + j), stored row by row in z) into the saved data, as the
    // server does for a map being updated
    bool sampled = MapItem::sampleMapData(*bs, 1, origin, origin, 128, 128, nullptr, &map, nullptr);
    std::vector<unsigned> pixels = *map.mPixels;
    ctx.require(pixels.size() == 128 * 128, "the map has 128 x 128 pixels");
    // pixel of a world column: (x - originX) + 64 per axis (scale 0), row-major in z or in x
    auto colourAt = [&](int x, int z, bool zRows) -> unsigned {
        int px = x - origin.x + 64, pz = z - origin.z + 64;
        if (px < 0 || px >= 128 || pz < 0 || pz >= 128) return 0;
        return pixels[static_cast<std::size_t>(zRows ? pz * 128 + px : px * 128 + pz)];
    };
    bool zRows  = colourAt(mx, mz, true) != colourAt(mx - 1, mz, true) && colourAt(mx, mz, true) != colourAt(mx + 1, mz, true);
    bool xRows  = colourAt(mx, mz, false) != colourAt(mx - 1, mz, false) && colourAt(mx, mz, false) != colourAt(mx + 1, mz, false);
    bool marker = zRows || xRows;
    unsigned probeColour  = colourAt((cx - 1) * 16 + 8, origin.z - 8, zRows || !xRows);
    unsigned plainsColour = colourAt(cx * 16 + 8, origin.z - 8, zRows || !xRows);
    unsigned desertColour = colourAt((cx + 1) * 16 + 8, origin.z - 8, zRows || !xRows);
    auto hex32 = [](unsigned v) {
        std::ostringstream o;
        o << std::hex << std::setw(8) << std::setfill('0') << v;
        return o.str();
    };
    // the map's colours: count and first texel of each (diagnostics of the layout)
    std::map<unsigned, std::pair<int, std::size_t>> histogram;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        auto& h = histogram.try_emplace(pixels[i], 0, i).first->second;
        ++h.first;
    }
    std::ostringstream colours;
    for (auto const& [c, h] : histogram) colours << " " << hex32(c) << " x" << h.first << " @" << (h.second % 128) << "," << (h.second / 128);
    gf::log().info("worldgen custom biome map: {} colours:{}", histogram.size(), colours.str());
    gf::log().info(
        "worldgen custom biome map: sampleMapData {}, origin {} {}, marker {}, grass map colours probe {} plains {} desert {} (ABGR)",
        sampled,
        origin.x,
        origin.z,
        marker ? (zRows ? "found (z rows)" : "found (x rows)") : "not found",
        hex32(probeColour),
        hex32(plainsColour),
        hex32(desertColour)
    );
    ctx.check(marker, "the lapis marker is on the map at its column");
    ctx.check(probeColour != 0 && plainsColour != 0 && desertColour != 0, "the three columns are rendered");
    ctx.check(desertColour != plainsColour, "a painted vanilla biome changes the map colour (control)");
    ctx.check(probeColour != plainsColour && probeColour != desertColour, "the custom biome has its own map colour");
});

// ScatteredOreFeature in memory: single blocks within 7 of the origin, only on its targets.
GF_REGISTER_SELFTEST("worldgen.scattered_ore", [](Context& ctx) {
    JsonT ore = JsonT::parse(R"({"type":"minecraft:scattered_ore","config":{"size":20,"discard_chance_on_air_exposure":0.0,
        "targets":[{"target":{"predicate_type":"minecraft:block_match","block":"minecraft:stone"},"state":{"Name":"minecraft:coal_ore"}}]}})");
    std::string first;
    for (int run = 0; run < 2; ++run) {
        auto world = flatMemoryWorld(4242);
        auto stone = gf::game::blockWith("minecraft:stone");
        for (int x = -8; x < 24; ++x)
            for (int z = -8; z < 24; ++z)
                for (int y = 4; y < 20; ++y) world->setBlock({x, y, z}, *stone);
        auto               region = world->region(0, 0, 1);
        jr::WorldgenRandom random(std::make_unique<jr::Xoroshiro128pp>(0));
        random.setFeatureSeed(random.setDecorationSeed(4242, 0, 0), 3, 6);
        ctx.require(gf::features::placeJson(ore, *region, {8, 12, 8}, random), "placed");
        int  coal = 0;
        bool near = true;
        for (int x = -8; x < 24; ++x)
            for (int z = -8; z < 24; ++z)
                for (int y = 0; y < 30; ++y)
                    if (world->getBlock({x, y, z}).getTypeName() == "minecraft:coal_ore") {
                        ++coal;
                        if (std::abs(x - 8) > 7 || std::abs(y - 12) > 7 || std::abs(z - 8) > 7) near = false;
                    }
        ctx.check(coal > 0 && coal <= 20, "1..20 ore blocks (" + std::to_string(coal) + ")");
        ctx.check(near, "every ore block within 7 of the origin");
        auto h = hex(worldHash(*world));
        if (run == 0) first = h;
        else ctx.check(h == first, "the same blocks for the same seed");
    }
    ctx.info("scattered ore hash " + first);
});

// spawnMob and the placement rules.
GF_REGISTER_SELFTEST("worldgen.spawn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5540, 8);
    auto* bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    gf::spawn::registerPlacement("minecraft:chicken", gf::spawn::PlacementRule::OnGround);
    bool   initRan = false;
    Actor* a       = gf::spawn::spawnMob(*bs, "minecraft:chicken", Vec3{box.minX + 2.5f, static_cast<float>(box.minY + 1), box.minZ + 2.5f},
                                   gf::spawn::SpawnReason::Command, [&](Actor&) { initRan = true; });
    ctx.require(a != nullptr, "a chicken spawned");
    ctx.check(initRan, "the init function ran");
    ctx.check(a->getTypeName() == "minecraft:chicken", "the right entity");
    a->remove();
    ctx.check(gf::spawn::spawnMob(*bs, "gf_test:no_such_entity", Vec3{box.minX + 2.5f, static_cast<float>(box.minY + 1), box.minZ + 2.5f},
                                  gf::spawn::SpawnReason::Command) == nullptr, "an unknown entity is refused");

    // NaturalSpawner#getRandomPosWithin's ChunkAccess#getHeight(WORLD_SURFACE): the highest
    // non-air block of a known column, compared with the engine's heightmap (which counts only
    // light-blocking blocks: a short grass on top is not in it)
    int      x = box.minX + 4, z = box.minZ + 4, top = box.minY + 6;
    BlockPos column{x, 0, z};
    ctx.require(ctx.setBlock({ctx.dimension(), x, top, z}, "minecraft:stone"), "a stone 6 above the ground");
    int ws     = gf::spawn::detail::worldSurfaceY(*bs, x, z);
    int engine = bs->getHeightmapPos(column).y;
    ctx.check(ws == top, "WORLD_SURFACE: the stone is the top block (" + std::to_string(ws) + ")");
    ctx.require(ctx.setBlock({ctx.dimension(), x, top + 1, z}, "minecraft:short_grass"), "a short grass on the stone");
    int ws2     = gf::spawn::detail::worldSurfaceY(*bs, x, z);
    int engine2 = bs->getHeightmapPos(column).y;
    ctx.check(ws2 == top + 1, "WORLD_SURFACE counts the short grass (" + std::to_string(ws2) + ")");
    gf::log().info(
        "worldgen spawn: WORLD_SURFACE top {} / {} (stone / short grass), the engine heightmap {} / {} (offset {} / {})",
        ws,
        ws2,
        engine,
        engine2,
        engine - ws,
        engine2 - ws2
    );
});

// The Blanket stage runs at surface time (TF's chunkBlanketing after buildSurface): the blanket
// sees the bare ground's WORLD_SURFACE_WG height, runs before the carvers and every feature (which
// see its blocks), gets the WorldGenRegion random and the chunk's biomes.
GF_REGISTER_SELFTEST("worldgen.blanket", [](Context& ctx) {
    constexpr std::int64_t seed   = 424242;
    auto const*            canopy = gf::game::blockWith("gf_test:fixture_block");
    auto const*            stone  = gf::game::blockWith("minecraft:stone");
    ctx.require(canopy && stone, "fixture blocks");
    wg::detail::Providers p;
    p.terrain               = [](wg::Chunk& c) { wg::detail::fallbackTerrain(c); };
    p.biomeSource           = [](int, int qy, int) { return qy < 0 ? std::string("gf_test:blanket_under") : std::string("gf_test:blanket_biome"); };
    p.biomeSourceGeneration = 0x7e570001;
    std::vector<std::string> order;
    int                      seenHeight = -1000;
    std::vector<std::string> seenBiomes;
    std::int64_t             forked     = 0;
    bool                     featureSaw = false;
    auto here = [](wg::Chunk const& c) { return c.chunkX() == 0 && c.chunkZ() == 0; };
    auto step = [&](char const* name) {
        return [&, name](wg::Chunk& c, wg::Region&) {
            if (here(c)) order.emplace_back(name);
        };
    };
    p.stages[wg::Stage::Surface].push_back({"gf_test.surface", 0, 0, step("surface")});
    p.stages[wg::Stage::Carvers].push_back({"gf_test.carvers", 0, 1, step("carvers")});
    p.blankets["gf_test:canopy"] = [&](wg::Chunk& c, wg::Region&) {
        if (!here(c)) return;
        order.emplace_back("blanket");
        if (auto* b = wg::ext::blanketContext()) {
            seenBiomes = b->chunkBiomes;
            if (b->regionRandom) forked = b->regionRandom->fork()->nextLong();
        }
        // CanopyBlanketProcessor: chunk.getHeight(WORLD_SURFACE_WG) + 14
        seenHeight = c.height(wg::Heightmap::WorldSurfaceWg, 3, 3);
        c.setBlock(3, seenHeight + 14, 3, *canopy);
    };
    p.stages[wg::Stage::Features].push_back({"gf_test.features", 0, 2, [&](wg::Chunk& c, wg::Region& r) {
                                                if (!here(c)) return;
                                                order.emplace_back("features");
                                                featureSaw = r.getBlock({3, 14, 3}).getTypeName() == "gf_test:fixture_block";
                                                for (int y = 1; y < 14; ++y) r.setBlock({3, y, 3}, *stone); // a tree trunk under the canopy
                                            }});
    wg::detail::MemoryWorld world(-32, 256, seed);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) {
            auto&                       c = wg::detail::asChunk(world.chunk(cx, cz));
            wg::detail::ChunkOnlyRegion region(c, seed, -1, p.biomeSource);
            wg::detail::generateChunk(p, c, region, seed);
        }
    auto region = world.region(0, 0, 1, p.biomeSource);
    wg::detail::decorateChunk(p, &wg::detail::asChunk(world.chunk(0, 0)), *region, seed);
    ctx.check(wg::ext::blanketContext() == nullptr, "no blanket context outside a blanket call");
    std::string seq;
    for (auto const& s : order) seq += (seq.empty() ? "" : " ") + s;
    ctx.check(seq == "surface blanket carvers features", "surface -> blanket -> carvers -> features (" + seq + ")");
    ctx.check(seenHeight == 0, "the blanket sees the bare ground (WORLD_SURFACE_WG height 0, got " + std::to_string(seenHeight) + ")");
    ctx.check(featureSaw, "the features see the blanket's blocks");
    ctx.check(world.getBlock({3, 14, 3}).getTypeName() == "gf_test:fixture_block" && world.getBlock({3, 27, 3}).isAir(),
              "the canopy is 14 above the ground, not above the trunk placed later");
    auto expect = wg::detail::worldGenRegionRandom(seed, 0, 0)->fork()->nextLong();
    ctx.check(forked == expect, "the blanket forks the surface step's WorldGenRegion random");
    ctx.check(std::find(seenBiomes.begin(), seenBiomes.end(), "gf_test:blanket_biome") != seenBiomes.end()
                  && std::find(seenBiomes.begin(), seenBiomes.end(), "gf_test:blanket_under") != seenBiomes.end(),
              "the blanket gets the biomes of the chunk's sections");
    gf::log().info("worldgen blanket: {} at surface time, height {}", seq, seenHeight);
});

// Every decoration step runs the Pieces stage before its features, with the decoration context:
// step, decoration seed and the structure randoms of Java's setFeatureSeed(decorationSeed, index, step).
GF_REGISTER_SELFTEST("worldgen.pieces", [](Context& ctx) {
    constexpr std::int64_t seed = 8675309;
    wg::detail::Providers  p;
    p.terrain = [](wg::Chunk& c) { wg::detail::fallbackTerrain(c); };
    std::vector<int>          steps;
    std::vector<std::int64_t> firsts;
    std::int64_t              decoSeed = 0;
    bool                      context  = true;
    p.stages[wg::Stage::Pieces].push_back({"gf_test.pieces", 0, 0, [&](wg::Chunk& c, wg::Region&) {
                                              auto const* d = wg::ext::decorationContext();
                                              if (!d || d->chunkX != c.chunkX() || d->chunkZ != c.chunkZ()) {
                                                  context = false;
                                                  return;
                                              }
                                              steps.push_back(d->step);
                                              decoSeed = d->decorationSeed;
                                              firsts.push_back(d->structureRandom(2)->nextLong());
                                          }});
    p.stages[wg::Stage::Features].push_back({"gf_test.features", 0, 1, [&](wg::Chunk&, wg::Region&) { steps.push_back(100); }});
    wg::detail::MemoryWorld world(-32, 64, seed);
    for (int cx = 4; cx <= 6; ++cx)
        for (int cz = -6; cz <= -4; ++cz) {
            auto&                       c = wg::detail::asChunk(world.chunk(cx, cz));
            wg::detail::ChunkOnlyRegion region(c, seed, -1);
            wg::detail::generateChunk(p, c, region, seed);
        }
    auto region = world.region(5, -5, 1);
    wg::detail::decorateChunk(p, &wg::detail::asChunk(world.chunk(5, -5)), *region, seed);
    ctx.check(context && wg::ext::decorationContext() == nullptr, "the context is set during the Pieces calls only");
    std::vector<int> want{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};
    ctx.check(steps == want, "Pieces at every step 0..10, then the Features stage");
    jr::WorldgenRandom w(std::make_unique<jr::Xoroshiro128pp>(0));
    ctx.check(decoSeed == w.setDecorationSeed(seed, 80, -80), "the decoration seed of the chunk");
    bool seeds = firsts.size() == 11;
    for (int l = 0; seeds && l < 11; ++l) {
        jr::WorldgenRandom s(std::make_unique<jr::Xoroshiro128pp>(0));
        s.setFeatureSeed(decoSeed, 2, l);
        if (s.nextLong() != firsts[static_cast<std::size_t>(l)]) seeds = false;
    }
    ctx.check(seeds, "structureRandom(index) is setFeatureSeed(decorationSeed, index, step)");
});

// CarvingMaskPlacement: the blocks the carvers carved, kept from the Carvers stage until the
// chunk's decoration, streamed in CarvingMask#stream's order.
GF_REGISTER_SELFTEST("worldgen.carving_mask", [](Context& ctx) {
    constexpr std::int64_t seed = 99;
    int                    cx = 77, cz = -91;
    wg::detail::Providers  p;
    p.biomeSource                    = [](int, int, int) { return std::string("gf_test:carver_biome"); };
    p.biomeSourceGeneration          = 0x7e570002;
    p.carvers["gf_test:carver_one"] = [&](wg::Chunk&, wg::Region&, wg::RandomSource&) {
        auto* c = wg::ext::carverContext();
        if (!c || !c->mask || c->sourceChunkX != cx || c->sourceChunkZ != cz) return;
        c->mask->set(15, 7, 15);
        c->mask->set(1, 5, 2);
        c->mask->set(0, 3, 0);
    };
    auto lists = [](std::string const&) { return std::vector<std::string>{"gf_test:carver_one"}; };
    wg::detail::MemoryWorld world(-32, 64, seed);
    auto&                   chunk  = wg::detail::asChunk(world.chunk(cx, cz));
    auto                    region = world.region(cx, cz, 0);
    wg::detail::applyCarvers(p, chunk, *region, seed, lists);
    gf::features::PlacementFn fn;
    gf::features::detail::withRegistries([&](auto const& r) {
        if (auto it = r.placements.find("minecraft:carving_mask"); it != r.placements.end()) fn = it->second;
        return 0;
    });
    ctx.require(static_cast<bool>(fn), "minecraft:carving_mask is registered");
    jr::Xoroshiro128pp       random(1);
    JsonT                    config = {{"type", "minecraft:carving_mask"}};
    auto                     out    = fn({*region, random, config, ""}, BlockPos{cx * 16, 0, cz * 16});
    std::vector<BlockPos>    want{{cx * 16, 3, cz * 16}, {cx * 16 + 1, 5, cz * 16 + 2}, {cx * 16 + 15, 7, cz * 16 + 15}};
    bool                     same = out.size() == want.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) same = out[i].x == want[i].x && out[i].y == want[i].y && out[i].z == want[i].z;
    ctx.check(same, "the carved blocks in ascending mask index (" + std::to_string(out.size()) + " positions)");
    wg::detail::dropCarvingMask(-1, cx, cz);
    ctx.check(fn({*region, random, config, ""}, BlockPos{cx * 16, 0, cz * 16}).empty(), "nothing once the chunk's decoration dropped the mask");
});

// count_on_every_layer treats flowing water and lava like Java's Blocks.WATER / LAVA (empty).
GF_REGISTER_SELFTEST("worldgen.layer_fluids", [](Context& ctx) {
    auto const* stone   = gf::game::blockWith("minecraft:stone");
    auto const* flowing = gf::game::blockWith("minecraft:flowing_water");
    ctx.require(stone && flowing, "blocks");
    wg::detail::MemoryWorld world(-32, 64, 5);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    auto& c = wg::detail::asChunk(world.chunk(cx, cz));
                    for (int y = 0; y < 4; ++y) c.setBlock(x, y, z, *stone);
                    for (int y = 4; y < 6; ++y) c.setBlock(x, y, z, *flowing);
                }
    gf::features::PlacementFn fn;
    gf::features::detail::withRegistries([&](auto const& r) {
        if (auto it = r.placements.find("minecraft:count_on_every_layer"); it != r.placements.end()) fn = it->second;
        return 0;
    });
    ctx.require(static_cast<bool>(fn), "minecraft:count_on_every_layer is registered");
    auto               region = world.region(0, 0, 1);
    jr::Xoroshiro128pp random(5);
    JsonT              config = {{"type", "minecraft:count_on_every_layer"}, {"count", 1}};
    auto               out    = fn({*region, random, config, ""}, BlockPos{0, 0, 0});
    ctx.check(out.size() == 1 && out[0].y == 4, "one layer, on the stone under the flowing water (y " + (out.empty() ? std::string("-") : std::to_string(out[0].y)) + ")");
});

// BiomeManager's zoom: region biomes at block positions read the quart Java's getBiome picks.
GF_REGISTER_SELFTEST("worldgen.biome_zoom", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace bm = gf::core::biome;
    ctx.check(bm::obfuscateSeed(42) == -4111196313959201555LL, "BiomeManager.obfuscateSeed(42) (Java oracle)");
    ctx.check(bm::zoom(bm::obfuscateSeed(42), 7, 64, -9) == bm::Quart{1, 15, -2}, "the quart of (7, 64, -9) for seed 42 (Java oracle)");
    // an in-memory world with a quart checkerboard: biome(pos) is the noise biome of zoom(pos)
    constexpr std::int64_t seed = 42;
    wg::detail::MemoryWorld world(-32, 64, seed);
    auto quartBiome = [](int qx, int qy, int qz) { return ((qx + qy + qz) & 1) ? std::string("gf_test:odd") : std::string("gf_test:even"); };
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) {
            auto& c = wg::detail::asChunk(world.chunk(cx, cz));
            for (int lx = 0; lx < 16; lx += 4)
                for (int lz = 0; lz < 16; lz += 4)
                    for (int y = -32; y < 64; y += 4) c.setBiome(lx, y, lz, quartBiome((cx * 16 + lx) >> 2, y >> 2, (cz * 16 + lz) >> 2));
        }
    auto region = world.region(0, 0, 1);
    bool match = true;
    int  moved = 0, total = 0;
    for (int x = -8; x < 24; x += 3)
        for (int z = -8; z < 24; z += 3)
            for (int y = -20; y < 50; y += 7) {
                auto q    = bm::zoom(bm::obfuscateSeed(seed), x, y, z);
                auto want = quartBiome(q.x, std::clamp(q.y, -8, 15), q.z);
                if (region->biome({x, y, z}) != want) match = false;
                if (want != quartBiome(x >> 2, y >> 2, z >> 2)) ++moved;
                ++total;
            }
    ctx.check(match, "Region::biome is BiomeManager#getBiome over the quart biomes");
    ctx.check(moved > 0, "the zoom moves some positions to a neighbouring quart (" + std::to_string(moved) + " of " + std::to_string(total) + ")");

    // the same through a BlockSource region over live chunks
    auto box = co_await ctx.flatArea(5570, 8);
    auto* bs = ctx.region();
    ctx.require(bs != nullptr, "test region");
    wg::detail::SourceRegionAdapter view(*bs, box.minX >> 4, box.minZ >> 4, 0, bs->getMinHeight(), bs->getMaxHeight(), 1234, ctx.dimension());
    BlockPos at{box.minX + 1, box.minY + 2, box.minZ + 3};
    auto     q = bm::zoom(bm::obfuscateSeed(1234), at.x, at.y, at.z);
    ctx.check(view.biome(at) == view.noiseBiome(q.x, q.y, q.z), "the BlockSource region zooms too");
    gf::log().info("worldgen biome zoom: {} of {} sample positions read a neighbouring quart", moved, total);
});
