// Chunk and region adapters of the generator pipeline (F5): the LevelChunk being generated, a
// BlockSource window (Java's WorldGenRegion) and an in-memory world for timing / determinism runs.
// Chunk heights follow ChunkAccess#getHeight, region heights WorldGenLevel#getHeight, and region
// biomes LevelReader#getBiome (BiomeManager's zoom over the chunks' quart biomes).
#include <algorithm>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "core/VolumeHash.h"
#include "core/jrand/Mth.h"
#include "feature/worldgen/Internal.h"

namespace gf::worldgen::detail {

namespace {
ChunkBlockPos cpos(int lx, int y, int lz, int minY) {
    return ChunkBlockPos{static_cast<uchar>(lx), ChunkLocalHeight{static_cast<short>(y - minY)}, static_cast<uchar>(lz)};
}
/// ChunkAccess#getNoiseBiome's clamp of a quart y to the chunk's quarts.
int clampQuartY(int qy, int minY, int maxY) {
    int lo = core::mth::fromBlock(minY);
    int hi = lo + core::mth::fromBlock(maxY - minY) - 1;
    return std::clamp(qy, lo, hi);
}
} // namespace

// ==== LevelChunkAdapter ================================================================================

LevelChunkAdapter::LevelChunkAdapter(LevelChunk& chunk, BiomeRegistry const& biomes, int minY, int maxY)
: mChunk(chunk),
  mBiomes(biomes),
  mX(chunk.mPosition->x),
  mZ(chunk.mPosition->z),
  mMinY(minY),
  mMaxY(maxY) {}

Block const& LevelChunkAdapter::getBlock(int lx, int y, int lz) const {
    if (y < mMinY || y >= mMaxY || lx < 0 || lx > 15 || lz < 0 || lz > 15) return features::detail::air();
    return mChunk.getBlock(cpos(lx, y, lz, mMinY));
}

void LevelChunkAdapter::setBlock(int lx, int y, int lz, Block const& block) {
    if (y < mMinY || y >= mMaxY || lx < 0 || lx > 15 || lz < 0 || lz > 15) return;
    mChunk.setBlockSimple(cpos(lx, y, lz, mMinY), block);
}

int LevelChunkAdapter::height(Heightmap type, int lx, int lz) const {
    // ChunkAccess#getHeight: the highest matching block (minY - 1 for an empty column)
    for (int y = mMaxY - 1; y >= mMinY; --y)
        if (heightmapMatches(type, getBlock(lx, y, lz))) return y;
    return mMinY - 1;
}

std::string LevelChunkAdapter::biome(int lx, int y, int lz) const {
    if (y < mMinY) y = mMinY;
    if (y >= mMaxY) y = mMaxY - 1;
    return biomeIdOf(mChunk.getBiome(cpos(lx & 15, y, lz & 15, mMinY)));
}

void LevelChunkAdapter::setBiome(int lx, int y, int lz, std::string const& biomeId) {
    if (y < mMinY || y >= mMaxY) return;
    if (auto const* b = resolve(biomeId)) mChunk._setBiome(*b, cpos(lx & 15, y, lz & 15, mMinY), false);
}

void LevelChunkAdapter::setBiomeColumn(int lx, int lz, std::string const& biomeId) {
    auto const* b = resolve(biomeId);
    if (!b) return;
    // one call fills the whole column (fillYDimension); worldgen.biomes checks both ends
    mChunk._setBiome(*b, cpos(lx & 15, mMinY, lz & 15, mMinY), true);
}

Biome const* LevelChunkAdapter::resolve(std::string const& id) {
    for (auto const& [k, b] : mResolved)
        if (k == id) return b;
    auto const* b = engineBiome(mBiomes, id);
    mResolved.emplace_back(id, b);
    return b;
}

void LevelChunkAdapter::setQuartColumn(int qx, int qz, int qMin, std::vector<std::string> const& ids) {
    if (ids.empty()) return;
    // the engine biome of every quart, resolved once per distinct id
    std::vector<Biome const*> quarts(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) quarts[i] = (i > 0 && ids[i] == ids[i - 1]) ? quarts[i - 1] : resolve(ids[i]);
    // the most frequent biome fills the column in one call; the other quarts are written per block
    std::vector<std::pair<Biome const*, int>> counts;
    for (auto const* b : quarts) {
        if (!b) continue;
        auto it = std::find_if(counts.begin(), counts.end(), [&](auto const& c) { return c.first == b; });
        if (it == counts.end()) counts.emplace_back(b, 1);
        else ++it->second;
    }
    if (counts.empty()) return;
    Biome const* dominant = std::max_element(counts.begin(), counts.end(), [](auto const& a, auto const& b) { return a.second < b.second; })->first;
    for (int dx = 0; dx < 4; ++dx)
        for (int dz = 0; dz < 4; ++dz) {
            int lx = (qx & 3) * 4 + dx, lz = (qz & 3) * 4 + dz;
            mChunk._setBiome(*dominant, cpos(lx, mMinY, lz, mMinY), true);
            for (std::size_t i = 0; i < quarts.size(); ++i) {
                if (!quarts[i] || quarts[i] == dominant) continue;
                int y0 = std::max(mMinY, (qMin + static_cast<int>(i)) * 4), y1 = std::min(mMaxY, (qMin + static_cast<int>(i)) * 4 + 4);
                for (int y = y0; y < y1; ++y) mChunk._setBiome(*quarts[i], cpos(lx, y, lz, mMinY), false);
            }
        }
}

// ==== SourceRegionAdapter ===============================================================================

SourceRegionAdapter::SourceRegionAdapter(
    BlockSource&  source,
    int           cx,
    int           cz,
    int           radius,
    int           minY,
    int           maxY,
    std::int64_t  seed,
    int           dimension,
    BiomeSourceFn fallback
)
: mSource(source),
  mCX(cx),
  mCZ(cz),
  mRadius(radius),
  mMinY(minY),
  mMaxY(maxY),
  mDim(dimension),
  mSeed(seed),
  mFallback(std::move(fallback)) {}

bool SourceRegionAdapter::isInside(int x, int z) const {
    return std::abs((x >> 4) - mCX) <= mRadius && std::abs((z >> 4) - mCZ) <= mRadius;
}

Block const& SourceRegionAdapter::getBlock(BlockPos const& pos) const {
    if (pos.y < mSource.getMinHeight() || pos.y >= mSource.getMaxHeight()) return features::detail::air();
    return mSource.getBlock(pos);
}

bool SourceRegionAdapter::setBlock(BlockPos const& pos, Block const& block, int) {
    if (!isInside(pos.x, pos.z) || pos.y < mSource.getMinHeight() || pos.y >= mSource.getMaxHeight()) return false;
    // Java's WorldGenRegion#setBlock sends no neighbour updates during generation
    BlockChangeContext ctx{};
    bool ok = mSource.setBlock(pos, block, 2, nullptr, ctx);
    if (ok) {
        ++mWrites;
        mHeights.erase(core::mth::chunkPosAsLong(pos.x, pos.z)); // the column's heightmaps change
    }
    return ok;
}

int SourceRegionAdapter::height(Heightmap type, int x, int z) const {
    auto& column = mHeights.try_emplace(core::mth::chunkPosAsLong(x, z), std::array<int, 6>{-1000, -1000, -1000, -1000, -1000, -1000}).first->second;
    auto& h      = column[static_cast<std::size_t>(type)];
    if (h != -1000) return h;
    h = mMinY;
    for (int y = mMaxY - 1; y >= mMinY; --y) {
        if (heightmapMatches(type, getBlock({x, y, z}))) {
            h = y + 1;
            break;
        }
    }
    return h;
}

std::string SourceRegionAdapter::noiseBiome(int qx, int qy, int qz) const {
    int minY = mSource.getMinHeight(), maxY = mSource.getMaxHeight();
    if (auto const* lc = mSource.getChunk(qx >> 2, qz >> 2)) {
        int y = clampQuartY(qy, minY, maxY) * 4;
        return biomeIdOf(lc->getBiome(cpos((qx & 3) * 4, y, (qz & 3) * 4, minY)));
    }
    // LevelReader#getNoiseBiome: a chunk that is not there asks the biome source
    if (mFallback) return mFallback(qx, qy, qz);
    BlockPos p{qx * 4, clampQuartY(qy, minY, maxY) * 4, qz * 4};
    return biomeIdOf(mSource.getBiome(p));
}

std::string SourceRegionAdapter::biome(BlockPos const& pos) const {
    return zoomedBiome(mSeed, pos, [this](int qx, int qy, int qz) { return noiseBiome(qx, qy, qz); });
}

bool SourceRegionAdapter::chunkBiomes(int radius, std::vector<std::string>& out) const {
    std::vector<Biome const*> seen;
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz) {
            auto const* lc = mSource.getChunk(mCX + dx, mCZ + dz);
            if (!lc) return false;
            // every quart of every section (Java collects each section's biome palette); biomes
            // are compared by pointer and named once
            Biome const* last = nullptr;
            for (int y = mMinY; y < mMaxY; y += 4)
                for (int qz = 0; qz < 16; qz += 4)
                    for (int qx = 0; qx < 16; qx += 4) {
                        Biome const* b = &lc->getBiome(cpos(qx, y, qz, mMinY));
                        if (b == last) continue;
                        last = b;
                        if (std::find(seen.begin(), seen.end(), b) == seen.end()) seen.push_back(b);
                    }
        }
    for (auto const* b : seen) out.push_back(biomeIdOf(*b));
    return true;
}

// ==== ChunkOnlyRegion ===================================================================================

ChunkOnlyRegion::ChunkOnlyRegion(Chunk& chunk, std::int64_t seed, int dimension, BiomeSourceFn biomeSource)
: mC(chunk),
  mSeed(seed),
  mDim(dimension),
  mSource(std::move(biomeSource)) {}

Block const& ChunkOnlyRegion::getBlock(BlockPos const& p) const {
    if (!isInside(p.x, p.z)) return features::detail::air();
    return mC.getBlock(p.x & 15, p.y, p.z & 15);
}

bool ChunkOnlyRegion::setBlock(BlockPos const& p, Block const& b, int) {
    if (!isInside(p.x, p.z)) return false;
    mC.setBlock(p.x & 15, p.y, p.z & 15, b);
    return true;
}

int ChunkOnlyRegion::height(Heightmap type, int x, int z) const { return isInside(x, z) ? mC.height(type, x & 15, z & 15) + 1 : mC.minY(); }

std::string ChunkOnlyRegion::noiseBiome(int qx, int qy, int qz) const {
    if ((qx >> 2) == mC.chunkX() && (qz >> 2) == mC.chunkZ()) return mC.biome((qx & 3) * 4, clampQuartY(qy, mC.minY(), mC.maxY()) * 4, (qz & 3) * 4);
    if (mSource) return mSource(qx, qy, qz);
    return mC.biome(0, clampQuartY(qy, mC.minY(), mC.maxY()) * 4, 0); // no biome source: the single default biome
}

std::string ChunkOnlyRegion::biome(BlockPos const& pos) const {
    return zoomedBiome(mSeed, pos, [this](int qx, int qy, int qz) { return noiseBiome(qx, qy, qz); });
}

// ==== MemoryWorld =======================================================================================

class MemoryWorld::MemChunk final : public Chunk, public QuartBiomeWriter {
public:
    MemChunk(MemoryWorld& world, int cx, int cz)
    : mWorld(world),
      mX(cx),
      mZ(cz),
      mBlocks(static_cast<std::size_t>(world.maxY() - world.minY()) * 256, &features::detail::air()),
      mBiomes(static_cast<std::size_t>((world.maxY() - world.minY() + 3) / 4) * 16) {}

    int          chunkX() const override { return mX; }
    int          chunkZ() const override { return mZ; }
    int          minY() const override { return mWorld.minY(); }
    int          maxY() const override { return mWorld.maxY(); }
    Block const& getBlock(int lx, int y, int lz) const override {
        if (y < minY() || y >= maxY() || lx < 0 || lx > 15 || lz < 0 || lz > 15) return features::detail::air();
        return *mBlocks[index(lx, y, lz)];
    }
    void setBlock(int lx, int y, int lz, Block const& block) override {
        if (y < minY() || y >= maxY() || lx < 0 || lx > 15 || lz < 0 || lz > 15) return;
        mBlocks[index(lx, y, lz)] = &block;
    }
    int height(Heightmap type, int lx, int lz) const override {
        for (int y = maxY() - 1; y >= minY(); --y)
            if (heightmapMatches(type, getBlock(lx, y, lz))) return y;
        return minY() - 1;
    }
    std::string biome(int lx, int y, int lz) const override {
        y = std::clamp(y, minY(), maxY() - 1);
        return mBiomes[qindex(lx, y, lz)];
    }
    void setBiome(int lx, int y, int lz, std::string const& id) override {
        if (y < minY() || y >= maxY()) return;
        mBiomes[qindex(lx, y, lz)] = id;
    }
    void setQuartColumn(int qx, int qz, int qMin, std::vector<std::string> const& ids) override {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            int y = (qMin + static_cast<int>(i)) * 4;
            if (y < minY() || y >= maxY()) continue;
            mBiomes[qindex((qx & 3) * 4, y, (qz & 3) * 4)] = ids[i];
        }
    }

private:
    std::size_t index(int lx, int y, int lz) const { return static_cast<std::size_t>(((y - minY()) * 16 + lz) * 16 + lx); }
    std::size_t qindex(int lx, int y, int lz) const {
        return static_cast<std::size_t>((((y - minY()) >> 2) * 4 + ((lz & 15) >> 2)) * 4 + ((lx & 15) >> 2));
    }

    MemoryWorld&              mWorld;
    int                       mX, mZ;
    std::vector<Block const*> mBlocks;
    std::vector<std::string>  mBiomes;
};

Chunk& asChunk(MemoryWorld::MemChunk& c) { return c; }

namespace {

class MemoryRegion final : public Region, public NoiseBiomeRegion {
public:
    MemoryRegion(MemoryWorld& w, int cx, int cz, int r, BiomeSourceFn fallback)
    : mWorld(w),
      mCX(cx),
      mCZ(cz),
      mR(r),
      mFallback(std::move(fallback)) {}
    int          centerChunkX() const override { return mCX; }
    int          centerChunkZ() const override { return mCZ; }
    int          minY() const override { return mWorld.minY(); }
    int          maxY() const override { return mWorld.maxY(); }
    bool         isInside(int x, int z) const override { return std::abs((x >> 4) - mCX) <= mR && std::abs((z >> 4) - mCZ) <= mR; }
    Block const& getBlock(BlockPos const& pos) const override { return mWorld.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override {
        if (!isInside(pos.x, pos.z) || pos.y < minY() || pos.y >= maxY()) return false;
        mWorld.setBlock(pos, block);
        return true;
    }
    int height(Heightmap type, int x, int z) const override {
        for (int y = maxY() - 1; y >= minY(); --y)
            if (heightmapMatches(type, mWorld.getBlock({x, y, z}))) return y + 1;
        return minY();
    }
    std::string noiseBiome(int qx, int qy, int qz) const override {
        if (!mWorld.has(qx >> 2, qz >> 2)) return mFallback ? mFallback(qx, qy, qz) : std::string();
        int y = clampQuartY(qy, minY(), maxY()) * 4;
        return mWorld.biome({qx * 4, y, qz * 4});
    }
    std::string biome(BlockPos const& pos) const override {
        return zoomedBiome(mWorld.seed(), pos, [this](int qx, int qy, int qz) { return noiseBiome(qx, qy, qz); });
    }
    std::int64_t seed() const override { return mWorld.seed(); }
    int          dimension() const override { return -1; }

private:
    MemoryWorld&  mWorld;
    int           mCX, mCZ, mR;
    BiomeSourceFn mFallback;
};

} // namespace

MemoryWorld::MemoryWorld(int minY, int maxY, std::int64_t seed) : mMinY(minY), mMaxY(maxY), mSeed(seed) {}
MemoryWorld::~MemoryWorld() = default;

MemoryWorld::MemChunk& MemoryWorld::chunk(int cx, int cz) {
    auto& c = mChunks[{cx, cz}];
    if (!c) c = std::make_unique<MemChunk>(*this, cx, cz);
    return *c;
}

bool MemoryWorld::has(int cx, int cz) const { return mChunks.contains({cx, cz}); }

Block const& MemoryWorld::getBlock(BlockPos const& pos) const {
    auto it = mChunks.find({pos.x >> 4, pos.z >> 4});
    if (it == mChunks.end()) return features::detail::air();
    return it->second->getBlock(pos.x & 15, pos.y, pos.z & 15);
}

void MemoryWorld::setBlock(BlockPos const& pos, Block const& block) { chunk(pos.x >> 4, pos.z >> 4).setBlock(pos.x & 15, pos.y, pos.z & 15, block); }

std::string MemoryWorld::biome(BlockPos const& pos) const {
    auto it = mChunks.find({pos.x >> 4, pos.z >> 4});
    if (it == mChunks.end()) return {};
    return it->second->biome(pos.x & 15, pos.y, pos.z & 15);
}

std::uint64_t MemoryWorld::hash(int cx, int cz) const {
    std::uint64_t h  = 0xcbf29ce484222325ull;
    auto          it = mChunks.find({cx, cz});
    if (it == mChunks.end()) return h;
    // every block's id and permutation (states) in a fixed order, through core::fnv1a64
    for (int y = mMinY; y < mMaxY; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto const& b = it->second->getBlock(x, y, z);
                h             = (h ^ core::fnv1a64(b.getTypeName())) * 0x100000001b3ull;
                h             = (h ^ b.getData()) * 0x100000001b3ull;
            }
    return h;
}

std::unique_ptr<Region> MemoryWorld::region(int cx, int cz, int radius, BiomeSourceFn fallback) {
    return std::make_unique<MemoryRegion>(*this, cx, cz, radius, std::move(fallback));
}

} // namespace gf::worldgen::detail
