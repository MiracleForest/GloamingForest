// Chunk and region adapters of the generator pipeline (F5): the LevelChunk being generated, a
// BlockSource window (Java's WorldGenRegion) and an in-memory world for timing / determinism runs.
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
    for (int y = mMaxY - 1; y >= mMinY; --y)
        if (heightmapMatches(type, getBlock(lx, y, lz))) return y + 1;
    return mMinY;
}

std::string LevelChunkAdapter::biome(int lx, int y, int lz) const {
    if (y < mMinY) y = mMinY;
    if (y >= mMaxY) y = mMaxY - 1;
    return biomeIdOf(mChunk.getBiome(cpos(lx & 15, y, lz & 15, mMinY)));
}

void LevelChunkAdapter::setBiome(int lx, int y, int lz, std::string const& biomeId) {
    if (y < mMinY || y >= mMaxY) return;
    if (auto const* b = engineBiome(mBiomes, biomeId)) mChunk._setBiome(*b, cpos(lx & 15, y, lz & 15, mMinY), false);
}

void LevelChunkAdapter::setBiomeColumn(int lx, int lz, std::string const& biomeId) {
    auto const* b = engineBiome(mBiomes, biomeId);
    if (!b) return;
    for (int y = mMinY; y < mMaxY; ++y) mChunk._setBiome(*b, cpos(lx & 15, y, lz & 15, mMinY), false);
}

// ==== SourceRegionAdapter ===============================================================================

SourceRegionAdapter::SourceRegionAdapter(BlockSource& source, int cx, int cz, int radius, int minY, int maxY, std::int64_t seed, int dimension)
: mSource(source),
  mCX(cx),
  mCZ(cz),
  mRadius(radius),
  mMinY(minY),
  mMaxY(maxY),
  mDim(dimension),
  mSeed(seed) {}

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

std::string SourceRegionAdapter::biome(BlockPos const& pos) const {
    BlockPos p{pos.x, std::clamp(pos.y, static_cast<int>(mSource.getMinHeight()), mSource.getMaxHeight() - 1), pos.z};
    return biomeIdOf(mSource.getBiome(p));
}

// ==== MemoryWorld =======================================================================================

class MemoryWorld::MemChunk final : public Chunk {
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
            if (heightmapMatches(type, getBlock(lx, y, lz))) return y + 1;
        return minY();
    }
    std::string biome(int lx, int y, int lz) const override {
        y = std::clamp(y, minY(), maxY() - 1);
        return mBiomes[qindex(lx, y, lz)];
    }
    void setBiome(int lx, int y, int lz, std::string const& id) override {
        if (y < minY() || y >= maxY()) return;
        mBiomes[qindex(lx, y, lz)] = id;
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

class MemoryRegion final : public Region {
public:
    MemoryRegion(MemoryWorld& w, int cx, int cz, int r) : mWorld(w), mCX(cx), mCZ(cz), mR(r) {}
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
    std::string  biome(BlockPos const& pos) const override { return mWorld.biome(pos); }
    std::int64_t seed() const override { return mWorld.seed(); }
    int          dimension() const override { return -1; }

private:
    MemoryWorld& mWorld;
    int          mCX, mCZ, mR;
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

std::unique_ptr<Region> MemoryWorld::region(int cx, int cz, int radius) { return std::make_unique<MemoryRegion>(*this, cx, cz, radius); }

} // namespace gf::worldgen::detail
