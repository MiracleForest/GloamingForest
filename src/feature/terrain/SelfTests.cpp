// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) selftests (terrain.*, testPos dx 200-299). Most run the providers on chunks held in
// memory (MemWorld below), so they do not depend on the e2e world's biomes; terrain.golden_* run the
// generator on the F13 goldens' biomes and compare with their heights / surface (fingerprint below).
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/JRand.h"
#include "feature/ext/landmark/LandmarkExt.h"
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

GF_REGISTER_SELFTEST("terrain.skylight_option", [](Context& ctx) {
    auto sky = makeState(42, true);
    ctx.require(sky->gen != nullptr, "the skylight generator builds (" + sky->error + ")");
    ctx.check(sky->gen->defaultFluid() == code::Air, "skylight: no sea (default fluid air)");
    int water = 0, solid = 0;
    for (int cx = 0; cx < 3; ++cx) {
        Grid g(-32, 288);
        sky->gen->fillNoise(cx, 0, [](int, int) { return DensityData{0.1, 1.25}; }, {}, g);
        for (auto c : g.cells) water += c == code::Water, solid += c == code::DefaultBlock;
    }
    ctx.check(water == 0, "skylight: no water");
    ctx.info("skylight sample: " + std::to_string(solid) + " solid blocks in 3 chunks");
    ctx.check(game::db::get("gf:terrain:skylight").has_value(), "the world keeps its preset (gf:terrain:skylight)");
    ctx.check(skylightWorld() == game::db::get("gf:terrain:skylight").value_or(false).get<bool>(), "the preset in use is the world's");
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
    auto lh = landmark::ext::firstOccupiedHeight(-1, 8, 8);
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
        int vx = box.minX - 14, vy = box.minY + (maxTop - yBase) + 12, vz = box.minZ + 24;
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

} // namespace
} // namespace gf::feature::terrain
