// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/registration/surface_rules/TFSurfaceRules.java
//   src/main/java/twilightforest/world/components/chunkblanketing/CanopyBlanketProcessor.java
//   src/main/java/twilightforest/world/components/chunkblanketing/GlacierBlanketProcessor.java
//   src/main/java/twilightforest/world/components/chunkblanketing/ChunkBlanketProcessor.java
//   src/main/java/twilightforest/init/custom/ChunkBlanketProcessors.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U2 (terrain): the generator of the Twilight Forest without the game (pure C++, gf-core), so the
// gtests run exactly what the game runs:
//   - Grid: one chunk's blocks as codes (air, the default block, the default fluid, another
//     solid, another fluid, then named blocks of a BlockTable);
//   - SurfaceRules: Minecraft 1.21.1's SurfaceSystem#buildSurface / #topMaterial with the
//     SurfaceRules sources TF's noise settings use (sequence, condition, block; conditions
//     biome, stone_depth, water, y_above, noise_threshold, vertical_gradient, not, hole);
//   - Blankets: TF's chunk blanket processors (canopy, glacier) from their registry JSON;
//   - Generator: the noise settings (NoiseBasedChunkGenerator#doFill, #getBaseHeight) tying the
//     density graph, the surface and the blankets together.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/terrain/Density.h"

namespace gf::core::terrain {

// ---- blocks -------------------------------------------------------------------------------------------
namespace code {
inline constexpr std::uint16_t Air          = 0;
inline constexpr std::uint16_t DefaultBlock = 1; ///< the settings' default block (stone)
inline constexpr std::uint16_t Water        = 2; ///< water (the default fluid of twilight_noise_gen)
inline constexpr std::uint16_t OtherSolid   = 3; ///< any other non-air, non-fluid block
inline constexpr std::uint16_t Lava         = 4; ///< any other fluid
inline constexpr std::uint16_t FirstNamed   = 16;
} // namespace code

/// The named blocks rules and blankets place: code FirstNamed + index.
class BlockTable {
public:
    struct Entry {
        std::string    name;       ///< Java id ("minecraft:podzol")
        nlohmann::json properties; ///< Java properties ({} when none)
        bool           fluid = false;
    };
    /// The code of a Java block state JSON {"Name", "Properties"} (added when new).
    std::uint16_t                           codeOf(nlohmann::json const& state);
    std::uint16_t                           codeOf(std::string const& name, nlohmann::json const& properties = nlohmann::json::object());
    [[nodiscard]] std::vector<Entry> const& entries() const { return mEntries; }
    [[nodiscard]] Entry const*              entry(std::uint16_t code) const;

private:
    std::vector<Entry> mEntries;
};

[[nodiscard]] inline bool isAirCode(std::uint16_t c) { return c == code::Air; }
/// Water or lava (BlockState#liquid / a non-empty fluid state).
[[nodiscard]] bool isFluidCode(std::uint16_t c, BlockTable const& table);

/// A chunk's blocks, y from minY to minY + height - 1.
struct Grid {
    int                        minY = -32, height = 288;
    std::vector<std::uint16_t> cells; ///< ((y - minY) * 16 + z) * 16 + x
    Grid(int minY_, int height_) : minY(minY_), height(height_), cells(static_cast<std::size_t>(height_) * 256, code::Air) {}
    [[nodiscard]] std::uint16_t get(int x, int y, int z) const {
        if (y < minY || y >= minY + height) return code::Air;
        return cells[static_cast<std::size_t>(((y - minY) * 16 + z) * 16 + x)];
    }
    void set(int x, int y, int z, std::uint16_t c) {
        if (y < minY || y >= minY + height) return;
        cells[static_cast<std::size_t>(((y - minY) * 16 + z) * 16 + x)] = c;
    }
    /// ChunkAccess#getHeight(WORLD_SURFACE_WG): the highest non-air y (minY - 1 when empty).
    [[nodiscard]] int worldSurfaceWg(int x, int z) const;
    /// ChunkAccess#getHeight(OCEAN_FLOOR_WG): the highest y blocking motion (not air, not a fluid).
    [[nodiscard]] int oceanFloorWg(int x, int z, BlockTable const& table) const;
};

/// The biome at a block (BiomeManager#getBiome, the zoom over the noise biomes).
using BlockBiomeFn = std::function<std::string const&(int x, int y, int z)>;

// ---- surface rules -------------------------------------------------------------------------------------
/// WorldGenerationContext: the generation range the vertical anchors resolve in.
struct GenContext {
    int minY   = -32;
    int height = 256;
};

class SurfaceRules {
public:
    /// Compiles a surface rule JSON for `seed`; its blocks are added to `table`. nullptr with
    /// `error` set for an unsupported rule or condition.
    [[nodiscard]] static std::shared_ptr<SurfaceRules const>
    compile(nlohmann::json const& rule, std::int64_t seed, GenContext gen, BlockTable& table, std::string* error = nullptr);

    /// SurfaceSystem#buildSurface over chunk (chunkX, chunkZ): replaces the default block where the
    /// rule answers a block.
    void buildSurface(int chunkX, int chunkZ, Grid& grid, BlockTable const& table, BlockBiomeFn const& biome) const;
    /// SurfaceSystem#topMaterial (CarvingContext#topMaterial): the rule's block at a position with
    /// stone depths 1 / 1 and the water height above it when `hasFluid` (nullopt: no block).
    [[nodiscard]] std::optional<std::uint16_t> topMaterial(int x, int y, int z, bool hasFluid, BlockBiomeFn const& biome) const;
    /// SurfaceSystem#getSurfaceDepth.
    [[nodiscard]] int surfaceDepth(int x, int z) const;

    struct Impl;
    explicit SurfaceRules(std::shared_ptr<Impl const> impl) : mImpl(std::move(impl)) {}

private:
    std::shared_ptr<Impl const> mImpl;
};

// ---- chunk blankets ------------------------------------------------------------------------------------
class Blankets {
public:
    struct Processor {
        std::string              id;   ///< registry entry ("twilightforest:dark_forest_canopy")
        std::string              type; ///< "twilightforest:canopy" / "twilightforest:glacier"
        std::vector<std::string> biomes;
        std::vector<std::string> avoidStructures; ///< canopy
        std::uint16_t            block = code::Air, top = code::Air; ///< canopy block / glacier body, glacier top
        int                      height = 0;
    };
    /// Adds a processor from its registry JSON (false: unknown type or malformed).
    bool add(std::string const& id, nlohmann::json const& json, BlockTable& table);
    [[nodiscard]] std::vector<Processor> const& processors() const { return mProcessors; }

    /// What the canopy needs about the structures referencing the chunk: whether a start of one of
    /// `avoid` references it (ChunkAccess#getAllReferences).
    using ReferencesFn = std::function<bool(std::vector<std::string> const& avoid)>;
    /// ChunkBlanketProcessors#chunkBlanketing: every processor (in registry order) whose biome mask
    /// meets `chunkBiomes`; `fork` is called once per such processor (WorldGenRegion#getRandom().fork(),
    /// unused by the two types).
    void apply(int chunkX, int chunkZ, Grid& grid, BlockTable const& table, std::vector<std::string> const& chunkBiomes, BlockBiomeFn const& biome,
               ReferencesFn const& references, std::function<void()> const& fork = {}) const;

private:
    std::vector<Processor> mProcessors;
};

// ---- the generator ------------------------------------------------------------------------------------
class Generator {
public:
    /// From a noise_settings JSON; `densities` resolves the router's references. nullptr with
    /// `error` set when the router or the surface rule does not compile.
    [[nodiscard]] static std::shared_ptr<Generator const>
    create(nlohmann::json const& settings, std::int64_t seed, Density::Resolver const& densities, BlockTable& table, std::string* error = nullptr);

    /// NoiseBasedChunkGenerator#doFill: the default block where the density (plus `extra`) is
    /// above 0, else the default fluid below sea level (lava below min(-54, sea level)), else air.
    void fillNoise(int chunkX, int chunkZ, TerrainSampler const& sampler, ExtraDensity const& extra, Grid& grid) const;
    /// NoiseBasedChunkGenerator#getBaseHeight: the y above the highest block of column (x, z)
    /// matching the heightmap (`oceanFloor`: blocks motion, else not air); minY when none.
    [[nodiscard]] int baseHeight(int x, int z, bool oceanFloor, TerrainSampler const& sampler) const;
    /// The block code a density gives at y (getInterpolatedState + the default block).
    [[nodiscard]] std::uint16_t stateAt(double density, int y) const;

    [[nodiscard]] Density const&      density() const { return *mDensity; }
    [[nodiscard]] SurfaceRules const& surface() const { return *mSurface; }
    [[nodiscard]] noise::NoiseShape   shape() const { return mDensity->shape(); }
    [[nodiscard]] int                 seaLevel() const { return mSeaLevel; }
    [[nodiscard]] std::string const&  defaultBlock() const { return mDefaultBlock; }
    /// The default fluid's code (code::Water, or code::Air for air as in skylight_noise_gen).
    [[nodiscard]] std::uint16_t       defaultFluid() const { return mDefaultFluid; }

    Generator() = default;

private:
    std::shared_ptr<Density const>      mDensity;
    std::shared_ptr<SurfaceRules const> mSurface;
    int                                 mSeaLevel = 0;
    std::string                         mDefaultBlock = "minecraft:stone";
    std::uint16_t                       mDefaultFluid = code::Water;
};

} // namespace gf::core::terrain
