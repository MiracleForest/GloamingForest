// Contract "worldgen" (F5): the provider slots of the Twilight Forest generator (biome source,
// terrain, surface, carvers, blankets, stage steps), the world seed and the biome lookups.
#include "feature/worldgen/Worldgen.h"

#include <algorithm>
#include <mutex>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

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

Biome const* engineBiome(BiomeRegistry const& registry, std::string const& id) {
    static std::mutex                                                     m;
    static std::map<std::pair<BiomeRegistry const*, std::string>, Biome const*> cache;
    std::lock_guard                                                       lock(m);
    auto key = std::make_pair(&registry, id);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::string  name = id.starts_with("minecraft:") ? id.substr(10) : id;
    Biome const* b    = registry.lookupByName(name);
    if (!b) b = registry.lookupByName(id);
    if (!b) features::detail::warnOnce("biome " + id, "unknown biome " + id + " (the chunk keeps its default biome)");
    cache[key] = b;
    return b;
}

std::string biomeIdOf(Biome const& biome) {
    auto const& name = biome.mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
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
    update([&](detail::Providers& p) { p.biomeSource = std::move(fn); });
}
void setMainBiomeSource(MainBiomeFn fn) {
    update([&](detail::Providers& p) { p.mainBiome = std::move(fn); });
}

std::string biomeAt(BlockPos const& pos) {
    auto p = detail::providers();
    if (!p->biomeSource) return {};
    return p->biomeSource(core::mth::fromBlock(pos.x), core::mth::fromBlock(pos.y), core::mth::fromBlock(pos.z));
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

void addStage(Stage stage, std::string const& id, int order, ChunkFn fn) {
    update([&](detail::Providers& p) {
        auto& steps = p.stages[stage];
        std::erase_if(steps, [&](detail::Step const& s) { return s.id == id; });
        steps.push_back({id, order, detail::stepSeq()++, std::move(fn)});
        std::stable_sort(steps.begin(), steps.end(), [](detail::Step const& a, detail::Step const& b) {
            return a.order != b.order ? a.order < b.order : a.seq < b.seq;
        });
    });
}

std::int64_t seed() { return detail::currentSeed(); }

// ==== extension ======================================================================================

namespace ext {

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
    std::vector<std::vector<std::string>> out;
    auto const& biomes = features::detail::data().biomes;
    auto        it     = biomes.find(features::detail::normalizeId(biomeId));
    if (it == biomes.end()) return out;
    for (auto const& step : it->second.value("features", nlohmann::json::array())) {
        auto& list = out.emplace_back();
        for (auto const& f : step)
            if (f.is_string()) list.push_back(features::detail::normalizeId(f.get<std::string>()));
    }
    return out;
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
