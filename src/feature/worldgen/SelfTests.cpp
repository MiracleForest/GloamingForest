// Selftests of the generator pipeline, the feature interpreter and the spawn engine (F5, prefix
// "worldgen.", testPos dx 5500..5599). The determinism goldens are the same on the server and the
// client build: the fixture feature and the in-memory pipeline must hash identically on both.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/MapDataManager.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/JRand.h"
#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
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
    // biomes) and across the chunk's borders, through writeBiomes: every block gets the biome
    // BiomeManager's zoom picks (Java's per-block getBiome), the generation reads the raw quarts
    // back through Chunk::biome; timed, then every block's biome restored
    int  minY = bs->getMinHeight(), maxY = bs->getMaxHeight();
    int  qMin = minY >> 2, qMax = (maxY - 1) >> 2;
    auto layered = [original](int qx, int qy, int qz) {
        if (qy < 0) return std::string("minecraft:desert");
        return ((qx + qz + qy) % 7 == 0) ? std::string("minecraft:forest") : original;
    };
    struct RestoreBlocks { // every block's biome as it was, also when a check aborts
        LevelChunk&               lc;
        int                       minY, maxY;
        std::vector<Biome const*> saved;
        ~RestoreBlocks() {
            std::size_t i = 0;
            for (int y = minY; y < maxY; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x, ++i) {
                        ChunkBlockPos p{static_cast<uchar>(x), ChunkLocalHeight{static_cast<short>(y - minY)}, static_cast<uchar>(z)};
                        if (&lc.getBiome(p) != saved[i]) lc._setBiome(*saved[i], p, false);
                    }
        }
    } restoreBlocks{*lc, minY, maxY, {}};
    for (int y = minY; y < maxY; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                restoreBlocks.saved.push_back(&lc->getBiome(ChunkBlockPos{static_cast<uchar>(x), ChunkLocalHeight{static_cast<short>(y - minY)}, static_cast<uchar>(z)}));
    constexpr std::int64_t seed = 20241102;
    wg::detail::Providers  p;
    p.biomeSource = [&](int qx, int qy, int qz) { return layered(qx, qy, qz); };
    wg::detail::LevelChunkAdapter generating(*lc, dim.getBiomeRegistry(), minY, maxY);
    auto                          start = std::chrono::steady_clock::now();
    auto                          distinct = wg::detail::writeBiomes(p, generating, seed);
    double                        ms    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::set<std::string> layers{"minecraft:desert", "minecraft:forest", original};
    ctx.check(std::set<std::string>(distinct.begin(), distinct.end()) == layers, "the chunk's distinct raw biomes (" + std::to_string(distinct.size()) + ")");
    bool raw = true;
    for (int x = 0; x < 16 && raw; ++x)
        for (int z = 0; z < 16 && raw; ++z)
            for (int qy = qMin; qy <= qMax && raw; ++qy) {
                auto want = layered((lc->mPosition->x * 16 + x) >> 2, qy, (lc->mPosition->z * 16 + z) >> 2);
                if (generating.biome(x, qy * 4, z) != want || generating.biome(x, std::min(qy * 4 + 3, maxY - 1), z) != want) raw = false;
            }
    ctx.check(raw, "during the generation Chunk::biome reads the raw quart of every block");
    auto zoomSeed = gf::core::biome::obfuscateSeed(seed);
    bool zoomed   = true;
    int  moved = 0, total = 0;
    for (int y = minY; y < maxY && zoomed; ++y)
        for (int z = 0; z < 16 && zoomed; ++z)
            for (int x = 0; x < 16 && zoomed; ++x) {
                int  bx = lc->mPosition->x * 16 + x, bz = lc->mPosition->z * 16 + z;
                auto q  = gf::core::biome::zoom(zoomSeed, bx, y, bz);
                auto want = layered(q.x, std::clamp(q.y, qMin, qMax), q.z);
                auto have = wg::detail::biomeIdOf(lc->getBiome(ChunkBlockPos{static_cast<uchar>(x), ChunkLocalHeight{static_cast<short>(y - minY)}, static_cast<uchar>(z)}));
                if (have != want) zoomed = false;
                if (want != layered(bx >> 2, y >> 2, bz >> 2)) ++moved;
                ++total;
            }
    ctx.check(zoomed, "the chunk stores BiomeManager's zoomed biome at every block");
    ctx.check(moved > 0, "the zoom moves blocks to a neighbouring quart (" + std::to_string(moved) + " of " + std::to_string(total) + ")");
    std::ostringstream o;
    o << "worldgen biomes: height-varying biomes written into a live chunk in " << std::fixed << std::setprecision(3) << ms << " ms (zoomed per block, "
      << moved << " of " << total << " blocks read a neighbouring quart)";
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

namespace {

/// A special spawn function of the tests, registered after F6's structureSpecialSpawns
/// (gf::spawn::onSpecialSpawn has no removal: registered once, it answers only while a test has it
/// active, inside the test's box): a monster list, an empty creature list and an ambient list;
/// nullopt otherwise. It shows the order of the chain: where F6 answers it is not asked.
struct SpecialProbe {
    std::atomic<bool>  active{false};
    std::mutex         mutex;
    int                dim = 0;
    gf::core::BlockBox box{};
};
SpecialProbe& specialProbe() {
    static SpecialProbe p;
    return p;
}
std::vector<gf::spawn::SpawnEntry> probeMonsters() { return {{"minecraft:spider", 10, 4, 4}, {"minecraft:zombie", 5, 1, 2}}; }
std::vector<gf::spawn::SpawnEntry> probeAmbient() { return {{"minecraft:parrot", 3, 1, 1}}; }

/// Activates the probe over `box` of `dim` until destroyed (also when the test aborts).
struct SpecialProbeScope {
    SpecialProbeScope(int dim, gf::core::BlockBox box) {
        static std::once_flag once;
        std::call_once(once, [] {
            gf::spawn::onSpecialSpawn([](BlockSource& region, BlockPos const& pos, std::string const& category) -> std::optional<std::vector<gf::spawn::SpawnEntry>> {
                auto& p = specialProbe();
                if (!p.active.load(std::memory_order_acquire)) return std::nullopt;
                {
                    std::lock_guard lock(p.mutex);
                    if (static_cast<int>(region.getDimensionId()) != p.dim || !p.box.contains(pos.x, pos.y, pos.z)) return std::nullopt;
                }
                if (category == "monster") return probeMonsters();
                if (category == "creature") return std::vector<gf::spawn::SpawnEntry>{};
                if (category == "ambient") return probeAmbient();
                return std::nullopt;
            });
        });
        auto& p = specialProbe();
        {
            std::lock_guard lock(p.mutex);
            p.dim = dim;
            p.box = box;
        }
        p.active.store(true, std::memory_order_release);
    }
    ~SpecialProbeScope() { specialProbe().active.store(false, std::memory_order_release); }
    SpecialProbeScope(SpecialProbeScope const&)            = delete;
    SpecialProbeScope& operator=(SpecialProbeScope const&) = delete;
};

/// "[id weight min-max, ...]"
std::string entriesText(std::vector<gf::spawn::SpawnEntry> const& list) {
    std::string s;
    for (auto const& e : list)
        s += (s.empty() ? "" : ", ") + e.entityId + " " + std::to_string(e.weight) + " " + std::to_string(e.minCount) + "-" + std::to_string(e.maxCount);
    return "[" + s + "]";
}

} // namespace

// TF's landmark rules in the spawner and the feature placements, on F6's real starts. The starts are
// forced with landmark::ext::ScopedStart far from every test row, where no chunk is loaded (stored,
// no block placed), and removed when the test ends (a start they replaced is put back), in every world.
// ScopedStart cleans up only on a normal scope exit: if the process crashes or is killed, or the world
// is saved, while the scopes are open, the gf_test fixture starts and a fake dark tower site about
// 200000 blocks out can stay in the world's landmark data (F6's store). The e2e world GF-Test is made
// afresh for every run; in a kept development world run this test only where that is acceptable.
// - NaturalSpawner#mobsAt (spawn::detail::spawnListAt): the special spawn functions in registration
//   order, the first list wins whole, nullopt passes to the next and then to the biome's list. F6's
//   EntityEvents#structureSpecialSpawns end to end:
//   - gf_test:landmark_fixture (ControlledSpawns, a protected shell and a porch): its
//     Generator::spawnIndex gives labelled monster list 1 on the shell's upper floor, list 0 on the
//     porch and none on the shell's ground floor (the biome's monsters); its empty ambient list in
//     the chunks it reaches, ConfigurableSpawns#getSpawnableList's empty list for creatures;
//   - gf_test:landmark_fixture_open (no ControlledSpawns): Structure#spawnOverrides, monsters "piece"
//     (in the shell and the porch, the biome's beside them), ambient "full" (empty in the start box,
//     the biome's outside it), no override for creatures (the biome's);
//   - the tests' own special spawn function, registered after F6's, shows the order: where F6
//     answers it is not asked, where F6 passes it is.
// - twilightforest:no_structure (AvoidLandmarkModifier) over the locked fixture (surface decorations
//   and vegetation not allowed, underground ones allowed, clearance radius 0) and TF's dark tower
//   (clearance radius 1 chunk, no surface decorations); a dark tower site without pieces (no
//   generator yet: StructureStart#isValid false) blocks nothing, which F6's featureBlocked decides.
// Then concurrent spawn table lookups.
GF_REGISTER_SELFTEST("worldgen.landmark_rules", [](Context& ctx) {
    namespace sd = gf::spawn::detail;
    namespace lm = gf::landmark;
    constexpr char kFixture[] = "gf_test:landmark_fixture";
    constexpr char kOpen[]    = "gf_test:landmark_fixture_open";
    constexpr char kTower[]   = "twilightforest:dark_tower";
    static constexpr char kBiome[] = "gf_test:landmark_rules";
    auto*          bs         = ctx.region();
    ctx.require(bs != nullptr, "test region");
    auto row = ctx.testPos(5590, 2);
    gf::spawn::Table table;
    table.byCategory["monster"]       = {{"minecraft:skeleton", 100, 1, 1}};
    table.byCategory["ambient"]       = {{"minecraft:bat", 10, 8, 8}};
    table.byCategory["creature"]      = {{"minecraft:pig", 10, 4, 4}};
    table.byCategory["water_ambient"] = {{"minecraft:cod", 15, 3, 6}};
    gf::spawn::setBiomeSpawns(kBiome, table);
    struct ClearTable {
        ClearTable()                             = default;
        ClearTable(ClearTable const&)            = delete;
        ClearTable& operator=(ClearTable const&) = delete;
        ~ClearTable() { gf::spawn::setBiomeSpawns(kBiome, gf::spawn::Table{}); }
    } clearTable;
    auto list = [&](BlockPos const& p, char const* category) { return entriesText(sd::spawnListAt(*bs, p, category, kBiome)); };
    // a labelled list of a descriptor ("[id weight min-max, ...]")
    auto labelled = [](char const* key, char const* label) {
        std::vector<gf::spawn::SpawnEntry> converted;
        if (auto const* d = lm::descriptor(key))
            for (auto const& e : d->controlledSpawns.value("labelled_monster_spawns", JsonT::object()).value(label, JsonT::array()))
                converted.push_back({e.value("type", std::string{}), e.value("weight", 1), e.value("minCount", 1), e.value("maxCount", 1)});
        return entriesText(converted);
    };
    std::string const biomeMonsters = "[minecraft:skeleton 100 1-1]";

    // twilightforest:no_structure (AvoidLandmarkModifier)
    gf::features::PlacementFn fn;
    gf::features::detail::withRegistries([&](auto const& r) {
        if (auto it = r.placements.find("twilightforest:no_structure"); it != r.placements.end()) fn = it->second;
        return 0;
    });
    ctx.require(static_cast<bool>(fn), "twilightforest:no_structure is registered");
    jr::Xoroshiro128pp random(1);
    auto config = [](bool surface, bool underground, bool vegetation, int clearance, JsonT allowed = JsonT::array()) {
        return JsonT{{"type", "twilightforest:no_structure"},
                     {"occupies_surface", surface},
                     {"occupies_underground", underground},
                     {"occupies_vegetation", vegetation},
                     {"additional_clearance", clearance},
                     {"structures_allowed", std::move(allowed)}};
    };
    auto placedAt = [&](JsonT const& cfg, BlockPos const& p) {
        wg::detail::SourceRegionAdapter view(*bs, p.x >> 4, p.z >> 4, 0, bs->getMinHeight(), bs->getMaxHeight(), 0, row.dim);
        return !fn({view, random, cfg, ""}, p).empty();
    };

    // the middle of a chunk: the fixtures' boxes (x +-3, z -3..+6) stay inside it
    gf::core::GlobalPos center{row.dim, ((row.x + 200000) & ~15) + 8, 64, (row.z & ~15) + 6};
    auto startsOf = [&](int dx, char const* key) {
        auto all = lm::startsIntersecting(center.dim, {(center.x + dx) >> 4, center.z >> 4});
        return std::count_if(all.begin(), all.end(), [&](lm::Start const& s) { return s.key == key; });
    };
    auto before = std::array{startsOf(0, kFixture), startsOf(-160, kOpen), startsOf(160, kTower)};
    {
        lm::ext::ScopedStart start(kFixture, center);
        ctx.require(start && start->generated && start->pieceBoxes.size() == 2, "F6 forced its fixture landmark (shell and porch)");
        lm::ChunkPos chunk{center.x >> 4, center.z >> 4};
        auto         starts = lm::startsIntersecting(center.dim, chunk);
        ctx.check(std::any_of(starts.begin(), starts.end(), [&](lm::Start const& s) { return s.key == kFixture; }), "startsIntersecting (F6) lists the start");

        // NaturalSpawner#mobsAt: EntityEvents#structureSpecialSpawns (F6's special spawn) before the biome
        BlockPos inside{center.x, center.y + 1, center.z}; // the shell's ground floor
        BlockPos upper{center.x, center.y + 5, center.z};  // the shell's upper floor
        BlockPos porch{center.x, center.y, center.z + 5};  // the porch
        auto     upperList = labelled(kFixture, "1");
        ctx.check(upperList != "[]", "the fixture has labelled monster list 1 " + upperList);
        ctx.check(list(upper, "monster") == upperList, "monsters on the shell's upper floor: labelled list 1 replaces the biome's whole, weights and group sizes kept " + list(upper, "monster"));
        ctx.check(list(porch, "monster") == labelled(kFixture, "0"), "monsters on the porch: labelled list 0 " + list(porch, "monster"));
        ctx.check(list(inside, "monster") == biomeMonsters, "monsters on the shell's ground floor: spawn index -1, no controlled list: the biome's " + list(inside, "monster"));
        ctx.check(!lm::ext::potentialSpawns(center.dim, "monster", inside.x, inside.y, inside.z).has_value(), "F6's gatherPotentialSpawns agrees: null there");
        ctx.check(list(inside, "ambient") == "[]", "ambient in the start's chunk: the fixture's empty ambient list replaces the bats " + list(inside, "ambient"));
        // the corner of the start's chunk, outside the start's box (the shell and porch span x +-3, z -3..+6)
        BlockPos corner{chunk.x * 16, center.y, chunk.z * 16};
        ctx.check(list(corner, "creature") == "[]", "creatures anywhere in the start's chunk: ConfigurableSpawns#getSpawnableList's empty default " + list(corner, "creature"));
        {
            // F6 registered its special spawn first: where it answers, the later functions are not asked
            SpecialProbeScope active(center.dim, {chunk.x * 16, -64, chunk.z * 16, chunk.x * 16 + 15, 320, chunk.z * 16 + 15});
            ctx.check(list(inside, "ambient") == "[]", "F6's ambient list wins over a later special spawn " + list(inside, "ambient"));
            ctx.check(list(upper, "monster") == upperList, "F6's labelled list wins over a later special spawn " + list(upper, "monster"));
            ctx.check(list(inside, "monster") == entriesText(probeMonsters()), "F6 passes on the ground floor's monsters: the next special spawn answers " + list(inside, "monster"));
        }
        BlockPos far{center.x + 400, center.y, center.z};
        ctx.check(list(far, "ambient") == "[minecraft:bat 10 8-8]" && list(far, "monster") == biomeMonsters, "outside the start's chunks: the biome's lists");

        ctx.check(!placedAt(config(true, false, false, 0), inside), "a surface feature in the shell is removed");
        ctx.check(placedAt(config(false, true, false, 0), inside), "an underground feature is kept (the fixture allows underground decorations)");
        ctx.check(!placedAt(config(false, false, true, 0), inside), "a vegetation feature is removed (allow_biome_vegetation false)");
        ctx.check(placedAt(config(true, false, false, 0, JsonT::array({kFixture})), inside), "structures_allowed lifts it");
        BlockPos beside{center.x + 5, center.y + 1, center.z}; // 2 east of the shell (BoundingBoxUtils#greatestAxalDistance)
        ctx.check(placedAt(config(true, false, false, 1), beside) && !placedAt(config(true, false, false, 2), beside), "2 blocks beside the shell: kept with additional_clearance 1, removed with 2");
        ctx.check(placedAt(config(true, true, true, 8), far), "far from the start every feature is kept");

        // Structure#spawnOverrides: the open fixture, 10 chunks west (no ControlledSpawns)
        {
            gf::core::GlobalPos  openAt{row.dim, center.x - 160, 64, center.z};
            lm::ext::ScopedStart open(kOpen, openAt);
            ctx.require(open && open->generated && open->pieceBoxes.size() == 2, "F6 forced its open fixture landmark");
            std::string const overrideMonsters = "[minecraft:witch 3 1-1, minecraft:creeper 7 2-4]";
            BlockPos          shell{openAt.x, openAt.y + 1, openAt.z};
            BlockPos          openPorch{openAt.x, openAt.y, openAt.z + 5};
            BlockPos          besideShell{openAt.x + 5, openAt.y + 1, openAt.z}; // in the start's chunk, in no piece
            BlockPos          openCorner{(openAt.x >> 4) * 16, openAt.y, (openAt.z >> 4) * 16};
            ctx.check(list(shell, "monster") == overrideMonsters, "monsters in the shell: the \"piece\" override " + list(shell, "monster"));
            ctx.check(list(openPorch, "monster") == overrideMonsters, "monsters on the porch: the \"piece\" override " + list(openPorch, "monster"));
            ctx.check(list(besideShell, "monster") == biomeMonsters, "monsters beside the pieces: the biome's " + list(besideShell, "monster"));
            ctx.check(list(shell, "ambient") == "[]", "ambient in the start box: the empty \"full\" override " + list(shell, "ambient"));
            ctx.check(list(openCorner, "ambient") == "[minecraft:bat 10 8-8]", "ambient outside the start box: the biome's " + list(openCorner, "ambient"));
            ctx.check(list(shell, "creature") == "[minecraft:pig 10 4-4]", "creatures: no override, the biome's " + list(shell, "creature"));
            ctx.check(list(shell, "water_ambient") == "[minecraft:cod 15 3-6]", "a category no special spawn answers: the biome's " + list(shell, "water_ambient"));
            {
                SpecialProbeScope active(openAt.dim, {openCorner.x, -64, openCorner.z, openCorner.x + 15, 320, openCorner.z + 15});
                ctx.check(list(shell, "monster") == overrideMonsters, "the override wins over a later special spawn " + list(shell, "monster"));
                ctx.check(list(shell, "creature") == "[]", "F6 passes on creatures: the next special spawn's empty list " + list(shell, "creature"));
            }
        }

        // StructureStart#isValid: TF's dark tower, 10 chunks east. Without its generator (before W1) F6
        // reserves the site: no pieces, the clearance square as its box, and Java skips such a start;
        // with one the start has pieces and its clearance (radius 1 chunk) removes surface features
        gf::core::GlobalPos  towerAt{row.dim, center.x + 160, 64, center.z};
        lm::ext::ScopedStart tower(kTower, towerAt);
        ctx.require(static_cast<bool>(tower), "F6 forced a dark tower start");
        auto const& tb = tower->box;
        BlockPos    middle{tb.minX + (tb.maxX - tb.minX + 1) / 2, towerAt.y, tb.minZ + (tb.maxZ - tb.minZ + 1) / 2}; // BoundingBox#getCenter
        bool        f6Blocks = lm::ext::featureBlocked(row.dim, middle.x, middle.y, middle.z, {.surface = true});
        if (tower->pieceBoxes.empty()) {
            ctx.check(placedAt(config(true, false, false, 0), middle) && !f6Blocks, "the dark tower's site without pieces blocks no surface feature (StructureStart#isValid)");
            ctx.check(list(middle, "monster") == biomeMonsters, "nor changes the spawns there " + list(middle, "monster"));
        } else {
            ctx.check(!placedAt(config(true, false, false, 0), middle) && f6Blocks, "the dark tower's start (with pieces) removes a surface feature at its center");
            ctx.check(placedAt(config(true, false, false, 0, "#twilightforest:landmark"), middle), "#twilightforest:landmark in structures_allowed lifts it");
            // its controlled spawns through F6: the labelled list of the pieces' spawn index at the
            // center, or (index -1) the dark tower's empty "full" monster override; never the biome's
            std::set<std::string> lists{"[]"};
            if (auto const* d = lm::descriptor(kTower)) {
                // a local: items() of a temporary would dangle in the range-for
                JsonT const labels = d->controlledSpawns.value("labelled_monster_spawns", JsonT::object());
                for (auto const& entry : labels.items()) lists.insert(labelled(kTower, entry.key().c_str()));
            }
            ctx.check(lists.contains(list(middle, "monster")), "monsters at the dark tower: one of its labelled lists or its override " + list(middle, "monster"));
            ctx.check(list(middle, "creature") == "[]", "creatures at the dark tower: none " + list(middle, "creature"));
        }
        ctx.check(placedAt(config(false, true, false, 0), middle), "an underground feature is kept at the dark tower");
        gf::log().info("worldgen landmark rules: forced starts checked (fixtures, {} with {} pieces)", kTower, tower->pieceBoxes.size());
    }
    // ScopedStart removed the forced starts (any start they replaced is back)
    auto after = std::array{startsOf(0, kFixture), startsOf(-160, kOpen), startsOf(160, kTower)};
    ctx.check(after == before, "the forced starts are gone after the test (fixture, open fixture, dark tower: " + std::to_string(after[0]) + " " + std::to_string(after[1]) + " " + std::to_string(after[2]) + " left, " + std::to_string(before[0]) + " " + std::to_string(before[1]) + " " + std::to_string(before[2]) + " before)");

    // tableOf from several threads while tables are registered (no data race, the same result)
    std::atomic<int>         mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i) {
                auto tbl = sd::tableOf("gf_test:race_biome_" + std::to_string((i + t) % 16));
                if (!tbl.byCategory.empty() && tbl.byCategory.begin()->first != "monster") ++mismatches;
            }
        });
    for (int i = 0; i < 16; ++i) {
        gf::spawn::Table tbl;
        tbl.byCategory["monster"] = {{"minecraft:zombie", 1, 1, 1}};
        gf::spawn::setBiomeSpawns("gf_test:race_biome_" + std::to_string(i), tbl);
    }
    for (auto& t : threads) t.join();
    ctx.check(mismatches.load() == 0, "concurrent spawn table lookups");
    ctx.check(sd::tableOf("gf_test:race_biome_3").byCategory.contains("monster"), "a registered table replaces the imported one");
});

// The block and biome tags world generation and the spawner read, against F9's gf::tags: TF's tag
// data (Java ids renamed to the game's) and its vanilla bridge of the Java 1.21.1 tags, with nothing
// built into F5; BlockState#canBeReplaced with TF's replaceable blocks.
GF_REGISTER_SELFTEST("worldgen.tags", [](Context& ctx) {
    namespace sd = gf::spawn::detail;
    struct Case {
        char const* block;
        char const* tag;
        bool        member;
    };
    static constexpr Case cases[] = {
        // vanilla tags gf::tags bridges, with TF's members from its data
        {"minecraft:grass_block", "minecraft:dirt", true},
        {"minecraft:dirt_with_roots", "minecraft:dirt", true}, // Java's rooted_dirt
        {"twilightforest:uberous_soil", "minecraft:dirt", true},
        {"minecraft:stone", "minecraft:dirt", false},
        {"minecraft:deepslate", "minecraft:base_stone_overworld", true},
        {"minecraft:dirt", "minecraft:base_stone_overworld", false},
        {"minecraft:granite", "minecraft:stone_ore_replaceables", true},
        {"minecraft:deepslate", "minecraft:stone_ore_replaceables", false},
        {"minecraft:oak_log", "minecraft:logs", true},
        {"twilightforest:twilight_oak_log", "minecraft:logs", true},
        {"minecraft:oak_planks", "minecraft:logs", false},
        {"minecraft:oak_leaves", "minecraft:replaceable_by_trees", true},
        {"twilightforest:canopy_leaves", "minecraft:replaceable_by_trees", true},
        {"twilightforest:mayapple", "minecraft:replaceable_by_trees", true},
        {"minecraft:short_grass", "minecraft:replaceable_by_trees", true},
        {"minecraft:stone", "minecraft:replaceable_by_trees", false},
        {"minecraft:cobblestone_wall", "minecraft:walls", true},
        {"twilightforest:wrought_iron_fence", "minecraft:walls", true},
        {"twilightforest:canopy_fence_gate", "minecraft:fence_gates", true},
        // TF's own tags: the data's Java ids are the game's after F9's renaming (snow_block -> snow)
        {"minecraft:snow", "twilightforest:carver_replaceables", true},
        {"minecraft:stone", "twilightforest:carver_replaceables", true},
        {"minecraft:obsidian", "twilightforest:carver_replaceables", false},
        // vanilla tags with nested tags (the bridge's includes bring TF's members from its data)
        {"minecraft:oak_fence", "minecraft:fences", true},
        {"minecraft:nether_brick_fence", "minecraft:fences", true},
        {"twilightforest:twilight_oak_fence", "minecraft:fences", true},
        {"minecraft:cobblestone_wall", "minecraft:fences", false},
        {"minecraft:iron_door", "minecraft:doors", true},
        {"minecraft:wooden_door", "minecraft:doors", true},
        {"twilightforest:canopy_door", "minecraft:doors", true},
        {"minecraft:iron_trapdoor", "minecraft:trapdoors", true},
        {"twilightforest:dark_trapdoor", "minecraft:trapdoors", true},
        {"minecraft:oak_log", "minecraft:overworld_natural_logs", true},
        {"minecraft:stripped_oak_log", "minecraft:overworld_natural_logs", false},
        {"twilightforest:twilight_oak_log", "minecraft:overworld_natural_logs", false},
        {"minecraft:copper_door", "minecraft:doors", true},
        {"minecraft:copper_trapdoor", "minecraft:trapdoors", true},
        // nested includes reaching TF's data members two or three levels down (the carvers, the lush ground
        // and lava lake rules read these)
        {"twilightforest:uberous_soil", "twilightforest:carver_replaceables", true}, // -> overworld_carver_replaceables -> dirt
        {"twilightforest:root", "twilightforest:worldgen_replaceables", true},       // -> lush_ground_replaceable -> moss_replaceable
        {"twilightforest:liveroot_block", "twilightforest:worldgen_replaceables", true},
        {"twilightforest:trollsteinn", "twilightforest:worldgen_replaceables", true},
        {"twilightforest:uberous_soil", "twilightforest:worldgen_replaceables", true}, // -> moss_replaceable -> dirt
        {"twilightforest:canopy_leaves", "minecraft:lava_pool_stone_cannot_replace", true}, // -> leaves
        {"twilightforest:dark_leaves", "minecraft:lava_pool_stone_cannot_replace", true},
        {"twilightforest:twilight_oak_log", "minecraft:lava_pool_stone_cannot_replace", true}, // -> logs -> twilightforest:logs
        {"twilightforest:dark_log", "minecraft:lava_pool_stone_cannot_replace", true},
        {"minecraft:stone", "minecraft:lava_pool_stone_cannot_replace", false},
    };
    int checked = 0;
    for (auto const& c : cases) {
        auto const* b = gf::game::blockWith(c.block);
        if (!b) {
            ctx.check(false, std::string("unknown block ") + c.block);
            continue;
        }
        bool got = kit::hasTag(*b, c.tag);
        ctx.check(got == c.member, std::string(c.block) + (c.member ? " is in #" : " is not in #") + c.tag);
        ++checked;
    }
    // inside a generation pass (features::detail::TagCacheScope, nested as the pipeline nests them)
    // the answers come from the pass's cache from the second call on: the same answers
    {
        gf::features::detail::TagCacheScope pass;
        for (int round = 0; round < 2; ++round) {
            gf::features::detail::TagCacheScope nested;
            int                                 wrong = 0;
            for (auto const& c : cases)
                if (auto const* b = gf::game::blockWith(c.block); b && kit::hasTag(*b, c.tag) != c.member) ++wrong;
            ctx.check(wrong == 0, "inside a TagCacheScope, round " + std::to_string(round + 1) + ": " + std::to_string(wrong) + " wrong answers");
        }
        // structures_allowed (HolderSet<Structure>): an id, "#tags" through gf::tags; the cache checks the
        // value, not only the address
        JsonT set = "twilightforest:lich_tower";
        auto  one = gf::features::detail::structureIds(set);
        set       = JsonT::array({"#twilightforest:landmark"});
        auto all  = gf::features::detail::structureIds(set);
        ctx.check(one->size() == 1 && one->front() == "twilightforest:lich_tower", "structures_allowed: an id");
        auto has = [&](char const* id) { return std::find(all->begin(), all->end(), id) != all->end(); };
        ctx.check(all->size() >= 16 && has("twilightforest:dark_tower") && has("twilightforest:final_castle") && !has("twilightforest:camp"),
                  "structures_allowed: #twilightforest:landmark holds TF's 16 landmarks (" + std::to_string(all->size()) + ")");
    }

    // BlockState#canBeReplaced (Properties#replaceable): vanilla's and TFBlocks'
    static constexpr std::pair<char const*, bool> replaceable[] = {
        {"minecraft:short_grass", true},           {"minecraft:snow_layer", true},          {"minecraft:bubble_column", true},
        {"minecraft:water", true},                 {"minecraft:snow", false},               {"minecraft:stone", false},
        {"twilightforest:fallen_leaves", true},    {"twilightforest:fiddlehead", true},     {"twilightforest:wispy_cloud", true},
        {"twilightforest:ominous_fire", true},     {"twilightforest:moss_patch", false},    {"twilightforest:canopy_leaves", false},
    };
    for (auto const& [id, want] : replaceable) {
        auto const* b = gf::game::blockWith(id);
        if (!b) {
            ctx.check(false, std::string("unknown block ") + id);
            continue;
        }
        ctx.check(kit::isReplaceable(*b) == want, std::string(id) + (want ? " can be replaced" : " cannot be replaced"));
        ++checked;
    }

    // biome tags: TF's and the spawner's vanilla river tag, both from gf::tags
    ctx.check(sd::biomeHasTag("twilightforest:dark_forest", "twilightforest:in_twilight_forest") && !sd::biomeHasTag("minecraft:plains", "twilightforest:in_twilight_forest"),
              "#twilightforest:in_twilight_forest holds TF's biomes (gf::tags)");
    ctx.check(sd::biomeHasTag("minecraft:river", "minecraft:reduce_water_ambient_spawns") && sd::biomeHasTag("minecraft:frozen_river", "minecraft:reduce_water_ambient_spawns")
                  && !sd::biomeHasTag("twilightforest:stream", "minecraft:reduce_water_ambient_spawns"),
              "#minecraft:reduce_water_ambient_spawns: the rivers (#minecraft:is_river)");
    gf::log().info("worldgen tags: {} block tag and replaceable answers checked against gf::tags", checked);
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
    std::vector<std::pair<BlockPos, Block const*>> blocks; ///< the water ring's original ground blocks
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
    std::string probeId  = probe ? wg::detail::biomeIdOf(*probe) : std::string();
    if (!probe) {
        gf::log().info("worldgen custom biome: probe {} not installed", kProbeBiome);
        ctx.info(std::string("the probe biome ") + kProbeBiome + " is not installed (the worldgen e2e scenario installs it): only the vanilla control is painted");
    } else {
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
        if (a.biome.empty()) continue; // no probe: the vanilla control only (remote clients)
        // loaded by the test's simulated player, then kept loaded by a ticking area until the
        // restore test (the painted chunks must not be reloaded from disk before the client comes)
        ctx.require(co_await ctx.ensureLoaded({dim, a.x, fy, a.z}), std::string("the chunks 3000 blocks ") + a.where + " load on the server");
        auto* region = gf::game::regionOf(dim);
        ctx.require(region != nullptr, "the tagged player's region");
        int n = paintChunks(*region, a.x, fy, a.z, a.biome, out);
        // around the probe's spot a ring of water in the ground (its client biome's water colour on
        // the screenshot; the desert control keeps its plain ground for the ground tint comparison)
        if (auto const* water = a.biome == probeId ? gf::game::blockWith("minecraft:water") : nullptr)
            for (int dx = -5; dx <= 5; ++dx)
                for (int dz = -5; dz <= 5; ++dz) {
                    if (std::abs(dx) <= 1 && std::abs(dz) <= 1) continue;
                    BlockPos p{a.x + dx, fy - 1, a.z + dz};
                    out.blocks.emplace_back(p, &region->getBlock(p));
                    gf::game::setBlock(*region, p, *water);
                }
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
    if (!p.blocks.empty())
        if (auto* region = gf::game::regionOf(p.dim))
            for (auto const& [pos, block] : p.blocks) gf::game::setBlock(*region, pos, *block);
    p.blocks.clear();
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
    auto& map = level->createMapSavedData(level->getNewUniqueID(), BlockPos{far.x, far.y, far.z}, DimensionType(dim), 0);
    // the map is only a sampling buffer: never saved, removed from the map manager at the end of the
    // test (also when a check aborts it), so no map record is left in the world
    map.mDirtyForSave = false;
    struct DropMap {
        ActorUniqueID id;
        ~DropMap() {
            auto level = ll::service::getLevel();
            if (!level) return;
            if (auto* manager = level->getMapDataManager().get().get()) manager->mMapData->erase(id);
            (void)level->getLevelStorage().deleteData(MapItemSavedData::getSerializationKey(id), DBHelpers::Category::Item);
        }
    } dropMap{*map.mMapId};
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
    map.mDirtyForSave = false;
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
    // vanilla ids: a bare id and Java's names of Bedrock entities; a natural spawn the caller
    // decided stands even where Bedrock's own spawn rules refuse it (a spider in daylight)
    ctx.check(gf::spawn::detail::bedrockEntityId("spider") == "minecraft:spider", "a bare id gets minecraft:");
    ctx.check(gf::spawn::detail::bedrockEntityId("minecraft:zombified_piglin") == "minecraft:zombie_pigman", "Java's zombified piglin is Bedrock's zombie pigman");
    // (monsters need a difficulty above peaceful, the e2e worlds' default: Java's Monster spawn rules)
    auto level            = ll::service::getLevel();
    auto previousDifficulty = level ? level->getDifficulty() : SharedTypes::Legacy::Difficulty::Peaceful;
    if (level) level->setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
    for (auto reason : {gf::spawn::SpawnReason::Natural, gf::spawn::SpawnReason::ChunkGeneration, gf::spawn::SpawnReason::Spawner}) {
        Actor* spider = gf::spawn::spawnMob(*bs, "minecraft:spider", Vec3{box.minX + 4.5f, static_cast<float>(box.minY + 1), box.minZ + 4.5f}, reason);
        ctx.check(spider != nullptr && spider->getTypeName() == "minecraft:spider",
                  "minecraft:spider spawns (reason " + std::to_string(static_cast<int>(reason)) + ")");
        if (spider) spider->remove();
    }
    if (level) level->setDifficulty(previousDifficulty);

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

// The spawners' default mob answers (Java's base classes): Mob#checkSpawnObstruction's liquid
// check over the spawn box (WaterAnimal's without it), no entity check during chunk generation
// (WorldGenRegion#getEntities is empty: a group's members may overlap) and the walk target values
// (the biome tags the natural spawner reads: worldgen.tags).
GF_REGISTER_SELFTEST("worldgen.spawn_checks", [](Context& ctx) {
    namespace sd = gf::spawn::detail;
    namespace se = gf::spawn::ext;
    auto        world  = flatMemoryWorld(7);
    auto const* water  = gf::game::blockWith("minecraft:water");
    auto const* stone  = gf::game::blockWith("minecraft:stone");
    ctx.require(water && stone, "blocks");
    world->setBlock({5, 4, 5}, *water); // a water block on the grass (top y 3)
    auto region = world->region(0, 0, 1);

    // the box of a 0.6 wide mob at x 6.0 covers columns 5 and 6 (Java's chunk-generation spawns
    // stand on integer x / z): the water next to it obstructs a land mob, not a water animal
    se::SpawnBehaviour land;
    ctx.check(!sd::obstructionDefault(*region, nullptr, land, "creature", 6.0, 4.0, 5.5), "a land mob beside water: Mob#checkSpawnObstruction refuses (containsAnyLiquid)");
    ctx.check(sd::obstructionDefault(*region, nullptr, land, "creature", 7.0, 4.0, 5.5), "one column further: no liquid in the box");
    ctx.check(sd::obstructionDefault(*region, nullptr, land, "water_creature", 6.0, 4.0, 5.5), "the water categories: WaterAnimal's check (entities only)");
    se::SpawnBehaviour drowned{.obstruction = se::Obstruction::EntitiesOnly};
    ctx.check(sd::obstructionDefault(*region, nullptr, drowned, "monster", 6.0, 4.0, 5.5), "Obstruction::EntitiesOnly ignores the liquid");
    se::SpawnBehaviour wide{.width = 2.0};
    ctx.check(sd::obstructionDefault(*region, nullptr, land, "creature", 6.9, 4.0, 5.5)
                  && !sd::obstructionDefault(*region, nullptr, wide, "creature", 6.9, 4.0, 5.5),
              "at x 6.9 a 0.6-wide box misses the water, a 2-wide one covers it");

    // walk target values: Animal (grass below or light), Zero (a creature that is not an Animal),
    // Monster (dark enough); generation light estimated from the blocks above
    for (int x = 8; x <= 10; ++x)
        for (int z = 8; z <= 10; ++z) world->setBlock({x, 8, z}, *stone); // a roof: dark below
    world->setBlock({9, 3, 9}, *stone); // no grass below the dark spot
    BlockPos dark{9, 4, 9}, open{12, 4, 12};
    ctx.check(sd::spawnRulesDefault(*region, open, se::WalkTarget::ByCategory, "creature", true), "an Animal on grass");
    ctx.check(!sd::spawnRulesDefault(*region, dark, se::WalkTarget::ByCategory, "creature", true), "an Animal in the dark, off grass: refused");
    ctx.check(sd::spawnRulesDefault(*region, dark, se::WalkTarget::Zero, "creature", true), "a creature that is not an Animal (walk target 0): allowed");
    ctx.check(sd::spawnRulesDefault(*region, dark, se::WalkTarget::ByCategory, "monster", true)
                  && !sd::spawnRulesDefault(*region, open, se::WalkTarget::ByCategory, "monster", true),
              "a Monster: in the dark only");

    // chunk generation: every member of a group is placed (the members never obstruct each other):
    // on open grass every try succeeds, so the mobs come in whole groups of 4
    gf::spawn::Table table;
    table.creatureProbability   = 0.5F;
    table.byCategory["creature"] = {{"gf_test:spawn_checks_mob", 1, 4, 4}};
    gf::spawn::setBiomeSpawns("gf_test:spawn_checks", table);
    auto open3 = flatMemoryWorld(11);
    auto view  = open3->region(0, 0, 1);
    int  whole = 0, mobs = 0;
    for (int s = 0; s < 64; ++s) {
        // (decoration seeds of scattered chunks: java.util.Random's first float barely moves over
        // consecutive small seeds)
        jr::WorldgenRandom random(std::make_unique<jr::LegacyRandom>(0));
        random.setDecorationSeed(20241102, s * 160 - 4000, 3000 - s * 112);
        int n = sd::spawnForChunkGeneration(*view, "gf_test:spawn_checks", 0, 0, random);
        mobs += n;
        if (n % 4 == 0) ++whole;
    }
    gf::spawn::setBiomeSpawns("gf_test:spawn_checks", gf::spawn::Table{}); // no test spawns left registered
    ctx.check(whole == 64 && mobs > 0, "every group of 4 is placed whole (" + std::to_string(mobs) + " mobs over 64 chunks)");

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

// Structures during the decoration: a plain Pieces step runs once (Worldgen.h's single stage, at
// SurfaceStructures); an ext::addStructureStep step runs at every decoration step 0..10, both
// before the step's features, with the decoration context: step, decoration seed and the structure
// randoms of Java's setFeatureSeed(decorationSeed, index, step), the index counting every structure
// of the step in Java's registry order (Minecraft's own included).
GF_REGISTER_SELFTEST("worldgen.pieces", [](Context& ctx) {
    constexpr std::int64_t seed = 8675309;
    wg::detail::Providers  p;
    p.terrain = [](wg::Chunk& c) { wg::detail::fallbackTerrain(c); };
    std::vector<int>          steps;     ///< the structure step's calls (step), the plain Pieces' (200 + step), Features (100)
    std::vector<std::int64_t> firsts;
    std::int64_t              decoSeed = 0;
    bool                      context  = true;
    bool                      labyrinthSeed = false, otherStepNull = true;
    p.structureSteps.push_back({"gf_test.structures", 0, 1, [&](wg::Chunk& c, wg::Region&) {
                                    auto const* d = wg::ext::decorationContext();
                                    if (!d || d->chunkX != c.chunkX() || d->chunkZ != c.chunkZ()) {
                                        context = false;
                                        return;
                                    }
                                    steps.push_back(d->step);
                                    decoSeed = d->decorationSeed;
                                    firsts.push_back(d->structureRandom(2)->nextLong());
                                    if (d->step == static_cast<int>(wg::ext::DecorationStep::UndergroundStructures)) {
                                        auto r = d->structureRandomOf("twilightforest:labyrinth");
                                        labyrinthSeed = r && r->nextLong() == d->structureRandom(2)->nextLong();
                                    } else if (d->structureRandomOf("twilightforest:labyrinth")) {
                                        otherStepNull = false;
                                    }
                                }});
    // a plain Pieces step registered earlier with a later order: after the structure step
    p.stages[wg::Stage::Pieces].push_back({"gf_test.pieces", 1, 0, [&](wg::Chunk&, wg::Region&) {
                                               auto const* d = wg::ext::decorationContext();
                                               steps.push_back(200 + (d ? d->step : -100));
                                           }});
    p.stages[wg::Stage::Features].push_back({"gf_test.features", 0, 2, [&](wg::Chunk&, wg::Region&) { steps.push_back(100); }});
    wg::detail::MemoryWorld world(-32, 64, seed);
    for (int cx = 4; cx <= 6; ++cx)
        for (int cz = -6; cz <= -4; ++cz) {
            auto&                       c = wg::detail::asChunk(world.chunk(cx, cz));
            wg::detail::ChunkOnlyRegion region(c, seed, -1);
            wg::detail::generateChunk(p, c, region, seed);
        }
    auto region = world.region(5, -5, 1);
    wg::detail::decorateChunk(p, &wg::detail::asChunk(world.chunk(5, -5)), *region, seed);
    ctx.check(context && wg::ext::decorationContext() == nullptr, "the context is set during the structure calls only");
    std::vector<int> want{0, 1, 2, 3, 4, 204, 5, 6, 7, 8, 9, 10, 100};
    std::string      seq;
    for (int s : steps) seq += (seq.empty() ? "" : " ") + std::to_string(s);
    ctx.check(steps == want, "structure steps at every step 0..10, the plain Pieces once at SurfaceStructures, then Features (" + seq + ")");
    jr::WorldgenRandom w(std::make_unique<jr::Xoroshiro128pp>(0));
    ctx.check(decoSeed == w.setDecorationSeed(seed, 80, -80), "the decoration seed of the chunk");
    bool seeds = firsts.size() == 11;
    for (int l = 0; seeds && l < 11; ++l) {
        jr::WorldgenRandom s(std::make_unique<jr::Xoroshiro128pp>(0));
        s.setFeatureSeed(decoSeed, 2, l);
        if (s.nextLong() != firsts[static_cast<std::size_t>(l)]) seeds = false;
    }
    ctx.check(seeds, "structureRandom(index) is setFeatureSeed(decorationSeed, index, step)");
    ctx.check(labyrinthSeed && otherStepNull, "structureRandomOf(labyrinth) is index 2 of underground_structures, nullptr at other steps");

    // Java's structure registry: the file paths sorted, every structure of a step counted
    auto slotIs = [](char const* id, int step, int index) {
        auto s = wg::ext::structureSlot(id);
        return s && s->step == step && s->index == index;
    };
    auto by = wg::ext::structuresByStep();
    ctx.check(by.size() == 11 && by[3].size() == 8 && by[4].size() == 43 && by[7].size() == 3 && by[10].size() == 1,
              "34 vanilla and 21 TF structures: 8 underground_structures, 43 surface_structures, 3 underground_decoration, 1 top_layer_modification");
    ctx.check(slotIs("minecraft:buried_treasure", 3, 0) && slotIs("twilightforest:knight_stronghold", 3, 1) && slotIs("twilightforest:labyrinth", 3, 2)
                  && slotIs("minecraft:trail_ruins", 3, 5) && slotIs("minecraft:trial_chambers", 3, 6) && slotIs("twilightforest:troll_cave", 3, 7),
              "underground_structures in registry order");
    ctx.check(slotIs("twilightforest:aurora_palace", 4, 0) && slotIs("twilightforest:dark_tower", 4, 3) && slotIs("twilightforest:hedge_maze", 4, 8)
                  && slotIs("twilightforest:large_hollow_hill", 4, 13) && slotIs("twilightforest:lich_tower", 4, 14)
                  && slotIs("minecraft:shipwreck", 4, 31) && slotIs("minecraft:shipwreck_beached", 4, 32) && slotIs("minecraft:stronghold", 4, 34)
                  && slotIs("twilightforest:yeti_cave", 4, 42),
              "surface_structures in registry order, vanilla structures counted");
    ctx.check(slotIs("twilightforest:giant_house", 10, 0) && !wg::ext::structureSlot("twilightforest:no_such_structure"), "giant house alone at top_layer_modification");
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
    JsonT                    config = {{"type", "minecraft:carving_mask"}, {"step", "air"}};
    auto                     out    = fn({*region, random, config, ""}, BlockPos{cx * 16, 0, cz * 16});
    std::vector<BlockPos>    want{{cx * 16, 3, cz * 16}, {cx * 16 + 1, 5, cz * 16 + 2}, {cx * 16 + 15, 7, cz * 16 + 15}};
    bool                     same = out.size() == want.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) same = out[i].x == want[i].x && out[i].y == want[i].y && out[i].z == want[i].z;
    ctx.check(same, "the carved blocks in ascending mask index (" + std::to_string(out.size()) + " positions)");
    JsonT liquid = {{"type", "minecraft:carving_mask"}, {"step", "liquid"}};
    ctx.check(fn({*region, random, liquid, ""}, BlockPos{cx * 16, 0, cz * 16}).empty(), "the liquid step's mask is empty (no liquid carvers in 1.21.1)");
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

    // the contract's raw quart biome (BiomeSource#getNoiseBiome) under the zoomed biomeAt
    if (wg::detail::providers()->biomeSource) {
        bool rawMatch = true, zoomMatch = true;
        auto obf      = bm::obfuscateSeed(wg::detail::currentSeed());
        for (int x = -40; x < 40; x += 7)
            for (int z = -40; z < 40; z += 9) {
                auto q = bm::zoom(obf, x, 64, z);
                if (wg::biomeAt(BlockPos{x, 64, z}) != wg::noiseBiomeAt(q.x, q.y, q.z)) zoomMatch = false;
                if (wg::noiseBiomeAt(x >> 2, 16, z >> 2) != wg::detail::providers()->biomeSource(x >> 2, 16, z >> 2)) rawMatch = false;
            }
        ctx.check(rawMatch, "noiseBiomeAt is the biome source's quart, unzoomed");
        ctx.check(zoomMatch, "biomeAt is noiseBiomeAt at BiomeManager's quart");
        ctx.check(wg::noiseBiomeAt(0, 100000, 0) == wg::noiseBiomeAt(0, gf::core::mth::fromBlock(wg::detail::twilightMaxY() - 1), 0),
                  "noiseBiomeAt clamps the quart y to the dimension");
    } else ctx.info("no biome source installed: noiseBiomeAt not checked");

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

// Java block states in Bedrock terms for the feature kit: renamed vanilla ids, stairs / slabs /
// walls / doors / fences / mushroom blocks, property variants (lit furnace, double slabs); and the
// survival rules of TF's JSON blocks (BushBlock, MossPatchBlock, TorchberryPlantBlock, ...) that
// kit::canSurvive answers from their Java classes.
GF_REGISTER_SELFTEST("worldgen.java_states", [](Context& ctx) {
    auto name = [](JsonT const& state) -> std::string {
        auto const* b = kit::javaState(state);
        return b ? b->getTypeName() : std::string("<none>");
    };
    auto stateOf = [](JsonT const& state, char const* key) -> std::string {
        auto const* b = kit::javaState(state);
        if (!b) return "<none>";
        auto s  = gf::game::statesOf(*b);
        auto it = s.find(key);
        return it == s.end() ? std::string("<absent>") : it->second;
    };
    ctx.check(name({{"Name", "minecraft:cobblestone_stairs"}}) == "minecraft:stone_stairs", "cobblestone stairs: " + name({{"Name", "minecraft:cobblestone_stairs"}}));
    ctx.check(name({{"Name", "minecraft:rooted_dirt"}}) == "minecraft:dirt_with_roots", "rooted dirt");
    ctx.check(name({{"Name", "minecraft:red_bed"}}) == "minecraft:bed", "beds are minecraft:bed");
    ctx.check(name({{"Name", "minecraft:furnace"}, {"Properties", {{"lit", "true"}, {"facing", "north"}}}}) == "minecraft:lit_furnace", "a lit furnace");
    ctx.check(name({{"Name", "minecraft:cobblestone_slab"}, {"Properties", {{"type", "double"}}}}) == "minecraft:cobblestone_double_slab", "a double slab");
    ctx.check(stateOf({{"Name", "minecraft:oak_stairs"}, {"Properties", {{"facing", "north"}, {"half", "top"}, {"shape", "straight"}}}}, "weirdo_direction") == "3",
              "stairs facing north: weirdo_direction 3");
    ctx.check(stateOf({{"Name", "minecraft:oak_stairs"}, {"Properties", {{"facing", "east"}, {"half", "top"}}}}, "upside_down_bit") == "true", "upside-down stairs");
    ctx.check(stateOf({{"Name", "minecraft:mossy_cobblestone_slab"}, {"Properties", {{"type", "top"}}}}, "minecraft:vertical_half") == "top", "a top slab");
    ctx.check(stateOf({{"Name", "minecraft:cobblestone_wall"}, {"Properties", {{"north", "low"}, {"up", "true"}}}}, "wall_connection_type_north") == "short",
              "a wall's low side is short");
    ctx.check(kit::javaState({{"Name", "minecraft:oak_fence"}, {"Properties", {{"north", "true"}, {"east", "false"}}}}) == gf::game::blockWith("minecraft:oak_fence"),
              "fence connections are Bedrock's own");
    ctx.check(stateOf({{"Name", "minecraft:brown_mushroom_block"}, {"Properties", {{"up", "true"}, {"down", "false"}, {"north", "true"}, {"west", "true"}, {"east", "false"}, {"south", "false"}}}},
                      "huge_mushroom_bits") == "1",
              "a north-west cap corner: huge_mushroom_bits 1");
    ctx.check(kit::javaId(*gf::game::blockWith("minecraft:stone_stairs")) == "minecraft:cobblestone_stairs", "Bedrock's stone stairs are Java's cobblestone stairs");

    // survival of TF's blocks on an in-memory region (grass top at y 3)
    auto world  = flatMemoryWorld(7);
    auto region = world->region(0, 0, 1);
    auto const* stone   = gf::game::blockWith("minecraft:stone");
    auto const* air     = gf::game::blockWith("minecraft:air");
    auto const* mayapple = kit::javaBlock("twilightforest:mayapple");
    auto const* moss     = kit::javaBlock("twilightforest:moss_patch");
    auto const* torch    = kit::javaBlock("twilightforest:torchberry_plant");
    auto const* sapling  = kit::javaBlock("twilightforest:twilight_oak_sapling");
    ctx.require(stone && air && mayapple && moss && torch && sapling, "blocks");
    ctx.check(gf::features::detail::survivalRule(*mayapple) == gf::features::detail::Survival::Bush, "the mayapple is a BushBlock (skeleton classes)");
    BlockPos onGrass{4, 4, 4};
    ctx.check(kit::canSurvive(*region, onGrass, *mayapple), "a mayapple stands on grass");
    ctx.check(kit::canSurvive(*region, onGrass, *sapling), "a sapling stands on grass");
    ctx.check(!kit::canSurvive(*region, BlockPos{4, 6, 4}, *mayapple), "a mayapple in the air does not");
    region->setBlock({6, 4, 6}, *stone);
    ctx.check(!kit::canSurvive(*region, BlockPos{6, 5, 6}, *mayapple), "nor on stone");
    ctx.check(kit::canSurvive(*region, BlockPos{6, 5, 6}, *moss), "a moss patch stands on stone (sturdy face)");
    ctx.check(!kit::canSurvive(*region, BlockPos{6, 6, 6}, *moss), "but not in the air");
    // a torchberry plant hangs under #twilightforest:plants_hang_on (#minecraft:dirt: the grass block)
    region->setBlock({8, 2, 8}, *air);
    ctx.check(kit::canSurvive(*region, BlockPos{8, 2, 8}, *torch), "a torchberry plant hangs under grass");
    ctx.check(!kit::canSurvive(*region, BlockPos{8, 6, 8}, *torch), "not under air");
});
