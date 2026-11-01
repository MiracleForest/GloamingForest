// Contract "worldgen" (F5): the provider slots of the Twilight Forest generator (biome source,
// terrain, surface, carvers, blankets, stage steps), the world seed and the biome lookups.
#include "feature/worldgen/Worldgen.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

#include "core/jrand/BiomeManager.h"
#include "core/jrand/Mth.h"
#include "feature/worldgen/Internal.h"
#include "mod/Log.h"

namespace gf::worldgen {

namespace detail {

namespace {

std::mutex& providersMutex() {
    static std::mutex m;
    return m;
}
std::shared_ptr<Providers const>& providersPtr() {
    static std::shared_ptr<Providers const> p = std::make_shared<Providers>();
    return p;
}
std::size_t& stepSeq() {
    static std::size_t s = 0;
    return s;
}

template <class F>
void update(F&& fn) {
    std::lock_guard lock(providersMutex());
    auto            next = std::make_shared<Providers>(*providersPtr());
    fn(*next);
    providersPtr() = std::move(next);
}

std::atomic<std::int64_t> gSeed{0};

} // namespace

std::shared_ptr<Providers const> providers() {
    std::lock_guard lock(providersMutex());
    return providersPtr();
}

void         setSeed(std::int64_t seed) { gSeed.store(seed); }
std::int64_t currentSeed() {
    if (auto level = ll::service::getLevel()) return static_cast<std::int64_t>(level->getLevelSeed64().mValue);
    return gSeed.load();
}

std::int64_t zoomSeedOf(std::int64_t seed) {
    // one SHA-256 per seed and thread: region biome lookups ask for it at every position
    thread_local std::int64_t lastSeed = 0, lastZoom = core::biome::obfuscateSeed(0);
    if (seed != lastSeed) {
        lastZoom = core::biome::obfuscateSeed(seed);
        lastSeed = seed;
    }
    return lastZoom;
}

std::string zoomedBiome(std::int64_t seed, BlockPos const& pos, NoiseBiomeFn const& noiseBiome) {
    auto q = core::biome::zoom(zoomSeedOf(seed), pos.x, pos.y, pos.z);
    return noiseBiome(q.x, q.y, q.z);
}

namespace {
std::atomic<int> gTwilightMinY{-32}, gTwilightMaxY{256}; // TF's dimension type: min_y -32, height 288
} // namespace

void setTwilightHeight(int minY, int maxY) {
    gTwilightMinY.store(minY);
    gTwilightMaxY.store(maxY);
}
int twilightMinY() { return gTwilightMinY.load(); }
int twilightMaxY() { return gTwilightMaxY.load(); }

std::unique_ptr<RandomSource> worldGenRegionRandom(std::int64_t seed, int cx, int cz) {
    core::jrand::Xoroshiro128pp base(seed);
    auto                        factory = base.forkPositional()->fromHashOf("minecraft:worldgen_region_random")->forkPositional();
    return factory->at(cx * 16, 0, cz * 16);
}

namespace {
std::mutex& biomeCacheMutex() {
    static std::mutex m;
    return m;
}
std::map<std::pair<BiomeRegistry const*, std::string>, Biome const*>& biomeCache() {
    static std::map<std::pair<BiomeRegistry const*, std::string>, Biome const*> c;
    return c;
}
/// Engine biome -> id (region.biome() is asked for every placement position).
std::shared_mutex& biomeNameMutex() {
    static std::shared_mutex m;
    return m;
}
std::unordered_map<Biome const*, std::string>& biomeNames() {
    static std::unordered_map<Biome const*, std::string> n;
    return n;
}
} // namespace

void clearWorldCaches() {
    {
        std::lock_guard lock(biomeCacheMutex());
        biomeCache().clear(); // a later world's registry may reuse the address
    }
    {
        std::unique_lock lock(biomeNameMutex());
        biomeNames().clear();
    }
    clearCarverBiomeCache();
    clearCarvingMasks();
    clearKeptChunkBiomes();
    features::detail::clearBlockCache();
}

Biome const* engineBiome(BiomeRegistry const& registry, std::string const& id) {
    std::lock_guard lock(biomeCacheMutex());
    auto&           cache = biomeCache();
    auto            key   = std::make_pair(&registry, id);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::string  name = id.starts_with("minecraft:") ? id.substr(10) : id;
    Biome const* b    = registry.lookupByName(name);
    if (!b) b = registry.lookupByName(id);
    if (!b) features::detail::warnOnce("biome " + id, "unknown biome " + id + " (the chunk keeps its default biome)");
    cache[key] = b;
    return b;
}

std::string biomeIdOf(Biome const& biome) {
    {
        std::shared_lock lock(biomeNameMutex());
        if (auto it = biomeNames().find(&biome); it != biomeNames().end()) return it->second;
    }
    auto const& name = biome.mHash->getString();
    std::string id   = name.find(':') == std::string::npos ? "minecraft:" + name : name;
    std::unique_lock lock(biomeNameMutex());
    return biomeNames().try_emplace(&biome, std::move(id)).first->second;
}

bool heightmapMatches(Heightmap type, Block const& block) {
    switch (type) {
    case Heightmap::WorldSurfaceWg:
    case Heightmap::WorldSurface:
        return !block.isAir();
    case Heightmap::OceanFloorWg:
    case Heightmap::OceanFloor:
        return features::kit::blocksMotion(block);
    case Heightmap::MotionBlocking:
        return features::kit::blocksMotion(block) || features::kit::isFluid(block);
    case Heightmap::MotionBlockingNoLeaves:
        return (features::kit::blocksMotion(block) || features::kit::isFluid(block)) && !features::kit::isLeaves(block);
    }
    return false;
}

} // namespace detail

using detail::update;

void setBiomeSource(BiomeSourceFn fn) {
    update([&](detail::Providers& p) {
        p.biomeSource = std::move(fn);
        ++p.biomeSourceGeneration;
    });
}
void setMainBiomeSource(MainBiomeFn fn) {
    update([&](detail::Providers& p) { p.mainBiome = std::move(fn); });
}

std::string biomeAt(BlockPos const& pos) {
    // Level#getBiome over generated chunks: BiomeManager's zoom over the biome source's quarts, the
    // quart y clamped to the dimension's range as ChunkAccess#getNoiseBiome does
    auto p = detail::providers();
    if (!p->biomeSource) return {};
    int lo = core::mth::fromBlock(detail::twilightMinY()), hi = core::mth::fromBlock(detail::twilightMaxY() - 1);
    return detail::zoomedBiome(detail::currentSeed(), pos, [&](int qx, int qy, int qz) { return p->biomeSource(qx, std::clamp(qy, lo, hi), qz); });
}

std::string mainBiomeAt(int blockX, int blockZ) {
    auto p = detail::providers();
    if (p->mainBiome) return p->mainBiome(blockX, blockZ);
    if (p->biomeSource) return p->biomeSource(core::mth::fromBlock(blockX), 0, core::mth::fromBlock(blockZ));
    return {};
}

void setTerrain(TerrainProvider fn) {
    update([&](detail::Providers& p) { p.terrain = std::move(fn); });
}
void setSurface(SurfaceProvider fn) {
    update([&](detail::Providers& p) { p.surface = std::move(fn); });
}
void registerCarver(std::string const& id, CarverFn fn) {
    update([&](detail::Providers& p) { p.carvers[features::detail::normalizeId(id)] = std::move(fn); });
}
void registerBlanket(std::string const& id, BlanketFn fn) {
    update([&](detail::Providers& p) { p.blankets[features::detail::normalizeId(id)] = std::move(fn); });
}

namespace detail {
namespace {
void addStep(std::vector<Step>& steps, std::string const& id, int order, ChunkFn fn) {
    std::erase_if(steps, [&](Step const& s) { return s.id == id; });
    steps.push_back({id, order, stepSeq()++, std::move(fn)});
    std::stable_sort(steps.begin(), steps.end(), [](Step const& a, Step const& b) { return a.order != b.order ? a.order < b.order : a.seq < b.seq; });
}
} // namespace
} // namespace detail

void addStage(Stage stage, std::string const& id, int order, ChunkFn fn) {
    update([&](detail::Providers& p) { detail::addStep(p.stages[stage], id, order, std::move(fn)); });
}

std::int64_t seed() { return detail::currentSeed(); }

// ==== extension ======================================================================================

namespace ext {

void addStructureStep(std::string const& id, int order, ChunkFn fn) {
    update([&](detail::Providers& p) { detail::addStep(p.structureSteps, id, order, std::move(fn)); });
}

void setPossibleBiomes(std::vector<std::string> biomeIds) {
    for (auto& id : biomeIds) id = features::detail::normalizeId(id);
    update([&](detail::Providers& p) { p.possibleBiomes = std::move(biomeIds); });
}

std::vector<std::string> possibleBiomes() {
    auto p = detail::providers();
    if (!p->possibleBiomes.empty()) return p->possibleBiomes;
    std::vector<std::string> out;
    for (auto const& [id, _] : features::detail::data().biomes) out.push_back(id);
    return out;
}

std::vector<std::vector<std::string>> biomeFeatures(std::string const& biomeId) {
    auto const& steps = features::detail::data().biomeSteps;
    auto        it    = steps.find(features::detail::normalizeId(biomeId));
    return it == steps.end() ? std::vector<std::vector<std::string>>{} : it->second;
}

std::vector<std::string> biomeCarvers(std::string const& biomeId) {
    std::vector<std::string> out;
    auto const& biomes = features::detail::data().biomes;
    auto        it     = biomes.find(features::detail::normalizeId(biomeId));
    if (it == biomes.end() || !it->second.contains("carvers")) return out;
    auto const& c   = it->second["carvers"];
    auto        add = [&](nlohmann::json const& v) {
        if (v.is_string()) out.push_back(features::detail::normalizeId(v.get<std::string>()));
        else if (v.is_array())
            for (auto const& e : v)
                if (e.is_string()) out.push_back(features::detail::normalizeId(e.get<std::string>()));
    };
    if (c.is_object()) {
        if (c.contains("air")) add(c["air"]);
    } else {
        add(c);
    }
    return out;
}

} // namespace ext

} // namespace gf::worldgen
