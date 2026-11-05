// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFDimensionData.java
//   src/main/java/twilightforest/init/TFDensityFunctions.java
//   src/main/java/twilightforest/init/custom/ChunkBlanketProcessors.java
//   src/main/java/twilightforest/asmhooks/WorldgenHooks.java
//   src/main/java/twilightforest/world/components/placements/ChunkBlanketingModifier.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TerrainDensityRouter.java
//   src/main/java/twilightforest/world/components/chunkgenerators/NoiseDensityRouter.java
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
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "core/terrain/Beard.h"
#include "core/terrain/WorldPreset.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/terrain/PieceBeard.h"
#include "feature/ext/terrain/Pipeline.h"
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
constexpr char kBlanketId[]   = "twilightforest:chunk_blanketing";

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

namespace {
std::mutex& flagMutex() {
    static std::mutex m;
    return m;
}
std::condition_variable& flagKnown() {
    static std::condition_variable cv;
    return cv;
}
/// The preset chunks were generated with before the world's own was known (-1: none).
std::atomic<int>& provisionalFlag() {
    static std::atomic<int> v{-1};
    return v;
}
/// The thread that runs the world lifecycle (install on BDS, onWorldReady): it never waits for the
/// flag it is the one to set.
std::mutex&      lifecycleThreadMutex() {
    static std::mutex m;
    return m;
}
std::thread::id& lifecycleThread() {
    static std::thread::id id;
    return id;
}
void noteLifecycleThread() {
    std::lock_guard lock(lifecycleThreadMutex());
    lifecycleThread() = std::this_thread::get_id();
}
bool onLifecycleThread() {
    std::lock_guard lock(lifecycleThreadMutex());
    return lifecycleThread() == std::this_thread::get_id();
}
} // namespace

bool skylightWorld() {
    int v = skylightFlag().load();
    if (v >= 0) return v == 1;
    // The world's preset is read on the server thread when the world is ready (the world DB is
    // server-thread only). A chunk generated before that (a client loading a world whose player
    // stands in the Twilight Forest) waits a little for it, then uses the config's preset and
    // remembers it, so onWorldReady keeps a new world consistent with those chunks.
    if (int p = provisionalFlag().load(); p >= 0) return p == 1; // waited once already
    if (!onLifecycleThread()) {
        std::unique_lock lock(flagMutex());
        flagKnown().wait_for(lock, std::chrono::milliseconds(2000), [] { return skylightFlag().load() >= 0; });
        v = skylightFlag().load();
    }
    if (v >= 0) return v == 1;
    bool sky = config::get<TerrainConfig>().skylight;
    int  expected = -1;
    if (provisionalFlag().compare_exchange_strong(expected, sky ? 1 : 0)) gf::log().warn("terrain: a chunk generates before the world's preset is known: {} (config) for now", sky ? "skylight" : "twilight");
    return provisionalFlag().load() == 1;
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

namespace {
std::atomic<std::uint64_t>& recorded() {
    static std::atomic<std::uint64_t> n{0};
    return n;
}
} // namespace

void recordChunkMs(double ms) {
    std::lock_guard lock(timesMutex());
    ++recorded();
    times().push_back(ms);
    if (times().size() > 4096) times().pop_front();
}
void addChunkMs(double ms) { tChunkMs += ms; }
std::uint64_t chunkTimesRecorded() { return recorded().load(); }
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

// ---- the beardifier ---------------------------------------------------------------------------------------

namespace {

/// The rigids of the starts recently generated near, least recently used evicted first (a start
/// stays while chunks near it generate, however many other starts come and go).
struct BeardCache {
    static constexpr std::size_t kCapacity = 1024;
    using Rigids                           = std::shared_ptr<std::vector<PieceRigid> const>;
    struct Entry {
        Rigids                           rigids;
        std::list<std::string>::iterator use;
    };
    std::mutex                             mutex;
    std::list<std::string>                 uses; ///< most recent first
    std::unordered_map<std::string, Entry> starts;

    Rigids find(std::string const& key) {
        auto it = starts.find(key);
        if (it == starts.end()) return nullptr;
        uses.splice(uses.begin(), uses, it->second.use);
        return it->second.rigids;
    }
    void put(std::string const& key, Rigids rigids) {
        if (auto it = starts.find(key); it != starts.end()) {
            it->second.rigids = std::move(rigids);
            uses.splice(uses.begin(), uses, it->second.use);
            return;
        }
        uses.push_front(key);
        starts.emplace(key, Entry{std::move(rigids), uses.begin()});
        while (starts.size() > kCapacity) {
            starts.erase(uses.back());
            uses.pop_back();
        }
    }
    void clear() {
        starts.clear();
        uses.clear();
    }
};
BeardCache& beardCache() {
    static BeardCache c;
    return c;
}

/// The structure-level terrain adaptation of a landmark (its structure JSON's terrain_adaptation,
/// kept by F6 in the descriptor's extra.gf).
beard::TerrainAdjustment structureAdaptation(std::string const& key) {
    auto const* d = landmark::descriptor(key);
    if (!d) return beard::TerrainAdjustment::None;
    std::string adaptation = "none";
    if (auto it = d->extra.find("gf"); it != d->extra.end() && it->is_object()) {
        auto a = it->find("terrain_adaptation");
        if (a != it->end() && a->is_string()) adaptation = a->get<std::string>();
    }
    return beard::adjustmentOf(adaptation);
}

std::string startCacheKey(landmark::Start const& st) {
    auto const& b = st.box;
    std::string k = std::to_string(st.dimension) + "|" + st.key + "|" + std::to_string(st.seed) + "|" + std::to_string(b.minX) + "," + std::to_string(b.minY) + "," +
                    std::to_string(b.minZ) + "," + std::to_string(b.maxX) + "," + std::to_string(b.maxY) + "," + std::to_string(b.maxZ) + "|" +
                    std::to_string(st.pieceBoxes.size()) + "|" + std::to_string(pieceBeardVersion());
    if (st.data.is_object())
        if (auto c = st.data.find("chunk"); c != st.data.end()) k += "|" + c->dump();
    return k;
}

} // namespace

std::vector<PieceRigid> rigidsOfPieces(std::vector<gf::landmark::ext::StartPiece> const& pieces, beard::TerrainAdjustment structure) {
    std::vector<PieceRigid> out;
    out.reserve(pieces.size());
    for (auto const& p : pieces) {
        auto        answer = gf::terrain::ext::pieceBeard(p);
        beard::PieceBeard pb{answer.modifier, beard::adjustmentOf(answer.adjustment), answer.box, answer.groundLevelDelta};
        if (auto r = beard::rigidOf(pb, p.box, structure)) out.push_back({p.box, *r});
    }
    return out;
}

std::vector<beard::Rigid> beardRigids(int dim, int chunkX, int chunkZ) {
    std::vector<beard::Rigid> out;
    for (auto const& st : landmark::startsIntersecting(dim, {chunkX, chunkZ})) {
        // a reserved site (no generator yet) is no StructureStart#isValid start
        if (!st.generated) continue;
        // StructureManager#startsForStructure(chunk, s -> s.terrainAdaptation() != NONE)
        auto adaptation = structureAdaptation(st.key);
        if (adaptation == beard::TerrainAdjustment::None) continue;
        std::shared_ptr<std::vector<PieceRigid> const> rigids;
        auto                                           key = startCacheKey(st);
        {
            auto&           c = beardCache();
            std::lock_guard lock(c.mutex);
            rigids = c.find(key);
        }
        if (!rigids) {
            rigids = std::make_shared<std::vector<PieceRigid> const>(rigidsOfPieces(landmark::ext::startPieces(st), adaptation));
            auto&           c = beardCache();
            std::lock_guard lock(c.mutex);
            c.put(key, rigids);
        }
        for (auto const& pr : *rigids)
            if (beard::isCloseToChunk(pr.pieceBox, chunkX, chunkZ, 12)) out.push_back(pr.rigid);
    }
    return out;
}

void clearBeardCache() {
    auto&           c = beardCache();
    std::lock_guard lock(c.mutex);
    c.clear();
}

// ---- the grids the stages hand on ---------------------------------------------------------------------------

namespace {

/// The grid the last stage of this thread's chunk left (noise, then surface): F5 runs noise ->
/// surface -> blanket for one chunk on one thread, so the next stage reads it instead of the chunk.
struct HandedGrid {
    int           chunkX = 0, chunkZ = 0;
    std::uint64_t generation = 0;
    int           stage      = -1; ///< 0: after noise, 1: after surface
    std::unique_ptr<Grid> grid;
};
thread_local HandedGrid tHanded;

void handOn(State const& s, worldgen::Chunk const& chunk, int stage, Grid&& grid) {
    tHanded.chunkX     = chunk.chunkX();
    tHanded.chunkZ     = chunk.chunkZ();
    tHanded.generation = s.generation;
    tHanded.stage      = stage;
    tHanded.grid       = std::make_unique<Grid>(std::move(grid));
}

/// The block class readGrid gives a code (what the chunk holds after the code was written).
std::uint16_t classOf(std::uint16_t c, State const& s) {
    if (c < code::FirstNamed) return c;
    auto const* b = s.block(c);
    return b ? classify(s, *b) : code::Air;
}

/// Whether anything else can write the chunk between the provider that hands a grid on (stage 0:
/// the terrain provider, 1: the surface provider) and the one that takes it (the surface provider,
/// the chunk_blanketing blanket) in the pass running on this thread: F5's Stage::Noise steps run
/// between the first two, its Stage::Surface steps and the blankets whose id sorts before ours
/// between the last two. A pass on Providers other than F5's global ones (the probe cannot see
/// their steps) counts as written.
bool foreignWritersBetween(worldgen::Chunk const& chunk, int stage) {
    auto w = gf::terrain::ext::passWriters(chunk.chunkX(), chunk.chunkZ());
    if (!w.known) return true;
    return stage == 0 ? w.noise : w.surface;
}

std::atomic<std::uint64_t>& handoffsTaken() {
    static std::atomic<std::uint64_t> v{0};
    return v;
}
std::atomic<std::uint64_t>& handoffsDropped() {
    static std::atomic<std::uint64_t> v{0};
    return v;
}

} // namespace

std::pair<std::uint64_t, std::uint64_t> handoffCounts() { return {handoffsTaken().load(), handoffsDropped().load()}; }

bool takeHandedGrid(State const& s, worldgen::Chunk& chunk, int stage, Grid& out) {
    if (tHanded.stage != stage || !tHanded.grid || tHanded.chunkX != chunk.chunkX() || tHanded.chunkZ != chunk.chunkZ() || tHanded.generation != s.generation)
        return false;
    tHanded.stage = -1;
    Grid& g       = *tHanded.grid;
    if (g.minY != out.minY || g.height != out.height) return false;
    // another unit's step may have changed any block of the chunk (a pit, a lake, an underground
    // placement): then the next stage reads the chunk itself
    if (foreignWritersBetween(chunk, stage)) {
        tHanded.grid.reset();
        ++handoffsDropped();
        return false;
    }
    // and a cheap check that the chunk still is what the grid says where the next stage looks
    // first (the top block of every column and the air above it)
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            int top = g.worldSurfaceWg(x, z);
            if ((top + 1 < chunk.maxY() && classify(s, chunk.getBlock(x, top + 1, z)) != code::Air)
                || (top >= chunk.minY() && classify(s, chunk.getBlock(x, top, z)) != classOf(g.get(x, top, z), s))) {
                tHanded.grid.reset();
                ++handoffsDropped();
                return false;
            }
        }
    out.cells.swap(g.cells);
    tHanded.grid.reset();
    ++handoffsTaken();
    return true;
}

void terrainProvider(worldgen::Chunk& chunk) {
    tChunkMs = 0;
    gf::terrain::ext::beginPass();
    tHanded.stage = -1;
    tHanded.grid.reset();
    StageTimer timer;
    auto       s = state();
    if (!s) return;
    ChunkColumns cols(*s, chunk.chunkX(), chunk.chunkZ());
    // the beardifier: Beardifier#forStructuresInChunk (NeoForge: each piece's
    // PieceBeardifierModifier, else its box with the structure's adaptation) plus
    // WorldgenHooks#getCustomDensity (the TF terraformers)
    ExtraDensity extra;
    int          dim = twilightDim();
    if (dim >= 0) {
        auto rigid = beardRigids(dim, chunk.chunkX(), chunk.chunkZ());
        landmark::ext::ChunkTerraformer t;
        if (landmark::ext::hasTerraformers(dim, chunk.chunkX(), chunk.chunkZ())) t = landmark::ext::chunkTerraformer(dim, chunk.chunkX(), chunk.chunkZ());
        if (!rigid.empty() || t)
            extra = [rigid = std::move(rigid), t](int x, int y, int z) {
                double d0 = beard::beardifierDensity(rigid, x, y, z);
                return t ? d0 + t(x, y, z) : d0;
            };
    }
    // the grid spans the chunk and the noise (the Twilight Forest's chunks reach y 256, the noise
    // settings stop at 224: air above, as Java's ProtoChunk holds it)
    auto shape = s->gen->shape();
    int  lo = std::min(shape.minY, chunk.minY()), hi = std::max(shape.minY + shape.height, chunk.maxY());
    Grid grid(lo, hi - lo);
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
    if (lo == chunk.minY() && hi == chunk.maxY()) handOn(*s, chunk, 0, std::move(grid));
}

void surfaceProvider(worldgen::Chunk& chunk, worldgen::Region&) {
    StageTimer timer;
    auto       s = state();
    if (!s) return;
    ChunkColumns cols(*s, chunk.chunkX(), chunk.chunkZ());
    Grid         before(chunk.minY(), chunk.maxY() - chunk.minY());
    if (!takeHandedGrid(*s, chunk, 0, before)) readGrid(*s, chunk, before);
    Grid after = before;
    s->gen->surface().buildSurface(chunk.chunkX(), chunk.chunkZ(), after, s->table, [&](int x, int y, int z) -> std::string const& {
        return cols.blockBiome(x, y, z);
    });
    writeGrid(*s, chunk, before, after);
    handOn(*s, chunk, 1, std::move(after));
}

bool chunkReferences(int dim, int chunkX, int chunkZ, std::vector<std::string> const& structures) {
    // ChunkAccess#getAllReferences: the valid starts whose box reaches the chunk (a reserved site,
    // F6's clearance square before the structure's generator exists, is no start yet)
    if (dim < 0) return false;
    for (auto const& st : landmark::startsIntersecting(dim, {chunkX, chunkZ}))
        if (st.generated && std::find(structures.begin(), structures.end(), st.key) != structures.end()) return true;
    return false;
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
    if (!takeHandedGrid(*s, chunk, 1, before)) readGrid(*s, chunk, before);
    Grid after = before;
    int  dim   = twilightDim();
    s->blankets.apply(
        chunk.chunkX(), chunk.chunkZ(), after, s->table, biomes,
        [&](int x, int y, int z) -> std::string const& { return cols.blockBiome(x, y, z); },
        [&](std::vector<std::string> const& avoid) { return chunkReferences(dim, chunk.chunkX(), chunk.chunkZ(), avoid); },
        [&] {
            if (ctx && ctx->regionRandom) (void)ctx->regionRandom->fork();
        }
    );
    writeGrid(*s, chunk, before, after);
}

std::optional<SavedChunkProbe> findSavedChunks(int dim, bool fullScan) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto const&     db = level->getLevelStorage();
    SavedChunkProbe r;
    auto            saved = [&](int x, int z) {
        ++r.probed;
        for (auto const& k : core::terrain::chunkVersionKeys(x, z, dim))
            if (db.hasKey(k, DBHelpers::Category::Chunk)) return true;
        return false;
    };
    // the chunks a Twilight Forest is entered by first: around its origin, and around where a portal
    // at the world spawn leads (F4: Twilight Forest = overworld / coordinateScale), nearest first
    BlockPos const& spawn         = level->getDefaultSpawn();
    double          toTf          = 1.0 / gf::world::coordinateScale();
    int const       centres[3][2] = {
        {0,                                                 0                                                },
        {static_cast<int>(std::floor(spawn.x * toTf)) >> 4, static_cast<int>(std::floor(spawn.z * toTf)) >> 4},
        {spawn.x >> 4,                                      spawn.z >> 4                                     },
    };
    for (int ring = 0; ring <= kSavedChunkProbeRadius && !r.found; ++ring)
        for (auto const& c : centres) {
            for (int dx = -ring; dx <= ring && !r.found; ++dx)
                for (int dz = -ring; dz <= ring && !r.found; ++dz)
                    if ((std::abs(dx) == ring || std::abs(dz) == ring) && saved(c[0] + dx, c[1] + dz)) r.found = true;
            if (r.found) break;
        }
    if (r.found || !fullScan) return r;
    // nothing near the entrances: a world whose players wandered far. Every chunk record has to be
    // looked at (the callback cannot stop the iteration; it only stops counting)
    r.scanned = true;
    db.forEachKeyWithPrefix("", DBHelpers::Category::Chunk, [&](std::string_view key, std::string_view) {
        if (r.found) return;
        if (auto d = core::terrain::chunkVersionKeyDimension(key); d && *d == dim) r.found = true;
    });
    return r;
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
    // TerrainDensityRouter / NoiseDensityRouter: the biome terrain source is the biome grid of the
    // mod's data (BiomeDensitySource, seed-independent: it reads the world's biome source through
    // worldgen::mainBiomeAt, as Java reads the chunk generator's), so the evaluator needs no world
    // state; base_factor / base_offset are density functions (HOLDER_HELPER_CODEC: a constant, an
    // inline function or a reference) compiled with the seed the evaluator is built for
    auto grid = std::make_shared<TerrainGrid const>(TerrainGrid::fromJson(data().biomeGrid));
    for (bool terrain : {true, false})
        NDensity::registerType(terrain ? "twilightforest:biome_driven_terrain" : "twilightforest:biome_driven_noise",
                               [terrain, grid](Json const& j, std::int64_t seed, NDensity::Resolver const& r) -> NDensity::Evaluator {
            if (grid->empty()) return {};
            auto columnAt = [grid](int qx, int qz) -> TerrainColumn const* {
                auto id = worldgen::mainBiomeAt(core::mth::toBlock(qx), core::mth::toBlock(qz));
                if (auto const* c = grid->column(id)) return c;
                return grid->column("twilightforest:forest");
            };
            if (!terrain)
                return [columnAt](gf::core::noise::DensityPoint const& p) { return sampleTerrain(p.blockX, p.blockZ, columnAt).scale; };
            if (!j.contains("base_factor") || !j.contains("base_offset")) return {};
            auto baseFactor = NDensity::fromJson(j["base_factor"], seed, r);
            auto baseOffset = NDensity::fromJson(j["base_offset"], seed, r);
            if (!baseFactor || !baseOffset) return {};
            return [columnAt, f = *baseFactor, o = *baseOffset](gf::core::noise::DensityPoint const& p) {
                auto d = sampleTerrain(p.blockX, p.blockZ, columnAt);
                return o.compute(p) + d.depth * f.compute(p) + d.depth;
            };
        });
}

// ---- landmark heights ----------------------------------------------------------------------------------

std::optional<int> heightProvider(int dimension, int x, int z, worldgen::Heightmap type) {
    // TF's heights only for the Twilight Forest (no dimension registered: no Twilight Forest)
    int tf = twilightDim();
    if (tf < 0 || dimension != tf) return std::nullopt;
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

/// ChunkBlanketingModifier.CODEC: integrity (floatRange 0..1) and heightmap (Heightmap.Types) are
/// required fields, biome_lock optional. Data Java's codec rejects places nothing here (one warning
/// per placed feature, rule R17).
bool blanketingConfigValid(features::PlacementContext const& ctx) {
    static constexpr std::string_view kHeightmaps[] = {"WORLD_SURFACE_WG", "WORLD_SURFACE", "OCEAN_FLOOR_WG", "OCEAN_FLOOR", "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES"};
    auto const& c       = ctx.config;
    char const* problem = nullptr;
    if (!c.contains("integrity") || !c["integrity"].is_number()) problem = "no integrity";
    else if (double i = c["integrity"].get<double>(); !(i >= 0.0 && i <= 1.0)) problem = "integrity outside 0..1";
    else if (!c.contains("heightmap") || !c["heightmap"].is_string()) problem = "no heightmap";
    else if (std::find(std::begin(kHeightmaps), std::end(kHeightmaps), c["heightmap"].get<std::string>()) == std::end(kHeightmaps)) problem = "unknown heightmap";
    if (!problem) return true;
    static std::mutex                      m;
    static std::unordered_set<std::string> warned;
    std::lock_guard                        lock(m);
    if (warned.insert(ctx.placedFeatureId).second)
        gf::log().warn("terrain: {} has an invalid twilightforest:chunk_blanketing modifier ({}): it places nothing", ctx.placedFeatureId.empty() ? std::string("a placed feature") : ctx.placedFeatureId, problem);
    return false;
}

std::vector<BlockPos> chunkBlanketing(features::PlacementContext const& ctx, BlockPos const& pos) {
    std::vector<BlockPos> out;
    if (!blanketingConfigValid(ctx)) return out;
    float                 integrity = ctx.config["integrity"].get<float>();
    auto                  heightmap = kit::heightmapOf(ctx.config["heightmap"].get<std::string>());
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
    noteLifecycleThread();
    // TF's preset is chosen when the world is created (the dimension's chunk generator) and kept
    // with it: a stored choice wins; a world whose Twilight Forest already holds saved chunks (an
    // existing world, generated before a preset was kept) stays twilight_noise_gen; a new world
    // takes the one chunks already generated with this session (before the world was ready), else
    // the config's
    int  provisional = provisionalFlag().load();
    bool                configured  = config::get<TerrainConfig>().skylight;
    auto name        = [](bool s) { return s ? "skylight" : "twilight"; };
    std::optional<bool> stored;
    if (auto v = game::db::get(kSkylightKey); v && v->is_boolean()) stored = v->get<bool>();
    bool saved = false;
    // the DB is read only when saved chunks would turn a skylight answer into twilight (the default
    // twilight config never reads it), and then only once per world: the answer is stored below
    if (int dim = twilightDim(); dim >= 0 && core::terrain::savedChunksMatter(stored, provisional, configured)) {
        auto start = std::chrono::steady_clock::now();
        auto probe = findSavedChunks(dim, true);
        saved      = probe && probe->found;
        gf::log().info(
            "terrain: saved Twilight Forest chunks: {} ({} positions looked up{}, {:.0f} ms)",
            saved ? "yes" : "no",
            probe ? probe->probed : 0,
            probe && probe->scanned ? ", chunk table scanned" : "",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
        );
    }
    auto choice = core::terrain::choosePreset(stored, saved, provisional, configured);
    bool sky    = choice.skylight;
    switch (choice.source) {
    case core::terrain::PresetSource::Stored:
        if (provisional >= 0 && (provisional == 1) != sky)
            gf::log().warn("terrain: chunks generated before the world was ready used {} but the world is {}", name(provisional == 1), name(sky));
        break;
    case core::terrain::PresetSource::SavedChunks:
        if (configured)
            gf::log().warn(
                "terrain: this world's Twilight Forest was generated before a preset was kept: it stays twilight "
                "(config skylight ignored)"
            );
        if (provisional == 1)
            gf::log().warn(
                "terrain: chunks generated before the world was ready used skylight, but the world stays twilight"
            );
        break;
    case core::terrain::PresetSource::Provisional:
        if (sky != configured)
            gf::log().warn(
                "terrain: chunks generated before the world was ready used {}: the new world keeps it (config {})",
                name(sky),
                name(configured)
            );
        break;
    case core::terrain::PresetSource::Config:
        break;
    }
    if (!stored) game::db::put(kSkylightKey, sky);
    {
        std::lock_guard lock(flagMutex());
        skylightFlag() = sky ? 1 : 0;
    }
    flagKnown().notify_all();
    gf::log().info("terrain: world preset {}", name(sky));
}

struct TerrainModule {
    void install() {
        noteLifecycleThread();
        installed() = true;
        registerDensityTypes();
        worldgen::setTerrain(terrainProvider);
        worldgen::setSurface(surfaceProvider);
        worldgen::registerBlanket(kBlanketId, blanketProvider);
        gf::terrain::ext::installPassProbe(kBlanketId);
        worldgen::registerCarver("twilightforest:tf_caves", [](worldgen::Chunk& c, worldgen::Region& r, core::jrand::RandomSource& rnd) {
            carve(false, c, r, rnd);
        });
        worldgen::registerCarver("twilightforest:highland_caves", [](worldgen::Chunk& c, worldgen::Region& r, core::jrand::RandomSource& rnd) {
            carve(true, c, r, rnd);
        });
        // the chunk's total (noise .. carvers) once its Carvers stage ends
        worldgen::addStage(
            worldgen::Stage::Carvers,
            "terrain.timing",
            1000000,
            [](worldgen::Chunk&, worldgen::Region&) {
                recordChunkMs(tChunkMs);
                tChunkMs = 0;
            }
        );
        features::registerPlacement("twilightforest:chunk_blanketing", chunkBlanketing);
        landmark::ext::setHeightProvider(heightProvider);
        game::db::onWorldReady(onWorldReady);
        if (game::db::available()) onWorldReady(); // BDS: the world was ready before the mods were enabled

        game::db::onWorldClosed([] {
            skylightFlag()    = -1;
            provisionalFlag() = -1;
            clearBeardCache();
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
