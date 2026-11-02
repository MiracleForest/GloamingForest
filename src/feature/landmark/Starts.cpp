// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java
//   src/main/java/twilightforest/world/components/structures/util/ConquerableStructure.java
//   src/main/java/twilightforest/world/components/structures/util/CustomStructureData.java
//   src/main/java/twilightforest/world/components/structures/util/DecorationClearance.java
//   src/main/java/twilightforest/world/components/structures/start/TFStructureStart.java
//   src/main/java/twilightforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/twilightforest/asmhooks/WorldgenHooks.java
//   src/main/java/twilightforest/util/landmarks/LandmarkUtil.java
//   src/main/java/twilightforest/util/WorldUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Persistent landmark starts. Java keeps structure starts in the chunk NBT; Bedrock has no such
// slot, so every start lives in the world DB under gf:landmark:start:<dim>:<cx>:<cz> (one document
// per start chunk: {"<structure key>": start}), cached in memory. Starts are made by the worldgen
// stages (Starts: the start chunk's structure sets, ChunkGenerator#createStructures; the
// landmark.pieces structure step: at its structure's decoration step, every start reaching the
// chunk places its pieces, StructureStart#placeInChunk, with Java's per-structure decoration
// random, ChunkGenerator#applyBiomeDecoration), by forceStart /
// generateAt (commands, saplings, selftests) and on demand by lookups (protection, clearance,
// terraforming; /twilightforest locate only probes and stores nothing), so a site is reserved even
// before its generator exists (rule: placement is exact before W1 lands). Generation may run on
// worker threads: the cache is locked, the server thread never waits for a worker, and the DB is
// written on the server thread only.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "core/Keys.h"
#include "core/jrand/JRand.h"
#include "core/landmark/Decoration.h"
#include "core/landmark/Grid.h"
#include "core/landmark/JavaMath.h"
#include "core/landmark/Landmark.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/landmark/Internal.h"
#include "feature/progression/Progression.h"
#include "feature/structure/bases/Component.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::landmark {

namespace cl  = core::landmark;
namespace plc = core::landmark::placement;
namespace dec = core::landmark::decoration;
namespace ext = gf::landmark::ext;

namespace {

/// GenerationStep.Decoration.SURFACE_STRUCTURES (Structure.StructureSettings' default step).
constexpr int kSurfaceStructures = 4;

// ---- generators --------------------------------------------------------------------------------------

struct Generators {
    std::mutex                                                                             mutex;
    std::map<std::string, std::shared_ptr<Generator const>>                                gens;
    /// addTerraformer's functions per key
    std::map<std::string, std::vector<std::function<double(Start const&, int, int, int)>>> extraTerraformers;
    /// copy-on-write lists of every terraformer per key: per-block readers only copy the pointer
    std::map<std::string, TerraformerList> terraformers;
    ext::HeightFn                                                                          height;
};
Generators& generators() {
    static Generators g;
    return g;
}

// ---- record store ------------------------------------------------------------------------------------

struct ChunkKey {
    int  dim, x, z;
    bool operator==(ChunkKey const&) const = default;
};
struct ChunkKeyHash {
    std::size_t operator()(ChunkKey const& k) const noexcept {
        auto h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.x)) * 0x9E3779B97F4A7C15ULL;
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.z)) + 0x7F4A7C15ULL + (h << 6) + (h >> 2);
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.dim)) << 17;
        return static_cast<std::size_t>(h);
    }
};

/// Bound of the evaluated / aroundDone / probe caches (entries; rebuilt on demand when dropped).
constexpr std::size_t kMaxEvaluatedChunks = std::size_t{1} << 18;

struct Records {
    std::shared_mutex                                                       mutex;
    std::unordered_map<ChunkKey, std::vector<RecordPtr>, ChunkKeyHash>      byStartChunk;
    std::unordered_map<ChunkKey, std::vector<RecordPtr>, ChunkKeyHash>      byChunk; ///< chunks a box covers
    std::unordered_set<ChunkKey, ChunkKeyHash>                              evaluated; ///< start chunks whose sets ran
    /// start chunks whose sets are being evaluated, with the thread that claimed them
    std::unordered_map<ChunkKey, std::thread::id, ChunkKeyHash>             inProgress;
    std::condition_variable_any                                             evaluatedChanged;
    std::unordered_set<ChunkKey, ChunkKeyHash>                              aroundDone; ///< ensureStartsAround ran
    /// probeStart's answers: (dim, start chunk, structure set) -> the key starting there ("" none)
    std::map<std::tuple<int, int, int, std::string>, std::string>            probes;
    std::set<std::tuple<int, int, int>>                                     dirty; ///< start chunks to write
    std::uint64_t                                                           world = 0; ///< bumped by clearRecords
    std::atomic<std::int64_t>                                               seed{0};
    std::atomic<bool>                                                       haveSeed{false};
};
Records& records() {
    static Records r;
    return r;
}

/// recordsVersion(): bumped on every change of a record, a generator or a terraformer.
std::atomic<std::uint64_t> gVersion{0};
void bumpVersion() { gVersion.fetch_add(1, std::memory_order_acq_rel); }

/// The server thread (noteServerThread, from the server tick): it never waits for another thread.
std::atomic<std::thread::id> gServerThread{};
bool onServerThread() { return std::this_thread::get_id() == gServerThread.load(std::memory_order_relaxed); }

/// The start chunks this thread is evaluating, innermost last. A layout asking about its own chunk
/// gets what is stored so far, and a thread inside an evaluation never waits for another one (two
/// layouts asking about each other's chunks would wait for each other).
thread_local std::vector<ChunkKey> tEvaluating;
bool evaluatingHere(ChunkKey const& key) {
    // any_of, not find: MSVC's vectorized find rejects a 12-byte trivially comparable type
    return std::any_of(tEvaluating.begin(), tEvaluating.end(), [&](ChunkKey const& k) { return k == key; });
}

/// How long a generation thread waits for another thread evaluating the same start chunk before it
/// evaluates the chunk itself (the evaluating thread never waits, so this is only a safety net).
constexpr auto kEvaluationWait = std::chrono::seconds(2);

/// Chunks covered by a box (column-wise).
template <class Fn>
void forChunks(BlockBox const& b, Fn&& fn) {
    for (int cx = b.minX >> 4; cx <= (b.maxX >> 4); ++cx)
        for (int cz = b.minZ >> 4; cz <= (b.maxZ >> 4); ++cz) fn(cx, cz);
}

void unindexRecord(Records& r, int dim, int cx, int cz, std::string const& key);

void indexRecord(Records& r, RecordPtr const& rec) {
    unindexRecord(r, rec->start.dimension, rec->chunkX, rec->chunkZ, rec->start.key); // a replaced record
    r.byStartChunk[{rec->start.dimension, rec->chunkX, rec->chunkZ}].push_back(rec);
    forChunks(rec->start.box, [&](int cx, int cz) { r.byChunk[{rec->start.dimension, cx, cz}].push_back(rec); });
}

void unindexRecord(Records& r, int dim, int cx, int cz, std::string const& key) {
    auto it = r.byStartChunk.find({dim, cx, cz});
    if (it == r.byStartChunk.end()) return;
    RecordPtr found;
    for (auto const& rec : it->second)
        if (rec->start.key == key) found = rec;
    if (!found) return;
    std::erase_if(it->second, [&](RecordPtr const& o) { return o == found; });
    forChunks(found->start.box, [&](int x, int z) {
        auto c = r.byChunk.find({dim, x, z});
        if (c != r.byChunk.end()) std::erase_if(c->second, [&](RecordPtr const& o) { return o == found; });
    });
}

// ---- piece serialization -----------------------------------------------------------------------------

int orientationData(structure::Orientation o) {
    switch (o) {
    case structure::Orientation::South: return 0;
    case structure::Orientation::West: return 1;
    case structure::Orientation::North: return 2;
    case structure::Orientation::East: return 3;
    }
    return -1;
}
structure::Orientation orientationOf(int data) {
    switch (data) {
    case 0: return structure::Orientation::South;
    case 1: return structure::Orientation::West;
    case 3: return structure::Orientation::East;
    default: return structure::Orientation::North;
    }
}

Json boxJson(BlockBox const& b) { return Json::array({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ}); }
BlockBox boxOf(Json const& j) {
    if (!j.is_array() || j.size() != 6) return {};
    auto v = [&](int i) { return j[i].is_number_integer() ? j[i].get<int>() : 0; };
    return {v(0), v(1), v(2), v(3), v(4), v(5)};
}

Json savePiece(structure::Piece const& p) {
    Json j = Json::object();
    try {
        auto extra = p.save();
        if (extra.is_object()) j = extra;
    } catch (...) {}
    j["id"]          = p.typeId;
    j["BB"]          = boxJson(p.bbox);
    j["O"]           = orientationData(p.orientation);
    j["GD"]          = p.genDepth;
    j["gf_mirror"]   = static_cast<int>(p.mirror);
    j["gf_rotation"] = static_cast<int>(p.rotation);
    return j;
}

std::shared_ptr<structure::Piece> loadPiece(Json const& j) {
    std::unique_ptr<structure::Piece> p;
    try {
        p = structure::loadPiece(j);
    } catch (...) {}
    if (!p) return nullptr;
    p->bbox        = boxOf(j.value("BB", Json()));
    p->orientation = orientationOf(j.value("O", -1));
    p->genDepth    = j.value("GD", 0);
    p->mirror      = static_cast<structure::Mirror>(std::clamp(j.value("gf_mirror", 0), 0, 2));
    p->rotation    = static_cast<structure::Rotation>(std::clamp(j.value("gf_rotation", 0), 0, 3));
    return std::shared_ptr<structure::Piece>(std::move(p));
}

PieceInfo infoOf(structure::Piece const& p) {
    PieceInfo i;
    i.box  = p.bbox;
    i.type = p.typeId;
    if (auto const* c = dynamic_cast<structure::bases::Component const*>(&p)) {
        i.protectedPiece = c->isComponentProtected();
        i.spawnIndex     = c->getSpawnIndex();
    }
    if (auto const* u = dynamic_cast<structure::bases::UtilityPiece const*>(&p)) i.utilityAllowsFeatures = u->allowFeatures;
    return i;
}

// ---- the region adapter for loaded chunks ------------------------------------------------------------

/// worldgen::Region over the server's BlockSource: places pieces into loaded chunks (commands,
/// saplings, forceStart).
class LoadedRegion final : public worldgen::Region {
public:
    LoadedRegion(BlockSource& region, int chunkX, int chunkZ, std::int64_t seed) : mRegion(region), mChunkX(chunkX), mChunkZ(chunkZ), mSeed(seed) {}
    [[nodiscard]] int          centerChunkX() const override { return mChunkX; }
    [[nodiscard]] int          centerChunkZ() const override { return mChunkZ; }
    [[nodiscard]] int          minY() const override { return mRegion.getMinHeight(); }
    [[nodiscard]] int          maxY() const override { return mRegion.getMaxHeight(); }
    [[nodiscard]] bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool                       setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return false;
        return game::setBlock(mRegion, pos, block);
    }
    [[nodiscard]] int height(worldgen::Heightmap type, int x, int z) const override {
        bool water = type == worldgen::Heightmap::WorldSurface || type == worldgen::Heightmap::WorldSurfaceWg || type == worldgen::Heightmap::MotionBlocking;
        bool leaves = type != worldgen::Heightmap::MotionBlockingNoLeaves;
        return mRegion.getAboveTopSolidBlock(x, z, water, leaves);
    }
    [[nodiscard]] std::string biome(BlockPos const& pos) const override { return mRegion.getBiome(pos).mHash->getString(); }
    [[nodiscard]] std::int64_t seed() const override { return mSeed; }
    [[nodiscard]] int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

private:
    BlockSource& mRegion;
    int          mChunkX, mChunkZ;
    std::int64_t mSeed;
};

} // namespace

ext::HeightFn heightProvider() {
    std::lock_guard lock(generators().mutex);
    return generators().height;
}

std::optional<int> adjustForTerrainAroundChunk(ext::HeightFn const& provider, int dim, int x, int z, int radiusFromCenterChunk, int gridLength) {
    if (!provider) return std::nullopt;
    return cl::adjustForTerrainAroundChunk(x, z, radiusFromCenterChunk, gridLength, [&](int cx, int cz) {
        return provider(dim, cx, cz, worldgen::Heightmap::WorldSurfaceWg);
    });
}

int stepOf(Info const& info) {
    int step = dec::stepIndex(info.step);
    return step < 0 ? kSurfaceStructures : step;
}

namespace {

/// StructureStart#placeInChunk for chunk (cx, cz) of `region`, with the structure's decoration
/// random (shared by every start of the structure placed in this chunk at this step, as Java's
/// applyBiomeDecoration passes one WorldgenRandom to all of them).
void placeInChunk(Record const& rec, worldgen::Region& region, int chunkX, int chunkZ, worldgen::RandomSource& random) {
    if (rec.livePieces.empty()) return;
    // ChunkGenerator#getWritableArea: the chunk column from minBuildHeight + 1 to maxBuildHeight - 1
    BlockBox chunkBox{chunkX * 16, region.minY() + 1, chunkZ * 16, chunkX * 16 + 15, region.maxY() - 1, chunkZ * 16 + 15};
    auto const& first = rec.livePieces.front()->bbox;
    auto        c     = cl::boxCenter(first);
    BlockPos    pivot{c.x, first.minY, c.z};
    for (auto const& piece : rec.livePieces) {
        if (!piece->bbox.intersects(chunkBox)) continue;
        try {
            piece->postProcess(region, random, chunkBox, pivot);
        } catch (std::exception const& e) {
            log().error("landmark: piece {} of {} failed: {}", piece->typeId, rec.start.key, e.what());
        }
    }
}

/// LandmarkStructure#findGenerationPoint's y: the structure's adjustForTerrain (Info::terrainOverride
/// for the classes overriding it, else DecorationClearance#adjustForTerrain). Without a terrain
/// provider: sea level.
int heightFor(Info const& info, int dim, int x, int z) {
    int  sea      = seaLevelOf(dim);
    auto provider = heightProvider(); // once: the samples below need no lock
    if (!provider) return sea;        // no terrain provider yet: every landmark at sea level
    if (auto const& o = info.terrainOverride) {
        auto h = adjustForTerrainAroundChunk(provider, dim, x, z, o->radius, o->gridLength);
        return h ? *h + o->offset : sea;
    }
    if (!info.decoration.adjustElevation) return sea;
    auto h = provider(dim, x, z, worldgen::Heightmap::WorldSurfaceWg);
    if (!h) return sea;
    return std::clamp(*h, sea + 1, sea + 7);
}

} // namespace

// ---- generators --------------------------------------------------------------------------------------

std::shared_ptr<Generator const> generatorFor(std::string const& key) {
    std::lock_guard lock(generators().mutex);
    auto            it = generators().gens.find(key);
    return it == generators().gens.end() ? nullptr : it->second;
}

namespace {

/// The number of structures the worldgen's Structure registry holds at decoration step `step`.
int registryStepSize(int step) {
    auto steps = worldgen::ext::structuresByStep();
    return step >= 0 && step < static_cast<int>(steps.size()) ? static_cast<int>(steps[static_cast<std::size_t>(step)].size()) : 0;
}

} // namespace

void assignSlot(Info& info, int unregisteredBefore) {
    if (auto s = worldgen::ext::structureSlot(info.d.key)) {
        info.slot       = {s->step, s->index};
        info.inRegistry = true;
        return;
    }
    int step        = stepOf(info);
    info.slot       = {step, registryStepSize(step) + unregisteredBefore};
    info.inRegistry = false;
}

dec::Slot decorationSlot(std::string const& key) {
    if (auto const* inf = info(key)) return inf->slot;
    if (auto s = worldgen::ext::structureSlot(key)) return {s->step, s->index};
    return {kSurfaceStructures, registryStepSize(kSurfaceStructures)};
}

std::unique_ptr<core::jrand::WorldgenRandom> decorationRandom(std::int64_t levelSeed, int chunkX, int chunkZ, std::string const& key) {
    return dec::structureRandom(dec::decorationSeed(levelSeed, chunkX, chunkZ), decorationSlot(key));
}

namespace {
void rebuildTerraformers(Generators& g, std::string const& key, std::function<double(Start const&, int, int, int)> added);
}

void setGenerator(std::string const& key, Generator generator) {
    std::lock_guard lock(generators().mutex);
    auto            k = normalizeKey(key);
    generators().gens[k] = std::make_shared<Generator const>(std::move(generator));
    rebuildTerraformers(generators(), k, {});
    bumpVersion();
}

void removeGenerator(std::string const& key) {
    std::lock_guard lock(generators().mutex);
    auto            k = normalizeKey(key);
    generators().gens.erase(k);
    rebuildTerraformers(generators(), k, {});
    bumpVersion();
}

TerraformerList terraformersFor(std::string const& key) {
    std::lock_guard lock(generators().mutex);
    auto            it = generators().terraformers.find(key);
    return it == generators().terraformers.end() ? nullptr : it->second;
}

namespace {
/// Rebuilds the copy-on-write terraformer list of `key` (Generator::terraform first, then the
/// addTerraformer ones). Called with the generators lock held.
void rebuildTerraformers(Generators& g, std::string const& key, std::function<double(Start const&, int, int, int)> added = {}) {
    std::vector<std::function<double(Start const&, int, int, int)>> list;
    if (auto it = g.gens.find(key); it != g.gens.end() && it->second->terraform) list.push_back(it->second->terraform);
    if (auto it = g.extraTerraformers.find(key); it != g.extraTerraformers.end()) list.insert(list.end(), it->second.begin(), it->second.end());
    if (added) {
        g.extraTerraformers[key].push_back(added);
        list.push_back(std::move(added));
    }
    if (list.empty()) g.terraformers.erase(key);
    else g.terraformers[key] = std::make_shared<std::vector<std::function<double(Start const&, int, int, int)>> const>(std::move(list));
}
} // namespace

void addTerraformer(std::string const& key, std::function<double(Start const&, int, int, int)> fn) {
    std::lock_guard lock(generators().mutex);
    rebuildTerraformers(generators(), normalizeKey(key), std::move(fn));
    bumpVersion();
}

// ---- world facts -------------------------------------------------------------------------------------

std::int64_t levelSeed() {
    auto& r = records();
    if (r.haveSeed.load()) return r.seed.load();
    if (auto level = ll::service::getLevel()) {
        auto seed = static_cast<std::int64_t>(level->getLevelSeed64().mValue);
        r.seed.store(seed);
        r.haveSeed.store(true);
        return seed;
    }
    return 0;
}

namespace {
/// setStructureDimOverride (selftests): INT_MIN = none.
std::atomic<int> gStructureDimOverride{std::numeric_limits<int>::min()};
} // namespace

void setStructureDimOverride(std::optional<int> dim) { gStructureDimOverride.store(dim.value_or(std::numeric_limits<int>::min())); }

std::optional<int> twilightDim() {
    if (int o = gStructureDimOverride.load(); o != std::numeric_limits<int>::min()) return o;
    if (auto t = gf::world::twilight()) return static_cast<int>(*t);
    return std::nullopt;
}

int seaLevelOf(int dim) {
    auto tf = twilightDim();
    return tf && *tf == dim ? kTwilightSeaLevel : kDefaultSeaLevel;
}

std::string biomeAt(int dim, int x, int y, int z) {
    auto tf = twilightDim();
    if (tf && *tf == dim) {
        auto b = worldgen::biomeAt(BlockPos{x, y, z});
        if (!b.empty()) return b;
    }
    if (auto* region = game::regionOf(dim); region && region->hasChunksAt(BlockPos{x, y, z}, 0, false)) {
        auto const& name = region->getBiome(BlockPos{x, y, z}).mHash->getString();
        return name.find(':') == std::string::npos ? "minecraft:" + name : name;
    }
    return {};
}

bool biomeMatches(std::string const& biomes, std::string const& biomeId) {
    if (biomes.empty() || biomeId.empty()) return false;
    if (biomes.front() == '#') return tags::has(tags::Registry::Biome, biomes.substr(1), biomeId);
    return biomes == biomeId;
}

// ---- record store ------------------------------------------------------------------------------------

std::string recordDbKey(int dim, int chunkX, int chunkZ) {
    return core::worldKey(kUnit, "start:" + std::to_string(dim) + ":" + std::to_string(chunkX) + ":" + std::to_string(chunkZ));
}

RecordPtr findRecord(int dim, int chunkX, int chunkZ, std::string const& key) {
    auto&            r = records();
    std::shared_lock lock(r.mutex);
    auto             it = r.byStartChunk.find({dim, chunkX, chunkZ});
    if (it == r.byStartChunk.end()) return nullptr;
    for (auto const& rec : it->second)
        if (rec->start.key == key) return rec;
    return nullptr;
}

std::vector<RecordPtr> recordsStartedIn(int dim, int chunkX, int chunkZ) {
    auto&            r = records();
    std::shared_lock lock(r.mutex);
    auto             it = r.byStartChunk.find({dim, chunkX, chunkZ});
    return it == r.byStartChunk.end() ? std::vector<RecordPtr>{} : it->second;
}

std::vector<RecordPtr> recordsIntersecting(int dim, int chunkX, int chunkZ) {
    auto&            r = records();
    std::shared_lock lock(r.mutex);
    auto             it = r.byChunk.find({dim, chunkX, chunkZ});
    return it == r.byChunk.end() ? std::vector<RecordPtr>{} : it->second;
}

std::vector<RecordPtr> allRecords(int dim) {
    auto&                  r = records();
    std::shared_lock       lock(r.mutex);
    std::vector<RecordPtr> out;
    for (auto const& [k, list] : r.byStartChunk)
        if (k.dim == dim) out.insert(out.end(), list.begin(), list.end());
    return out;
}

namespace {

/// The stored record of `key` started in chunk (cx, cz) (records lock held).
RecordPtr findLocked(Records& r, int dim, int chunkX, int chunkZ, std::string const& key) {
    auto it = r.byStartChunk.find({dim, chunkX, chunkZ});
    if (it == r.byStartChunk.end()) return nullptr;
    for (auto const& rec : it->second)
        if (rec->start.key == key) return rec;
    return nullptr;
}

/// Stores a record (records lock held).
void storeLocked(Records& r, RecordPtr const& record) {
    record->start.data["chunk"] = Json::array({record->chunkX, record->chunkZ});
    indexRecord(r, record);
    r.dirty.emplace(record->start.dimension, record->chunkX, record->chunkZ);
    bumpVersion();
}

/// Stores a start the Starts stage made for world generation `world`: dropped when that world was
/// closed meanwhile (clearRecords), and never in place of a start of the key already stored there
/// (another thread evaluating the same chunk made the same one first) other than a /place record.
void putStartRecord(RecordPtr const& record, std::uint64_t world) {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    if (r.world != world) return;
    if (auto old = findLocked(r, record->start.dimension, record->chunkX, record->chunkZ, record->start.key); old && !old->placed) return;
    storeLocked(r, record);
}

/// Replaces `existing` by `revived` (its pieces rebuilt) when `existing` is still the stored record
/// and the world is still generation `world`; the conquered flag set meanwhile is kept.
void replaceRecord(RecordPtr const& existing, RecordPtr const& revived, std::uint64_t world) {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    if (r.world != world) return;
    if (findLocked(r, existing->start.dimension, existing->chunkX, existing->chunkZ, existing->start.key) != existing) return;
    revived->start.conquered = existing->start.conquered;
    storeLocked(r, revived);
}

} // namespace

void putRecord(RecordPtr record) {
    if (!record) return;
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    storeLocked(r, record);
}

std::uint64_t recordWorld() {
    auto&            r = records();
    std::shared_lock lock(r.mutex);
    return r.world;
}

std::uint64_t recordsVersion() { return gVersion.load(std::memory_order_acquire); }

void eraseRecord(int dim, int chunkX, int chunkZ, std::string const& key) {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    unindexRecord(r, dim, chunkX, chunkZ, key);
    r.dirty.emplace(dim, chunkX, chunkZ);
    bumpVersion();
}

void markDirty(Record const& rec) {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    r.dirty.emplace(rec.start.dimension, rec.chunkX, rec.chunkZ);
}

Json recordToJson(Record const& rec) {
    Json pieces = Json::array();
    for (std::size_t i = 0; i < rec.pieces.size(); ++i) {
        auto const& pi = rec.pieces[i];
        Json        p{{"box", boxJson(pi.box)}, {"protected", pi.protectedPiece}, {"spawn_index", pi.spawnIndex}, {"allow_features", pi.utilityAllowsFeatures}, {"type", pi.type}};
        if (i < rec.livePieces.size() && rec.livePieces[i]) p["piece"] = savePiece(*rec.livePieces[i]);
        pieces.push_back(std::move(p));
    }
    return Json{
        {"key", rec.start.key},
        {"dim", rec.start.dimension},
        {"chunk", Json::array({rec.chunkX, rec.chunkZ})},
        {"center", Json::array({rec.start.center.x, rec.start.center.y, rec.start.center.z})},
        {"box", boxJson(rec.start.box)},
        {"seed", rec.start.seed},
        {"conquered", rec.start.conquered},
        {"generated", rec.start.generated},
        {"forced", rec.forced},
        {"placed", rec.placed},
        {"pieces", pieces},
        {"data", rec.start.data},
    };
}

RecordPtr recordFromJson(Json const& j) {
    if (!j.is_object() || !j.contains("key")) return nullptr;
    auto rec              = std::make_shared<Record>();
    rec->start.key        = j.value("key", std::string{});
    rec->start.dimension  = j.value("dim", 0);
    auto chunk            = j.value("chunk", Json::array({0, 0}));
    rec->chunkX           = chunk.is_array() && chunk.size() == 2 ? chunk[0].get<int>() : 0;
    rec->chunkZ           = chunk.is_array() && chunk.size() == 2 ? chunk[1].get<int>() : 0;
    auto center           = j.value("center", Json::array({0, 0, 0}));
    if (center.is_array() && center.size() == 3) rec->start.center = {rec->start.dimension, center[0].get<int>(), center[1].get<int>(), center[2].get<int>()};
    rec->start.box        = boxOf(j.value("box", Json()));
    rec->start.seed       = j.value("seed", std::int64_t{0});
    rec->start.conquered  = j.value("conquered", false);
    rec->start.generated  = j.value("generated", false);
    rec->forced           = j.value("forced", false);
    rec->placed           = j.value("placed", false);
    rec->start.data       = j.value("data", Json::object());
    bool allLive          = true;
    std::vector<std::shared_ptr<structure::Piece>> live;
    for (auto const& p : j.value("pieces", Json::array())) {
        PieceInfo pi;
        pi.box                   = boxOf(p.value("box", Json()));
        pi.protectedPiece        = p.value("protected", true);
        pi.spawnIndex            = p.value("spawn_index", -1);
        pi.utilityAllowsFeatures = p.value("allow_features", false);
        pi.type                  = p.value("type", std::string{});
        rec->pieces.push_back(pi);
        rec->start.pieceBoxes.push_back(pi.box);
        std::shared_ptr<structure::Piece> piece;
        if (p.contains("piece")) piece = loadPiece(p["piece"]);
        if (!piece) allLive = false;
        live.push_back(std::move(piece));
    }
    // CustomStructureData#forDeserialization: a start is usable with every piece back, else it
    // keeps its boxes only (protection, spawns and clearance still work)
    if (allLive) rec->livePieces = std::move(live);
    return rec;
}

void flushRecords() {
    if (!game::db::available()) return;
    auto&                               r = records();
    std::set<std::tuple<int, int, int>> dirty;
    std::map<std::tuple<int, int, int>, Json> docs;
    {
        std::unique_lock lock(r.mutex);
        dirty.swap(r.dirty);
        for (auto const& [dim, cx, cz] : dirty) {
            Json doc = Json::object();
            if (auto it = r.byStartChunk.find({dim, cx, cz}); it != r.byStartChunk.end())
                for (auto const& rec : it->second) doc[rec->start.key] = recordToJson(*rec);
            docs[{dim, cx, cz}] = std::move(doc);
        }
    }
    for (auto const& [k, doc] : docs) {
        auto const& [dim, cx, cz] = k;
        if (doc.empty()) game::db::erase(recordDbKey(dim, cx, cz));
        else game::db::put(recordDbKey(dim, cx, cz), doc);
    }
}

void loadRecords() {
    clearRecords();
    if (!game::db::available()) return;
    std::vector<std::string> keys;
    game::db::forEachKey(core::worldKey(kUnit, "start:"), [&](std::string_view suffix) { keys.emplace_back(suffix); });
    int n = 0;
    for (auto const& suffix : keys) {
        auto doc = game::db::get(core::worldKey(kUnit, "start:" + suffix));
        if (!doc || !doc->is_object()) continue;
        for (auto const& [key, value] : doc->items()) {
            RecordPtr rec;
            try {
                rec = recordFromJson(value);
            } catch (std::exception const& e) {
                log().warn("landmark: unreadable start {} in {}: {}", key, suffix, e.what());
            }
            if (rec) {
                auto&            r = records();
                std::unique_lock lock(r.mutex);
                rec->start.data["chunk"] = Json::array({rec->chunkX, rec->chunkZ});
                indexRecord(r, rec);
                bumpVersion();
                ++n;
            }
        }
    }
    if (n > 0) log().info("landmark: {} starts loaded", n);
}

void clearRecords() {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    r.byStartChunk.clear();
    r.byChunk.clear();
    r.evaluated.clear();
    ++r.world; // a chunk still being evaluated for the closed world never lands in the next one
    r.aroundDone.clear();
    r.probes.clear();
    r.dirty.clear();
    r.haveSeed.store(false);
    bumpVersion();
}

// ---- start creation ----------------------------------------------------------------------------------

RecordPtr createStart(Info const& info, int dim, int chunkX, int chunkZ, std::optional<GlobalPos> at, bool forced) {
    // LandmarkStructure#findGenerationPoint: x / z in the start chunk, y by the terrain rule
    int x = at ? at->x : chunkX * 16 + (info.centerInChunk ? 7 : 0);
    int z = at ? at->z : chunkZ * 16 + (info.centerInChunk ? 7 : 0);
    int y = at ? at->y : heightFor(info, dim, x, z);

    auto rec              = std::make_shared<Record>();
    rec->chunkX           = chunkX;
    rec->chunkZ           = chunkZ;
    rec->forced           = forced;
    rec->start.key        = info.d.key;
    rec->start.dimension  = dim;
    rec->start.center     = {dim, x, y, z};
    rec->start.seed       = levelSeed();
    rec->start.conquered  = false;
    rec->start.data       = Json{{"chunk", Json::array({chunkX, chunkZ})}};

    auto gen = generatorFor(info.d.key);
    if (gen && gen->layout) {
        // getFirstPiece's RandomSource.create(seed + chunkX * 25117L + chunkZ * 151121L); what Java
        // draws from Structure.GenerationContext#random() the layout takes from ext::contextRandom
        core::jrand::LegacyRandom    random(core::landmark::java::chunkSeed(rec->start.seed, chunkX, chunkZ));
        gf::landmark::LayoutContext  ctx{rec->start, random, {}};
        bool                         ok = false;
        try {
            ok = gen->layout(ctx);
        } catch (std::exception const& e) {
            log().error("landmark: layout of {} failed: {}", info.d.key, e.what());
        }
        if (!ok || ctx.pieces.empty()) return nullptr; // StructureStart.INVALID_START
        // LandmarkStructure#getStructurePieceGenerationStubFunction: pieces sorted by SortablePiece key
        std::stable_sort(ctx.pieces.begin(), ctx.pieces.end(), [](auto const& a, auto const& b) {
            auto key = [](structure::Piece const& p) {
                auto const* s = dynamic_cast<structure::bases::SortablePiece const*>(&p);
                return s ? s->getSortKey() : 0;
            };
            return key(*a) < key(*b);
        });
        BlockBox all = ctx.pieces.front()->bbox;
        for (auto& p : ctx.pieces) {
            rec->pieces.push_back(infoOf(*p));
            rec->start.pieceBoxes.push_back(p->bbox);
            all = {std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ),
                   std::max(all.maxX, p->bbox.maxX), std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ)};
            rec->livePieces.emplace_back(std::move(p));
        }
        // StructureStart#getBoundingBox: Structure#adjustBoundingBox
        rec->start.box       = cl::adjustBoundingBox(all, info.adjustment);
        rec->start.generated = true;
    } else {
        // no generator yet: the site is reserved with the clearance square around the center
        int half              = std::max(8, static_cast<int>(std::ceil(info.decoration.chunkClearanceRadius * 16.0f)));
        rec->start.box        = {x - half, y, z - half, x + half, y, z + half};
        rec->start.generated  = false;
    }
    return rec;
}

int placeLoaded(Record const& rec, BlockSource& region) {
    if (rec.livePieces.empty()) return 0;
    BlockBox all = rec.livePieces.front()->bbox;
    for (auto const& p : rec.livePieces)
        all = {std::min(all.minX, p->bbox.minX), all.minY, std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX), all.maxY, std::max(all.maxZ, p->bbox.maxZ)};
    int placed = 0;
    forChunks(all, [&](int cx, int cz) {
        if (!region.hasChunksAt(BlockPos{cx * 16, region.getMinHeight(), cz * 16}, 0, false)) return;
        LoadedRegion adapter(region, cx, cz, rec.start.seed);
        // Java's /place structure passes the level's random; this takes the one the chunk's
        // decoration would have used, so a placement is reproducible
        auto random = decorationRandom(rec.start.seed, cx, cz, rec.start.key);
        placeInChunk(rec, adapter, cx, cz, *random);
        ++placed;
    });
    return placed;
}

void reviveRecord(RecordPtr const& existing) {
    // a start whose pieces are not in memory (reserved before its generator existed, or read back
    // from the world DB with pieces the structure kit cannot rebuild) gets them again from its
    // deterministic layout, at its recorded center; chunks generated from then on show them
    if (!existing || !existing->livePieces.empty() || existing->reviveTried.exchange(true)) return;
    auto const* inf = info(existing->start.key);
    if (!inf || !generatorFor(existing->start.key)) {
        existing->reviveTried = false; // try again once a generator registers
        return;
    }
    auto                     world = recordWorld(); // a world closed meanwhile never gets the result
    std::optional<GlobalPos> at;
    if (existing->forced || existing->start.generated) at = existing->start.center;
    auto rec = createStart(*inf, existing->start.dimension, existing->chunkX, existing->chunkZ, at, existing->forced);
    if (!rec) return;
    rec->placed = existing->placed;
    replaceRecord(existing, rec, world);
}

// ---- worldgen: structure sets ------------------------------------------------------------------------

namespace {

/// Structure#findValidGenerationPoint + generate for start chunk (cx, cz): a new start of `info`
/// where its biome allows one (nullptr: none). Nothing is stored.
RecordPtr makeNewStart(Info const& info, int dim, int chunkX, int chunkZ) {
    // Only the worldgen biome source here: this runs on generation threads, where the level's
    // BlockSource must not be touched.
    if (info.traits.landmark) {
        // LandmarkStructure#findValidGenerationPoint: the main biome at the center of the
        // biome-grid tile decides before the generation point is looked for
        auto col   = cl::biomeSampleColumn(chunkX, chunkZ);
        auto biome = worldgen::mainBiomeAt(col.x, col.z);
        if (biome.empty()) biome = worldgen::biomeAt(BlockPos{col.x, seaLevelOf(dim), col.z});
        if (!biomeMatches(info.biomes, biome)) return nullptr;
    }
    auto rec = createStart(info, dim, chunkX, chunkZ, std::nullopt, false);
    if (!rec) return nullptr;
    if (!info.traits.landmark) {
        // Structure#findValidGenerationPoint: findGenerationPoint, then isValidBiome at the stub's
        // position (the noise biome at the generation point, its y included)
        auto const& c = rec->start.center;
        if (!biomeMatches(info.biomes, worldgen::biomeAt(BlockPos{c.x, c.y, c.z}))) return nullptr;
    }
    return rec;
}

/// The stored start of `info` in start chunk (cx, cz) that the chunk's evaluation keeps (a
/// /twilightforest place record of the key is no start: the chunk's own start replaces it).
RecordPtr existingStart(Info const& info, int dim, int chunkX, int chunkZ) {
    auto existing = findRecord(dim, chunkX, chunkZ, info.d.key);
    return existing && !existing->placed ? existing : nullptr;
}

bool tryGenerateStructure(Info const& info, int dim, int chunkX, int chunkZ, std::uint64_t world) {
    if (auto existing = existingStart(info, dim, chunkX, chunkZ)) {
        reviveRecord(existing);
        return true;
    }
    auto rec = makeNewStart(info, dim, chunkX, chunkZ);
    if (!rec) return false;
    putStartRecord(rec, world);
    return true;
}

plc::SetLookup setLookup() {
    return [](std::string const& id) -> plc::Placement const* {
        auto const* s = structureSet(id);
        return s ? &s->placement : nullptr;
    };
}

/// ChunkGenerator#createStructures for one structure set of start chunk (cx, cz): its placement,
/// then vanilla's weighted pick of its structures until one starts (`tryOne`). The key that
/// started ("" none).
std::string evaluateSet(StructureSet const& set, std::int64_t seed, int chunkX, int chunkZ, std::function<bool(Info const&)> const& tryOne) {
    if (set.placement.kind == plc::Kind::Unknown) return {};
    auto variety = [seed](int cx, int cz) { return cl::pickVariety(seed, cx, cz); };
    if (!plc::isStructureChunk(set.placement, seed, chunkX, chunkZ, variety, setLookup())) return {};
    std::vector<int> weights;
    for (auto const& [k, w] : set.structures) weights.push_back(w);
    int index = plc::tryWeighted(seed, chunkX, chunkZ, weights, [&](int i) {
        auto const* inf = info(set.structures[i].first);
        return inf && tryOne(*inf);
    });
    return index >= 0 ? set.structures[index].first : std::string{};
}

enum class Claim { Known, Claimed, Busy };

/// Claims start chunk `key` for this thread's evaluation: Known (evaluated already), Claimed (the
/// world generation it is evaluated for in `world`), Busy (another thread evaluates it).
Claim tryClaim(Records& r, ChunkKey const& key, std::uint64_t& world) {
    std::unique_lock lock(r.mutex);
    if (r.evaluated.contains(key)) return Claim::Known;
    if (r.inProgress.contains(key)) return Claim::Busy;
    r.inProgress.emplace(key, std::this_thread::get_id());
    world = r.world;
    return Claim::Claimed;
}

/// ChunkGenerator#createStructures for start chunk `key`: every structure set, the starts stored for
/// world generation `world` (dropped when that world was closed meanwhile). `claimed`: this thread
/// holds the chunk's inProgress entry; else another thread does and this is a duplicate
/// evaluation (deterministic: the first start stored per key is kept). True when the chunk's starts
/// are known afterwards.
bool evaluateStarts(Records& r, ChunkKey const& key, std::uint64_t world, bool claimed) {
    struct Finish {
        Records&            r;
        ChunkKey const      key;
        std::uint64_t const world;
        bool const          claimed;
        ~Finish() {
            tEvaluating.pop_back();
            {
                std::unique_lock lock(r.mutex);
                if (claimed) r.inProgress.erase(key);
                if (r.world == world) {
                    if (r.evaluated.size() >= kMaxEvaluatedChunks) r.evaluated.clear(); // re-evaluating is idempotent
                    r.evaluated.insert(key);
                }
            }
            r.evaluatedChanged.notify_all();
        }
    };
    tEvaluating.push_back(key);
    Finish finish{r, key, world, claimed};
    auto   seed = levelSeed();
    for (auto const* set : structureSets())
        (void)evaluateSet(*set, seed, key.x, key.z, [&](Info const& inf) { return tryGenerateStructure(inf, key.dim, key.x, key.z, world); });
    std::shared_lock lock(r.mutex);
    return r.world == world;
}

} // namespace

void noteServerThread() { gServerThread.store(std::this_thread::get_id(), std::memory_order_relaxed); }

void setEvaluationBusy(int dim, int chunkX, int chunkZ, bool busy) {
    auto& r = records();
    {
        std::unique_lock lock(r.mutex);
        // std::thread::id{} is no thread's id: the chunk looks claimed by another thread
        if (busy) r.inProgress[{dim, chunkX, chunkZ}] = std::thread::id{};
        else r.inProgress.erase({dim, chunkX, chunkZ});
    }
    r.evaluatedChanged.notify_all();
}

void forgetEvaluation(int dim, int chunkX, int chunkZ) {
    auto&            r = records();
    std::unique_lock lock(r.mutex);
    r.evaluated.erase({dim, chunkX, chunkZ});
    std::erase_if(r.aroundDone, [&](ChunkKey const& k) {
        return k.dim == dim && std::abs(k.x - chunkX) <= kReferenceRange && std::abs(k.z - chunkZ) <= kReferenceRange;
    });
    std::erase_if(r.probes, [&](auto const& e) { return std::get<0>(e.first) == dim && std::get<1>(e.first) == chunkX && std::get<2>(e.first) == chunkZ; });
}

bool ensureStartsIn(int dim, int chunkX, int chunkZ) {
    auto tf = twilightDim();
    if (!tf || *tf != dim) return true; // TF's structure sets only place in the Twilight Forest
    auto&          r = records();
    ChunkKey const key{dim, chunkX, chunkZ};
    {
        std::shared_lock lock(r.mutex);
        if (r.evaluated.contains(key)) return true;
    }
    // a layout asking about its own chunk: what is stored so far (the evaluation is under way here)
    if (evaluatingHere(key)) return false;
    auto const deadline = std::chrono::steady_clock::now() + kEvaluationWait;
    for (;;) {
        std::uint64_t world = 0;
        switch (tryClaim(r, key, world)) {
        case Claim::Known: return true;
        case Claim::Claimed: return evaluateStarts(r, key, world, true);
        case Claim::Busy: break;
        }
        // Another thread evaluates the chunk. A generation thread outside any evaluation waits for
        // it (that thread never waits itself, so it finishes); the server thread never waits, and
        // neither does a thread inside an evaluation (two layouts asking about each other's chunks
        // would wait for each other): they evaluate the chunk themselves
        if (tEvaluating.empty() && !onServerThread()) {
            bool settled = false;
            {
                std::unique_lock lock(r.mutex);
                settled = r.evaluatedChanged.wait_until(lock, deadline, [&] { return r.evaluated.contains(key) || !r.inProgress.contains(key); });
            }
            if (settled) continue; // known now, or free to claim (the other thread's world was closed)
            log().warn("landmark: chunk {} {}: another thread is still evaluating its starts after {} s; evaluating them here", chunkX, chunkZ,
                       std::chrono::duration_cast<std::chrono::seconds>(kEvaluationWait).count());
        }
        return evaluateStarts(r, key, recordWorld(), false);
    }
}

bool startsEvaluated(int dim, int chunkX, int chunkZ) {
    auto&            r = records();
    std::shared_lock lock(r.mutex);
    return r.evaluated.contains({dim, chunkX, chunkZ});
}

void ensureStartsAround(int dim, int chunkX, int chunkZ) {
    auto tf = twilightDim();
    if (!tf || *tf != dim) return;
    auto& r = records();
    {
        // fast path for the hot queries (feature clearance, terraforming): one shared lock
        std::shared_lock lock(r.mutex);
        if (r.aroundDone.contains({dim, chunkX, chunkZ})) return;
    }
    // the free chunks first, the ones another thread is evaluating last (generation threads working
    // on neighbouring chunks walk the same range: they share it out instead of queueing chunk by chunk)
    bool                  complete = true;
    std::vector<ChunkKey> busy;
    for (int dx = -kReferenceRange; dx <= kReferenceRange; ++dx) {
        for (int dz = -kReferenceRange; dz <= kReferenceRange; ++dz) {
            ChunkKey const key{dim, chunkX + dx, chunkZ + dz};
            if (evaluatingHere(key)) {
                complete = false; // this thread's own evaluation (a layout's query): not known yet
                continue;
            }
            std::uint64_t world = 0;
            switch (tryClaim(r, key, world)) {
            case Claim::Known: break;
            case Claim::Claimed: complete = evaluateStarts(r, key, world, true) && complete; break;
            case Claim::Busy: busy.push_back(key); break;
            }
        }
    }
    for (auto const& key : busy) complete = ensureStartsIn(key.dim, key.x, key.z) && complete;
    if (!complete) return;
    std::unique_lock lock(r.mutex);
    if (r.aroundDone.size() >= kMaxEvaluatedChunks) r.aroundDone.clear(); // only a fast path
    r.aroundDone.insert({dim, chunkX, chunkZ});
}

std::string probeStart(int dim, StructureSet const& set, int chunkX, int chunkZ) {
    auto&         r = records();
    auto          probeKey = std::make_tuple(dim, chunkX, chunkZ, set.id);
    std::uint64_t world    = 0;
    {
        std::shared_lock lock(r.mutex);
        if (auto it = r.probes.find(probeKey); it != r.probes.end()) return it->second;
        world = r.world;
    }
    // what evaluateStarts would store for this set: the stored start of a key, else a new one
    auto found = evaluateSet(set, levelSeed(), chunkX, chunkZ, [&](Info const& inf) {
        return existingStart(inf, dim, chunkX, chunkZ) || makeNewStart(inf, dim, chunkX, chunkZ);
    });
    std::unique_lock lock(r.mutex);
    if (r.world == world) {
        if (r.probes.size() >= kMaxEvaluatedChunks) r.probes.clear();
        r.probes[probeKey] = found;
    }
    return found;
}

bool chunkLoaded(int dim, int chunkX, int chunkZ) {
    // the level's BlockSource belongs to the server thread: a caller on another thread (a W1 unit
    // asking isProtectedFor during generation) gets no ready check rather than a racy read
    if (gServerThread.load(std::memory_order_relaxed) != std::thread::id{} && !onServerThread()) return true;
    auto* region = game::regionOf(dim);
    return region && region->hasChunksAt(BlockPos{chunkX * 16, region->getMinHeight(), chunkZ * 16}, 0, false);
}

RecordPtr nearestLandmark(int dim, int chunkX, int chunkZ, std::optional<GlobalPos> pos) {
    auto center = cl::nearestCenter(chunkX, chunkZ);
    int  ccx = center.x >> 4, ccz = center.z >> 4;
    // LandmarkUtil#locateNearestMatchingLandmark(..., checkReady = true): nothing while the center
    // chunk is not loaded (Level#hasChunk), then ChunkAccess#getAllStarts, which holds valid starts only
    if (chunkLoaded(dim, ccx, ccz)) {
        (void)ensureStartsIn(dim, ccx, ccz);
        for (auto const& rec : recordsStartedIn(dim, ccx, ccz)) {
            if (rec->placed || rec->pieces.empty()) continue; // /place stores no start; a reserved site is no valid start
            auto const* inf = info(rec->start.key);
            if (inf && inf->traits.landmarkTag) return rec;
        }
    }
    if (pos) {
        // TF reads the grid center chunk only. forceStart's starts (the selftests' stand-in for a
        // grid start, at test positions off the grid) count too; /twilightforest place's never do
        for (auto const& rec : recordsIntersecting(dim, pos->x >> 4, pos->z >> 4)) {
            auto const* inf = info(rec->start.key);
            if (rec->forced && !rec->placed && !rec->pieces.empty() && inf && inf->traits.landmarkTag && rec->start.box.contains(pos->x, pos->y, pos->z))
                return rec;
        }
    }
    return nullptr;
}

RecordPtr nearestLandmarkOf(int dim, std::string const& key, int blockX, int blockZ) {
    auto center = cl::nearestCenter(blockX >> 4, blockZ >> 4);
    int  ccx = center.x >> 4, ccz = center.z >> 4;
    ensureStartsAround(dim, ccx, ccz);
    // the starts of `key` referencing the center chunk (ChunkAccess#getReferencesForStructure) whose
    // start chunk is loaded (level.hasChunk(packedX, packedZ)), valid ones only (StructureStart
    // #isValid: a reserved site has no pieces). There is no ready check on the center chunk itself
    for (auto const& rec : recordsIntersecting(dim, ccx, ccz))
        if (rec->start.key == key && !rec->placed && !rec->pieces.empty() && chunkLoaded(dim, rec->chunkX, rec->chunkZ)) return rec;
    // TF reads the grid center chunk only; forceStart's starts (the selftests' stand-in for a grid
    // start, off the grid) count where their box covers the column. Natural and placed ones never do
    for (auto const& rec : recordsIntersecting(dim, blockX >> 4, blockZ >> 4))
        if (rec->forced && !rec->placed && !rec->pieces.empty() && rec->start.key == key && rec->start.box.minX <= blockX && blockX <= rec->start.box.maxX
            && rec->start.box.minZ <= blockZ && blockZ <= rec->start.box.maxZ)
            return rec;
    return nullptr;
}

bool playerBoxReaches(AABB const& box, int x, int y, int z, double inflate) {
    // AABB(BlockPos).inflate(r): [pos - r, pos + 1 + r]; AABB#intersects is strict on both sides
    double minX = x - inflate, minY = y - inflate, minZ = z - inflate;
    double maxX = x + 1.0 + inflate, maxY = y + 1.0 + inflate, maxZ = z + 1.0 + inflate;
    return box.min.x < maxX && box.max.x > minX && box.min.y < maxY && box.max.y > minY && box.min.z < maxZ && box.max.z > minZ;
}

std::vector<BlockBox> pieceBoxesAt(Record const& r, int x, int y, int z) {
    std::vector<BlockBox> out;
    for (auto const& p : r.pieces)
        if (p.box.contains(x, y, z)) out.push_back(p.box);
    return out;
}

int spawnIndexAt(Record const& r, int x, int y, int z) {
    if (auto gen = generatorFor(r.start.key); gen && gen->spawnIndex) return gen->spawnIndex(r.start, BlockPos{x, y, z});
    int highest = -1;
    for (auto const& p : r.pieces)
        if (p.box.contains(x, y, z) && p.spawnIndex > highest) highest = p.spawnIndex;
    return highest;
}

// ---- worldgen stages ---------------------------------------------------------------------------------

int decorateStep(worldgen::Region& region, int chunkX, int chunkZ, worldgen::ext::DecorationContext const& d) {
    // the pipeline runs this at all 11 steps; no landmark structure decorates at most of them
    if (d.step < 0 || d.step >= dec::kSteps || !(decorationStepMask() & (1u << d.step))) return 0;
    int dim = region.dimension();
    ensureStartsAround(dim, chunkX, chunkZ); // known after the first step: one shared lookup
    // StructureManager#startsForStructure for the structures decorating at this step: every start
    // reaching the chunk. One whose pieces are not in memory gets them again first (reviveRecord
    // replaces the record, so it is read again)
    struct Due {
        dec::Slot slot;
        RecordPtr rec;
    };
    std::vector<Due> due;
    for (auto const& rec : recordsIntersecting(dim, chunkX, chunkZ)) {
        auto slot = decorationSlot(rec->start.key);
        if (slot.step != d.step) continue;
        auto live = rec;
        if (live->livePieces.empty()) {
            reviveRecord(live);
            live = findRecord(dim, rec->chunkX, rec->chunkZ, rec->start.key);
            if (!live || live->livePieces.empty()) continue;
        }
        due.push_back({slot, live});
    }
    // in registry order (their index); a structure's starts share its random
    std::stable_sort(due.begin(), due.end(), [](Due const& a, Due const& b) {
        return a.slot.index != b.slot.index ? a.slot.index < b.slot.index : a.rec->start.key < b.rec->start.key;
    });
    int placed = 0;
    for (std::size_t i = 0; i < due.size();) {
        // worldgenrandom.setFeatureSeed(decorationSeed, index, step), then the structure's starts
        auto random = d.structureRandom(due[i].slot.index);
        auto key    = due[i].rec->start.key;
        for (; i < due.size() && due[i].rec->start.key == key; ++i) {
            placeInChunk(*due[i].rec, region, chunkX, chunkZ, *random);
            ++placed;
        }
    }
    return placed;
}

void registerWorldgenStages() {
    // ChunkStatus.STRUCTURE_STARTS: the starts beginning in this chunk
    worldgen::addStage(worldgen::Stage::Starts, "landmark.starts", 0, [](worldgen::Chunk& chunk, worldgen::Region& region) {
        ensureStartsIn(region.dimension(), chunk.chunkX(), chunk.chunkZ());
    });
    // ChunkGenerator#applyBiomeDecoration's structure part, at every GenerationStep.Decoration step
    // before that step's features: the starts reaching the chunk of the structures of that step
    worldgen::ext::addStructureStep("landmark.pieces", 0, [](worldgen::Chunk& chunk, worldgen::Region& region) {
        if (auto const* d = worldgen::ext::decorationContext()) (void)decorateStep(region, chunk.chunkX(), chunk.chunkZ(), *d);
    });
}

} // namespace gf::feature::landmark

// ---- the contract ------------------------------------------------------------------------------------

namespace gf::landmark {

namespace fl = gf::feature::landmark;

void registerGenerator(std::string const& key, Generator generator) { fl::setGenerator(key, std::move(generator)); }

std::optional<Start> startAt(int dimension, BlockPos const& pos) {
    fl::ensureStartsAround(dimension, pos.x >> 4, pos.z >> 4);
    for (auto const& rec : fl::recordsIntersecting(dimension, pos.x >> 4, pos.z >> 4))
        if (!rec->placed && rec->start.box.contains(pos.x, pos.y, pos.z)) return rec->start;
    return std::nullopt;
}

std::vector<Start> startsIntersecting(int dimension, ChunkPos chunk) {
    fl::ensureStartsAround(dimension, chunk.x, chunk.z);
    std::vector<Start> out;
    for (auto const& rec : fl::recordsIntersecting(dimension, chunk.x, chunk.z))
        if (!rec->placed) out.push_back(rec->start); // /place stores no start
    return out;
}

bool isConquered(Start const& start) {
    // a Start built by another unit (a test's, F5's spawn rules) may carry no data object at all
    auto chunk = start.data.is_object() ? start.data.value("chunk", Json::array()) : Json::array();
    if (chunk.is_array() && chunk.size() == 2 && chunk[0].is_number_integer() && chunk[1].is_number_integer())
        if (auto rec = fl::findRecord(start.dimension, chunk[0].get<int>(), chunk[1].get<int>(), start.key)) return rec->start.conquered;
    return start.conquered;
}

void setConquered(int dimension, BlockPos const& pos, std::string const& key, bool conquered) {
    // LandmarkUtil#markStructureConquered
    auto        k   = fl::normalizeKey(key);
    auto        rec = fl::nearestLandmarkOf(dimension, k, pos.x, pos.z);
    auto const* inf = fl::info(k);
    if (!rec || !inf || !inf->traits.conquerable) return; // only TFStructureStarts carry the flag
    if (rec->start.conquered != conquered) {
        rec->start.conquered = conquered;
        fl::markDirty(*rec);
    }
    gf::log().info("landmark: {} at {} {} {} conquered={}", k, rec->start.center.x, rec->start.center.y, rec->start.center.z, conquered);
    // TFAdvancements.STRUCTURE_CLEARED for the players within 32 blocks: Level#getEntitiesOfClass(
    // ServerPlayer, AABB(pos).inflate(32)) takes every player whose bounding box intersects the area
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<ActorUniqueID> near;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dimension) return true;
        if (fl::playerBoxReaches(p.getAABB(), pos.x, pos.y, pos.z, 32.0)) near.push_back(p.getOrCreateUniqueID());
        return true;
    });
    for (auto id : near)
        if (auto* p = level->getPlayer(id)) gf::progression::trigger(*p, "twilightforest:structure_cleared", Json{{"structure", k}});
}

bool generateAt(std::string const& key, BlockSource& region, BlockPos const& pos) {
    auto const* inf = fl::info(key);
    if (!inf) {
        gf::log().warn("landmark: generateAt: unknown structure {}", key);
        return false;
    }
    int  dim = static_cast<int>(region.getDimensionId());
    auto rec = fl::createStart(*inf, dim, pos.x >> 4, pos.z >> 4, GlobalPos{dim, pos.x, pos.y, pos.z}, true);
    if (!rec || !rec->start.generated) return false;
    // Java's /place structure (and sapling growth) places the pieces and stores no start: the record
    // is kept only so the parts in chunks generated later still get placed, marked `placed` so
    // protection, locate, conquest and the controlled spawns ignore it, and never in place of the
    // chunk's own start of that key
    rec->placed = true;
    auto old    = fl::findRecord(dim, pos.x >> 4, pos.z >> 4, inf->d.key);
    if (!old || old->placed) {
        fl::putRecord(rec);
        fl::flushRecords();
    }
    fl::placeLoaded(*rec, region);
    return true;
}

std::optional<Start> forceStart(std::string const& key, GlobalPos const& pos) {
    auto const* inf = fl::info(key);
    if (!inf) {
        gf::log().warn("landmark: forceStart: unknown structure {}", key);
        return std::nullopt;
    }
    auto rec = fl::createStart(*inf, pos.dim, pos.x >> 4, pos.z >> 4, pos, true);
    if (!rec) return std::nullopt;
    if (auto old = fl::findRecord(pos.dim, pos.x >> 4, pos.z >> 4, inf->d.key)) rec->start.conquered = old->start.conquered;
    fl::putRecord(rec);
    if (auto* region = game::regionOf(pos.dim)) fl::placeLoaded(*rec, *region);
    fl::flushRecords();
    gf::log().info("landmark: forced {} at {} {} {} (generated={})", inf->d.key, pos.x, pos.y, pos.z, rec->start.generated);
    return rec->start;
}

void addTerraformer(std::string const& key, std::function<double(Start const& start, int x, int y, int z)> fn) {
    fl::addTerraformer(key, std::move(fn));
}

bool decorationAllowed(ChunkPos chunk, DecoKind kind) {
    auto tf = fl::twilightDim();
    if (!tf) return true;
    fl::ensureStartsAround(*tf, chunk.x, chunk.z);
    core::landmark::FeatureOccupancy occ;
    switch (kind) {
    case DecoKind::Surface:
    case DecoKind::Trees: occ.surface = true; break;              // AvoidLandmarkModifier.checkSurface()
    case DecoKind::Underground: occ.underground = true; break;    // checkUnderground()
    case DecoKind::Lakes: occ.surface = occ.underground = true; break; // checkBoth()
    }
    int x = chunk.x * 16 + 8, z = chunk.z * 16 + 8;
    for (auto const& rec : fl::recordsIntersecting(*tf, chunk.x, chunk.z)) {
        if (rec->placed) continue; // no structure references the chunk
        auto const* inf = fl::info(rec->start.key);
        if (!inf || core::landmark::clearFromStructureZone(inf->decoration, occ)) continue;
        if (inf->decoration.chunkClearanceRadius > 0) {
            if (core::landmark::placementIsBlocked(x, 0, z, rec->start.box, {}, inf->decoration.chunkClearanceRadius, 0)) return false;
        } else {
            BlockBox column{chunk.x * 16, rec->start.box.minY, chunk.z * 16, chunk.x * 16 + 15, rec->start.box.maxY, chunk.z * 16 + 15};
            for (auto const& p : rec->pieces)
                if (!p.utilityAllowsFeatures && p.box.intersects(column)) return false;
        }
    }
    return true;
}

} // namespace gf::landmark

// ---- the extension -----------------------------------------------------------------------------------

namespace gf::landmark::ext {

namespace fl = gf::feature::landmark;

namespace {
/// The starts referencing a chunk (the byChunk index: their box intersects it, as
/// ChunkGenerator#createReferences records) that terraform, with their terraformers.
std::vector<std::pair<Start, fl::TerraformerList>> terraformingStarts(int dimension, int chunkX, int chunkZ) {
    fl::ensureStartsAround(dimension, chunkX, chunkZ);
    std::vector<std::pair<Start, fl::TerraformerList>> out;
    for (auto const& rec : fl::recordsIntersecting(dimension, chunkX, chunkZ)) {
        // /place stores no start, and a reserved site (no generator yet) is no valid start: neither
        // is among StructureManager#startsForStructure's results
        if (rec->placed || rec->pieces.empty()) continue;
        if (auto list = fl::terraformersFor(rec->start.key)) out.emplace_back(rec->start, std::move(list));
    }
    return out;
}
} // namespace

ChunkTerraformer chunkTerraformer(int dimension, int chunkX, int chunkZ) {
    // WorldgenHooks#gatherCustomTerrain: every CustomDensitySource start of the chunk, once per chunk;
    // getCustomDensity adds them up at each position, anywhere in the chunk (no per-block box test)
    auto starts = terraformingStarts(dimension, chunkX, chunkZ);
    if (starts.empty()) return {};
    return [starts = std::move(starts)](int x, int y, int z) {
        double sum = 0.0;
        for (auto const& [start, list] : starts)
            for (auto const& fn : *list) sum += fn(start, x, y, z);
        return sum;
    };
}

double terraformDensity(int dimension, int x, int y, int z) {
    // a per-position caller (one noise cell after another) stays in one chunk for a long run: the
    // chunk's gathered terraformer is kept per thread until the chunk, or anything it was gathered
    // from (records, generators, terraformers, the world), changes
    struct Cached {
        bool             valid = false;
        int              dimension = 0, chunkX = 0, chunkZ = 0;
        std::uint64_t    version = 0;
        ChunkTerraformer fn;
    };
    thread_local Cached cache;
    int                 chunkX = x >> 4, chunkZ = z >> 4;
    auto                version = fl::recordsVersion();
    if (!cache.valid || cache.dimension != dimension || cache.chunkX != chunkX || cache.chunkZ != chunkZ || cache.version != version) {
        cache.fn = chunkTerraformer(dimension, chunkX, chunkZ);
        // the version read before gathering: a change made meanwhile makes the next call gather again
        cache.valid     = true;
        cache.dimension = dimension;
        cache.chunkX    = chunkX;
        cache.chunkZ    = chunkZ;
        cache.version   = version;
    }
    return cache.fn ? cache.fn(x, y, z) : 0.0;
}

bool hasTerraformers(int dimension, int chunkX, int chunkZ) { return !terraformingStarts(dimension, chunkX, chunkZ).empty(); }

void setHeightProvider(HeightFn fn) {
    std::lock_guard lock(fl::generators().mutex);
    fl::generators().height = std::move(fn);
}

std::optional<int> firstOccupiedHeight(int dimension, int x, int z, worldgen::Heightmap type) {
    auto provider = fl::heightProvider();
    return provider ? provider(dimension, x, z, type) : std::nullopt;
}

std::optional<int> adjustForTerrain(int dimension, int xMin, int zMin, int xMax, int zMax, int gridLength, worldgen::Heightmap type) {
    auto provider = fl::heightProvider();
    if (!provider) return std::nullopt;
    return core::landmark::adjustForTerrain(xMin, zMin, xMax, zMax, gridLength, [&](int x, int z) { return provider(dimension, x, z, type); });
}

std::optional<int> adjustForTerrain(int dimension, int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength) {
    return fl::adjustForTerrainAroundChunk(fl::heightProvider(), dimension, xInCenterChunk, zInCenterChunk, radiusFromCenterChunk, gridLength);
}

int seaLevel(int dimension) { return fl::seaLevelOf(dimension); }

std::unique_ptr<core::jrand::WorldgenRandom> contextRandom(Start const& start) {
    int  chunkX = start.center.x >> 4, chunkZ = start.center.z >> 4;
    auto chunk  = start.data.is_object() ? start.data.value("chunk", Json::array()) : Json::array();
    if (chunk.is_array() && chunk.size() == 2 && chunk[0].is_number_integer() && chunk[1].is_number_integer()) {
        chunkX = chunk[0].get<int>();
        chunkZ = chunk[1].get<int>();
    }
    // Structure.GenerationContext#makeRandom: new WorldgenRandom(new LegacyRandomSource(0L)),
    // setLargeFeatureSeed(seed, chunkPos.x, chunkPos.z)
    auto random = std::make_unique<core::jrand::WorldgenRandom>(std::make_unique<core::jrand::LegacyRandom>(0));
    random->setLargeFeatureSeed(start.seed, chunkX, chunkZ);
    return random;
}

double invertedPyramid(Start const& start, int yOffset, int horizontalPadding, int x, int y, int z) {
    return core::landmark::InvertedPyramid{start.box, yOffset, horizontalPadding}.compute(x, y, z);
}

double boxDensity(std::vector<BlockBox> const& boxes, int dYMin, int dYMax, std::string const& adjustment, int x, int y, int z) {
    std::vector<core::landmark::BoxDensity> list;
    auto                                    adj = core::landmark::terrainAdjustmentOf(adjustment);
    for (auto const& b : boxes) list.push_back(core::landmark::BoxDensity::make(b, dYMin, dYMax, adj));
    return core::landmark::combinedBoxDensity(list, x, y, z);
}

bool featureBlocked(int dimension, int x, int y, int z, Occupancy const& occupancy) {
    fl::ensureStartsAround(dimension, x >> 4, z >> 4);
    core::landmark::FeatureOccupancy occ{occupancy.surface, occupancy.underground, occupancy.vegetation, occupancy.additionalClearance};
    for (auto const& rec : fl::recordsIntersecting(dimension, x >> 4, z >> 4)) {
        if (rec->placed) continue; // no structure references the chunk
        if (std::find(occupancy.structuresAllowed.begin(), occupancy.structuresAllowed.end(), rec->start.key) != occupancy.structuresAllowed.end()) continue;
        auto const* inf = fl::info(rec->start.key);
        if (!inf || core::landmark::clearFromStructureZone(inf->decoration, occ)) continue;
        std::vector<core::landmark::ClearancePiece> pieces;
        for (auto const& p : rec->pieces) pieces.push_back({p.box, p.utilityAllowsFeatures});
        if (core::landmark::placementIsBlocked(x, y, z, rec->start.box, pieces, inf->decoration.chunkClearanceRadius, occupancy.additionalClearance)) return true;
    }
    return false;
}

std::optional<std::vector<nlohmann::json>> potentialSpawns(int dimension, std::string const& category, int x, int y, int z) {
    fl::ensureStartsAround(dimension, x >> 4, z >> 4);
    // StructureManager#startsForStructure(new ChunkPos(pos), ControlledSpawns): every start that
    // references the position's chunk, whether or not its box holds the position
    for (auto const& rec : fl::recordsIntersecting(dimension, x >> 4, z >> 4)) {
        if (rec->placed) continue; // /place stores no start, so no structure references the chunk
        auto const* inf = fl::info(rec->start.key);
        if (!inf || !inf->traits.controlledSpawns || rec->pieces.empty()) continue; // a reserved site is no valid start yet
        auto const& cs   = inf->d.controlledSpawns;
        auto        list = [](Json const& arr) {
            std::vector<Json> out;
            if (arr.is_array())
                for (auto const& e : arr) out.push_back(e);
            return out;
        };
        // ConfigurableSpawns#getSpawnableList
        if (category != "monster") {
            if (category == "ambient") return list(cs.value("ambient_spawns", Json::array()));
            if (category == "water_creature") return list(cs.value("water_spawns", Json::array()));
            return std::vector<Json>{};
        }
        if (rec->start.conquered) return std::nullopt;
        // HollowHillStructure#canSpawnMob: the hollow hill unit refines this through its generator
        int index = fl::spawnIndexAt(*rec, x, y, z);
        if (index < 0) return std::nullopt;
        return list(cs.value("labelled_monster_spawns", Json::object()).value(std::to_string(index), Json::array()));
    }
    return std::nullopt;
}

} // namespace gf::landmark::ext
