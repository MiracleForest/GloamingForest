#pragma once

// Contract "worldgen" (header owned by F0, implemented by F5; spike S2).
//
// The Gloaming Forest's chunk generator runs as a staged pipeline (Java's ChunkStatus order):
//   Biomes -> Starts -> Noise -> Surface -> Carvers -> Features -> Pieces -> Blanket -> Spawns
// Units plug providers into it by string id and never touch the engine's generator:
//
//     gf::worldgen::setBiomeSource([](int qx, int qy, int qz) { return layers.biomeAt(qx, qz); });  // U1
//     gf::worldgen::setTerrain([](gf::worldgen::Chunk& c) { ... });                                 // U2
//     gf::worldgen::registerCarver("gloamingforest:gf_caves", carve);                               // U2
//     gf::worldgen::addStage(gf::worldgen::Stage::Pieces, "landmark.pieces", 0, placePieces);       // F6/F7
//
// Generation is deterministic for a seed and may run on worker threads: providers must be
// thread-safe and must not touch the level outside the chunk / region they are given. Blocks are
// the engine's immutable `Block` permutations (look them up once, e.g. with game::blockWithStates).

#include <cstdint>
#include <functional>
#include <string>

#include "core/jrand/JRand.h"

class Block;
class BlockPos;

namespace gf::worldgen {

using core::jrand::RandomSource;

enum class Stage { Biomes, Starts, Noise, Surface, Carvers, Features, Pieces, Blanket, Spawns };

/// Java's Heightmap.Types.
enum class Heightmap { WorldSurfaceWg, WorldSurface, OceanFloorWg, OceanFloor, MotionBlocking, MotionBlockingNoLeaves };

/// One chunk being generated (Java's ChunkAccess / ProtoChunk). Local x / z are 0..15, y is absolute.
class Chunk {
public:
    virtual ~Chunk() = default;
    [[nodiscard]] virtual int          chunkX() const                                       = 0;
    [[nodiscard]] virtual int          chunkZ() const                                       = 0;
    [[nodiscard]] virtual int          minY() const                                         = 0;
    [[nodiscard]] virtual int          maxY() const                                         = 0; ///< exclusive
    [[nodiscard]] virtual Block const& getBlock(int lx, int y, int lz) const                = 0;
    virtual void                       setBlock(int lx, int y, int lz, Block const& block)  = 0;
    [[nodiscard]] virtual int          height(Heightmap type, int lx, int lz) const         = 0;
    /// The biome id at a local position ("gloamingforest:dark_forest").
    [[nodiscard]] virtual std::string  biome(int lx, int y, int lz) const                   = 0;
    virtual void                       setBiome(int lx, int y, int lz, std::string const& biomeId) = 0;
};

/// The chunk being decorated plus its generated neighbours (Java's WorldGenRegion: a 3x3 chunk
/// window around the center chunk). Absolute block coordinates.
class Region {
public:
    virtual ~Region() = default;
    [[nodiscard]] virtual int          centerChunkX() const                                   = 0;
    [[nodiscard]] virtual int          centerChunkZ() const                                   = 0;
    [[nodiscard]] virtual int          minY() const                                           = 0;
    [[nodiscard]] virtual int          maxY() const                                           = 0; ///< exclusive
    /// Whether (x, z) lies in the writable window.
    [[nodiscard]] virtual bool         isInside(int x, int z) const                           = 0;
    [[nodiscard]] virtual Block const& getBlock(BlockPos const& pos) const                    = 0;
    /// Sets a block in the window (outside: false; use features::LargeWriter for those).
    virtual bool                       setBlock(BlockPos const& pos, Block const& block, int flags = 3) = 0;
    [[nodiscard]] virtual int          height(Heightmap type, int x, int z) const             = 0;
    [[nodiscard]] virtual std::string  biome(BlockPos const& pos) const                       = 0;
    [[nodiscard]] virtual std::int64_t seed() const                                           = 0;
    /// The dimension being generated (engine id).
    [[nodiscard]] virtual int          dimension() const                                      = 0;
};

/// Biome at quart (4-block) coordinates (Java's BiomeSource#getNoiseBiome); returns a biome id.
using BiomeSourceFn = std::function<std::string(int quartX, int quartY, int quartZ)>;
/// upstream's "main biome" of a column (GloamingBiomeProvider: the layer stack result before underground
/// biomes); used by landmark placement and the magic map.
using MainBiomeFn     = std::function<std::string(int blockX, int blockZ)>;
using TerrainProvider = std::function<void(Chunk& chunk)>;
using SurfaceProvider = std::function<void(Chunk& chunk, Region& region)>;
using CarverFn        = std::function<void(Chunk& chunk, Region& region, RandomSource& random)>;
using BlanketFn       = std::function<void(Chunk& chunk, Region& region)>;
using ChunkFn         = std::function<void(Chunk& chunk, Region& region)>;

/// Replaces the biome source of the Gloaming Forest (U1).
void setBiomeSource(BiomeSourceFn fn);
void setMainBiomeSource(MainBiomeFn fn);
/// The biome id at a block of the Gloaming Forest ("" when no source is set).
[[nodiscard]] std::string biomeAt(BlockPos const& pos);
/// upstream's main biome at a column.
[[nodiscard]] std::string mainBiomeAt(int blockX, int blockZ);

/// The base terrain (density -> stone / water) and the surface rules (U2).
void setTerrain(TerrainProvider fn);
void setSurface(SurfaceProvider fn);
/// A carver referenced by id from biome / configured_carver JSON ("gloamingforest:gf_caves").
void registerCarver(std::string const& id, CarverFn fn);
/// A chunk blanket processor ("gloamingforest:canopy", "gloamingforest:glacier"; Blanket stage).
void registerBlanket(std::string const& id, BlanketFn fn);
/// Adds a step to a stage; steps of a stage run by ascending `order`, then registration order.
void addStage(Stage stage, std::string const& id, int order, ChunkFn fn);

/// The seed of the world being generated (the overworld seed, as upstream uses it).
[[nodiscard]] std::int64_t seed();

} // namespace gf::worldgen
