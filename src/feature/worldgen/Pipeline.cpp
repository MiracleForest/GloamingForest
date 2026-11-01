// The staged pipeline of the Twilight Forest generator (F5), in Java's ChunkStatus order:
//   loadChunk:  Biomes -> Starts -> Noise -> Surface -> Carvers
//   decoration: Features (ChunkGenerator#applyBiomeDecoration, with the Pieces stage at the
//               surface-structures step) -> LargeWriter -> Blanket -> Spawns
// Deterministic for a seed: every placed feature is seeded with
// WorldgenRandom#setFeatureSeed(decorationSeed, featureIndex, step) over FeatureSorter's order.
#include <algorithm>
#include <chrono>
#include <mutex>
#include <map>
#include <set>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/spawn/Internal.h"
#include "feature/worldgen/Internal.h"
#include "game/Blocks.h"
#include "mod/Config.h"

namespace gf::worldgen {

GF_REGISTER_CONFIG("worldgen", WorldgenConfig);

namespace detail {

using features::detail::normalizeId;
using Json = nlohmann::json;

namespace {

std::mutex& statsMutex() {
    static std::mutex m;
    return m;
}
ext::Stats& statsRef() {
    static ext::Stats s;
    return s;
}

/// FeatureSorter's per-step order over the possible biomes, with each biome's feature indices per
/// step (cached per biome list and per loaded data).
struct FeatureOrder {
    std::vector<std::string>                       biomes;
    std::uint64_t                                  dataGeneration = 0;
    std::set<std::string>                          possible;
    std::vector<std::vector<std::string>>          steps;        ///< step -> sorted placed feature ids
    std::map<std::string, std::vector<std::vector<int>>> indices; ///< biome -> step -> feature indices
};

std::shared_ptr<FeatureOrder const> featureOrder() {
    static std::mutex                          m;
    static std::shared_ptr<FeatureOrder const> cached;
    auto                                       biomes = ext::possibleBiomes();
    auto const&                                data   = features::detail::data();
    std::lock_guard                            lock(m);
    if (cached && biomes == cached->biomes && cached->dataGeneration == data.generation) return cached;
    auto const&                            biomeSteps = data.biomeSteps;
    std::vector<core::jrand::StepFeatures> sources;
    for (auto const& b : biomes) {
        auto it = biomeSteps.find(b);
        sources.push_back(it == biomeSteps.end() ? core::jrand::StepFeatures{} : it->second);
    }
    bool cycle = false;
    auto out   = std::make_shared<FeatureOrder>();
    out->steps = core::jrand::featureOrder(sources, &cycle);
    if (cycle) features::detail::warnOnce("feature order cycle", "the biomes' feature lists order features in a cycle (Java refuses such data)");
    std::vector<std::unordered_map<std::string, int>> index(out->steps.size());
    for (std::size_t l = 0; l < out->steps.size(); ++l)
        for (std::size_t i = 0; i < out->steps[l].size(); ++i) index[l].emplace(out->steps[l][i], static_cast<int>(i));
    for (std::size_t b = 0; b < biomes.size(); ++b) {
        auto& per = out->indices[biomes[b]];
        per.resize(out->steps.size());
        for (std::size_t l = 0; l < sources[b].size() && l < index.size(); ++l)
            for (auto const& f : sources[b][l])
                if (auto it = index[l].find(f); it != index[l].end()) per[l].push_back(it->second);
    }
    out->biomes         = std::move(biomes);
    out->dataGeneration = data.generation;
    out->possible       = std::set<std::string>(out->biomes.begin(), out->biomes.end());
    cached        = out;
    return cached;
}

void runSteps(Providers const& p, Stage stage, Chunk& chunk, Region& region) {
    if (auto it = p.stages.find(stage); it != p.stages.end())
        for (auto const& s : it->second) s.fn(chunk, region);
}

/// A Chunk view of the center chunk of a region (decoration stages get a Chunk too).
class RegionChunk final : public Chunk {
public:
    explicit RegionChunk(Region& r) : mR(r) {}
    int          chunkX() const override { return mR.centerChunkX(); }
    int          chunkZ() const override { return mR.centerChunkZ(); }
    int          minY() const override { return mR.minY(); }
    int          maxY() const override { return mR.maxY(); }
    Block const& getBlock(int lx, int y, int lz) const override { return mR.getBlock(pos(lx, y, lz)); }
    void         setBlock(int lx, int y, int lz, Block const& block) override { mR.setBlock(pos(lx, y, lz), block, 2); }
    int height(Heightmap type, int lx, int lz) const override { return mR.height(type, chunkX() * 16 + lx, chunkZ() * 16 + lz); }
    std::string biome(int lx, int y, int lz) const override { return mR.biome(pos(lx, y, lz)); }
    void        setBiome(int, int, int, std::string const&) override {} // biomes are final after loadChunk

private:
    BlockPos pos(int lx, int y, int lz) const { return {chunkX() * 16 + (lx & 15), y, chunkZ() * 16 + (lz & 15)}; }
    Region&  mR;
};

} // namespace

void addStats(bool decorate, double ms) {
    std::lock_guard lock(statsMutex());
    auto&           s = statsRef();
    if (decorate) {
        ++s.chunksDecorated;
        s.decorateMs += ms;
    } else {
        ++s.chunksGenerated;
        s.generateMs += ms;
    }
}

void fallbackTerrain(Chunk& chunk) {
    static Block const* bedrock = game::blockWith("minecraft:bedrock");
    static Block const* stone   = game::blockWith("minecraft:stone");
    static Block const* dirt    = game::blockWith("minecraft:dirt");
    static Block const* grass   = game::blockWith("minecraft:grass_block");
    if (!bedrock || !stone || !dirt || !grass) return;
    int minY = chunk.minY();
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            chunk.setBlock(x, minY, z, *bedrock);
            for (int y = minY + 1; y < -3; ++y) chunk.setBlock(x, y, z, *stone);
            for (int y = -3; y < 0; ++y) chunk.setBlock(x, y, z, *dirt);
            chunk.setBlock(x, 0, z, *grass);
        }
}

void generateChunk(Chunk& chunk, Region& chunkRegion, std::int64_t seed) {
    auto p = providers();
    // Biomes: the biome source at quart resolution
    if (p->biomeSource) {
        int  qMin = chunk.minY() >> 2, qMax = (chunk.maxY() - 1) >> 2;
        auto* lca = dynamic_cast<LevelChunkAdapter*>(&chunk);
        for (int qx = 0; qx < 4; ++qx)
            for (int qz = 0; qz < 4; ++qz) {
                std::vector<std::string> column;
                column.reserve(static_cast<std::size_t>(qMax - qMin + 1));
                for (int qy = qMin; qy <= qMax; ++qy) column.push_back(p->biomeSource(chunk.chunkX() * 4 + qx, qy, chunk.chunkZ() * 4 + qz));
                bool uniform = std::all_of(column.begin(), column.end(), [&](std::string const& s) { return s == column[0]; });
                for (int dx = 0; dx < 4; ++dx)
                    for (int dz = 0; dz < 4; ++dz) {
                        int lx = qx * 4 + dx, lz = qz * 4 + dz;
                        if (uniform && lca) {
                            lca->setBiomeColumn(lx, lz, column[0]);
                            continue;
                        }
                        for (int y = chunk.minY(); y < chunk.maxY(); ++y)
                            chunk.setBiome(lx, y, lz, column[static_cast<std::size_t>((y >> 2) - qMin)]);
                    }
            }
    }
    runSteps(*p, Stage::Biomes, chunk, chunkRegion);
    runSteps(*p, Stage::Starts, chunk, chunkRegion);
    if (p->terrain) p->terrain(chunk);
    else if (config::get<WorldgenConfig>().fallbackTerrain) fallbackTerrain(chunk);
    runSteps(*p, Stage::Noise, chunk, chunkRegion);
    if (p->surface) p->surface(chunk, chunkRegion);
    runSteps(*p, Stage::Surface, chunk, chunkRegion);
    applyCarvers(*p, chunk, chunkRegion, seed);
    runSteps(*p, Stage::Carvers, chunk, chunkRegion);
}

// ==== carvers ===========================================================================================

namespace {

thread_local ext::CarverContext* tCarverContext = nullptr;

struct CarverScope {
    ext::CarverContext* previous;
    explicit CarverScope(ext::CarverContext* context) : previous(tCarverContext) { tCarverContext = context; }
    ~CarverScope() { tCarverContext = previous; }
    CarverScope(CarverScope const&)            = delete;
    CarverScope& operator=(CarverScope const&) = delete;
};

/// The carver biomes of source chunks (ChunkAccess#carverBiome: the biome source at the chunk's
/// minimum block, quart y 0), shared by the generation threads: a chunk is the source of 289
/// carves. Dropped when the biome source or the seed changes, or on world close.
struct CarverBiomeCache {
    std::mutex                                     mutex;
    std::uint64_t                                  sourceGeneration = 0;
    std::int64_t                                   seed             = 0;
    std::unordered_map<std::int64_t, std::string>  biomes;
};
CarverBiomeCache& carverBiomeCache() {
    static CarverBiomeCache c;
    return c;
}

constexpr int kCarverRange = 8; // ChunkGenerator#applyCarvers

} // namespace

void clearCarverBiomeCache() {
    auto&           c = carverBiomeCache();
    std::lock_guard lock(c.mutex);
    c.biomes.clear();
}

void applyCarvers(Providers const& p, Chunk& chunk, Region& region, std::int64_t seed, CarverListFn const& carversOf) {
    if (p.carvers.empty()) return;
    constexpr int side = 2 * kCarverRange + 1;
    int           cx = chunk.chunkX(), cz = chunk.chunkZ();

    // the biome of every source chunk
    std::vector<std::string> sourceBiomes(static_cast<std::size_t>(side * side));
    auto slot = [&](int j, int k) -> std::string& { return sourceBiomes[static_cast<std::size_t>((j + kCarverRange) * side + (k + kCarverRange))]; };
    if (!p.biomeSource) {
        // no biome source: every chunk has the dimension's single default biome
        auto b = chunk.biome(0, 0, 0);
        for (auto& s : sourceBiomes) s = b;
    } else {
        auto&                     cache = carverBiomeCache();
        std::vector<std::pair<int, int>> missing;
        {
            std::lock_guard lock(cache.mutex);
            if (cache.sourceGeneration != p.biomeSourceGeneration || cache.seed != seed || cache.biomes.size() > 65536) {
                cache.biomes.clear();
                cache.sourceGeneration = p.biomeSourceGeneration;
                cache.seed             = seed;
            }
            for (int j = -kCarverRange; j <= kCarverRange; ++j)
                for (int k = -kCarverRange; k <= kCarverRange; ++k) {
                    auto it = cache.biomes.find(core::mth::chunkPosAsLong(cx + j, cz + k));
                    if (it != cache.biomes.end()) slot(j, k) = it->second;
                    else missing.emplace_back(j, k);
                }
        }
        if (!missing.empty()) {
            for (auto [j, k] : missing)
                slot(j, k) = p.biomeSource(core::mth::fromBlock(core::mth::chunkMinBlock(cx + j)), 0, core::mth::fromBlock(core::mth::chunkMinBlock(cz + k)));
            std::lock_guard lock(cache.mutex);
            if (cache.sourceGeneration == p.biomeSourceGeneration && cache.seed == seed)
                for (auto [j, k] : missing) cache.biomes.try_emplace(core::mth::chunkPosAsLong(cx + j, cz + k), slot(j, k));
        }
    }

    // each biome's carver list, resolved once per call
    struct Resolved {
        std::string     id;
        CarverFn const* fn     = nullptr;
        Json const*     config = nullptr;
    };
    static Json const                                     emptyConfig = Json::object();
    auto const&                                           data        = features::detail::data();
    std::unordered_map<std::string, std::vector<Resolved>> lists;
    auto resolve = [&](std::string const& biome) -> std::vector<Resolved> const& {
        auto [it, fresh] = lists.try_emplace(biome);
        if (!fresh) return it->second;
        for (auto const& id : carversOf ? carversOf(biome) : ext::biomeCarvers(biome)) {
            Resolved    r{id, nullptr, &emptyConfig};
            std::string type = id;
            if (auto c = data.carvers.find(id); c != data.carvers.end()) {
                type     = normalizeId(c->second.value("type", id));
                r.config = &features::detail::fieldObj(c->second, "config");
            }
            auto fn = p.carvers.find(id);
            if (fn == p.carvers.end()) fn = p.carvers.find(type);
            if (fn != p.carvers.end()) r.fn = &fn->second;
            it->second.push_back(std::move(r));
        }
        return it->second;
    };

    ext::CarvingMask            mask(chunk.minY(), chunk.maxY());
    core::jrand::WorldgenRandom random(std::make_unique<core::jrand::LegacyRandom>(0)); // re-seeded per carver
    for (int j = -kCarverRange; j <= kCarverRange; ++j)
        for (int k = -kCarverRange; k <= kCarverRange; ++k) {
            int         sx = cx + j, sz = cz + k;
            auto const& list = resolve(slot(j, k));
            for (std::size_t l = 0; l < list.size(); ++l) {
                if (!list[l].fn) continue; // Java still counts it: the index stays the list position
                random.setLargeFeatureSeed(core::mth::ladd(seed, static_cast<std::int64_t>(l)), sx, sz);
                ext::CarverContext ctx{sx, sz, static_cast<int>(l), list[l].id, list[l].config, &mask};
                CarverScope        scope(&ctx);
                (*list[l].fn)(chunk, region, random);
            }
        }
}

void decorateChunk(Chunk* chunkIn, Region& region, std::int64_t seed) {
    auto        p = providers();
    RegionChunk viewChunk(region);
    Chunk&      chunk = chunkIn ? *chunkIn : viewChunk;
    int         cx = region.centerChunkX(), cz = region.centerChunkZ();
    int         minX = cx * 16, minZ = cz * 16;
    BlockPos    origin{minX, region.minY(), minZ}; // SectionPos.of(chunk, minSection).origin()

    // the biomes of the 3x3 chunks (every section's palette, as Java's applyBiomeDecoration),
    // restricted to the possible biomes
    auto                  order = featureOrder();
    std::set<std::string> present;
    if (!order->possible.empty()) {
        std::vector<std::string> ids;
        auto*                    source = dynamic_cast<SourceRegionAdapter*>(&region);
        if (!source || !source->chunkBiomes(1, ids)) {
            ids.clear();
            for (int x = minX - 16; x < minX + 32; x += 4)
                for (int z = minZ - 16; z < minZ + 32; z += 4)
                    for (int y = region.minY(); y < region.maxY(); y += 4) {
                        auto b = region.biome({x, y, z});
                        if (std::find(ids.begin(), ids.end(), b) == ids.end()) ids.push_back(std::move(b));
                    }
        }
        for (auto& b : ids)
            if (order->possible.contains(b)) present.insert(std::move(b));
    }

    core::jrand::WorldgenRandom random(std::make_unique<core::jrand::Xoroshiro128pp>(0));
    std::int64_t                decorationSeed = random.setDecorationSeed(seed, minX, minZ);
    int                         steps          = static_cast<int>(order->steps.size());
    int                         k              = std::max(11, steps);
    auto const&                 placed         = features::detail::data().placed;
    for (int l = 0; l < k; ++l) {
        if (l == static_cast<int>(ext::DecorationStep::SurfaceStructures)) runSteps(*p, Stage::Pieces, chunk, region);
        if (l >= steps) continue;
        std::set<int> indices;
        auto const&   stepList = order->steps[static_cast<std::size_t>(l)];
        for (auto const& b : present)
            if (auto it = order->indices.find(b); it != order->indices.end())
                indices.insert(it->second[static_cast<std::size_t>(l)].begin(), it->second[static_cast<std::size_t>(l)].end());
        for (int index : indices) {
            auto const& id = stepList[static_cast<std::size_t>(index)];
            random.setFeatureSeed(decorationSeed, index, l);
            if (auto it = placed.find(id); it != placed.end()) features::detail::placePlacedObject(it->second, id, region, origin, random);
            else features::detail::warnOnce("placed " + id, "unknown placed feature " + id);
        }
    }
    runSteps(*p, Stage::Features, chunk, region);
    if (region.dimension() >= 0) features::detail::drainLargeWriter(region.dimension(), region);
    for (auto const& [id, fn] : p->blankets) fn(chunk, region);
    runSteps(*p, Stage::Blanket, chunk, region);
    // Spawns: ChunkGenerator#spawnOriginalMobs (biome at the chunk origin, top of the world)
    if (region.dimension() >= 0) {
        core::jrand::WorldgenRandom spawnRandom(std::make_unique<core::jrand::LegacyRandom>(0));
        spawnRandom.setDecorationSeed(seed, minX, minZ);
        spawn::detail::spawnForChunkGeneration(region, region.biome({minX, region.maxY() - 1, minZ}), cx, cz, spawnRandom);
    }
    runSteps(*p, Stage::Spawns, chunk, region);
}

} // namespace detail

namespace ext {

CarvingMask::CarvingMask(int minY, int maxY)
: mBits(static_cast<std::size_t>(std::max(0, maxY - minY)) * 256),
  mMinY(minY),
  mMaxY(maxY) {}

std::size_t CarvingMask::index(int localX, int y, int localZ) const {
    // CarvingMask#getIndex: x & 15 | (z & 15) << 4 | (y - minY) << 8
    return static_cast<std::size_t>((localX & 15) | ((localZ & 15) << 4) | ((y - mMinY) << 8));
}

bool CarvingMask::get(int localX, int y, int localZ) const {
    if (y < mMinY || y >= mMaxY) return false;
    return mBits[index(localX, y, localZ)];
}

void CarvingMask::set(int localX, int y, int localZ) {
    if (y < mMinY || y >= mMaxY) return;
    auto i = index(localX, y, localZ);
    if (!mBits[i]) ++mCount;
    mBits[i] = true;
}

CarverContext* carverContext() { return detail::tCarverContext; }

bool isStartChunk(RandomSource& random, CarverContext const& context) {
    float probability = context.config ? context.config->value("probability", 0.0F) : 0.0F;
    return random.nextFloat() <= probability;
}

Stats stats() {
    std::lock_guard lock(detail::statsMutex());
    return detail::statsRef();
}

double runInMemory(std::int64_t seed, int x0, int z0, int width, int count, std::uint64_t* hash) {
    auto                start = std::chrono::steady_clock::now();
    detail::MemoryWorld world(-32, 256, seed);
    width     = std::max(1, width);
    int rows  = (count + width - 1) / width;
    // generate the chunks plus a one-chunk border (decoration windows), then decorate
    for (int z = z0 - 1; z <= z0 + rows; ++z)
        for (int x = x0 - 1; x <= x0 + width; ++x) {
            auto& c      = world.chunk(x, z);
            auto  region = world.region(x, z, 0);
            detail::generateChunk(detail::asChunk(c), *region, seed);
        }
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < count; ++i) {
        int  x = x0 + i % width, z = z0 + i / width;
        auto region = world.region(x, z, 1);
        detail::decorateChunk(&detail::asChunk(world.chunk(x, z)), *region, seed);
    }
    for (int i = 0; i < count; ++i) h = (h ^ world.hash(x0 + i % width, z0 + i / width)) * 0x100000001b3ull;
    if (hash) *hash = h;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace ext

} // namespace gf::worldgen
