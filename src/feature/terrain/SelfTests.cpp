// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) selftests (terrain.*, testPos dx 200-299). Most run the providers on chunks held in
// memory (MemWorld below), so they do not depend on the e2e world's biomes; terrain.golden_* run the
// generator on the F13 goldens' biomes and compare with their heights / surface (fingerprint below).
#include <algorithm>
#include <array>
#include <optional>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/JRand.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/ext/terrain/PieceBeard.h"
#include "feature/ext/terrain/Pipeline.h"
#include "feature/landmark/Landmark.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/features/Features.h"
#include "feature/terrain/Terrain.h"
#include "core/landmark/Landmark.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace gf::feature::terrain {
namespace {

using selftest::Context;
using Json    = nlohmann::json;
namespace kit = gf::features::kit;
namespace jr  = gf::core::jrand;

#include "feature/terrain/GoldenFingerprint.inc"

// ---- an in-memory world -------------------------------------------------------------------------------

class MemChunk final : public worldgen::Chunk {
public:
    MemChunk(int cx, int cz, int minY, int maxY, Block const* air) : mX(cx), mZ(cz), mMinY(minY), mMaxY(maxY), mBlocks(static_cast<std::size_t>((maxY - minY) * 256), air) {}
    int          chunkX() const override { return mX; }
    int          chunkZ() const override { return mZ; }
    int          minY() const override { return mMinY; }
    int          maxY() const override { return mMaxY; }
    Block const& getBlock(int lx, int y, int lz) const override { return *mBlocks[idx(lx, y, lz)]; }
    void         setBlock(int lx, int y, int lz, Block const& b) override {
        if (y >= mMinY && y < mMaxY) mBlocks[idx(lx, y, lz)] = &b;
    }
    int height(worldgen::Heightmap type, int lx, int lz) const override {
        for (int y = mMaxY - 1; y >= mMinY; --y) {
            Block const& b = getBlock(lx, y, lz);
            bool         m = type == worldgen::Heightmap::WorldSurfaceWg || type == worldgen::Heightmap::WorldSurface ? !kit::isAir(b)
                           : type == worldgen::Heightmap::OceanFloorWg || type == worldgen::Heightmap::OceanFloor ? kit::blocksMotion(b)
                                                                                                                 : (kit::blocksMotion(b) || kit::isFluid(b));
            if (m) return y;
        }
        return mMinY - 1;
    }
    std::string biome(int, int, int) const override { return mBiome; }
    void        setBiome(int, int, int, std::string const& id) override { mBiome = id; }

private:
    std::size_t idx(int lx, int y, int lz) const { return static_cast<std::size_t>(((y - mMinY) * 16 + lz) * 16 + lx); }
    int                       mX, mZ, mMinY, mMaxY;
    std::vector<Block const*> mBlocks;
    std::string               mBiome = "twilightforest:forest";
};

class MemWorld final : public worldgen::Region {
public:
    MemWorld(int cx, int cz, int radius, std::int64_t seed) : mCX(cx), mCZ(cz), mSeed(seed) {
        mAir = kit::javaBlock("minecraft:air");
        for (int x = cx - radius; x <= cx + radius; ++x)
            for (int z = cz - radius; z <= cz + radius; ++z) mChunks[{x, z}] = std::make_unique<MemChunk>(x, z, -32, 256, mAir);
    }
    MemChunk* chunk(int cx, int cz) {
        auto it = mChunks.find({cx, cz});
        return it == mChunks.end() ? nullptr : it->second.get();
    }
    int          centerChunkX() const override { return mCX; }
    int          centerChunkZ() const override { return mCZ; }
    int          minY() const override { return -32; }
    int          maxY() const override { return 256; }
    bool         isInside(int x, int z) const override { return mChunks.contains({x >> 4, z >> 4}); }
    Block const& getBlock(BlockPos const& p) const override {
        auto it = mChunks.find({p.x >> 4, p.z >> 4});
        if (it == mChunks.end() || p.y < -32 || p.y >= 256) return *mAir;
        return it->second->getBlock(p.x & 15, p.y, p.z & 15);
    }
    bool setBlock(BlockPos const& p, Block const& b, int = 3) override {
        auto it = mChunks.find({p.x >> 4, p.z >> 4});
        if (it == mChunks.end()) return false;
        it->second->setBlock(p.x & 15, p.y, p.z & 15, b);
        return true;
    }
    int height(worldgen::Heightmap type, int x, int z) const override {
        auto it = mChunks.find({x >> 4, z >> 4});
        return it == mChunks.end() ? -32 : it->second->height(type, x & 15, z & 15) + 1;
    }
    std::string biome(BlockPos const&) const override { return mBiome; }
    std::int64_t seed() const override { return mSeed; }
    int          dimension() const override { return -1; }
    std::string  mBiome = "twilightforest:forest";
    int count(std::string const& javaId) const {
        int n = 0;
        for (auto const& [k, c] : mChunks)
            for (int y = -32; y < 256; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        if (kit::javaId(c->getBlock(x, y, z)) == javaId) ++n;
        return n;
    }

private:
    int                                                       mCX, mCZ;
    std::int64_t                                              mSeed;
    Block const*                                              mAir = nullptr;
    std::map<std::pair<int, int>, std::unique_ptr<MemChunk>>  mChunks;
};

std::string idOf(State const& s, std::uint16_t c) {
    auto const* b = s.block(c);
    return b ? kit::javaId(*b) : std::string("<none>");
}

/// The current world's state, or one for seed 0 (selftests outside a generated Twilight Forest).
std::shared_ptr<State const> anyState() {
    if (auto s = state()) return s;
    auto s = makeState(0, false);
    return s->gen && s->stone ? s : nullptr;
}

// ---- the golden fingerprints ------------------------------------------------------------------------------

struct GoldenWorld {
    Fingerprint const&               fp;
    std::shared_ptr<State const>     s;
    TerrainGrid const&               grid;
    std::int64_t                     zoomSeed;
    TerrainColumn const*             column(int qx, int qz) const {
        int i = qx - fp.qx0, j = qz - fp.qz0;
        if (i < 0 || j < 0 || i >= fp.qw || j >= fp.qh) return nullptr;
        char c = fp.biomes[static_cast<std::size_t>(j * fp.qw + i)];
        auto p = std::string_view(kBiomeChars).find(c);
        return p == std::string_view::npos ? nullptr : grid.column(kBiomePalette[p]);
    }
    TerrainSampler sampler() const {
        return [this](int x, int z) { return sampleTerrain(x, z, [this](int qx, int qz) { return column(qx, qz); }); };
    }
    std::string const& blockBiome(int x, int y, int z) const {
        static std::string const none;
        auto                     q = core::biome::zoom(zoomSeed, x, y, z);
        auto const*              c = column(q.x, q.z);
        return c ? c->biomeAt(q.y) : none;
    }
};

// ---- tests -----------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("terrain.density_types", [](Context& ctx) {
    auto f = Focused::fromPos(0, 0, 0, 10.0F, 1.0F, -1.0F);
    ctx.check(f.compute(100, 0, 0) == -1.0, "focused: far value outside the radius");
    auto h = HollowHill::fromPos(0, 0, 0, 20.0F, 1.0F);
    ctx.check(h.compute(0, 0, 0) == 1.0 && h.compute(30, 0, 0) == 0.0, "hollow_hill: inside 1, outside 0");
    AbsoluteDifference mx{true, 5.0, 10.0, 20.0};
    ctx.check(mx.compute(12, 23) == 3.0, "coord_max");
    // the F5 Density sees TF's types (registered by the module)
    auto d = gf::core::noise::Density::fromJson(Json{{"type", "twilightforest:sqrt"}, {"input", 16.0}}, 0, {});
    ctx.check(d && d->compute({0, 0, 0}) == 4.0, "twilightforest:sqrt through core/noise Density");
    auto s = anyState();
    ctx.require(s != nullptr, "the terrain generator builds from the mod's data");
    ctx.check(s->grid.size() == 20, "the biome grid has TF's 20 key biomes");
});

GF_REGISTER_SELFTEST("terrain.height_golden", [](Context& ctx) {
    auto base = anyState();
    ctx.require(base != nullptr, "generator");
    int checked = 0, bad = 0;
    for (auto const& fp : kFingerprints) {
        auto        s = makeState(fp.seed, false);
        ctx.require(s->gen != nullptr, "generator for seed " + std::to_string(fp.seed));
        GoldenWorld w{fp, s, s->grid, core::biome::obfuscateSeed(fp.seed)};
        auto        sampler = w.sampler();
        for (int i = 0; i < fp.hn; ++i)
            for (int j = 0; j < fp.hn; ++j) {
                int x = fp.hx0 + i * fp.hstep, z = fp.hz0 + j * fp.hstep;
                int ws = s->gen->baseHeight(x, z, false, sampler), of = s->gen->baseHeight(x, z, true, sampler);
                int ews = fp.worldSurface[i * fp.hn + j], eof = fp.oceanFloor[i * fp.hn + j];
                ++checked;
                if (ws != ews || of != eof) {
                    if (bad++ < 5) ctx.check(false, "seed " + std::to_string(fp.seed) + " (" + std::to_string(x) + ", " + std::to_string(z) + "): " + std::to_string(ws) + "/" + std::to_string(of) + " != " + std::to_string(ews) + "/" + std::to_string(eof));
                }
            }
    }
    ctx.info("heights checked " + std::to_string(checked) + ", mismatches " + std::to_string(bad));
    ctx.check(bad == 0 && checked > 0, "noise heights equal the F13 goldens for 3 seeds");
});

GF_REGISTER_SELFTEST("terrain.surface_golden", [](Context& ctx) {
    int checked = 0, bad = 0;
    for (auto const& fp : kFingerprints) {
        auto s = makeState(fp.seed, false);
        ctx.require(s->gen != nullptr, "generator");
        GoldenWorld w{fp, s, s->grid, core::biome::obfuscateSeed(fp.seed)};
        auto        sampler = w.sampler();
        BlockBiomeFn biome  = [&](int x, int y, int z) -> std::string const& { return w.blockBiome(x, y, z); };
        for (int cx = fp.sc0; cx < fp.sc0 + fp.sn; ++cx)
            for (int cz = fp.sc0; cz < fp.sc0 + fp.sn; ++cz) {
                Grid grid(-32, 288);
                s->gen->fillNoise(cx, cz, sampler, {}, grid);
                s->gen->surface().buildSurface(cx, cz, grid, s->table, biome);
                std::vector<std::string> chunkBiomes;
                for (int qx = cx * 4; qx < cx * 4 + 4; ++qx)
                    for (int qz = cz * 4; qz < cz * 4 + 4; ++qz)
                        if (auto const* c = w.column(qx, qz))
                            for (int qy = -8; qy <= 63; ++qy)
                                if (std::find(chunkBiomes.begin(), chunkBiomes.end(), c->biomeAt(qy)) == chunkBiomes.end()) chunkBiomes.push_back(c->biomeAt(qy));
                s->blankets.apply(cx, cz, grid, s->table, chunkBiomes, biome, [](auto const&) { return false; });
                for (int x = 0; x < 16; x += fp.sstep)
                    for (int z = 0; z < 16; z += fp.sstep) {
                        int bx = cx * 16 + x, bz = cz * 16 + z;
                        int i = (bx - fp.sx0) / fp.sstep, j = (bz - fp.sx0) / fp.sstep;
                        auto const& e = fp.surface[static_cast<std::size_t>(i * fp.sw + j)];
                        int  ws  = grid.worldSurfaceWg(x, z) + 1;
                        auto top = idOf(*s, grid.get(x, ws - 1, z));
                        if (grid.get(x, ws - 1, z) == code::DefaultBlock) top = "minecraft:stone";
                        if (grid.get(x, ws - 1, z) == code::Water) top = "minecraft:water";
                        ++checked;
                        if (ws != e.height || top != e.top) {
                            if (bad++ < 5) ctx.check(false, "seed " + std::to_string(fp.seed) + " (" + std::to_string(bx) + ", " + std::to_string(bz) + "): " + top + " at " + std::to_string(ws) + " != " + e.top + " at " + std::to_string(e.height));
                        }
                    }
            }
    }
    ctx.info("surface cells checked " + std::to_string(checked) + ", mismatches " + std::to_string(bad));
    ctx.check(bad == 0 && checked > 0, "surface heights and top blocks equal the F13 goldens for 3 seeds");
});

GF_REGISTER_SELFTEST("terrain.surface_rules", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    // a flat stone column at y 0..20 under air, per biome: the top block the rules choose
    auto topFor = [&](std::string const& biomeId, int x, int z) {
        Grid g(-32, 288);
        for (int bx = 0; bx < 16; ++bx)
            for (int bz = 0; bz < 16; ++bz)
                for (int y = -32; y <= 20; ++y) g.set(bx, y, bz, code::DefaultBlock);
        s->gen->surface().buildSurface(x >> 4, z >> 4, g, s->table, [&](int, int, int) -> std::string const& { return biomeId; });
        return std::make_pair(idOf(*s, g.get(x & 15, 20, z & 15)), idOf(*s, g.get(x & 15, 19, z & 15)));
    };
    std::set<std::string> highland;
    for (int i = 0; i < 64; ++i) highland.insert(topFor("twilightforest:highlands", i * 37, i * 11).first);
    ctx.check(highland.contains("minecraft:podzol") && highland.contains("minecraft:coarse_dirt"), "highlands: podzol and coarse dirt by the surface noise");
    auto [thornTop, thornBelow] = topFor("twilightforest:thornlands", 3, 3);
    ctx.check(thornTop == "twilightforest:weathered_deadrock", "thornlands: weathered deadrock on the floor (" + thornTop + ")");
    ctx.check(thornBelow == "twilightforest:cracked_deadrock" || thornBelow == "twilightforest:deadrock", "thornlands: deadrock below (" + thornBelow + ")");
    ctx.check(topFor("twilightforest:final_plateau", 3, 3).first == "twilightforest:weathered_deadrock", "final plateau: weathered deadrock");
    ctx.check(topFor("twilightforest:snowy_forest", 3, 3).first == "minecraft:snow_block", "snowy forest: snow block");
    ctx.check(topFor("twilightforest:glacier", 3, 3).first == "minecraft:gravel", "glacier: gravel");
    ctx.check(topFor("twilightforest:forest", 3, 3).first == "minecraft:grass_block", "forest: grass");
    ctx.check(topFor("twilightforest:forest", 3, 3).second == "minecraft:dirt", "forest: dirt under the grass");
    ctx.check(topFor("twilightforest:lake", 3, 3).first == "minecraft:grass_block", "lake shore above water: grass");
    // the bedrock floor
    Grid g(-32, 288);
    for (int y = -32; y <= 0; ++y) g.set(0, y, 0, code::DefaultBlock);
    s->gen->surface().buildSurface(0, 0, g, s->table, [](int, int, int) -> std::string const& {
        static std::string const f = "twilightforest:forest";
        return f;
    });
    ctx.check(idOf(*s, g.get(0, -32, 0)) == "minecraft:bedrock", "bedrock at the bottom");
});

GF_REGISTER_SELFTEST("terrain.canopy_blanket", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    Grid g(-32, 288);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = -32; y <= 4; ++y) g.set(x, y, z, code::DefaultBlock);
    static std::string const dark = "twilightforest:dark_forest";
    s->blankets.apply(0, 0, g, s->table, {dark}, [](int, int, int) -> std::string const& { return dark; }, [](auto const&) { return false; });
    // 9 cells of dark forest everywhere: thickness 5, from surface + 14 - 2 for 5 blocks
    int found = 0;
    for (int y = 4 + 14 - 2; y < 4 + 14 - 2 + 5; ++y)
        if (idOf(*s, g.get(7, y, 7)) == "twilightforest:hardened_dark_leaves") ++found;
    ctx.check(found == 5, "five hardened dark leaves at surface + 12 .. 16 (" + std::to_string(found) + ")");
    ctx.check(g.get(7, 4 + 14 - 3, 7) == code::Air && g.get(7, 4 + 14 + 3, 7) == code::Air, "air below and above the canopy");
    // a dark tower nearby clears it
    Grid g2(-32, 288);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z) g2.set(x, 4, z, code::DefaultBlock);
    auto c = core::landmark::nearestCenter(0, 0);
    s->blankets.apply(c.x >> 4, c.z >> 4, g2, s->table, {dark}, [](int, int, int) -> std::string const& { return dark; }, [](auto const&) { return true; });
    ctx.check(g2.get(c.x & 15, 18, c.z & 15) == code::Air, "no canopy at a dark tower's center");
});

GF_REGISTER_SELFTEST("terrain.glacier_blanket", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    Grid g(-32, 288);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = -32; y <= 2; ++y) g.set(x, y, z, code::DefaultBlock);
    static std::string const glacier = "twilightforest:glacier";
    s->blankets.apply(0, 0, g, s->table, {glacier}, [](int, int, int) -> std::string const& { return glacier; }, [](auto const&) { return false; });
    int packed = 0;
    for (int y = 3; y < 35; ++y)
        if (idOf(*s, g.get(5, y, 9)) == "minecraft:packed_ice") ++packed;
    ctx.check(packed == 32, "32 packed ice above the ground (" + std::to_string(packed) + ")");
    ctx.check(idOf(*s, g.get(5, 35, 9)) == "minecraft:ice", "capped with ice");
});

// The Skylight preset over the goldens' real biomes (the fingerprint's main biome grid of 3 seeds):
// islands of stone where the island noise is high, in a band around the biome terrain's height,
// floating over air (no sea), stone exactly where the density is above 0.
GF_REGISTER_SELFTEST("terrain.skylight_option", [](Context& ctx) {
    int scanned = 0, islandColumns = 0, chunks = 0, solid = 0, fluid = 0, disagree = 0, floating = 0;
    for (auto const& fp : kFingerprints) {
        auto sky = makeState(fp.seed, true);
        ctx.require(sky->gen != nullptr, "the skylight generator builds (" + sky->error + ")");
        ctx.check(sky->gen->defaultFluid() == code::Air, "skylight: no sea (default fluid air)");
        GoldenWorld w{fp, sky, sky->grid, core::biome::obfuscateSeed(fp.seed)};
        auto        sampler = w.sampler();
        int         minY    = sky->gen->shape().minY;
        std::vector<std::pair<int, int>> found;
        // the columns whose sampleTerrain neighbourhood (3 quarts) lies inside the biome grid
        for (int x = (fp.qx0 + 4) * 4; x < (fp.qx0 + fp.qw - 4) * 4; x += 16)
            for (int z = (fp.qz0 + 4) * 4; z < (fp.qz0 + fp.qh - 4) * 4; z += 16) {
                ++scanned;
                if (sky->gen->baseHeight(x, z, false, sampler) > minY) found.emplace_back(x, z);
            }
        islandColumns += static_cast<int>(found.size());
        for (std::size_t k = 0; k < std::min<std::size_t>(found.size(), 3); ++k) {
            auto [x, z] = found[found.size() * k / 3];
            Grid g(minY, sky->gen->shape().height);
            sky->gen->fillNoise(x >> 4, z >> 4, sampler, {}, g);
            std::vector<double> d;
            sky->gen->density().fillChunk(x >> 4, z >> 4, sampler, d);
            if (d.size() != g.cells.size()) {
                ctx.fail("the density and the noise fill disagree in size");
                return;
            }
            for (std::size_t i = 0; i < g.cells.size(); ++i) {
                solid += g.cells[i] == code::DefaultBlock;
                fluid += g.cells[i] == code::Water || g.cells[i] == code::Lava;
                disagree += (g.cells[i] == code::DefaultBlock) != (d[i] > 0.0);
            }
            int lowest = minY - 1;
            for (int y = minY; y < minY + g.height && lowest < minY; ++y)
                if (g.get(x & 15, y, z & 15) != code::Air) lowest = y;
            floating += lowest > minY + 8;
            ++chunks;
        }
    }
    ctx.info("skylight: " + std::to_string(islandColumns) + " of " + std::to_string(scanned) + " columns hold an island; " + std::to_string(chunks) + " island chunks, " +
             std::to_string(solid) + " stone");
    gf::log().info("terrain: skylight islands in {} of {} columns, {} chunks checked", islandColumns, scanned, chunks);
    ctx.check(islandColumns > 0 && islandColumns * 2 < scanned, "skylight: islands, and mostly void");
    ctx.check(chunks > 0 && solid > 0, "island chunks hold stone");
    ctx.check(fluid == 0, "skylight: no water or lava anywhere");
    ctx.check(disagree == 0, "stone exactly where the skylight density is above 0");
    ctx.check(floating == chunks, "every island floats (air down to the bottom below it)");
    // the world's preset
    auto stored = game::db::get("gf:terrain:skylight");
    ctx.check(stored && stored->is_boolean(), "the world keeps its preset (gf:terrain:skylight)");
    if (stored && stored->is_boolean()) ctx.check(skylightWorld() == stored->get<bool>(), "the preset in use is the world's");
});

GF_REGISTER_SELFTEST("terrain.chunk_pipeline", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    MemWorld w(0, 0, 0, s->seed);
    auto*    c = w.chunk(0, 0);
    terrainProvider(*c);
    surfaceProvider(*c, w);
    blanketProvider(*c, w);
    int stone = w.count("minecraft:stone"), bedrock = w.count("minecraft:bedrock");
    ctx.check(stone > 1000, "the noise terrain fills stone (" + std::to_string(stone) + ")");
    ctx.check(bedrock > 0, "a bedrock floor (" + std::to_string(bedrock) + ")");
    int top = c->height(worldgen::Heightmap::WorldSurfaceWg, 8, 8);
    ctx.info("chunk (0, 0) surface " + std::to_string(top) + ": " + kit::javaId(c->getBlock(8, top, 8)));
    // the landmark height provider answers the same terrain
    auto lh = gf::landmark::ext::firstOccupiedHeight(-1, 8, 8);
    ctx.check(!lh || *lh >= -32, "the landmark height provider answers");
});

GF_REGISTER_SELFTEST("terrain.carvers", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    auto const* cfgJson = [] {
        // the configured carvers live in F5's data (configured_carver); TF's values as a fallback
        static Json const tf = Json::parse(R"({"floor_level": -0.7, "horizontal_radius_multiplier": 1.05, "probability": 0.1, "vertical_radius_multiplier": 1.05,
            "y": {"type": "minecraft:uniform", "max_inclusive": {"absolute": -8}, "min_inclusive": {"above_bottom": 16}}, "yScale": 0.6})");
        return &tf;
    }();
    int carvedTotal = 0, exposed = 0, roofs = 0;
    for (int attempt = 0; attempt < 24 && carvedTotal < 50; ++attempt) {
        MemWorld w(attempt, 7, 0, s->seed);
        auto*    c = w.chunk(attempt, 7);
        terrainProvider(*c);
        surfaceProvider(*c, w);
        worldgen::ext::CarvingMask   mask(-32, 256);
        worldgen::ext::CarverContext cc{attempt, 7, 0, "twilightforest:tf_caves", cfgJson, &mask};
        for (int k = 0; k < 40; ++k) {
            jr::WorldgenRandom r(std::make_unique<jr::LegacyRandom>(0));
            r.setLargeFeatureSeed(s->seed + k, attempt, 7);
            carveWith(*s, false, *c, cc, r);
        }
        carvedTotal += mask.count();
        mask.forEach([&](int x, int y, int z) {
            if (!kit::isAir(c->getBlock(x, y, z))) return;
            for (auto [dx, dy, dz] : {std::array{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}}) {
                int nx = x + dx, ny = y + dy, nz = z + dz;
                if (nx < 0 || nz < 0 || nx > 15 || nz > 15 || ny < -32 || ny >= 256) continue;
                auto id = kit::javaId(c->getBlock(nx, ny, nz));
                if (id == "minecraft:water") ++exposed;
                if (dy == 1 && (id == "minecraft:dirt" || id == "minecraft:rooted_dirt" || id == "minecraft:coarse_dirt")) ++roofs;
            }
        });
    }
    ctx.info("tf_caves carved " + std::to_string(carvedTotal) + " blocks, dirt roof blocks " + std::to_string(roofs));
    ctx.check(carvedTotal > 0, "TF caves carve");
    ctx.check(exposed == 0, "carved air never touches water inside the chunk");
    ctx.check(roofs > 0, "dirt roofs over the caves");
});

GF_REGISTER_SELFTEST("terrain.chunk_blanketing_placement", [](Context& ctx) {
    MemWorld w(0, 0, 1, 1);
    auto const* stone = kit::javaBlock("minecraft:stone");
    ctx.require(stone != nullptr, "stone");
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z) w.chunk(0, 0)->setBlock(x, 3, z, *stone);
    Json placed = {{"feature", {{"type", "minecraft:simple_block"}, {"config", {{"to_place", {{"type", "minecraft:simple_state_provider"}, {"state", {{"Name", "minecraft:glass"}}}}}}}}},
                   {"placement", Json::array({{{"type", "twilightforest:chunk_blanketing"}, {"integrity", 1.0}, {"heightmap", "OCEAN_FLOOR_WG"}}})}};
    jr::Xoroshiro128pp r(5);
    gf::features::placeJson(placed, w, {0, 0, 0}, r);
    ctx.check(w.count("minecraft:glass") == 256, "one block per column at the heightmap (" + std::to_string(w.count("minecraft:glass")) + ")");
    MemWorld w2(0, 0, 1, 1);
    Json     locked = placed;
    locked["placement"][0]["integrity"]  = 0.7;
    locked["placement"][0]["biome_lock"] = Json::array({"twilightforest:thornlands"});
    gf::features::placeJson(locked, w2, {0, 0, 0}, r);
    ctx.check(w2.count("minecraft:glass") == 0, "biome_lock keeps other biomes out");
});

GF_REGISTER_SELFTEST("terrain.ores", [](Context& ctx) {
    char const* ores[][2] = {{"twilightforest:legacy_coal_ore", "minecraft:coal_ore"},       {"twilightforest:legacy_iron_ore", "minecraft:iron_ore"},
                             {"twilightforest:legacy_gold_ore", "minecraft:gold_ore"},       {"twilightforest:legacy_redstone_ore", "minecraft:redstone_ore"},
                             {"twilightforest:legacy_diamond_ore", "minecraft:diamond_ore"}, {"twilightforest:legacy_lapis_ore", "minecraft:lapis_ore"},
                             {"twilightforest:legacy_copper_ore", "minecraft:copper_ore"}};
    auto const* stone = kit::javaBlock("minecraft:stone");
    ctx.require(stone != nullptr, "stone");
    for (auto const& [id, ore] : ores) {
        ctx.check(kit::placedJson(id) != nullptr && kit::configuredJson(id) != nullptr, std::string(id) + " is loaded");
        MemWorld w(0, 0, 1, 9);
        for (int x = -8; x < 24; ++x)
            for (int z = -8; z < 24; ++z)
                for (int y = -32; y < 20; ++y) w.setBlock({x, y, z}, *stone);
        jr::Xoroshiro128pp r(17);
        bool               any = false;
        for (int i = 0; i < 6 && !any; ++i) any = gf::features::place(id, w, {8, -10 + i, 8}, r);
        ctx.check(w.count(ore) > 0, std::string(id) + " places " + ore);
    }
    for (char const* id : {"twilightforest:iron_oreberries", "twilightforest:gold_oreberries", "twilightforest:copper_oreberries", "twilightforest:iron_oreberries_enchanted_forest",
                           "twilightforest:gold_oreberries_enchanted_forest", "twilightforest:copper_oreberries_enchanted_forest", "twilightforest:essence_oreberries_enchanted_forest"})
        ctx.check(kit::placedJson(id) != nullptr, std::string(id) + " (placed) is loaded");
    ctx.check(kit::configuredJson("twilightforest:ore/wood_roots_spread") != nullptr, "twilightforest:ore/wood_roots_spread is loaded");
});

GF_REGISTER_SELFTEST("terrain.timing", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    std::vector<double> ms;
    for (int i = 0; i < 24; ++i) {
        MemWorld w(i % 6 + 40, i / 6 + 40, 0, s->seed);
        auto*    c     = w.chunk(i % 6 + 40, i / 6 + 40);
        auto     start = std::chrono::steady_clock::now();
        terrainProvider(*c);
        surfaceProvider(*c, w);
        blanketProvider(*c, w);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(ms.begin(), ms.end());
    double p95    = ms[static_cast<std::size_t>(ms.size() * 95 / 100)];
    double budget = config::get<TerrainConfig>().chunkBudgetMs;
    char   buf[160];
    std::snprintf(buf, sizeof buf, "terrain chunk p50 %.2f ms, p95 %.2f ms (budget %.0f ms)", ms[ms.size() / 2], p95, budget);
    ctx.info(buf);
    gf::log().info("terrain: {}", buf);
    ctx.check(p95 <= budget, "chunk generation p95 within the budget");
    auto live = chunkTimes();
    if (!live.empty()) {
        std::sort(live.begin(), live.end());
        ctx.info("world chunks: " + std::to_string(live.size()) + ", p95 " + std::to_string(live[live.size() * 95 / 100]) + " ms");
    }
});


// Generated samples in the world for the client's screenshots: 3 x 3 chunks of one key biome each
// (highlands, a dark forest under its canopy, a glacier), noise + surface + blankets of seed 0,
// copied into a flat area of the test row; with showcaseHoldTicks each stays that long.
GF_REGISTER_SELFTEST("terrain.showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = makeState(0, false);
    ctx.require(s->gen != nullptr && s->stone != nullptr, "generator");
    auto box = co_await ctx.flatArea(200, 48, 100);
    int  hold = config::get<TerrainConfig>().showcaseHoldTicks;
    for (char const* name : {"highlands", "dark_forest", "glacier"}) {
        std::string biome = std::string("twilightforest:") + name;
        auto const* column = s->grid.column(biome);
        ctx.require(column != nullptr, biome + " column");
        TerrainSampler sampler = [&](int x, int z) { return sampleTerrain(x, z, [&](int, int) { return column; }); };
        BlockBiomeFn   biomeFn = [&](int, int, int) -> std::string const& { return biome; };
        std::vector<std::unique_ptr<Grid>> grids;
        int minTop = 1000, maxTop = -1000;
        for (int i = 0; i < 9; ++i) {
            int  cx = i % 3, cz = i / 3;
            auto g  = std::make_unique<Grid>(-32, 288);
            s->gen->fillNoise(cx, cz, sampler, {}, *g);
            s->gen->surface().buildSurface(cx, cz, *g, s->table, biomeFn);
            s->blankets.apply(cx, cz, *g, s->table, {biome}, biomeFn, [](auto const&) { return false; });
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    int top = g->oceanFloorWg(x, z, s->table);
                    minTop  = std::min(minTop, top);
                    maxTop  = std::max(maxTop, g->worldSurfaceWg(x, z));
                }
            grids.push_back(std::move(g));
        }
        auto* bs = ctx.region();
        ctx.require(bs != nullptr, "test region");
        int yBase  = minTop - 4;                       // generated y copied to box.minY
        int height = std::min(maxTop - yBase + 2, 99);
        for (int i = 0; i < 9; ++i) {
            int cx = i % 3, cz = i / 3;
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    for (int dy = 0; dy < height; ++dy) {
                        auto         c = grids[static_cast<std::size_t>(i)]->get(x, yBase + dy, z);
                        Block const* b = s->block(c);
                        if (!b) continue;
                        game::setBlock(*bs, BlockPos{box.minX + cx * 16 + x, box.minY + dy, box.minZ + cz * 16 + z}, *b);
                    }
        }
        // the viewpoint: over the flat area's east edge (dx 247, inside U2's dx 200..299), looking
        // west over the sample
        int vx = box.minX + 47, vy = box.minY + (maxTop - yBase) + 14, vz = box.minZ + 24;
        // a glass block to stand on at the viewpoint (restored by the test)
        ctx.setBlock(core::GlobalPos{ctx.dimension(), vx, vy - 1, vz}, "minecraft:glass");
        gf::log().info("terrain showcase {} ready at {} {} {} (generated surface {}..{})", name, vx, vy, vz, minTop, maxTop);
        ctx.info(std::string(name) + " sample copied");
        if (hold > 0) co_await ctx.ticks(hold);
        // clear for the next sample
        for (int x = box.minX; x < box.minX + 48; ++x)
            for (int z = box.minZ; z < box.minZ + 48; ++z)
                for (int dy = 0; dy < height; ++dy) game::setBlock(*bs, BlockPos{x, box.minY + dy, z}, *s->block(code::Air));
    }
});

// ---- the beardifier ---------------------------------------------------------------------------------------

/// Beardifier#compute for one Rigid written out (Minecraft 1.21.1), to compare with.
double javaRigid(BlockBox const& b, beard::TerrainAdjustment a, int delta, int x, int y, int z) {
    using A = beard::TerrainAdjustment;
    int i1 = std::max(0, std::max(b.minX - x, x - b.maxX));
    int j1 = std::max(0, std::max(b.minZ - z, z - b.maxZ));
    int k1 = b.minY + delta;
    int l1 = y - k1;
    int i2 = a == A::Bury || a == A::BeardThin ? l1
           : a == A::BeardBox                  ? std::max(0, std::max(k1 - y, y - b.maxY))
           : a == A::Encapsulate               ? std::max(0, std::max(b.minY - y, y - b.maxY))
                                               : 0;
    switch (a) {
    case A::Bury:
        return core::landmark::buryContribution(i1, i2 / 2.0, j1);
    case A::BeardThin:
    case A::BeardBox:
        return core::landmark::beardContribution(i1, i2, j1, l1) * 0.8;
    case A::Encapsulate:
        return core::landmark::buryContribution(i1 / 2.0, i2 / 2.0, j1 / 2.0) * 0.8;
    default:
        return 0.0;
    }
}

gf::landmark::ext::StartPiece testPiece(std::string const& type, BlockBox const& box, Json tag = Json::object()) {
    tag["id"] = type;
    tag["BB"] = Json::array({box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ});
    return gf::landmark::ext::StartPiece{type, box, tag, true, -1};
}

// NeoForge's Beardifier#forStructuresInChunk per piece: a fake start whose pieces are a
// TFStructureComponentOld (NONE), a CourtyardTerrace (BEARD_BOX, ground delta 3), a protected
// stronghold piece (BURY), a plain template piece (the structure's beard_thin), a TwilightJigsawPiece
// (its tag's ENCAPSULATE, delta 2) and a type registered through ext::registerPieceBeard; the
// densities against Beardifier's arithmetic; the labyrinth (bury, every maze piece NONE) and the
// dark tower (beard_thin, only its entrance BEARD_BOX).
GF_REGISTER_SELFTEST("terrain.beardifier", [](Context& ctx) {
    using A = beard::TerrainAdjustment;
    namespace te = gf::terrain::ext;
    BlockBox a{0, 10, 0, 8, 16, 8}, b{20, 10, 0, 28, 16, 8}, c{0, 10, 20, 8, 16, 28}, d{20, 10, 20, 28, 16, 28}, e{40, 10, 0, 48, 16, 8}, f{40, 10, 20, 48, 16, 28};
    te::registerPieceBeard("gf_test:beard_piece", [](gf::landmark::ext::StartPiece const& p) { return te::PieceBeard{true, "bury", p.box, 1}; });
    struct Unregister {
        Unregister()                             = default;
        Unregister(Unregister const&)            = delete;
        Unregister& operator=(Unregister const&) = delete;
        ~Unregister() { te::registerPieceBeard("gf_test:beard_piece", {}); }
    } unregister;
    std::vector<gf::landmark::ext::StartPiece> pieces{testPiece("twilightforest:TFMMC", a),
                                                  testPiece("twilightforest:TFNCTe", b),
                                                  testPiece("twilightforest:TFSSH", c),
                                                  testPiece("twilightforest:TFQuest1", d),
                                                  testPiece("twilightforest:TFJigsawTemplate", e, {{"terrain_adaptation", "ENCAPSULATE"}, {"ground_offset", 2}}),
                                                  testPiece("gf_test:beard_piece", f)};
    auto rigids = rigidsOfPieces(pieces, A::BeardThin);
    ctx.require(rigids.size() == 5, "five Rigids: the NONE piece adds none (" + std::to_string(rigids.size()) + ")");
    struct Expected {
        BlockBox box;
        A        adj;
        int      delta;
    };
    std::vector<Expected> expected{{b, A::BeardBox, 3}, {c, A::Bury, 0}, {d, A::BeardThin, 0}, {e, A::Encapsulate, 2}, {f, A::Bury, 1}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto const& r = rigids[i].rigid;
        ctx.check(r.box == expected[i].box && r.adjustment == expected[i].adj && r.groundLevelDelta == expected[i].delta,
                  "Rigid " + std::to_string(i) + " has the piece's own box, adjustment and ground delta");
    }
    std::vector<beard::Rigid> plain;
    for (auto const& pr : rigids) plain.push_back(pr.rigid);
    int points = 0, bad = 0;
    for (int x = -12; x <= 60; x += 3)
        for (int y = 0; y <= 30; y += 2)
            for (int z = -12; z <= 40; z += 4) {
                double want = 0.0;
                for (auto const& ex : expected) want += javaRigid(ex.box, ex.adj, ex.delta, x, y, z);
                ++points;
                if (beard::beardifierDensity(plain, x, y, z) != want) ++bad;
            }
    ctx.check(bad == 0, "the beardifier density equals Beardifier#compute at " + std::to_string(points) + " points (" + std::to_string(bad) + " differ)");
    // a piece 12 blocks from a chunk still counts (StructurePiece#isCloseToChunk), 13 does not
    ctx.check(beard::isCloseToChunk({-12, 0, 0, -12, 4, 4}, 0, 0) && !beard::isCloseToChunk({-13, 0, 0, -13, 4, 4}, 0, 0), "isCloseToChunk(12)");
    auto maze = rigidsOfPieces({testPiece("twilightforest:TFMMaze", a), testPiece("twilightforest:TFMMC", b), testPiece("twilightforest:TFMMRB", c)}, A::Bury);
    ctx.check(maze.empty(), "the labyrinth (structure bury): its TFStructureComponentOld pieces add no beard");
    auto tower = rigidsOfPieces({testPiece("twilightforest:TFDTMai", a), testPiece("twilightforest:TFDTEnt", b), testPiece("twilightforest:TFDTWin", c)}, A::BeardThin);
    ctx.check(tower.size() == 1 && tower[0].rigid.adjustment == A::BeardBox && tower[0].rigid.groundLevelDelta == 1 && tower[0].rigid.box == b,
              "the dark tower (structure beard_thin): one BEARD_BOX at its entrance, ground delta 1");
    // a piece of no known type (a start the store does not hold): the structure's adaptation
    auto unknown = rigidsOfPieces({gf::landmark::ext::StartPiece{"", a, Json{{"BB", Json::array({a.minX, a.minY, a.minZ, a.maxX, a.maxY, a.maxZ})}}, false, -1}}, A::Bury);
    ctx.check(unknown.size() == 1 && unknown[0].rigid.adjustment == A::Bury && unknown[0].rigid.box == a, "an untyped piece takes the structure's adaptation");
});

// The canopy's avoid_structures (ChunkAccess#getAllReferences): a valid start clears the chunk, F6's
// reserved site (a structure without its generator yet: the clearance square, no pieces) does not.
GF_REGISTER_SELFTEST("terrain.canopy_avoid", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    auto row = ctx.testPos(260);
    // far from every test row and from F5's forced starts: no chunk there is loaded, nothing is placed
    core::GlobalPos center{row.dim, ((row.x + 300000) & ~15) + 8, 64, (row.z & ~15) + 6};
    constexpr char  kOpen[]  = "gf_test:landmark_fixture_open";
    constexpr char  kTower[] = "twilightforest:dark_tower";
    {
        gf::landmark::ext::ScopedStart open(kOpen, center);
        ctx.require(open && open->generated, "F6 forced its open fixture landmark");
        ctx.check(chunkReferences(center.dim, center.x >> 4, center.z >> 4, {kOpen}), "a valid start is a reference of its chunk");
        ctx.check(!chunkReferences(center.dim, center.x >> 4, center.z >> 4, {kTower}), "only the listed structures count");
    }
    core::GlobalPos towerAt{row.dim, center.x + 160, 64, center.z};
    gf::landmark::ext::ScopedStart tower(kTower, towerAt);
    ctx.require(static_cast<bool>(tower), "F6 forced a dark tower start");
    int  tcx = towerAt.x >> 4, tcz = towerAt.z >> 4;
    auto listed = gf::landmark::startsIntersecting(towerAt.dim, {tcx, tcz});
    ctx.check(std::any_of(listed.begin(), listed.end(), [&](gf::landmark::Start const& st) { return st.key == kTower; }), "startsIntersecting lists the dark tower start");
    bool valid = tower->generated && !tower->pieceBoxes.empty();
    bool refs  = chunkReferences(towerAt.dim, tcx, tcz, {kTower});
    ctx.check(refs == valid, valid ? "the dark tower (with pieces) is a reference of its chunk" : "the reserved dark tower site (no pieces before U26) is no reference");
    // the canopy blanket over that chunk, with the provider's avoid test
    static std::string const dark = "twilightforest:dark_forest";
    Grid g(-32, 288);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = -32; y <= 4; ++y) g.set(x, y, z, code::DefaultBlock);
    s->blankets.apply(tcx, tcz, g, s->table, {dark}, [](int, int, int) -> std::string const& { return dark; },
                      [&](std::vector<std::string> const& avoid) { return chunkReferences(towerAt.dim, tcx, tcz, avoid); });
    int leaves = 0;
    for (int y = 4 + 14 - 2; y < 4 + 14 - 2 + 5; ++y)
        if (idOf(*s, g.get(towerAt.x & 15, y, towerAt.z & 15)) == "twilightforest:hardened_dark_leaves") ++leaves;
    ctx.check(valid ? leaves == 0 : leaves == 5, valid ? "no canopy over the dark tower's chunk" : "the canopy stays over a reserved site (" + std::to_string(leaves) + " leaves)");
});

// ---- real generation ----------------------------------------------------------------------------------------

// The per-chunk budget on chunks the game generates in the Twilight Forest (noise, surface, blankets,
// carvers through F5's pipeline into Bedrock chunks, with F6's starts and terraformers): the test
// moves into the dimension, loads chunks no run generated before and checks the p95 of their times.
GF_REGISTER_SELFTEST("terrain.timing_world", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!gf::world::twilight()) {
        ctx.info("the Twilight Forest dimension is not registered (F4)");
        co_return;
    }
    ctx.require(co_await ctx.inTwilight(), "the test moves into the Twilight Forest");
    std::uint64_t before = chunkTimesRecorded();
    // fresh chunks: a place picked by the clock, far from every other test and trip
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int  x     = 2000000 + static_cast<int>(stamp % 40000) * 96;
    int  z     = -2000000 - static_cast<int>((stamp / 40000) % 1000) * 96;
    ctx.require(co_await ctx.ensureLoaded(core::GlobalPos{ctx.dimension(), x, 64, z}), "fresh Twilight Forest chunks load");
    for (int i = 0; i < 40 && chunkTimesRecorded() - before < 25; ++i) co_await ctx.ticks(5);
    std::uint64_t n   = chunkTimesRecorded() - before;
    auto          all = chunkTimes();
    ctx.require(n > 0, "the game generated Twilight Forest chunks through the terrain providers");
    std::vector<double> ms(all.end() - static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(n, all.size())), all.end());
    std::sort(ms.begin(), ms.end());
    double p95    = ms[std::min(ms.size() - 1, ms.size() * 95 / 100)];
    double budget = config::get<TerrainConfig>().chunkBudgetMs;
    char   buf[200];
    std::snprintf(buf, sizeof buf, "world chunk p50 %.2f ms, p95 %.2f ms over %zu chunks (budget %.0f ms)", ms[ms.size() / 2], p95, ms.size(), budget);
    ctx.info(buf);
    gf::log().info("terrain: {}", buf);
    ctx.check(p95 <= budget, "generated chunks' p95 within the budget");
});

Player* taggedPlayer(char const* tag) {
    Player* found = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer([&](Player& p) {
            if (p.hasTag(tag)) found = &p;
            return found == nullptr;
        });
    return found;
}

/// Where terrain.live_view took the tagged player from: kept in the world's DB, so it never sends
/// the player of another world (the client loads several in one process) anywhere.
constexpr char kLiveViewKey[] = "gf:terrain:liveview";

/// The landscapes the client e2e looks at, in order (TF 4.8's Twilight Forest biomes with a terrain
/// or a blanket of their own first).
constexpr std::array<char const*, 9> kLiveBiomes{"twilightforest:dark_forest", "twilightforest:highlands",       "twilightforest:glacier",
                                                 "twilightforest:snowy_forest", "twilightforest:enchanted_forest", "twilightforest:swamp",
                                                 "twilightforest:fire_swamp",   "twilightforest:thornlands",       "twilightforest:forest"};

/// A spot well inside each biome of kLiveBiomes (its main biome on a 48-block square around it),
/// searched in one pass over 32-block rings around (x0, z0), nearest first.
std::map<std::string, std::pair<int, int>> findBiomes(int x0, int z0, int radius) {
    std::map<std::string, std::pair<int, int>> found;
    auto visit = [&](int x, int z) {
        auto biome = worldgen::mainBiomeAt(x, z);
        if (found.contains(biome) || std::find(kLiveBiomes.begin(), kLiveBiomes.end(), biome) == kLiveBiomes.end()) return;
        for (int k = 0; k < 4; ++k)
            if (worldgen::mainBiomeAt(x + ((k & 1) ? 24 : -24), z + ((k & 2) ? 24 : -24)) != biome) return;
        found.emplace(biome, std::pair{x, z});
    };
    visit(x0, z0);
    for (int r = 32; r <= radius && found.size() < kLiveBiomes.size(); r += 32)
        for (int d = -r; d < r; d += 32) { // the ring's four edges, each corner once
            visit(x0 + d, z0 - r);
            visit(x0 + r, z0 + d);
            visit(x0 - d, z0 + r);
            visit(x0 - r, z0 - d);
        }
    return found;
}
// The client e2e's look at real Twilight Forest terrain: finds, around chunks no one generated
// before, a spot in each landscape of kLiveBiomes the biome source (U1) gives (without one: the
// forest column everywhere, one spot), logs each one ("terrain live view target ...") and sends the
// player tagged gf_terrain_live high above the first; the scenario flies to the others.
// terrain.live_view_restore brings the player back.
GF_REGISTER_SELFTEST("terrain.live_view", [](Context& ctx) {
    auto twilight = gf::world::twilight();
    if (!twilight) {
        ctx.info("the Twilight Forest dimension is not registered (F4)");
        return;
    }
    Player* shown = taggedPlayer("gf_terrain_live");
    if (!shown) {
        ctx.info("no player tagged gf_terrain_live");
        return;
    }
    // a return point kept by an aborted run is replaced unless the player still stands in the
    // live view's area (x >= 60000 in the Twilight Forest), where that point is the one to go back to
    auto here   = shown->getFeetPos();
    bool inView = static_cast<int>(shown->getDimensionId()) == static_cast<int>(*twilight) && here.x >= 60000.0F;
    if (!inView || !game::db::get(kLiveViewKey)) {
        auto back = here;
        game::db::put(kLiveViewKey, Json{{"dim", static_cast<int>(shown->getDimensionId())}, {"x", back.x}, {"y", back.y}, {"z", back.z}});
    }
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int  x0    = 60000 + static_cast<int>(stamp % 400) * 4096 + 8;
    int  z0    = 60000 + 8;
    int  dim   = static_cast<int>(*twilight);
    struct Target {
        std::string biome;
        int         x, z, h;
    };
    std::vector<Target> targets;
    auto found = findBiomes(x0, z0, 1536);
    for (char const* b : kLiveBiomes)
        if (auto it = found.find(b); it != found.end()) {
            auto [x, z] = it->second;
            auto h      = gf::landmark::ext::firstOccupiedHeight(dim, x, z, worldgen::Heightmap::WorldSurfaceWg);
            targets.push_back({b, x, z, h ? *h : 32});
        }
    if (targets.empty()) {
        // no biome source: every column is the forest's (main biome '')
        auto h = gf::landmark::ext::firstOccupiedHeight(dim, x0, z0, worldgen::Heightmap::WorldSurfaceWg);
        targets.push_back({worldgen::mainBiomeAt(x0, z0), x0, z0, h ? *h : 32});
    }
    for (auto const& t : targets) {
        gf::log().info("terrain live view target {} at {} {} {} (terrain height {})", t.biome.empty() ? std::string("none") : t.biome, t.x, t.h + 40, t.z, t.h);
    }
    ctx.info(std::to_string(targets.size()) + " landscape(s) found around " + std::to_string(x0) + ", " + std::to_string(z0));
    auto const& first = targets.front();
    ctx.check(gf::world::teleport(*shown, *twilight, Vec3{static_cast<float>(first.x) + 0.5F, static_cast<float>(first.h + 40), static_cast<float>(first.z) + 0.5F}, 0.0F),
              "the player is sent into new Twilight Forest chunks");
    gf::log().info("terrain live view at {} {} {} (terrain height {}, biome {})", first.x, first.h + 40, first.z, first.h, first.biome);
});

GF_REGISTER_SELFTEST("terrain.live_view_restore", [](Context& ctx) {
    auto v = game::db::get(kLiveViewKey);
    if (!v || !v->is_object()) {
        ctx.info("nothing to restore");
        return;
    }
    game::db::erase(kLiveViewKey);
    if (Player* shown = taggedPlayer("gf_terrain_live")) {
        shown->removeTag("gf_terrain_live");
        Vec3 back{v->value("x", 0.0F), v->value("y", 64.0F), v->value("z", 0.0F)};
        (void)gf::world::teleport(*shown, DimensionType(v->value("dim", 0)), back, 0.0F);
    }
    gf::log().info("terrain live view: the player is back");
});

// ---- the grid handoff and the passes it trusts ---------------------------------------------------------------

// The noise grid goes to the surface step only in a pass whose writers are known: F5's global
// providers take it (no other writer on main), a pass on a copy of them with another unit's
// Stage::Noise step reads the chunk, so the surface sees that step's blocks (an air pocket under
// the top stone stays air instead of taking the stale grid's dirt).
GF_REGISTER_SELFTEST("terrain.handoff_pass", [](Context& ctx) {
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    auto noiseSteps   = gf::terrain::ext::stageStepIds(worldgen::Stage::Noise);
    bool otherWriters = std::any_of(noiseSteps.begin(), noiseSteps.end(), [](std::string const& id) { return id != "terrain.pass_probe"; });
    ctx.check(std::find(noiseSteps.begin(), noiseSteps.end(), "terrain.pass_probe") != noiseSteps.end(), "the pass probe is the global providers' Stage::Noise step");
    auto before = handoffCounts();
    (void)worldgen::ext::runInMemory(s->seed, 40, -40, 2, 2);
    auto after = handoffCounts();
    ctx.info("global providers: " + std::to_string(after.first - before.first) + " grids taken, " + std::to_string(after.second - before.second) + " read back");
    if (!otherWriters) ctx.check(after.first > before.first, "a pass on F5's global providers hands its grids on");
    // a copy with a foreign Stage::Noise writer: air pockets under every column's top stone
    auto const* air   = kit::javaBlock("minecraft:air");
    auto const* stone = kit::javaBlock("minecraft:stone");
    ctx.require(air && stone, "air and stone");
    std::vector<std::array<int, 3>> pockets;
    before = handoffCounts();
    gf::terrain::ext::generateOnCopyWithNoiseStep(
        s->seed, 41, -40, -32, 256,
        [&](worldgen::Chunk& c, worldgen::Region&) {
            if (c.chunkX() != 41 || c.chunkZ() != -40) return;
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    int y = c.maxY() - 1;
                    while (y > c.minY() && kit::isAir(c.getBlock(x, y, z))) --y;
                    if (&c.getBlock(x, y, z) != stone || y - 2 <= c.minY()) continue; // water or bedrock on top: no pocket
                    c.setBlock(x, y - 1, z, *air);
                    c.setBlock(x, y - 2, z, *air);
                    pockets.push_back({x, y - 1, z});
                }
        },
        [&](worldgen::Chunk& c) {
            int filled = 0;
            for (auto const& [x, y, z] : pockets)
                if (!kit::isAir(c.getBlock(x, y, z))) ++filled;
            ctx.info(std::to_string(pockets.size()) + " pockets, " + std::to_string(filled) + " filled after the surface");
            ctx.check(!pockets.empty(), "the foreign writer made pockets");
            ctx.check(filled == 0, "the surface step saw the foreign writer's blocks (no stale grid)");
        });
    after = handoffCounts();
    ctx.check(after.second > before.second, "the pass on a copy of the providers reads the chunk back");
});

// ---- Bedrock's liquid layer -----------------------------------------------------------------------------------

// TFCavesCarver asks BlockState#getFluidState: a waterlogged block (Bedrock: water in the liquid
// layer of a real game chunk) is no replaceable block, and a water neighbour stops a carve. Runs
// the carver's carveBlock on a loaded Twilight Forest chunk with water put into the liquid layer.
GF_REGISTER_SELFTEST("terrain.liquid_layer", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!gf::world::twilight()) {
        ctx.info("the Twilight Forest dimension is not registered (F4)");
        co_return;
    }
    ctx.require(co_await ctx.inTwilight(), "the test moves into the Twilight Forest");
    auto s = anyState();
    ctx.require(s != nullptr, "generator");
    auto base = ctx.testPos(270, 2);
    // P with both x neighbours inside its chunk, in the air over the test row (the stones around it go
    // back to air afterwards)
    int  px   = ((base.x & 15) == 0) ? base.x + 1 : (((base.x & 15) == 15) ? base.x - 1 : base.x);
    core::GlobalPos P{base.dim, px, base.y + 3, base.z};
    ctx.require(co_await ctx.ensureLoaded(P), "the test chunk is loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "block source");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 3; ++dy)
            for (int dz = -1; dz <= 1; ++dz) ctx.setBlock({P.dim, P.x + dx, P.y + dy, P.z + dz}, "minecraft:stone");
    auto chunk = gf::terrain::ext::loadedGameChunk(*region, P.x >> 4, P.z >> 4);
    ctx.require(chunk != nullptr, "the game chunk");
    auto layer = gf::terrain::ext::LiquidLayer::of(*chunk);
    ctx.require(static_cast<bool>(layer), "the liquid layer of a game chunk is readable (x-z-y order verified)");
    auto const* water = kit::javaBlock("minecraft:water");
    auto const* air   = kit::javaBlock("minecraft:air");
    ctx.require(water && air, "water and air");
    int  lx = P.x & 15, lz = P.z & 15;
    auto stoneAt = [&](int x) { return ctx.blockAt({P.dim, x, P.y, P.z}) == "minecraft:stone"; };
    worldgen::ext::CarvingMask   mask(chunk->minY(), chunk->maxY());
    worldgen::ext::CarverContext cc{P.x >> 4, P.z >> 4, 0, "twilightforest:tf_caves", nullptr, &mask};
    // 1. a waterlogged block holds water: the carver keeps it
    layer.set(lx, P.y, lz, *water);
    ctx.check(layer.at(lx, P.y, lz) == water, "the liquid layer holds the water");
    ctx.check(chunk->getBlock(lx, P.y, lz).getTypeName() == "minecraft:stone", "the standard layer keeps the stone");
    ctx.check(!carveBlockAt(*s, false, *chunk, cc, P.x, P.y, P.z), "a waterlogged block is not carved");
    ctx.check(stoneAt(P.x) && layer.at(lx, P.y, lz) == water, "the waterlogged block survives with its water");
    // 2. emptying the layer
    layer.clear(lx, P.y, lz, *air);
    ctx.check(layer.at(lx, P.y, lz) == nullptr, "clear empties the liquid layer");
    // 3. a waterlogged neighbour stops the carve
    layer.set(lx + 1, P.y, lz, *water);
    ctx.check(!carveBlockAt(*s, false, *chunk, cc, P.x, P.y, P.z), "a waterlogged neighbour stops the carve");
    ctx.check(stoneAt(P.x), "the block next to the water stays");
    layer.clear(lx + 1, P.y, lz, *air);
    ctx.check(layer.at(lx + 1, P.y, lz) == nullptr, "the neighbour's water is gone");
    // 4. without water the same block carves
    ctx.check(carveBlockAt(*s, false, *chunk, cc, P.x, P.y, P.z), "a dry block carves");
    ctx.check(!stoneAt(P.x), "the carved block is air (" + ctx.blockAt({P.dim, P.x, P.y, P.z}) + ")");
    // leave no water behind (the stones and the carved block go back to air with the test's cleanup)
    layer.clear(lx, P.y, lz, *air);
    layer.clear(lx + 1, P.y, lz, *air);
});

// ---- the world preset's saved-chunk check ------------------------------------------------------------------

// onWorldReady counts the Twilight Forest's saved chunks (their version records in the world DB)
// when a world has no kept preset: the scan reads the records the game saved (a new world may have
// saved none yet, so the counts are reported, not checked).
GF_REGISTER_SELFTEST("terrain.saved_chunks", [](Context& ctx) {
    auto start     = std::chrono::steady_clock::now();
    auto overworld = savedChunkCount(0);
    ctx.require(overworld.has_value(), "the world DB is readable");
    int  dim      = gf::world::twilight() ? static_cast<int>(*gf::world::twilight()) : -1;
    auto twilight = dim >= 0 ? savedChunkCount(dim) : std::optional<std::size_t>{};
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ctx.info("saved chunks: overworld " + std::to_string(*overworld) + ", Twilight Forest " + (twilight ? std::to_string(*twilight) : std::string("-")) + " (" +
             std::to_string(static_cast<int>(ms)) + " ms)");
    auto stored = game::db::get("gf:terrain:skylight");
    ctx.check(stored && stored->is_boolean(), "the world keeps its preset");
});
} // namespace
} // namespace gf::feature::terrain
