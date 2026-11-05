// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U2 (terrain), internal: the per-world generator state shared by the terrain, surface and blanket
// providers (Terrain.cpp), the carvers (Carvers.cpp) and the selftests (SelfTests.cpp). Not a
// contract; other units reach terrain only through the worldgen / landmark contracts.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/terrain/Beard.h"
#include "core/terrain/Density.h"
#include "core/terrain/Functions.h"
#include "core/terrain/Surface.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/worldgen/Worldgen.h"

class Block;

namespace gf::feature::terrain {

using namespace gf::core::terrain;
using core::BlockBox;

/// Config section "terrain" (rule R13; docs/spec/config.md).
struct TerrainConfig {
    int  version = 1;
    /// Generate new worlds with TF's "Skylight" preset (twilightforest:skylight_noise_gen: floating
    /// islands over the void). Read when a world is first generated, then kept in the world
    /// (gf:terrain:skylight).
    bool skylight = false;
    /// Per-chunk generation budget in milliseconds (noise + surface + blanket + carvers) the
    /// terrain.timing selftest checks the p95 against.
    int chunkBudgetMs = 50;
    /// terrain.showcase (client e2e screenshots): ticks each generated sample stays in the world
    /// before the next one (0: none).
    int showcaseHoldTicks = 0;
};

/// The data TF's terrain reads (from <mod>/data, loaded once).
struct Data {
    nlohmann::json                         twilight, skylight; ///< noise settings
    std::map<std::string, nlohmann::json>  densities;          ///< twilightforest:<path> -> density function
    nlohmann::json                         biomeGrid;          ///< biome_terrain_data/twilightforest:biome_grid
    std::map<std::string, nlohmann::json>  blankets;           ///< chunk_blanket_processors/<id>
};
[[nodiscard]] Data const& data();

/// Everything one world (seed + preset) generates with.
struct State {
    std::int64_t                     seed     = 0;
    bool                             skylight = false;
    std::int64_t                     zoomSeed = 0;
    TerrainGrid                      grid;
    BlockTable                       table;
    std::shared_ptr<Generator const> gen;
    Blankets                         blankets;
    std::vector<Block const*>        blocks; ///< code -> Block (codes from the table; nullptr: unknown)
    Block const*                     stone = nullptr; ///< the default block
    Block const* caveAir = nullptr; ///< the carvers' blocks (Block pointers belong to the world: kept per state)
    Block const* rootedDirt = nullptr;
    Block const* coarseDirt = nullptr;
    std::uint64_t generation = 0; ///< unique per state (per-thread caches key on it, not on the address)
    std::string                      error;
    std::unique_ptr<core::jrand::PositionalRandomFactory> oreRandom; ///< RandomState#oreRandom

    [[nodiscard]] Block const* block(std::uint16_t c) const { return c < blocks.size() ? blocks[c] : nullptr; }
    /// The terrain column of a quart (U1's main biome through worldgen::mainBiomeAt; the forest's
    /// column when no biome source answers, so the terrain stays sane without U1).
    [[nodiscard]] TerrainColumn const* columnAt(int quartX, int quartZ) const;
    /// TFBiomeProvider#getNoiseBiome from the main biome and the column's layers.
    [[nodiscard]] std::string const& noiseBiome(int quartX, int quartY, int quartZ) const;
    /// BiomeManager#getBiome: the zoom over the noise biomes.
    [[nodiscard]] std::string const& blockBiome(int x, int y, int z) const;
};

/// The state of the world being generated (built for the current seed and preset on first use).
/// nullptr while the data or the generator cannot be built (the error is logged once).
[[nodiscard]] std::shared_ptr<State const> state();
/// A state for any seed / preset (selftests; not cached).
[[nodiscard]] std::shared_ptr<State const> makeState(std::int64_t seed, bool skylight);
/// Whether the current world uses the skylight preset.
[[nodiscard]] bool skylightWorld();

/// A per-chunk cache of the quart columns around a chunk (sampleTerrain reaches 3 quarts, the zoom
/// and the canopy 2, the canopy's 5 x 5 cells 5): quarts chunk * 4 - 4 .. chunk * 4 + 8.
class ChunkColumns {
public:
    ChunkColumns(State const& s, int chunkX, int chunkZ);
    [[nodiscard]] TerrainColumn const* column(int qx, int qz) const;
    [[nodiscard]] std::string const&   noiseBiome(int qx, int qy, int qz) const;
    [[nodiscard]] std::string const&   blockBiome(int x, int y, int z) const;
    [[nodiscard]] TerrainSampler       sampler() const;
    /// The distinct quart biomes of the chunk's sections (y range of the chunk).
    [[nodiscard]] std::vector<std::string> chunkBiomes(int minY, int maxY) const;

private:
    static constexpr int kPad = 4, kSide = 13;
    State const&                                            mS;
    int                                                     mQx0, mQz0;
    std::array<TerrainColumn const*, kSide * kSide>         mCols{};
};

/// Fills a grid from a chunk (blocks classified into codes) and writes the changed codes back.
void readGrid(State const& s, worldgen::Chunk& chunk, Grid& grid);
int  writeGrid(State const& s, worldgen::Chunk& chunk, Grid const& before, Grid const& after);

/// The providers (Terrain.cpp) and carvers (Carvers.cpp), for the in-memory selftests.
void terrainProvider(worldgen::Chunk& chunk);
void surfaceProvider(worldgen::Chunk& chunk, worldgen::Region& region);
void blanketProvider(worldgen::Chunk& chunk, worldgen::Region& region);
void carve(bool highlands, worldgen::Chunk& chunk, worldgen::Region& region, core::jrand::RandomSource& random);
/// TFCavesCarver#isStartChunk + #carve with an explicit context (source chunk, config, mask).
void carveWith(State const& s, bool highlands, worldgen::Chunk& chunk, worldgen::ext::CarverContext& ctx, core::jrand::RandomSource& random);

/// Milliseconds of the last chunks' terrain work (noise, surface, blanket, carvers), for terrain.timing.
void recordChunkMs(double ms);
/// Adds to the terrain time of the chunk being generated on this thread (the carvers).
void addChunkMs(double ms);
[[nodiscard]] std::vector<double> chunkTimes();
/// How many chunk times were recorded since the world loaded (chunkTimes keeps the last 4096).
[[nodiscard]] std::uint64_t chunkTimesRecorded();

// ---- the beardifier (Beardifier#forStructuresInChunk as NeoForge runs it) ------------------------------

/// A piece's Rigid with the piece's bounding box (isCloseToChunk tests that box).
struct PieceRigid {
    BlockBox     pieceBox;
    beard::Rigid rigid;
};
/// The Rigids of a start's pieces (ext::pieceBeard per piece; nothing for NONE pieces) for a
/// structure with terrain adaptation `structure`.
[[nodiscard]] std::vector<PieceRigid> rigidsOfPieces(std::vector<gf::landmark::ext::StartPiece> const& pieces, beard::TerrainAdjustment structure);
/// The Rigids the beardifier sums in chunk (chunkX, chunkZ) of `dim`: the valid starts referencing
/// it whose structure has a terrain adaptation, their pieces within 12 blocks (cached per start).
[[nodiscard]] std::vector<beard::Rigid> beardRigids(int dim, int chunkX, int chunkZ);
void clearBeardCache();
/// Bumped by every ext::registerPieceBeard (feature/ext/terrain/PieceBeard.cpp).
[[nodiscard]] std::uint64_t pieceBeardVersion();

/// ChunkAccess#getAllReferences contains a start of one of `structures` (the canopy's
/// avoid_structures): valid starts only, F6's reserved sites are none.
[[nodiscard]] bool chunkReferences(int dim, int chunkX, int chunkZ, std::vector<std::string> const& structures);

/// The stage before handed this thread's chunk its grid (0: noise, 1: surface) and the chunk still
/// holds it where the next stage looks: moves it into `out`.
[[nodiscard]] bool takeHandedGrid(State const& s, worldgen::Chunk& chunk, int stage, Grid& out);
/// How many grids the stages took from the stage before (first) and how many they read from the chunk
/// instead because something else could have written it (second), since the module loaded.
[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> handoffCounts();

/// The saved chunks of dimension `dim` in the loaded world's DB (nullopt without a level): a world
/// without a kept preset whose Twilight Forest has some stays twilight_noise_gen.
[[nodiscard]] std::optional<std::size_t> savedChunkCount(int dim);

/// TFCavesCarver#carveBlock (and its postCarveBlock) at one world position of `chunk`, as a tunnel
/// reaching it would run it (selftests: the fluid tests on Bedrock's liquid layer). True: carved.
bool carveBlockAt(State const& s, bool highlands, worldgen::Chunk& chunk, worldgen::ext::CarverContext& ctx, int x, int y, int z);

} // namespace gf::feature::terrain
