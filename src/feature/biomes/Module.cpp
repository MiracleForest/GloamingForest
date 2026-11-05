// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/biomesources/TFBiomeProvider.java
//   src/main/java/twilightforest/init/TFBiomes.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U1 `biomes`: installs TF's biome source (TFBiomeProvider over BiomeDensitySource) into F5's
// worldgen pipeline, the biome order FeatureSorter uses, and the biomes' spawn tables.
//
// Bedrock: the 22 biomes are behavior pack biomes (assets/pack/bp/biomes/biomes/, climate and map
// tints) with client biome files (assets/pack/rp/biomes/biomes/: sky, grass, foliage, water, fog
// ids of assets/pack/rp/fogs/biomes/); F5's generator writes the zoomed biome of every block, so
// vanilla clients draw them too. TF's position-dependent colours are drawn by the client mod
// (src/client/biomes/Tints.cpp).

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "core/biomes/BiomeLayers.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/worldgen/Worldgen.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::feature::biomes {

namespace fs = std::filesystem;
using core::biomes::BiomeProvider;
using core::biomes::Json;

std::vector<std::string> const& biomeIds() {
    static std::vector<std::string> const ids = {
        "twilightforest:clearing",
        "twilightforest:dark_forest",
        "twilightforest:dark_forest_center",
        "twilightforest:dense_forest",
        "twilightforest:dense_mushroom_forest",
        "twilightforest:enchanted_forest",
        "twilightforest:final_plateau",
        "twilightforest:fire_swamp",
        "twilightforest:firefly_forest",
        "twilightforest:forest",
        "twilightforest:glacier",
        "twilightforest:highlands",
        "twilightforest:highlands_underground",
        "twilightforest:lake",
        "twilightforest:mushroom_forest",
        "twilightforest:oak_savannah",
        "twilightforest:snowy_forest",
        "twilightforest:spooky_forest",
        "twilightforest:stream",
        "twilightforest:swamp",
        "twilightforest:thornlands",
        "twilightforest:underground",
    };
    return ids;
}

namespace {

std::mutex& dataMutex() {
    static std::mutex m;
    return m;
}

Json readJson(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + p.generic_string());
    return Json::parse(in);
}

/// <mod>/data/twilightforest/twilight/{biome_layer_stack/*.json, biome_terrain_data/biome_grid.json}
std::shared_ptr<SourceData const> loadSourceData() {
    auto data = std::make_shared<SourceData>();
    fs::path root;
    try {
        root = Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return nullptr;
    }
    std::error_code ec;
    try {
        for (auto const& ns : fs::directory_iterator(root, ec)) {
            if (!ns.is_directory()) continue;
            auto dir = ns.path() / "twilight" / "biome_layer_stack";
            if (!fs::is_directory(dir, ec)) continue;
            for (auto const& f : fs::recursive_directory_iterator(dir, ec)) {
                if (!f.is_regular_file() || f.path().extension() != ".json") continue;
                auto rel = fs::relative(f.path(), dir, ec).generic_string();
                data->stacks.entries[ns.path().filename().string() + ":" + rel.substr(0, rel.size() - 5)] = readJson(f.path());
            }
        }
        data->grid = readJson(root / "twilightforest" / "twilight" / "biome_terrain_data" / "biome_grid.json");
        // a provider must build (a broken stack throws here, once, instead of on a worker thread)
        BiomeProvider probe(data->grid, data->stacks, 0);
        (void)probe.mainBiome(0, 0);
    } catch (std::exception const& e) {
        gf::log().error("biomes: the Twilight Forest biome source is unavailable: {}", e.what());
        return nullptr;
    }
    return data;
}

std::shared_ptr<SourceData const> gSourceData;   ///< guarded by dataMutex()
bool                              gSourceFailed = false;
std::atomic<bool>                 gSourceFailedFast{false}; ///< gSourceFailed, read without the lock
std::atomic<std::uint64_t>        gWorldGeneration{0};

std::atomic<std::shared_ptr<BiomeProvider const>>& current() {
    static std::atomic<std::shared_ptr<BiomeProvider const>> p;
    return p;
}

std::atomic<std::uint64_t>& generation() {
    static std::atomic<std::uint64_t> g{0};
    return g;
}

} // namespace

std::shared_ptr<SourceData const> sourceData() {
    // a failed load is retried on the next call (the client loads many worlds in one process: one
    // early failure must not disable the biomes of every later world); the error is logged per try
    // (generation workers ask per quart: a failure is not retried before the next world)
    if (gSourceFailedFast.load(std::memory_order_acquire)) return nullptr;
    std::lock_guard lock(dataMutex());
    if (!gSourceData && !gSourceFailed) {
        gSourceData   = loadSourceData();
        gSourceFailed = gSourceData == nullptr;
        gSourceFailedFast.store(gSourceFailed, std::memory_order_release);
    }
    return gSourceData;
}

void retrySourceData() {
    std::lock_guard lock(dataMutex());
    gSourceFailed = false;
    gSourceFailedFast.store(false, std::memory_order_release);
}

std::uint64_t worldGeneration() { return gWorldGeneration.load(std::memory_order_acquire); }

std::shared_ptr<BiomeProvider const> providerFor(std::int64_t seed) {
    auto p = current().load(std::memory_order_acquire);
    if (p && p->seed() == seed) return p;
    auto data = sourceData();
    if (!data) return nullptr;
    std::shared_ptr<BiomeProvider const> fresh;
    try {
        fresh = std::make_shared<BiomeProvider const>(data->grid, data->stacks, seed);
    } catch (std::exception const& e) {
        gf::log().error("biomes: cannot build the biome source for seed {}: {}", seed, e.what());
        return nullptr;
    }
    // concurrent first calls may each build one; any of them is equal
    current().store(fresh, std::memory_order_release);
    generation().fetch_add(1, std::memory_order_acq_rel);
    return fresh;
}

std::shared_ptr<BiomeProvider const> provider() {
    // per thread: the generation workers ask for every quart; the shared atomic is read only when
    // the provider changed (another seed, uninstall)
    thread_local std::shared_ptr<BiomeProvider const> cached;
    thread_local std::uint64_t                        cachedGeneration = ~std::uint64_t(0);
    auto                                              seed             = worldgen::seed();
    auto                                              gen              = generation().load(std::memory_order_acquire);
    if (cached && cachedGeneration == gen && cached->seed() == seed) return cached;
    cached           = providerFor(seed);
    cachedGeneration = generation().load(std::memory_order_acquire);
    return cached;
}

struct BiomesModule {
    void install() {
        mInstalled = true;
        retrySourceData();
        tryInstall();
        // a world loaded later retries a failed load (the client loads many worlds in one process)
        static bool hooked = false;
        if (!hooked) {
            hooked = true;
            game::db::onWorldClosed([] { gWorldGeneration.fetch_add(1, std::memory_order_acq_rel); });
            game::db::onWorldReady([this] {
                gWorldGeneration.fetch_add(1, std::memory_order_acq_rel);
                if (!mInstalled || mSourceInstalled) return;
                retrySourceData();
                tryInstall();
            });
        }
    }
    void uninstall() {
        mInstalled = mSourceInstalled = false;
        worldgen::setBiomeSource({});
        worldgen::setMainBiomeSource({});
        current().store(nullptr);
        generation().fetch_add(1, std::memory_order_acq_rel);
    }

private:
    void tryInstall() {
        auto data = sourceData();
        if (!data || mSourceInstalled) return;
        mSourceInstalled = true;
        // FeatureSorter's order: BiomeSource#possibleBiomes (seed independent, from the grid alone)
        worldgen::ext::setPossibleBiomes(core::biomes::possibleBiomesOf(data->grid));
        worldgen::setBiomeSource([](int qx, int qy, int qz) -> std::string {
            auto p = provider();
            return p ? p->noiseBiome(qx, qy, qz) : std::string{};
        });
        // TFBiomeProvider#getMainBiome takes quart coordinates; the contract passes blocks
        worldgen::setMainBiomeSource([](int blockX, int blockZ) -> std::string {
            auto p = provider();
            return p ? p->mainBiome(blockX >> 2, blockZ >> 2) : std::string{};
        });
        registerSpawns();
        gf::log().info("biomes: Twilight Forest biome source installed ({} layer stacks, {} biomes)", data->stacks.entries.size(), biomeIds().size());
    }

    bool mInstalled       = false;
    bool mSourceInstalled = false;
};

} // namespace gf::feature::biomes

GF_REGISTER_CONFIG("biomes", gf::feature::biomes::BiomesConfig);
GF_REGISTER_MODULE(gf::feature::biomes::BiomesModule);
