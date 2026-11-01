#pragma once

// Internals of the generator pipeline (F5): the providers snapshot, the chunk / region adapters
// and the stage runner shared by the game generator and the in-memory runs. Not a contract.

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/ext/worldgen/Generation.h"
#include "feature/features/Internal.h"
#include "feature/worldgen/Worldgen.h"

class Biome;
class BiomeRegistry;
class Block;
class BlockSource;
class LevelChunk;

namespace gf::worldgen {

/// Config section "worldgen" (rule R13; TF has no config keys of its own for it).
struct WorldgenConfig {
    int  version = 1;
    /// Generate a flat placeholder ground while no terrain provider (U2) is registered.
    bool fallbackTerrain = true;
    /// Spawn the biomes' natural mobs in the Twilight Forest (Java's NaturalSpawner and mob caps).
    bool naturalSpawning = true;
    /// Per-chunk generation budget (ms) the timing selftest reports against.
    int chunkBudgetMs = 50;
};

} // namespace gf::worldgen

namespace gf::worldgen::detail {

struct Step {
    std::string id;
    int         order = 0;
    std::size_t seq   = 0; ///< registration order
    ChunkFn     fn;
};

/// Everything units registered; replaced as a whole (copy on write), read without locks.
struct Providers {
    BiomeSourceFn                    biomeSource;
    MainBiomeFn                      mainBiome;
    TerrainProvider                  terrain;
    SurfaceProvider                  surface;
    std::map<std::string, CarverFn>  carvers;
    std::map<std::string, BlanketFn> blankets;
    std::map<Stage, std::vector<Step>> stages; ///< sorted by (order, seq)
    std::vector<std::string>         possibleBiomes;
};
[[nodiscard]] std::shared_ptr<Providers const> providers();

/// The world seed of the running level (0 without a level).
void         setSeed(std::int64_t seed);
[[nodiscard]] std::int64_t currentSeed();

/// Drops the per-world caches (engine biomes, block permutations); on world close.
void clearWorldCaches();
/// Biome id <-> engine biome ("minecraft:plains" <-> "plains"); thread-safe, cached per registry.
[[nodiscard]] Biome const* engineBiome(BiomeRegistry const& registry, std::string const& id);
[[nodiscard]] std::string  biomeIdOf(Biome const& biome);

/// Java heightmap predicate.
[[nodiscard]] bool heightmapMatches(Heightmap type, Block const& block);

// ---- adapters ---------------------------------------------------------------------------------------
/// A chunk of the game being generated (loadChunk). Writes go straight into the LevelChunk.
class LevelChunkAdapter final : public Chunk {
public:
    LevelChunkAdapter(LevelChunk& chunk, BiomeRegistry const& biomes, int minY, int maxY);
    int          chunkX() const override { return mX; }
    int          chunkZ() const override { return mZ; }
    int          minY() const override { return mMinY; }
    int          maxY() const override { return mMaxY; }
    Block const& getBlock(int lx, int y, int lz) const override;
    void         setBlock(int lx, int y, int lz, Block const& block) override;
    int          height(Heightmap type, int lx, int lz) const override;
    std::string  biome(int lx, int y, int lz) const override;
    void         setBiome(int lx, int y, int lz, std::string const& biomeId) override;
    /// Sets the biome of a whole column (faster than per block).
    void setBiomeColumn(int lx, int lz, std::string const& biomeId);
    [[nodiscard]] LevelChunk& levelChunk() { return mChunk; }

private:
    LevelChunk&          mChunk;
    BiomeRegistry const& mBiomes;
    int                  mX, mZ, mMinY, mMaxY;
};

/// A window of the world through a BlockSource (decoration: the 3x3 chunks around the center;
/// selftests: any box). Heightmaps are computed by scanning, cached per column until a write.
class SourceRegionAdapter final : public features::detail::SourceRegion {
public:
    /// Writable chunks: center +- `radius`; heights scan [minY, maxY).
    SourceRegionAdapter(BlockSource& source, int centerChunkX, int centerChunkZ, int radius, int minY, int maxY, std::int64_t seed, int dimension);
    int          centerChunkX() const override { return mCX; }
    int          centerChunkZ() const override { return mCZ; }
    int          minY() const override { return mMinY; }
    int          maxY() const override { return mMaxY; }
    bool         isInside(int x, int z) const override;
    Block const& getBlock(BlockPos const& pos) const override;
    bool         setBlock(BlockPos const& pos, Block const& block, int flags = 3) override;
    int          height(Heightmap type, int x, int z) const override;
    std::string  biome(BlockPos const& pos) const override;
    std::int64_t seed() const override { return mSeed; }
    int          dimension() const override { return mDim; }
    BlockSource* source() override { return &mSource; }
    /// Blocks set through this region.
    [[nodiscard]] int writes() const { return mWrites; }

private:
    BlockSource&                                      mSource;
    int                                               mCX, mCZ, mRadius, mMinY, mMaxY, mDim;
    std::int64_t                                      mSeed;
    int                                               mWrites = 0;
    mutable std::unordered_map<std::int64_t, std::array<int, 6>> mHeights; ///< column -> height per Heightmap
};

/// An in-memory world of chunks (in-memory runs, determinism selftests).
class MemoryWorld {
public:
    MemoryWorld(int minY, int maxY, std::int64_t seed);
    ~MemoryWorld();
    class MemChunk;
    [[nodiscard]] MemChunk& chunk(int cx, int cz);
    [[nodiscard]] bool      has(int cx, int cz) const;
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const;
    void                       setBlock(BlockPos const& pos, Block const& block);
    [[nodiscard]] std::string  biome(BlockPos const& pos) const;
    [[nodiscard]] int          minY() const { return mMinY; }
    [[nodiscard]] int          maxY() const { return mMaxY; }
    [[nodiscard]] std::int64_t seed() const { return mSeed; }
    /// FNV-1a over every block id of chunk (cx, cz).
    [[nodiscard]] std::uint64_t hash(int cx, int cz) const;
    /// A Region over the chunks of this world (writable: center +- radius).
    [[nodiscard]] std::unique_ptr<Region> region(int centerChunkX, int centerChunkZ, int radius);

private:
    std::map<std::pair<int, int>, std::unique_ptr<MemChunk>> mChunks;
    int                                                      mMinY, mMaxY;
    std::int64_t                                             mSeed;
};
[[nodiscard]] Chunk& asChunk(MemoryWorld::MemChunk& c);

// ---- pipeline -----------------------------------------------------------------------------------------
/// Biomes, Starts, Noise, Surface and Carvers of one chunk (Bedrock's loadChunk).
void generateChunk(Chunk& chunk, Region& chunkRegion, std::int64_t seed);
/// Features (with the Pieces at the surface-structures step), LargeWriter, Blanket and Spawns of the
/// center chunk of `region` (Bedrock's decoration post-processing).
void decorateChunk(Chunk* chunk, Region& region, std::int64_t seed);
/// The flat placeholder terrain used while no terrain provider is registered (config).
void fallbackTerrain(Chunk& chunk);

void addStats(bool decorate, double ms);

} // namespace gf::worldgen::detail
