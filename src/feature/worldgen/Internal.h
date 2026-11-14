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

/// Config section "worldgen" (rule R13; upstream has no config keys of its own for it).
struct WorldgenConfig {
    int  version = 1;
    /// Generate a flat placeholder ground while no terrain provider (U2) is registered.
    bool fallbackTerrain = true;
    /// Spawn the biomes' natural mobs in the Gloaming Forest (Java's NaturalSpawner and mob caps).
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
    std::vector<Step>                structureSteps; ///< ext::addStructureStep (every decoration step), sorted by (order, seq)
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
/// The y range of the Gloaming Forest ([minY, maxY), set by the generator), for biomeAt's clamp.
void setGloamingHeight(int minY, int maxY);
[[nodiscard]] int gloamingMinY();
[[nodiscard]] int gloamingMaxY();
/// The WorldGenRegion random of the region centered on chunk (cx, cz): RandomState#
/// getOrCreateRandomFactory("minecraft:worldgen_region_random").at(the chunk's world position).
/// upstream's noise settings are not "legacy_random_source", so it is Xoroshiro-based.
[[nodiscard]] std::unique_ptr<RandomSource> worldGenRegionRandom(std::int64_t seed, int chunkX, int chunkZ);

/// Drops the per-world caches (engine biomes, block permutations); on world close.
void clearWorldCaches();
/// Biome id <-> engine biome ("minecraft:plains" <-> "plains"); thread-safe, cached per registry.
[[nodiscard]] Biome const* engineBiome(BiomeRegistry const& registry, std::string const& id);
[[nodiscard]] std::string  biomeIdOf(Biome const& biome);

/// Java heightmap predicate.
[[nodiscard]] bool heightmapMatches(Heightmap type, Block const& block);

// ---- adapters ---------------------------------------------------------------------------------------
/// A chunk that keeps quart biomes and takes a whole quart column at once (the in-memory chunks).
class QuartBiomeWriter {
public:
    virtual ~QuartBiomeWriter() = default;
    /// Sets the biomes of quart column (qx, qz) (0..3): ids[i] is the biome of quart y qMin + i.
    virtual void setQuartColumn(int qx, int qz, int qMin, std::vector<std::string> const& ids) = 0;
};

/// A chunk that stores a biome per block (the game's LevelChunk). The generator writes into it the
/// biome BiomeManager's zoom picks for every block (Java's LevelReader#getBiome: what Java's client
/// and gameplay read per block), so Bedrock's own per-block readers (grass, foliage and water
/// colours, precipitation, snow, the client) see Java's biome borders, not 4 x 4 x 4 steps; while the
/// chunk generates, Chunk::biome keeps answering the raw quarts.
class BlockBiomeWriter {
public:
    virtual ~BlockBiomeWriter() = default;
    /// `quarts`: the raw quart biomes of quart x chunkX * 4 - 1 .. chunkX * 4 + 4 and z likewise
    /// (the chunk and the one-quart ring the zoom reaches), quart y qMin .. qMin + nq - 1, at index
    /// ((dx * 6) + dz) * nq + (qy - qMin) with dx, dz in 0..5. The zoom's quart y is clamped to them
    /// (ChunkAccess#getNoiseBiome).
    virtual void writeZoomedBiomes(std::int64_t zoomSeed, int qMin, int nq, std::vector<std::string> const& quarts) = 0;
};

/// A chunk of the game being generated (loadChunk). Writes go straight into the LevelChunk.
class LevelChunkAdapter final : public Chunk, public BlockBiomeWriter {
public:
    LevelChunkAdapter(LevelChunk& chunk, BiomeRegistry const& biomes, int minY, int maxY);
    int          chunkX() const override { return mX; }
    int          chunkZ() const override { return mZ; }
    int          minY() const override { return mMinY; }
    int          maxY() const override { return mMaxY; }
    Block const& getBlock(int lx, int y, int lz) const override;
    void         setBlock(int lx, int y, int lz, Block const& block) override;
    int          height(Heightmap type, int lx, int lz) const override;
    /// The raw quart biome after writeZoomedBiomes (the generation), else the biome the chunk stores
    /// at the block.
    std::string  biome(int lx, int y, int lz) const override;
    /// Sets the block's biome (and, during the generation, its quart's raw biome). The zoomed biomes
    /// already written around it are not recomputed: change biomes through the biome source.
    void         setBiome(int lx, int y, int lz, std::string const& biomeId) override;
    /// Sets the biome of a whole column (faster than per block).
    void setBiomeColumn(int lx, int lz, std::string const& biomeId);
    /// The engine biomes are resolved once per id and chunk; a block column whose candidate quarts
    /// all agree takes their biome without the zoom, each column's most frequent biome is written in
    /// one call and the others block by block. An unknown id keeps what the chunk had there.
    void writeZoomedBiomes(std::int64_t zoomSeed, int qMin, int nq, std::vector<std::string> const& quarts) override;
    [[nodiscard]] LevelChunk& levelChunk() { return mChunk; }

private:
    Biome const* resolve(std::string const& id);
    LevelChunk&                                       mChunk;
    BiomeRegistry const&                              mBiomes;
    int                                               mX, mZ, mMinY, mMaxY;
    std::vector<std::pair<std::string, Biome const*>> mResolved; ///< id -> engine biome (this chunk's few ids)
    std::vector<std::string>                          mRawQuarts; ///< (qx * 4 + qz) * mRawNq + (qy - mRawQMin), during the generation
    int                                               mRawQMin = 0, mRawNq = 0;
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
/// Over chunks this generator wrote (the Gloaming Forest while the generator is installed, decided
/// by the constructor; setZoomedChunks overrides it): they store the zoomed biome per block, so
/// biome(pos) reads the chunk's block biome, noiseBiome() the biome source and chunkBiomes() the
/// raw biomes kept at generation (keptChunkBiomes); over other chunks (Bedrock's own quart biomes)
/// it zooms over the stored quarts.
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
    /// The chunks were written by this generator (zoomed biomes per block); see above.
    void setZoomedChunks(bool zoomed) { mZoomed = zoomed; }

private:
    BlockSource&                                      mSource;
    int                                               mCX, mCZ, mRadius, mMinY, mMaxY, mDim;
    std::int64_t                                      mSeed;
    BiomeSourceFn                                     mFallback;
    int                                               mWrites = 0;
    bool                                              mZoomed = false;
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
/// Writes the biome source's biomes into `chunk` (a BlockBiomeWriter: the zoomed biome of every
/// block for world seed `seed`; a QuartBiomeWriter / other chunk: the quarts); returns the distinct
/// raw biome ids of the chunk's quarts (empty without a biome source).
std::vector<std::string> writeBiomes(Providers const& providers, Chunk& chunk, std::int64_t seed);
/// The distinct raw biomes of the game chunks the generator wrote, kept for the decoration of their
/// 3 x 3 neighbourhoods (the chunks themselves store zoomed biomes). Bounded; nullopt: not kept.
void keepChunkBiomes(int dimension, int chunkX, int chunkZ, std::vector<std::string> biomes);
[[nodiscard]] std::optional<std::vector<std::string>> keptChunkBiomes(int dimension, int chunkX, int chunkZ);
void clearKeptChunkBiomes();
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
