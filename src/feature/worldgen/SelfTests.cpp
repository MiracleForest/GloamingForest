// Selftests of the generator pipeline, the feature interpreter and the spawn engine (F5, prefix
// "worldgen.", testPos dx 5500..5599). The determinism goldens are the same on the server and the
// client build: the fixture feature and the in-memory pipeline must hash identically on both.
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iomanip>
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
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"

#include "core/jrand/JRand.h"
#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "feature/dimension/World.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/ext/worldgen/Spawning.h"
#include "feature/features/Internal.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Internal.h"
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
    struct RestoreRules { // the shared test world gets its rules back even when a check aborts
        Context&                                          c;
        std::vector<std::pair<std::string, std::string>>& rules;
        ~RestoreRules() {
            for (auto const& [name, value] : rules)
                if (!value.empty()) runCommand(c, "gamerule " + name + " " + value);
        }
    } restoreRules{ctx, saved};
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
// back. When a player is tagged gf_worldgen_biome_show (client scenario), the chunks around that
// player are painted with it and kept until worldgen.custom_biome_restore, so the client's view of
// the biome (its fog and water colours from the test resource pack) can be captured.
namespace {
struct Painted {
    int                                                  dim = 0;
    std::vector<std::tuple<int, int, std::string>>       columns; ///< x, z, original biome
};
Painted& painted() {
    static Painted p;
    return p;
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
    auto* lc = bs->getChunkAt(BlockPos{box.minX, box.minY, box.minZ});
    ctx.require(lc != nullptr, "test chunk");
    wg::detail::LevelChunkAdapter chunk(*lc, registry, bs->getMinHeight(), bs->getMaxHeight());
    int                           lx = box.minX & 15, lz = box.minZ & 15;
    std::string                   original = chunk.biome(lx, box.minY, lz);
    struct Restore {
        wg::detail::LevelChunkAdapter& c;
        int                            x, z;
        std::string                    biome;
        ~Restore() { c.setBiomeColumn(x, z, biome); }
    } restore{chunk, lx, lz, original};
    std::string probeId = wg::detail::biomeIdOf(*probe);
    chunk.setBiomeColumn(lx, lz, probeId);
    ctx.check(chunk.biome(lx, box.minY, lz) == probeId, "the chunk reads back the custom biome " + probeId);
    auto viaSource = wg::detail::biomeIdOf(bs->getBiome(BlockPos{box.minX, box.minY + 1, box.minZ}));
    ctx.check(viaSource == probeId, "the block source sees the custom biome (" + viaSource + ")");
    gf::log().info("worldgen custom biome: {} written and read back (was {})", probeId, original);

    // client view: paint the chunks around a tagged player
    Player* shown = nullptr;
    ll::service::getLevel()->forEachPlayer([&](Player& p) {
        if (p.hasTag("gf_worldgen_biome_show")) shown = &p;
        return shown == nullptr;
    });
    if (!shown) co_return;
    auto* region = gf::game::regionOf(static_cast<int>(shown->getDimensionId()));
    ctx.require(region != nullptr, "the tagged player's region");
    auto pos  = shown->getFeetPos();
    int  pcx  = gf::core::mth::floor(pos.x) >> 4, pcz = gf::core::mth::floor(pos.z) >> 4;
    auto& out = painted();
    out       = {static_cast<int>(shown->getDimensionId()), {}};
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            auto* c = region->getChunk(pcx + dx, pcz + dz);
            if (!c) continue;
            wg::detail::LevelChunkAdapter a(*c, region->getDimension().getBiomeRegistry(), region->getMinHeight(), region->getMaxHeight());
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    out.columns.emplace_back((pcx + dx) * 16 + x, (pcz + dz) * 16 + z, a.biome(x, gf::core::mth::floor(pos.y), z));
                    a.setBiomeColumn(x, z, probeId);
                }
        }
    shown->removeTag("gf_worldgen_biome_show");
    gf::log().info("worldgen custom biome: painted {} columns around {}", out.columns.size(), shown->getRealName());
});

GF_REGISTER_SELFTEST("worldgen.custom_biome_restore", [](Context& ctx) {
    auto& p = painted();
    if (p.columns.empty()) {
        ctx.info("nothing painted");
        return;
    }
    auto* region = gf::game::regionOf(p.dim);
    ctx.require(region != nullptr, "the painted region");
    for (auto const& [x, z, biome] : p.columns) {
        auto* c = region->getChunk(x >> 4, z >> 4);
        if (!c) continue;
        wg::detail::LevelChunkAdapter a(*c, region->getDimension().getBiomeRegistry(), region->getMinHeight(), region->getMaxHeight());
        a.setBiomeColumn(x & 15, z & 15, biome);
    }
    gf::log().info("worldgen custom biome: restored {} columns", p.columns.size());
    p.columns.clear();
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
});
