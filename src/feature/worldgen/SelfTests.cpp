// Selftests of the generator pipeline, the feature interpreter and the spawn engine (F5, prefix
// "worldgen.", testPos dx 5500..5599). The determinism goldens are the same on the server and the
// client build: the fixture feature and the in-memory pipeline must hash identically on both.
#include <bit>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/jrand/JRand.h"
#include "core/noise/Noise.h"
#include "feature/dimension/World.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/features/Internal.h"
#include "feature/spawn/Spawn.h"
#include "feature/worldgen/Internal.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
namespace wg   = gf::worldgen;
namespace kit  = gf::features::kit;
namespace jr   = gf::core::jrand;

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
    ctx.check(h == "184f363d781b7a22", "the fixture placement matches the golden hash on every build (" + h + ")");

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
    ctx.check(h == "2b9b941f46633cf1", "the vanilla features match the golden hash on every build (" + h + ")");
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
    ctx.check(first == "8db0333bc36a00c3", "the tree matches the golden hash on every build (" + first + ")");
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
    for (int i = 0; i < 10 && ctx.blockAt(pos) != "gf_test:fixture_block"; ++i) co_await ctx.ticks(10);
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
    chunk.setBiomeColumn(lx, lz, other);
    ctx.check(chunk.biome(lx, box.minY, lz) == other, "the column reads back " + other);
    ctx.check(chunk.biome(lx, bs->getMaxHeight() - 1, lz) == other, "the whole column is set");
    chunk.setBiome(lx, box.minY + 4, lz, original);
    ctx.check(chunk.biome(lx, box.minY + 4, lz) == original, "a single block reads back");
    chunk.setBiomeColumn(lx, lz, original);
    ctx.check(chunk.biome(lx, box.minY, lz) == original, "restored");
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
    auto s = wg::ext::stats();
    if (s.chunksGenerated) {
        std::ostringstream g;
        g << "in-game generator: " << s.chunksGenerated << " chunks generated (" << std::setprecision(2) << s.generateMs / s.chunksGenerated
          << " ms each), " << s.chunksDecorated << " decorated (" << (s.chunksDecorated ? s.decorateMs / s.chunksDecorated : 0.0) << " ms each)";
        ctx.info(g.str());
    }
});

// In the Twilight Forest (when F4's dimension is registered): new chunks come from this generator.
GF_REGISTER_SELFTEST("worldgen.generator", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!gf::world::twilight()) {
        ctx.info("the Twilight Forest dimension is not registered (F4): in-dimension generation not exercised");
        co_return;
    }
    ctx.require(co_await ctx.inTwilight(), "in the Twilight Forest");
    ctx.check(wg::ext::generatorActive(), "the Twilight Forest generator is installed");
    auto before = wg::ext::stats();
    auto far    = ctx.testPos(5530);
    far.x += 4096;
    far.z += 4096;
    ctx.check(co_await ctx.ensureLoaded(far), "far chunks load");
    co_await ctx.ticks(40);
    auto after = wg::ext::stats();
    ctx.check(after.chunksGenerated > before.chunksGenerated, "new chunks generated by the pipeline");
    ctx.info("generated " + std::to_string(after.chunksGenerated - before.chunksGenerated) + " chunks, decorated "
             + std::to_string(after.chunksDecorated - before.chunksDecorated));
});

// The dev functions are in the behavior pack.
GF_REGISTER_SELFTEST("worldgen.functions", [](Context& ctx) {
    auto dir = gf::Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_BP" / "functions" / "twilightforest";
    ctx.check(std::filesystem::exists(dir / "dev_new_world.mcfunction"), "dev_new_world written");
    ctx.check(std::filesystem::exists(dir / "dev_powerwash_chunk.mcfunction"), "dev_powerwash_chunk written");
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
});
