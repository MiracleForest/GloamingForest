// Chunk and region adapters of the generator pipeline (F5): the LevelChunk being generated, a
// BlockSource window (Java's WorldGenRegion) and an in-memory world for timing / determinism runs.
// Chunk heights follow ChunkAccess#getHeight, region heights WorldGenLevel#getHeight, and region
// biomes LevelReader#getBiome (BiomeManager's zoom over the chunks' quart biomes). The game's
// chunks store a biome per block: the generator writes the zoomed biome of every block into them.
#include <algorithm>
#include <cstdint>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "core/VolumeHash.h"
#include "core/jrand/BiomeManager.h"
#include "core/jrand/Mth.h"
#include "feature/dimension/World.h"
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
    if (!mRawQuarts.empty()) {
        int qy = std::clamp(y >> 2, mRawQMin, mRawQMin + mRawNq - 1);
        return mRawQuarts[static_cast<std::size_t>((((lx & 15) >> 2) * 4 + ((lz & 15) >> 2)) * mRawNq + (qy - mRawQMin))];
    }
    return biomeIdOf(mChunk.getBiome(cpos(lx & 15, y, lz & 15, mMinY)));
}

void LevelChunkAdapter::setBiome(int lx, int y, int lz, std::string const& biomeId) {
    if (y < mMinY || y >= mMaxY) return;
    if (auto const* b = resolve(biomeId)) mChunk._setBiome(*b, cpos(lx & 15, y, lz & 15, mMinY), false);
    if (!mRawQuarts.empty()) {
        int qy = std::clamp(y >> 2, mRawQMin, mRawQMin + mRawNq - 1);
        mRawQuarts[static_cast<std::size_t>((((lx & 15) >> 2) * 4 + ((lz & 15) >> 2)) * mRawNq + (qy - mRawQMin))] = biomeId;
    }
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

void LevelChunkAdapter::writeZoomedBiomes(std::int64_t zoomSeed, int qMin, int nq, std::vector<std::string> const& quarts) {
    if (nq <= 0 || quarts.size() != static_cast<std::size_t>(36 * nq)) return;
    // the distinct ids (a chunk has a few), each resolved once; the quarts as indices into them
    std::vector<std::string const*> names;
    std::vector<Biome const*>       engine;
    std::vector<std::uint16_t>      index(quarts.size());
    for (std::size_t i = 0; i < quarts.size(); ++i) {
        std::size_t k = 0;
        if (i > 0 && quarts[i] == quarts[i - 1]) k = index[i - 1];
        else {
            while (k < names.size() && *names[k] != quarts[i]) ++k;
            if (k == names.size()) {
                names.push_back(&quarts[i]);
                engine.push_back(resolve(quarts[i]));
            }
        }
        index[i] = static_cast<std::uint16_t>(k);
    }
    // the chunk's own raw quarts answer Chunk::biome for the rest of the generation
    mRawQMin = qMin;
    mRawNq   = nq;
    mRawQuarts.assign(static_cast<std::size_t>(16 * nq), std::string());
    for (int qx = 0; qx < 4; ++qx)
        for (int qz = 0; qz < 4; ++qz)
            for (int i = 0; i < nq; ++i)
                mRawQuarts[static_cast<std::size_t>((qx * 4 + qz) * nq + i)] = quarts[static_cast<std::size_t>(((qx + 1) * 6 + (qz + 1)) * nq + i)];

    int  qx0 = mX * 4 - 1, qz0 = mZ * 4 - 1, qMax = qMin + nq - 1;
    auto at  = [&](int qx, int qy, int qz) {
        return index[static_cast<std::size_t>(((qx - qx0) * 6 + (qz - qz0)) * nq + (std::clamp(qy, qMin, qMax) - qMin))];
    };
    if (engine.size() == 1) { // one biome around the chunk: every block has it
        if (engine[0])
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz) mChunk._setBiome(*engine[0], cpos(lx, mMinY, lz, mMinY), true);
        return;
    }
    std::vector<std::uint16_t> column(static_cast<std::size_t>(mMaxY - mMinY));
    std::vector<int>           counts(engine.size());
    for (int lx = 0; lx < 16; ++lx)
        for (int lz = 0; lz < 16; ++lz) {
            int x = mX * 16 + lx, z = mZ * 16 + lz;
            int l = (x - 2) >> 2, j1 = (z - 2) >> 2; // BiomeManager#getBiome's cell
            std::fill(counts.begin(), counts.end(), 0);
            for (int y = mMinY; y < mMaxY; ++y) {
                int           i1 = (y - 2) >> 2;
                std::uint16_t a  = at(l, i1, j1);
                bool          same = true;
                for (int c = 1; c < 8 && same; ++c) same = at(l + ((c >> 2) & 1), i1 + ((c >> 1) & 1), j1 + (c & 1)) == a;
                if (!same) {
                    // the 8 candidate quarts differ: Java's jittered nearest corner decides
                    auto q = core::biome::zoom(zoomSeed, x, y, z);
                    a      = at(q.x, q.y, q.z);
                }
                column[static_cast<std::size_t>(y - mMinY)] = a;
                ++counts[a];
            }
            // an unknown id keeps what the chunk had there (as a per-block write would): no fill then
            bool          fill     = true;
            std::uint16_t dominant = 0;
            for (std::size_t k = 0; k < engine.size(); ++k) {
                if (counts[k] > 0 && !engine[k]) fill = false;
                if (counts[k] > counts[dominant]) dominant = static_cast<std::uint16_t>(k);
            }
            if (fill) mChunk._setBiome(*engine[dominant], cpos(lx, mMinY, lz, mMinY), true);
            for (int y = mMinY; y < mMaxY; ++y) {
                auto k = column[static_cast<std::size_t>(y - mMinY)];
                if ((fill && k == dominant) || !engine[k]) continue;
                mChunk._setBiome(*engine[k], cpos(lx, y, lz, mMinY), false);
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
  mFallback(std::move(fallback)) {
    // the Twilight Forest's chunks come from this generator while it is installed: they hold the
    // zoomed biome of every block (whoever builds the region)
    auto twilight = world::twilight();
    mZoomed       = twilight && static_cast<int>(*twilight) == dimension && ext::generatorActive();
}

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
        // chunks of this generator hold zoomed biomes: their raw quarts are the biome source's
        // (ChunkAccess#getNoiseBiome: y clamped to the chunk)
        if (mZoomed && mFallback) return mFallback(qx, clampQuartY(qy, minY, maxY), qz);
        int y = clampQuartY(qy, minY, maxY) * 4;
        return biomeIdOf(lc->getBiome(cpos((qx & 3) * 4, y, (qz & 3) * 4, minY)));
    }
    // LevelReader#getNoiseBiome: a chunk that is not there asks the biome source
    if (mFallback) return mFallback(qx, qy, qz);
    BlockPos p{qx * 4, clampQuartY(qy, minY, maxY) * 4, qz * 4};
    return biomeIdOf(mSource.getBiome(p));
}

std::string SourceRegionAdapter::biome(BlockPos const& pos) const {
    // chunks of this generator store the zoomed biome of every block (what the zoom below gives)
    if (mZoomed && pos.y >= mSource.getMinHeight() && pos.y < mSource.getMaxHeight())
        if (auto const* lc = mSource.getChunk(pos.x >> 4, pos.z >> 4))
            return biomeIdOf(lc->getBiome(cpos(pos.x & 15, pos.y, pos.z & 15, mSource.getMinHeight())));
    return zoomedBiome(mSeed, pos, [this](int qx, int qy, int qz) { return noiseBiome(qx, qy, qz); });
}

bool SourceRegionAdapter::chunkBiomes(int radius, std::vector<std::string>& out) const {
    std::vector<Biome const*> seen;
    std::vector<std::string>  raw; // chunks of this generator: their raw quart biomes
    int                       minY = mSource.getMinHeight(), maxY = mSource.getMaxHeight();
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz) {
            auto const* lc = mSource.getChunk(mCX + dx, mCZ + dz);
            if (!lc) return false;
            if (mZoomed) {
                auto kept = keptChunkBiomes(mDim, mCX + dx, mCZ + dz);
                if (!kept && mFallback) {
                    // not kept (generated before a restart): the biome source over the chunk's quarts
                    kept.emplace();
                    for (int qx = 0; qx < 4; ++qx)
                        for (int qz = 0; qz < 4; ++qz)
                            for (int qy = minY >> 2; qy <= (maxY - 1) >> 2; ++qy) {
                                auto b = mFallback((mCX + dx) * 4 + qx, qy, (mCZ + dz) * 4 + qz);
                                if (std::find(kept->begin(), kept->end(), b) == kept->end()) kept->push_back(std::move(b));
                            }
                    keepChunkBiomes(mDim, mCX + dx, mCZ + dz, *kept); // once per chunk
                }
                if (kept) {
                    for (auto& b : *kept)
                        if (std::find(raw.begin(), raw.end(), b) == raw.end()) raw.push_back(std::move(b));
                    continue;
                }
            }
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
    for (auto const* b : seen) {
        auto id = biomeIdOf(*b);
        if (std::find(raw.begin(), raw.end(), id) == raw.end()) out.push_back(std::move(id));
    }
    for (auto& b : raw) out.push_back(std::move(b));
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
