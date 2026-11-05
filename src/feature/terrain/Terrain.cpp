// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFDimensionData.java
//   src/main/java/twilightforest/init/TFDensityFunctions.java
//   src/main/java/twilightforest/init/custom/ChunkBlanketProcessors.java
//   src/main/java/twilightforest/asmhooks/WorldgenHooks.java
//   src/main/java/twilightforest/world/components/placements/ChunkBlanketingModifier.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U2 (terrain): the Twilight Forest's noise terrain, surface rules and chunk blankets as F5
// providers (setTerrain, setSurface, registerBlanket), TF's density function types for F5's
// Density, the landmark height provider (F6 ext::setHeightProvider), the chunk_blanketing
// placement modifier and the Skylight preset (config section "terrain", kept per world in
// gf:terrain:skylight).
//
// Java order kept: NoiseBasedChunkGenerator#fillFromNoise (density = final_density + beardifier,
// TF's WorldgenHooks#getCustomDensity adding the structure terraformers to the beardifier), then
// SurfaceSystem#buildSurface, then ChunkBlanketProcessors#chunkBlanketing (F5 runs the Blanket
// stage right after the surface), then the carvers (Carvers.cpp).
#include "feature/terrain/Terrain.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/Mth.h"
#include "core/landmark/Clearance.h"
#include "core/noise/Noise.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "feature/tags/Tags.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("terrain", gf::feature::terrain::TerrainConfig);

namespace gf::feature::terrain {

namespace fs = std::filesystem;
using Json   = nlohmann::json;
namespace kit = gf::features::kit;

namespace {

constexpr char kSkylightKey[] = "gf:terrain:skylight";

Json readJson(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return Json();
    std::stringstream ss;
    ss << in.rdbuf();
    return Json::parse(ss.str(), nullptr, false);
}

std::mutex& stateMutex() {
    static std::mutex m;
    return m;
}
std::shared_ptr<State const>& cachedState() {
    static std::shared_ptr<State const> s;
    return s;
}
std::atomic<int>& skylightFlag() { // -1 unknown, 0 / 1
    static std::atomic<int> v{-1};
    return v;
}

std::atomic<bool>& installed() {
    static std::atomic<bool> v{false};
    return v;
}

std::string const& emptyBiome() {
    static std::string const s;
    return s;
}

} // namespace

// ---- data -------------------------------------------------------------------------------------------------

Data const& data() {
    static Data const d = [] {
        Data   out;
        fs::path root;
        try {
            root = Mod::getInstance().getSelf().getModDir() / "data";
        } catch (...) {
            return out;
        }
        std::error_code ec;
        auto tf          = root / "twilightforest";
        out.twilight     = readJson(tf / "worldgen/noise_settings/twilight_noise_gen.json");
        out.skylight     = readJson(tf / "worldgen/noise_settings/skylight_noise_gen.json");
        out.biomeGrid    = readJson(tf / "twilight/biome_terrain_data/biome_grid.json");
        for (auto const& ns : fs::directory_iterator(root, ec)) {
            if (!ns.is_directory()) continue;
            auto nsName = ns.path().filename().string();
            auto dir    = ns.path() / "worldgen/density_function";
            if (fs::is_directory(dir, ec))
                for (auto const& f : fs::recursive_directory_iterator(dir, ec)) {
                    if (!f.is_regular_file() || f.path().extension() != ".json") continue;
                    auto rel = fs::relative(f.path(), dir, ec).generic_string();
                    out.densities[nsName + ":" + rel.substr(0, rel.size() - 5)] = readJson(f.path());
                }
            auto bdir = ns.path() / "twilight/chunk_blanket_processors";
            if (fs::is_directory(bdir, ec))
                for (auto const& f : fs::directory_iterator(bdir, ec)) {
                    if (!f.is_regular_file() || f.path().extension() != ".json") continue;
                    out.blankets[nsName + ":" + f.path().stem().string()] = readJson(f.path());
                }
        }
        return out;
    }();
    return d;
}

// ---- state ----------------------------------------------------------------------------------------------

TerrainColumn const* State::columnAt(int quartX, int quartZ) const {
    auto id = worldgen::mainBiomeAt(core::mth::toBlock(quartX), core::mth::toBlock(quartZ));
    if (auto const* c = grid.column(id)) return c;
    // no biome source (U1 not registered) or a biome outside the grid: the forest's column
    return grid.column("twilightforest:forest");
}

std::string const& State::noiseBiome(int quartX, int quartY, int quartZ) const {
    auto const* c = columnAt(quartX, quartZ);
    return c ? c->biomeAt(quartY) : emptyBiome();
}

std::string const& State::blockBiome(int x, int y, int z) const {
    auto q = core::biome::zoom(zoomSeed, x, y, z);
    return noiseBiome(q.x, q.y, q.z);
}

std::shared_ptr<State const> makeState(std::int64_t seed, bool skylight) {
    auto s      = std::make_shared<State>();
    s->seed     = seed;
    s->skylight = skylight;
    s->zoomSeed = core::biome::obfuscateSeed(seed);
    auto const& d = data();
    s->grid       = TerrainGrid::fromJson(d.biomeGrid);
    if (s->grid.empty()) {
        s->error = "biome_terrain_data/twilightforest:biome_grid is missing";
        return s;
    }
    Density::Resolver r;
    r.density = [&d](std::string const& id) -> std::optional<Json> {
        auto it = d.densities.find(id);
        if (it == d.densities.end()) return std::nullopt;
        return it->second;
    };
    auto const& settings = skylight ? d.skylight : d.twilight;
    s->gen               = Generator::create(settings, seed, r, s->table, &s->error);
    if (!s->gen) return s;
    for (auto const& [id, json] : d.blankets)
        if (!s->blankets.add(id, json, s->table)) gf::log().warn("terrain: chunk blanket processor {} not understood", id);
    // the blocks of every code
    s->blocks.assign(core::terrain::code::FirstNamed + s->table.entries().size(), nullptr);
    s->stone                                = kit::javaBlock(s->gen->defaultBlock());
    s->blocks[core::terrain::code::Air]     = kit::javaBlock("minecraft:air");
    s->blocks[core::terrain::code::DefaultBlock] = s->stone;
    s->blocks[core::terrain::code::Water]   = kit::javaBlock("minecraft:water");
    s->blocks[core::terrain::code::Lava]    = kit::javaBlock("minecraft:lava");
    for (std::size_t i = 0; i < s->table.entries().size(); ++i) {
        auto const& e = s->table.entries()[i];
        s->blocks[core::terrain::code::FirstNamed + i] = kit::javaBlock(e.name, e.properties);
        if (!s->blocks[core::terrain::code::FirstNamed + i]) gf::log().warn("terrain: block {} {} is unknown", e.name, e.properties.dump());
    }
    s->oreRandom  = randomStateFactory(seed)->fromHashOf("minecraft:ore")->forkPositional();
    s->caveAir    = kit::javaBlock("minecraft:cave_air");
    if (!s->caveAir) s->caveAir = s->blocks[core::terrain::code::Air];
    s->rootedDirt = kit::javaBlock("minecraft:rooted_dirt");
    s->coarseDirt = kit::javaBlock("minecraft:coarse_dirt");
    static std::atomic<std::uint64_t> generations{0};
    s->generation = ++generations;
    return s;
}

bool skylightWorld() {
    int v = skylightFlag().load();
    if (v >= 0) return v == 1;
    return config::get<TerrainConfig>().skylight;
}

std::shared_ptr<State const> state() {
    if (!installed()) return nullptr;
    std::int64_t seed     = worldgen::seed();
    bool         skylight = skylightWorld();
    static std::mutex buildMutex;
    std::lock_guard   building(buildMutex);
    {
        std::lock_guard lock(stateMutex());
        auto&           c = cachedState();
        if (c && c->seed == seed && c->skylight == skylight) return c->gen && c->stone ? c : nullptr;
    }
    auto s = makeState(seed, skylight);
    if (!s->gen || !s->stone) gf::log().error("terrain: the generator cannot be built: {}", s->error.empty() ? "default block unknown" : s->error);
    else gf::log().info("terrain: generator ready (seed {}, {})", seed, skylight ? "skylight" : "twilight_noise_gen");
    std::lock_guard lock(stateMutex());
    cachedState() = s;
    return s->gen && s->stone ? s : nullptr;
}

// ---- chunk columns --------------------------------------------------------------------------------------

ChunkColumns::ChunkColumns(State const& s, int chunkX, int chunkZ) : mS(s), mQx0(chunkX * 4 - kPad), mQz0(chunkZ * 4 - kPad) {
    for (int i = 0; i < kSide; ++i)
        for (int j = 0; j < kSide; ++j) mCols[static_cast<std::size_t>(i * kSide + j)] = s.columnAt(mQx0 + i, mQz0 + j);
}

TerrainColumn const* ChunkColumns::column(int qx, int qz) const {
    int i = qx - mQx0, j = qz - mQz0;
    if (i >= 0 && j >= 0 && i < kSide && j < kSide) return mCols[static_cast<std::size_t>(i * kSide + j)];
    return mS.columnAt(qx, qz);
}

std::string const& ChunkColumns::noiseBiome(int qx, int qy, int qz) const {
    auto const* c = column(qx, qz);
    return c ? c->biomeAt(qy) : emptyBiome();
}

std::string const& ChunkColumns::blockBiome(int x, int y, int z) const {
    auto q = core::biome::zoom(mS.zoomSeed, x, y, z);
    return noiseBiome(q.x, q.y, q.z);
}

TerrainSampler ChunkColumns::sampler() const {
    return [this](int x, int z) { return sampleTerrain(x, z, [this](int qx, int qz) { return column(qx, qz); }); };
}

std::vector<std::string> ChunkColumns::chunkBiomes(int minY, int maxY) const {
    std::vector<std::string> out;
    int                      qx0 = mQx0 + kPad, qz0 = mQz0 + kPad;
    for (int qx = qx0; qx < qx0 + 4; ++qx)
        for (int qz = qz0; qz < qz0 + 4; ++qz) {
            auto const* c = column(qx, qz);
            if (!c) continue;
            for (int qy = minY >> 2; qy <= (maxY - 1) >> 2; ++qy) {
                auto const& b = c->biomeAt(qy);
                if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
            }
        }
    return out;
}

// ---- grid <-> chunk -------------------------------------------------------------------------------------

namespace {

/// The code of a block (thread-local cache by permutation, reset when the state changes).
std::uint16_t classify(State const& s, Block const& b) {
    thread_local std::uint64_t                                  owner = 0;
    thread_local std::unordered_map<Block const*, std::uint16_t> cache;
    if (owner != s.generation) {
        cache.clear();
        owner = s.generation;
    }
    if (auto it = cache.find(&b); it != cache.end()) return it->second;
    std::uint16_t c;
    if (&b == s.stone) c = code::DefaultBlock;
    else if (kit::isAir(b)) c = code::Air;
    else if (kit::isWater(b)) c = code::Water;
    else if (kit::isLava(b)) c = code::Lava;
    else c = code::OtherSolid;
    cache.emplace(&b, c);
    return c;
}

} // namespace

void readGrid(State const& s, worldgen::Chunk& chunk, Grid& grid) {
    for (int y = grid.minY; y < grid.minY + grid.height; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) grid.set(x, y, z, classify(s, chunk.getBlock(x, y, z)));
}

int writeGrid(State const& s, worldgen::Chunk& chunk, Grid const& before, Grid const& after) {
    int n = 0;
    for (int y = after.minY; y < after.minY + after.height; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto c = after.get(x, y, z);
                if (c == before.get(x, y, z)) continue;
                if (auto const* b = s.block(c)) {
                    chunk.setBlock(x, y, z, *b);
                    ++n;
                }
            }
    return n;
}

// ---- timing ---------------------------------------------------------------------------------------------

namespace {
std::mutex& timesMutex() {
    static std::mutex m;
    return m;
}
std::deque<double>& times() {
    static std::deque<double> t;
    return t;
}
thread_local double tChunkMs = 0; // the chunk being generated on this thread (noise .. carvers)
} // namespace

void recordChunkMs(double ms) {
    std::lock_guard lock(timesMutex());
    times().push_back(ms);
    if (times().size() > 4096) times().pop_front();
}
void addChunkMs(double ms) { tChunkMs += ms; }
std::vector<double> chunkTimes() {
    std::lock_guard lock(timesMutex());
    return {times().begin(), times().end()};
}

namespace {
struct StageTimer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~StageTimer() { tChunkMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
};

int twilightDim() {
    if (auto t = gf::world::twilight()) return static_cast<int>(*t);
    return -1;
}
} // namespace

// ---- providers ------------------------------------------------------------------------------------------

namespace {
/// Beardifier#forStructuresInChunk: the pieces (Rigid boxes, ground level delta 0: TF has no
/// PoolElementStructurePiece) of the starts referencing the chunk whose structure has a terrain
/// adaptation, when the piece is within 12 blocks of the chunk (StructurePiece#isCloseToChunk).
std::vector<core::landmark::BoxDensity> beardPieces(int dim, int chunkX, int chunkZ) {
    std::vector<core::landmark::BoxDensity> out;
    int minX = chunkX * 16, minZ = chunkZ * 16;
    for (auto const& st : landmark::startsIntersecting(dim, {chunkX, chunkZ})) {
        if (!st.generated) continue;
        auto const* d = landmark::descriptor(st.key);
        if (!d) continue;
        std::string adaptation = "none";
        if (auto it = d->extra.find("gf"); it != d->extra.end() && it->is_object()) adaptation = it->value("terrain_adaptation", std::string("none"));
        auto adj = core::landmark::terrainAdjustmentOf(adaptation);
        if (adj == core::landmark::TerrainAdjustment::None) continue;
        for (auto const& box : st.pieceBoxes) {
            if (box.maxX < minX - 12 || box.minX > minX + 15 + 12 || box.maxZ < minZ - 12 || box.minZ > minZ + 15 + 12) continue;
            out.push_back(core::landmark::BoxDensity::make(box, 0, 0, adj));
        }
    }
    return out;
}
} // namespace

void terrainProvider(worldgen::Chunk& chunk) {
    tChunkMs = 0;
    StageTimer timer;
    auto       s = state();
    if (!s) return;
    ChunkColumns cols(*s, chunk.chunkX(), chunk.chunkZ());
    // the beardifier: Beardifier#forStructuresInChunk (the pieces of structures with a terrain
    // adaptation, as rigid boxes) plus WorldgenHooks#getCustomDensity (the TF terraformers)
    ExtraDensity extra;
    int          dim = twilightDim();
    if (dim >= 0) {
        auto rigid = beardPieces(dim, chunk.chunkX(), chunk.chunkZ());
        landmark::ext::ChunkTerraformer t;
        if (landmark::ext::hasTerraformers(dim, chunk.chunkX(), chunk.chunkZ())) t = landmark::ext::chunkTerraformer(dim, chunk.chunkX(), chunk.chunkZ());
        if (!rigid.empty() || t)
            extra = [rigid = std::move(rigid), t](int x, int y, int z) {
                double d0 = 0.0;
                for (auto const& r : rigid) d0 += r.compute(x, y, z);
                return t ? d0 + t(x, y, z) : d0;
            };
    }
    auto shape = s->gen->shape();
    Grid grid(shape.minY, shape.height);
    s->gen->fillNoise(chunk.chunkX(), chunk.chunkZ(), cols.sampler(), extra, grid);
    for (int y = shape.minY; y < shape.minY + shape.height; ++y) {
        if (y < chunk.minY() || y >= chunk.maxY()) continue;
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto c = grid.get(x, y, z);
                if (c == code::Air) continue;
                if (auto const* b = s->block(c)) chunk.setBlock(x, y, z, *b);
            }
    }
}

void surfaceProvider(worldgen::Chunk& chunk, worldgen::Region&) {
    StageTimer timer;
    auto       s = state();
    if (!s) return;
    ChunkColumns cols(*s, chunk.chunkX(), chunk.chunkZ());
    Grid         before(chunk.minY(), chunk.maxY() - chunk.minY());
    readGrid(*s, chunk, before);
    Grid after = before;
    s->gen->surface().buildSurface(chunk.chunkX(), chunk.chunkZ(), after, s->table, [&](int x, int y, int z) -> std::string const& {
        return cols.blockBiome(x, y, z);
    });
    writeGrid(*s, chunk, before, after);
}

void blanketProvider(worldgen::Chunk& chunk, worldgen::Region&) {
    StageTimer timer;
    auto       s = state();
    if (!s || s->blankets.processors().empty()) return;
    ChunkColumns cols(*s, chunk.chunkX(), chunk.chunkZ());
    auto*        ctx    = worldgen::ext::blanketContext();
    auto         biomes = ctx && !ctx->chunkBiomes.empty() ? ctx->chunkBiomes : cols.chunkBiomes(chunk.minY(), chunk.maxY());
    // cheap exit: no processor's biome in the chunk
    bool any = false;
    for (auto const& p : s->blankets.processors())
        for (auto const& b : p.biomes)
            if (std::find(biomes.begin(), biomes.end(), b) != biomes.end()) any = true;
    if (!any) return;
    Grid before(chunk.minY(), chunk.maxY() - chunk.minY());
    readGrid(*s, chunk, before);
    Grid after = before;
    int  dim   = twilightDim();
    s->blankets.apply(
        chunk.chunkX(), chunk.chunkZ(), after, s->table, biomes,
        [&](int x, int y, int z) -> std::string const& { return cols.blockBiome(x, y, z); },
        [&](std::vector<std::string> const& avoid) {
            // ChunkAccess#getAllReferences: the starts whose box reaches the chunk
            if (dim < 0) return false;
            for (auto const& st : landmark::startsIntersecting(dim, {chunk.chunkX(), chunk.chunkZ()}))
                if (std::find(avoid.begin(), avoid.end(), st.key) != avoid.end()) return true;
            return false;
        },
        [&] {
            if (ctx && ctx->regionRandom) (void)ctx->regionRandom->fork();
        }
    );
    writeGrid(*s, chunk, before, after);
}

namespace {

// ---- F5 Density: TF's types for other users of core/noise ----------------------------------------------

void registerDensityTypes() {
    using NDensity = gf::core::noise::Density;
    auto num = [](Json const& j, char const* k) { return j.contains(k) && j[k].is_number() ? j[k].get<double>() : 0.0; };
    NDensity::registerType("twilightforest:focused", [num](Json const& j, std::int64_t, NDensity::Resolver const&) -> NDensity::Evaluator {
        Focused f{static_cast<float>(num(j, "x_center")), static_cast<float>(num(j, "y_bottom")), static_cast<float>(num(j, "z_center")),
                  static_cast<float>(num(j, "radius")),   static_cast<float>(num(j, "near_value")), static_cast<float>(num(j, "far_value"))};
        return [f](gf::core::noise::DensityPoint const& p) { return f.compute(p.blockX, p.blockY, p.blockZ); };
    });
    NDensity::registerType("twilightforest:hollow_hill", [num](Json const& j, std::int64_t, NDensity::Resolver const&) -> NDensity::Evaluator {
        HollowHill h{static_cast<float>(num(j, "x_center")), static_cast<float>(num(j, "y_bottom")), static_cast<float>(num(j, "z_center")),
                     static_cast<float>(num(j, "radius")),   static_cast<float>(num(j, "height_scale"))};
        return [h](gf::core::noise::DensityPoint const& p) { return h.compute(p.blockX, p.blockY, p.blockZ); };
    });
    for (bool isMax : {false, true})
        NDensity::registerType(isMax ? "twilightforest:coord_max" : "twilightforest:coord_min", [num, isMax](Json const& j, std::int64_t, NDensity::Resolver const&) -> NDensity::Evaluator {
            AbsoluteDifference a{isMax, num(j, "max"), num(j, "x_center"), num(j, "z_center")};
            return [a](gf::core::noise::DensityPoint const& p) { return a.compute(p.blockX, p.blockZ); };
        });
    NDensity::registerType("twilightforest:sqrt", [](Json const& j, std::int64_t seed, NDensity::Resolver const& r) -> NDensity::Evaluator {
        auto in = NDensity::fromJson(j.contains("input") ? j["input"] : Json(), seed, r);
        if (!in) return {};
        return [in = *in](gf::core::noise::DensityPoint const& p) { return std::sqrt(in.compute(p)); };
    });
    for (bool terrain : {true, false})
        NDensity::registerType(terrain ? "twilightforest:biome_driven_terrain" : "twilightforest:biome_driven_noise",
                              [terrain](Json const& j, std::int64_t, NDensity::Resolver const&) -> NDensity::Evaluator {
            double baseFactor = j.value("base_factor", 0.0), baseOffset = j.value("base_offset", 0.0);
            return [=](gf::core::noise::DensityPoint const& p) {
                auto s = state();
                if (!s) return 0.0;
                auto d = sampleTerrain(p.blockX, p.blockZ, [&](int qx, int qz) { return s->columnAt(qx, qz); });
                return terrain ? baseOffset + d.depth * baseFactor + d.depth : d.scale;
            };
        });
}

// ---- landmark heights ----------------------------------------------------------------------------------

std::optional<int> heightProvider(int dimension, int x, int z, worldgen::Heightmap type) {
    int tf = twilightDim();
    if (tf >= 0 && dimension != tf) return std::nullopt;
    auto s = state();
    if (!s) return std::nullopt;
    // OCEAN_FLOOR(_WG): blocks motion; WORLD_SURFACE(_WG) and MOTION_BLOCKING(_NO_LEAVES) also
    // count fluids (the noise terrain has no leaves)
    bool ocean = type == worldgen::Heightmap::OceanFloorWg || type == worldgen::Heightmap::OceanFloor;
    TerrainSampler sampler = [&](int bx, int bz) { return sampleTerrain(bx, bz, [&](int qx, int qz) { return s->columnAt(qx, qz); }); };
    // ChunkGenerator#getFirstOccupiedHeight = getBaseHeight - 1
    return s->gen->baseHeight(x, z, ocean, sampler) - 1;
}

// ---- twilightforest:chunk_blanketing ---------------------------------------------------------------------

bool biomeInSet(Json const& set, std::string const& biome) {
    auto test = [&](Json const& e) {
        if (!e.is_string()) return false;
        auto s = e.get<std::string>();
        if (!s.empty() && s[0] == '#') return tags::has(tags::Registry::Biome, s.substr(1), biome);
        if (s.find(':') == std::string::npos) s = "minecraft:" + s;
        return s == biome;
    };
    if (set.is_array()) return std::any_of(set.begin(), set.end(), test);
    return test(set);
}

std::vector<BlockPos> chunkBlanketing(features::PlacementContext const& ctx, BlockPos const& pos) {
    std::vector<BlockPos> out;
    float                 integrity = ctx.config.value("integrity", 1.0F);
    auto                  heightmap = kit::heightmapOf(ctx.config.value("heightmap", std::string("WORLD_SURFACE_WG")));
    bool                  locked    = ctx.config.contains("biome_lock");
    int                   ox = (pos.x >> 4) << 4, oz = (pos.z >> 4) << 4;
    for (int zIn = 0; zIn < 16; ++zIn)
        for (int xIn = 0; xIn < 16; ++xIn) {
            if (ctx.random.nextFloat() > integrity) continue;
            // WorldGenLevel#getHeight = ChunkAccess#getHeight + 1
            BlockPos p(ox + xIn, ctx.region.height(heightmap, ox + xIn, oz + zIn), oz + zIn);
            if (!locked || biomeInSet(ctx.config["biome_lock"], ctx.region.biome(p))) out.push_back(p);
        }
    return out;
}

// ---- world lifecycle ------------------------------------------------------------------------------------

void onWorldReady() {
    // the preset is chosen when the world is first generated and kept with it
    bool sky = config::get<TerrainConfig>().skylight;
    if (auto v = game::db::get(kSkylightKey); v && v->is_boolean()) sky = v->get<bool>();
    else game::db::put(kSkylightKey, sky);
    skylightFlag() = sky ? 1 : 0;
    gf::log().info("terrain: world preset {}", sky ? "skylight" : "twilight");
}

struct TerrainModule {
    void install() {
        installed() = true;
        registerDensityTypes();
        worldgen::setTerrain(terrainProvider);
        worldgen::setSurface(surfaceProvider);
        worldgen::registerBlanket("twilightforest:chunk_blanketing", blanketProvider);
        worldgen::registerCarver("twilightforest:tf_caves", [](worldgen::Chunk& c, worldgen::Region& r, core::jrand::RandomSource& rnd) {
            carve(false, c, r, rnd);
        });
        worldgen::registerCarver("twilightforest:highland_caves", [](worldgen::Chunk& c, worldgen::Region& r, core::jrand::RandomSource& rnd) {
            carve(true, c, r, rnd);
        });
        // the chunk's total (noise .. carvers) once its Carvers stage ends
        worldgen::addStage(worldgen::Stage::Carvers, "terrain.timing", 1000000, [](worldgen::Chunk&, worldgen::Region&) {
            recordChunkMs(tChunkMs);
            tChunkMs = 0;
        });
        features::registerPlacement("twilightforest:chunk_blanketing", chunkBlanketing);
        landmark::ext::setHeightProvider(heightProvider);
        game::db::onWorldReady(onWorldReady);
        if (game::db::available()) onWorldReady(); // BDS: the world was ready before the mods were enabled
        game::db::onWorldClosed([] {
            skylightFlag() = -1;
            std::lock_guard lock(stateMutex());
            cachedState().reset();
        });
    }
    void uninstall() {
        worldgen::setTerrain({});
        worldgen::setSurface({});
        // F5 has no unregistration: the blanket, carvers and steps stay registered and do nothing
        installed() = false;
        landmark::ext::setHeightProvider({});
        std::lock_guard lock(stateMutex());
        cachedState().reset();
    }
};

} // namespace

} // namespace gf::feature::terrain

GF_REGISTER_MODULE(gf::feature::terrain::TerrainModule);
