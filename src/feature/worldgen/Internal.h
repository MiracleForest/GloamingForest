#pragma once

// Internals of the generator pipeline (F5): the providers snapshot, the chunk / region adapters
// and the stage runner shared by the game generator and the in-memory runs. Not a contract.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/ext/worldgen/Carving.h"
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
    std::uint64_t                    biomeSourceGeneration = 0; ///< increases with every setBiomeSource
};
[[nodiscard]] std::shared_ptr<Providers const> providers();

/// The world seed of the running level (0 without a level).
void         setSeed(std::int64_t seed);
[[nodiscard]] std::int64_t currentSeed();
/// BiomeManager#obfuscateSeed of `seed` (cached per thread).
[[nodiscard]] std::int64_t zoomSeedOf(std::int64_t seed);
/// The quart biome of a region / world at quart coordinates (Java's NoiseBiomeSource).
using NoiseBiomeFn = std::function<std::string(int qx, int qy, int qz)>;
/// Java's BiomeManager#getBiome(pos) for world seed `seed`: the noise biome of the quart the zoom
/// picks for `pos`.
[[nodiscard]] std::string zoomedBiome(std::int64_t seed, BlockPos const& pos, NoiseBiomeFn const& noiseBiome);
/// The y range of the Twilight Forest ([minY, maxY), set by the generator), for biomeAt's clamp.
void setTwilightHeight(int minY, int maxY);
[[nodiscard]] int twilightMinY();
[[nodiscard]] int twilightMaxY();
/// The WorldGenRegion random of the region centered on chunk (cx, cz): RandomState#
/// getOrCreateRandomFactory("minecraft:worldgen_region_random").at(the chunk's world position).
/// TF's noise settings are not "legacy_random_source", so it is Xoroshiro-based.
[[nodiscard]] std::unique_ptr<RandomSource> worldGenRegionRandom(std::int64_t seed, int chunkX, int chunkZ);

/// Drops the per-world caches (engine biomes, block permutations); on world close.
void clearWorldCaches();
/// Biome id <-> engine biome ("minecraft:plains" <-> "plains"); thread-safe, cached per registry.
[[nodiscard]] Biome const* engineBiome(BiomeRegistry const& registry, std::string const& id);
[[nodiscard]] std::string  biomeIdOf(Biome const& biome);

/// Java heightmap predicate.
[[nodiscard]] bool heightmapMatches(Heightmap type, Block const& block);

// ---- adapters ---------------------------------------------------------------------------------------
/// A chunk that takes a whole quart column of biomes at once (the generator's own biome writes).
class QuartBiomeWriter {
public:
    virtual ~QuartBiomeWriter() = default;
    /// Sets the biomes of quart column (qx, qz) (0..3): ids[i] is the biome of quart y qMin + i.
    virtual void setQuartColumn(int qx, int qz, int qMin, std::vector<std::string> const& ids) = 0;
};

/// A chunk of the game being generated (loadChunk). Writes go straight into the LevelChunk.
class LevelChunkAdapter final : public Chunk, public QuartBiomeWriter {
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
    /// The engine biomes are resolved once per id and chunk; each block column takes the most
    /// frequent biome of the quart column in one call and the other quarts block by block.
    void setQuartColumn(int qx, int qz, int qMin, std::vector<std::string> const& ids) override;
    [[nodiscard]] LevelChunk& levelChunk() { return mChunk; }

private:
    Biome const* resolve(std::string const& id);
    LevelChunk&                                       mChunk;
    BiomeRegistry const&                              mBiomes;
    int                                               mX, mZ, mMinY, mMaxY;
    std::vector<std::pair<std::string, Biome const*>> mResolved; ///< id -> engine biome (this chunk's few ids)
};

/// A region that answers the raw quart biomes (Java's NoiseBiomeSource of a WorldGenRegion:
/// ChunkAccess#getNoiseBiome of the chunks it holds, the biome source elsewhere); its biome(pos)
/// zooms over them.
class NoiseBiomeRegion {
public:
    virtual ~NoiseBiomeRegion() = default;
    [[nodiscard]] virtual std::string noiseBiome(int qx, int qy, int qz) const = 0;
};

/// A window of the world through a BlockSource (decoration: the 3x3 chunks around the center;
/// selftests: any box). Heightmaps are computed by scanning, cached per column until a write.
class SourceRegionAdapter final : public features::detail::SourceRegion, public NoiseBiomeRegion {
public:
    /// Writable chunks: center +- `radius`; heights scan [minY, maxY). `fallback` answers the quart
    /// biomes of chunks the source does not hold (the generator: the biome source); without it the
    /// source's own (default) biome is used.
    SourceRegionAdapter(
        BlockSource&  source,
        int           centerChunkX,
        int           centerChunkZ,
        int           radius,
        int           minY,
        int           maxY,
        std::int64_t  seed,
        int           dimension,
        BiomeSourceFn fallback = {}
    );
    int          centerChunkX() const override { return mCX; }
    int          centerChunkZ() const override { return mCZ; }
    int          minY() const override { return mMinY; }
    int          maxY() const override { return mMaxY; }
    bool         isInside(int x, int z) const override;
    Block const& getBlock(BlockPos const& pos) const override;
    bool         setBlock(BlockPos const& pos, Block const& block, int flags = 3) override;
    int          height(Heightmap type, int x, int z) const override;
    /// LevelReader#getBiome: BiomeManager's zoom over noiseBiome().
    std::string  biome(BlockPos const& pos) const override;
    std::int64_t seed() const override { return mSeed; }
    int          dimension() const override { return mDim; }
    BlockSource* source() override { return &mSource; }
    /// The raw quart biome (ChunkAccess#getNoiseBiome: y clamped to the chunk's quarts).
    [[nodiscard]] std::string noiseBiome(int qx, int qy, int qz) const override;
    /// Blocks set through this region.
    [[nodiscard]] int writes() const { return mWrites; }
    /// The biome ids in the section palettes of the chunks within `radius` of the center (the set
    /// Java's applyBiomeDecoration collects); false when one of the chunks is not available.
    bool chunkBiomes(int radius, std::vector<std::string>& out) const;

private:
    BlockSource&                                      mSource;
    int                                               mCX, mCZ, mRadius, mMinY, mMaxY, mDim;
    std::int64_t                                      mSeed;
    BiomeSourceFn                                     mFallback;
    int                                               mWrites = 0;
    mutable std::unordered_map<std::int64_t, std::array<int, 6>> mHeights; ///< column -> height per Heightmap
};

/// The writable region of loadChunk: the chunk itself (Java's WorldGenRegion of the proto-chunk
/// steps, which holds only that chunk). Other columns' biomes come from `biomeSource` (Java:
/// LevelReader#getNoiseBiome falls back to getUncachedNoiseBiome for a chunk it does not hold).
class ChunkOnlyRegion final : public Region, public NoiseBiomeRegion {
public:
    ChunkOnlyRegion(Chunk& chunk, std::int64_t seed, int dimension, BiomeSourceFn biomeSource = {});
    int          centerChunkX() const override { return mC.chunkX(); }
    int          centerChunkZ() const override { return mC.chunkZ(); }
    int          minY() const override { return mC.minY(); }
    int          maxY() const override { return mC.maxY(); }
    bool         isInside(int x, int z) const override { return (x >> 4) == mC.chunkX() && (z >> 4) == mC.chunkZ(); }
    Block const& getBlock(BlockPos const& pos) const override;
    bool         setBlock(BlockPos const& pos, Block const& block, int flags = 3) override;
    /// WorldGenLevel#getHeight: ChunkAccess#getHeight + 1 (outside the chunk: minY).
    int          height(Heightmap type, int x, int z) const override;
    std::string  biome(BlockPos const& pos) const override;
    std::string  noiseBiome(int qx, int qy, int qz) const override;
    std::int64_t seed() const override { return mSeed; }
    int          dimension() const override { return mDim; }

private:
    Chunk&        mC;
    std::int64_t  mSeed;
    int           mDim;
    BiomeSourceFn mSource;
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
    /// The raw biome stored at a block ("" when the chunk does not exist).
    [[nodiscard]] std::string  biome(BlockPos const& pos) const;
    [[nodiscard]] int          minY() const { return mMinY; }
    [[nodiscard]] int          maxY() const { return mMaxY; }
    [[nodiscard]] std::int64_t seed() const { return mSeed; }
    /// FNV-1a over every block id of chunk (cx, cz).
    [[nodiscard]] std::uint64_t hash(int cx, int cz) const;
    /// A Region over the chunks of this world (writable: center +- radius). Its biome() zooms over
    /// the stored quart biomes, with `fallback` for chunks the world does not hold.
    [[nodiscard]] std::unique_ptr<Region> region(int centerChunkX, int centerChunkZ, int radius, BiomeSourceFn fallback = {});

private:
    std::map<std::pair<int, int>, std::unique_ptr<MemChunk>> mChunks;
    int                                                      mMinY, mMaxY;
    std::int64_t                                             mSeed;
};
[[nodiscard]] Chunk& asChunk(MemoryWorld::MemChunk& c);

// ---- pipeline -----------------------------------------------------------------------------------------
/// Biomes, Starts, Noise, Surface, Blanket and Carvers of one chunk (Bedrock's loadChunk).
void generateChunk(Chunk& chunk, Region& chunkRegion, std::int64_t seed);
void generateChunk(Providers const& providers, Chunk& chunk, Region& chunkRegion, std::int64_t seed);
/// Writes the biome source's quart biomes into `chunk`; returns the distinct biome ids written
/// (empty without a biome source).
std::vector<std::string> writeBiomes(Providers const& providers, Chunk& chunk);
/// Per decoration step the Pieces and the placed features, then Features, LargeWriter and Spawns of
/// the center chunk of `region` (Bedrock's decoration post-processing).
void decorateChunk(Chunk* chunk, Region& region, std::int64_t seed);
void decorateChunk(Providers const& providers, Chunk* chunk, Region& region, std::int64_t seed);
/// ext::runInMemory with an explicit providers snapshot.
double runInMemory(Providers const& providers, std::int64_t seed, int x0, int z0, int width, int count, std::uint64_t* hash);
/// The flat placeholder terrain used while no terrain provider is registered (config).
void fallbackTerrain(Chunk& chunk);
/// The configured carver ids of a biome (default: ext::biomeCarvers).
using CarverListFn = std::function<std::vector<std::string>(std::string const& biomeId)>;
/// ChunkGenerator#applyCarvers: every source chunk within 8 chunks, the carvers of its biome, each
/// seeded with setLargeFeatureSeed(seed + index, sourceX, sourceZ) (see ext/worldgen/Carving.h).
void applyCarvers(Providers const& providers, Chunk& chunk, Region& region, std::int64_t seed, CarverListFn const& carversOf = {});
/// Forgets the cached carver biomes of source chunks (world close).
void clearCarverBiomeCache();
/// The carving masks kept between a chunk's carvers and its decoration (CarvingMaskPlacement).
[[nodiscard]] std::shared_ptr<ext::CarvingMask const> carvingMaskOf(int dimension, int chunkX, int chunkZ);
void dropCarvingMask(int dimension, int chunkX, int chunkZ);
void clearCarvingMasks();

void addStats(bool decorate, double ms);
void addBiomeWriteStats(double ms);

/// How often the mod answered dev_new_world's scriptevent (Functions.cpp; selftests).
[[nodiscard]] int devNewWorldRuns();

} // namespace gf::worldgen::detail
