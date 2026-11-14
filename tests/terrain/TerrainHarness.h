// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: the imported upstream data (noise settings, density functions, the biome grid,
// the blanket processors) and the F13 goldens, wired into core/terrain's generator the way the
// game wires them (src/feature/terrain/Terrain.cpp).
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include "../golden/GoldenRepo.h"
#include "core/jrand/BiomeManager.h"
#include "core/terrain/Density.h"
#include "core/terrain/Functions.h"
#include "core/terrain/Surface.h"

namespace gf::tests::terrain {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace gf::core::terrain;

inline fs::path repo() { return golden::goldenDir().parent_path().parent_path(); }
inline fs::path imported(std::string const& unit) { return repo() / "assets/gen/import" / unit / "data/gloamingforest"; }

/// Resolves gloamingforest:<path> density functions from the imported data.
inline Density::Resolver densityResolver() {
    Density::Resolver r;
    r.density = [](std::string const& id) -> std::optional<json> {
        auto colon = id.find(':');
        if (id.compare(0, colon, "gloamingforest") != 0) return std::nullopt;
        auto file = imported("terrain") / "worldgen/density_function" / (id.substr(colon + 1) + ".json");
        if (!fs::exists(file)) return std::nullopt;
        return golden::readJson(file);
    };
    return r;
}

inline TerrainGrid const& terrainGrid() {
    static TerrainGrid const g = TerrainGrid::fromJson(golden::readJson(imported("biomes") / "gloaming/biome_terrain_data/biome_grid.json"));
    return g;
}

/// The biomes golden of a seed as a main-biome map over its quart grid.
struct BiomeMap {
    int                      x0 = 0, z0 = 0, width = 0, height = 0;
    std::vector<std::string> palette;
    std::vector<std::string> rows; ///< rows[z] chars per x (or transposed when `transposed`)
    bool                     transposed = false;
    std::string              chars;

    [[nodiscard]] bool covers(int qx, int qz) const { return qx >= x0 && qz >= z0 && qx < x0 + width && qz < z0 + height; }
    [[nodiscard]] std::string const* main(int qx, int qz) const {
        if (!covers(qx, qz)) return nullptr;
        int  i = qx - x0, j = qz - z0;
        char c = transposed ? rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] : rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
        auto p = chars.find(c);
        return p == std::string::npos ? nullptr : &palette[p];
    }
    /// The terrain column of a quart (nullptr outside the grid).
    [[nodiscard]] TerrainColumn const* column(int qx, int qz) const {
        auto const* m = main(qx, qz);
        return m ? terrainGrid().column(*m) : nullptr;
    }
    /// sampleTerrain's whole neighbourhood is inside the grid.
    [[nodiscard]] bool samplable(int bx, int bz) const {
        return covers(((bx - 2) - 9) >> 2, ((bz - 2) - 9) >> 2) && covers(((bx - 2) + 9) >> 2, ((bz - 2) + 9) >> 2);
    }
};

inline BiomeMap biomeMap(json const& g, bool transposed) {
    BiomeMap m;
    auto const& main = g.at("main");
    m.x0 = main.at("x0"), m.z0 = main.at("z0"), m.width = main.at("width"), m.height = main.at("height");
    m.palette    = g.at("palette").get<std::vector<std::string>>();
    m.chars      = g.at("paletteChars").get<std::string>();
    m.rows       = main.at("rows").get<std::vector<std::string>>();
    m.transposed = transposed;
    return m;
}

inline std::string seedFile(std::int64_t seed) {
    if (seed < 0) return "seed-m" + std::to_string(-(seed + 1) + 1ULL) + ".json";
    return "seed-" + std::to_string(seed) + ".json";
}

/// Everything the generator of one seed needs, from the goldens.
struct World {
    std::int64_t                     seed = 0;
    BiomeMap                         biomes;
    BlockTable                       table;
    std::shared_ptr<Generator const> gen;
    Blankets                         blankets;
    std::int64_t                     zoomSeed = 0;
    std::string                      error;

    [[nodiscard]] TerrainSampler sampler() const {
        return [this](int x, int z) { return sampleTerrain(x, z, [this](int qx, int qz) { return biomes.column(qx, qz); }); };
    }
    /// GloamingBiomeProvider#getNoiseBiome (nullptr outside the grid).
    [[nodiscard]] std::string const* noiseBiome(int qx, int qy, int qz) const {
        auto const* c = biomes.column(qx, qz);
        return c ? &c->biomeAt(qy) : nullptr;
    }
    [[nodiscard]] BlockBiomeFn blockBiome() const {
        return [this](int x, int y, int z) -> std::string const& {
            static std::string const none;
            auto q = core::biome::zoom(zoomSeed, x, y, z);
            auto b = noiseBiome(q.x, q.y, q.z);
            return b ? *b : none;
        };
    }
};

inline std::unique_ptr<World> loadWorld(std::int64_t seed, std::string const& settingsName = "gloaming_noise_gen", bool transposed = false) {
    auto w      = std::make_unique<World>();
    w->seed     = seed;
    w->zoomSeed = core::biome::obfuscateSeed(seed);
    json bg     = golden::readJson(golden::goldenDir() / "biomes" / seedFile(seed));
    if (bg.is_discarded()) {
        w->error = "biomes golden missing";
        return w;
    }
    w->biomes     = biomeMap(bg, transposed);
    json settings = golden::readJson(imported("terrain") / "worldgen/noise_settings" / (settingsName + ".json"));
    w->gen        = Generator::create(settings, seed, densityResolver(), w->table, &w->error);
    for (char const* id : {"dark_forest_canopy", "snowy_forest_glacier"})
        w->blankets.add(std::string("gloamingforest:") + id, golden::readJson(imported("terrain") / "gloaming/chunk_blanket_processors" / (std::string(id) + ".json")), w->table);
    return w;
}

inline std::vector<std::int64_t> seeds() { return {0, 123456789, -8346138912345678901LL}; }

} // namespace gf::tests::terrain
