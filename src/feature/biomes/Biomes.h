#pragma once

// U1 `biomes`, the unit's internal interface (not a contract: other units use the worldgen
// contract's biomeAt / mainBiomeAt, or feature/ext/biomes/Biomes.h).
//
// The biome source of the Twilight Forest: TF's layer stack and terrain grid (registries
// biome_layer_stack and biome_terrain_data, read from the mod's data folder, which F1 imports
// from TF's data pack) evaluated for the world seed. Installed into F5's worldgen pipeline with
// worldgen::setBiomeSource / setMainBiomeSource and ext::setPossibleBiomes.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/biomes/BiomeLayers.h"
#include "feature/spawn/Spawn.h"

namespace gf::feature::biomes {

inline constexpr char kUnit[] = "biomes";

/// config/biomes.json (rule R13). TF has no biome config key (docs/spec/config.md): these are
/// GloamingForest's own switches.
struct BiomesConfig {
    int version = 1;
    /// Client mod: draw TF's position-dependent grass and foliage colours (spooky forest, dark
    /// forest centre, enchanted forest) on vanilla grass, plants and leaves.
    bool clientTints = true;
    /// Client mod: Java's biome blend radius for those colours (options.biomeBlendRadius, 2 = 5x5).
    int tintBlendRadius = 2;
};

/// The 22 biome ids of the Twilight Forest (TFBiomes), sorted.
[[nodiscard]] std::vector<std::string> const& biomeIds();

/// The layer stacks and the terrain grid read from the mod's data folder (nullptr: missing or
/// invalid; the reason was logged once).
struct SourceData {
    core::biomes::LayerStackRegistry stacks;
    core::biomes::Json               grid;
};
[[nodiscard]] std::shared_ptr<SourceData const> sourceData();

/// The biome provider for `seed` (cached for the last seed asked; thread-safe). nullptr without data.
[[nodiscard]] std::shared_ptr<core::biomes::BiomeProvider const> providerFor(std::int64_t seed);
/// The provider of the running world (worldgen::seed()).
[[nodiscard]] std::shared_ptr<core::biomes::BiomeProvider const> provider();

/// BiomeHelper's MobSpawnSettings per biome (BiomeMaker), as registered with spawn::setBiomeSpawns.
struct SpawnCost {
    std::string entityId;
    double      energyBudget = 0;
    double      charge       = 0;
};
struct BiomeSpawns {
    spawn::Table           table;
    std::vector<SpawnCost> costs; ///< MobSpawnSettings#mobSpawnCost (not used by F5's spawner yet)
};
[[nodiscard]] BiomeSpawns spawnsOf(std::string const& biomeId);
/// Registers every biome's table (install).
void registerSpawns();

} // namespace gf::feature::biomes
