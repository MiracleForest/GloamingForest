// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/layer/BiomeDensitySource.java
//   src/main/java/twilightforest/world/components/biomesources/TFBiomeProvider.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TerrainColumn.java
//   src/main/java/twilightforest/init/custom/BiomeLayerStack.java
//   src/main/java/twilightforest/init/custom/BiomeLayerTypes.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U1 `biomes`, pure part (gf-core, gtested against the F13 oracle goldens in tests/biomes/):
// TF's biome layer stack (registry `twilightforest:biome_layer_stack`, built from its JSON exactly
// as TF's codecs read it), the terrain grid (registry `twilightforest:biome_terrain_data`:
// TerrainColumn per key biome) and the biome source on top of them (BiomeDensitySource /
// TFBiomeProvider): the main (key) biome of a quart column, the noise biome of a quart (the
// column's vertical layers), the terrain blend (sampleTerrain) and the possible biomes in Java's
// order (FeatureSorter's feature indices depend on it).
//
//     auto stacks = BiomeLayerStacks::fromJson({{"twilightforest:random_forest_biomes", j1}, ...});
//     BiomeProvider provider(gridJson, stacks, worldSeed);
//     provider.mainBiome(qx, qz);          // "twilightforest:dark_forest"
//     provider.noiseBiome(qx, qy, qz);     // "twilightforest:underground" below the column
//
// Thread-safe: every layer keeps a lock-free cache of its samples (TF's LazyArea cache, which does
// not change results); providers are evaluated on the engine's generation worker threads.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::core::biomes {

using Json = nlohmann::json;

/// An interned biome id (index into BiomeNames).
using BiomeKey = std::uint16_t;

/// The ids the layers produce, interned (Java compares ResourceKeys by identity).
class BiomeNames {
public:
    [[nodiscard]] BiomeKey           intern(std::string const& id);
    [[nodiscard]] std::string const& name(BiomeKey key) const { return mNames.at(key); }
    [[nodiscard]] std::optional<BiomeKey> find(std::string const& id) const;
    [[nodiscard]] std::size_t        size() const { return mNames.size(); }

private:
    std::vector<std::string>        mNames;
    std::map<std::string, BiomeKey> mKeys;
};

/// One layer of the stack (Java's LazyArea over an AreaTransformer).
class Layer {
public:
    virtual ~Layer() = default;
    /// The biome of pixel (x, z) of this layer.
    [[nodiscard]] virtual BiomeKey get(int x, int z) const = 0;
};

/// The registry `twilightforest:biome_layer_stack` (id -> layer JSON).
struct LayerStackRegistry {
    std::map<std::string, Json> entries;
};

/// LazyAreaContext#mixSeed(seed, salt).
[[nodiscard]] std::int64_t mixSeed(std::int64_t seed, std::int64_t salt);

/// Builds the layer `layer` (a JSON object with "layer_type", or a string naming a registry
/// entry) for world seed `seed` (WorldUtil.getOverworldSeed()). Throws std::runtime_error on
/// unknown types or ids.
[[nodiscard]] std::unique_ptr<Layer>
buildLayer(Json const& layer, LayerStackRegistry const& registry, BiomeNames& names, std::int64_t seed);

/// TerrainColumn: a key biome, its vertical biome layers (quart elevation -> biome) and its
/// terrain values (TF ships constant density functions).
struct TerrainColumn {
    std::string                                key;
    std::vector<std::pair<double, std::string>> layers; ///< ascending elevation
    double                                     depth  = 0;
    double                                     scale  = 0;
    double                                     weight = 0;

    /// TerrainColumn#getBiome(quartY): the layer nearest to quartY (ties: the lower one); the key
    /// biome when there are no layers.
    [[nodiscard]] std::string const& biomeAt(int quartY) const;
};

/// BiomeDensitySource.DensityData.
struct DensityData {
    double depth = 0;
    double scale = 0;
};

/// BiomeDensitySource + TFBiomeProvider for one world seed.
class BiomeProvider {
public:
    /// `terrainData`: the biome_terrain_data entry ({"biome_landscape": [...], "biome_layer_config": id}).
    BiomeProvider(Json const& terrainData, LayerStackRegistry const& stacks, std::int64_t seed);

    [[nodiscard]] std::int64_t seed() const { return mSeed; }
    /// The layer stack's biome at quart column (qx, qz) (the key of the terrain column).
    [[nodiscard]] std::string const& keyBiome(int quartX, int quartZ) const;
    /// TFBiomeProvider#getMainBiome(qx, qz): the column's main biome.
    [[nodiscard]] std::string const& mainBiome(int quartX, int quartZ) const;
    /// TFBiomeProvider#getNoiseBiome(qx, qy, qz).
    [[nodiscard]] std::string const& noiseBiome(int quartX, int quartY, int quartZ) const;
    /// BiomeDensitySource#getTerrainColumn(qx, qz) (nullptr: the stack's biome has no column).
    [[nodiscard]] TerrainColumn const* terrainColumn(int quartX, int quartZ) const;
    [[nodiscard]] TerrainColumn const* terrainColumn(std::string const& keyBiome) const;
    /// BiomeDensitySource#sampleTerrain(blockX, blockZ, ctx) (the columns' values are constants).
    [[nodiscard]] DensityData sampleTerrain(int blockX, int blockZ) const;
    /// Every column (sorted by key).
    [[nodiscard]] std::vector<TerrainColumn> const& columns() const { return mColumns; }

private:
    std::int64_t                                    mSeed;
    BiomeNames                                      mNames;
    std::unique_ptr<Layer>                          mStack;
    std::vector<TerrainColumn>                      mColumns;      ///< sorted by ResourceLocation
    std::vector<TerrainColumn const*>               mColumnOfKey;  ///< by BiomeKey (nullptr: none)
    std::map<std::string, TerrainColumn const*>     mColumnByName;
};

/// The terrain grid's columns (`terrainData`: a biome_terrain_data entry), sorted by key as
/// BiomeProvider keeps them; no layer is built.
[[nodiscard]] std::vector<TerrainColumn> terrainColumnsOf(Json const& terrainData);
/// BiomeProvider#possibleBiomes of `terrainData` without building the layer stack (seed independent).
[[nodiscard]] std::vector<std::string> possibleBiomesOf(Json const& terrainData);

/// ResourceLocation#compareTo of two ids ("ns:path"): path first, then namespace.
[[nodiscard]] bool resourceLocationLess(std::string const& a, std::string const& b);

} // namespace gf::core::biomes
