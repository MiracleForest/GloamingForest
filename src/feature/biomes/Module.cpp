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

std::atomic<std::shared_ptr<BiomeProvider const>>& current() {
    static std::atomic<std::shared_ptr<BiomeProvider const>> p;
    return p;
}

} // namespace

std::shared_ptr<SourceData const> sourceData() {
    static std::shared_ptr<SourceData const> data;
    static bool                              loaded = false;
    std::lock_guard                          lock(dataMutex());
    if (!loaded) {
        data   = loadSourceData();
        loaded = true;
    }
    return data;
}

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
    return fresh;
}

std::shared_ptr<BiomeProvider const> provider() { return providerFor(worldgen::seed()); }

struct BiomesModule {
    void install() {
        auto data = sourceData();
        if (!data) return;
        // FeatureSorter's order: BiomeSource#possibleBiomes (seed independent)
        worldgen::ext::setPossibleBiomes(BiomeProvider(data->grid, data->stacks, 0).possibleBiomes());
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
    void uninstall() {
        worldgen::setBiomeSource({});
        worldgen::setMainBiomeSource({});
        current().store(nullptr);
    }
};

} // namespace gf::feature::biomes

GF_REGISTER_CONFIG("biomes", gf::feature::biomes::BiomesConfig);
GF_REGISTER_MODULE(gf::feature::biomes::BiomesModule);
