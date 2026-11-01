// The Twilight Forest's WorldGenerator (F5, spike S2): a VoidGenerator subclass whose loadChunk
// runs the Biomes .. Carvers stages and whose decoration post-processing runs Features .. Spawns
// over the 3x3 chunk neighbourhood. It replaces the generator of the game's custom dimension
// "twilightforest:twilight_forest" (CustomDimension::createGenerator), whatever F4 registers it
// with; ext::createGenerator hands it to another backend.
#include <chrono>
#include <shared_mutex>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/biome/source/BiomeSource.h"
#include "mc/world/level/levelgen/synth/PerlinSimplexNoise.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/ChunkViewSource.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/CustomDimension.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionHeightRange.h"
#include "mc/world/level/levelgen/VoidGenerator.h"

#include "core/jrand/Mth.h"
#include "feature/dimension/World.h"
#include "feature/worldgen/Internal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::worldgen {

namespace detail {

namespace {

std::atomic<int> gActive{0}; ///< live Twilight generators (server and client levels)

int minYOf(Dimension const& d) { return d.mHeightRange->mMin; }
int maxYOf(Dimension const& d) { return d.mHeightRange->mMax; }

/// The chunk a decoration neighbourhood is centered on: the middle of the view's bounds, checked
/// against the chunk states (the center is the one being decorated).
std::optional<std::pair<int, int>> centerOf(ChunkViewSource& view) {
    int minX, minY, minZ, maxX, maxY, maxZ;
    {
        auto&            area = *view.mArea;
        std::shared_lock lock(area.mChunkDataBox.mMutex);
        auto const&      b = area.mChunkDataBox.mContent.mBounds;
        minX = b.mMin->x, minY = b.mMin->y, minZ = b.mMin->z, maxX = b.mMax->x, maxY = b.mMax->y, maxZ = b.mMax->z;
    }
    auto decorating = [&](int x, int z) {
        auto lc = view.getExistingChunk(ChunkPos{x, z});
        return lc && lc->mLoadState->load() == ChunkState::DecorationPostProcessing;
    };
    // a chunk grid may keep its second axis in Pos::z or in Pos::y: try both
    for (auto [lo, hi] : {std::pair{minZ, maxZ}, std::pair{minY, maxY}}) {
        int cx = core::mth::floorDiv(minX + maxX, 2), cz = core::mth::floorDiv(lo + hi, 2);
        if (decorating(cx, cz)) return std::make_pair(cx, cz);
        for (int x = minX; x <= maxX; ++x)
            for (int z = lo; z <= hi; ++z)
                if (decorating(x, z)) return std::make_pair(x, z);
    }
    return std::nullopt;
}

class TwilightGenerator final : public VoidGenerator {
public:
    TwilightGenerator(Dimension& dimension, BiomeIdType defaultBiome) : VoidGenerator(dimension, defaultBiome), mDim(dimension) {
        ++gActive;
    }
    ~TwilightGenerator() override { --gActive; }

    void loadChunk(LevelChunk& lc, bool) override {
        auto start = std::chrono::steady_clock::now();
        int  minY = minYOf(mDim), maxY = maxYOf(mDim);
        try {
            LevelChunkAdapter chunk(lc, mDim.getBiomeRegistry(), minY, maxY);
            ChunkOnlyRegion   region(chunk, currentSeed(), static_cast<int>(mDim.getDimensionId()));
            generateChunk(chunk, region, currentSeed());
        } catch (std::exception const& e) {
            features::detail::warnOnce("generate error", std::string("chunk generation failed: ") + e.what());
        }
        lc.recomputeHeightMap(false);
        lc.tryChangeState(ChunkState::Generating, ChunkState::Generated);
        addStats(false, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    bool structurePostProcessChunk(ChunkViewSource&) override { return true; }

    bool decorationPostProcessChunk(ChunkViewSource& view) override {
        auto start  = std::chrono::steady_clock::now();
        auto center = centerOf(view);
        if (!center) {
            features::detail::warnOnce("no decoration center", "a decoration neighbourhood without a chunk being decorated (skipped)");
            return true;
        }
        auto lc = view.getExistingChunk(ChunkPos{center->first, center->second});
        if (!lc) return true;
        try {
            BlockSource         source(*mLevel, mDim, view, false, true, false, true);
            SourceRegionAdapter region(source, center->first, center->second, 1, minYOf(mDim), maxYOf(mDim), currentSeed(), static_cast<int>(mDim.getDimensionId()));
            // stages write through the region (BlockSource, height cache), never the chunk directly
            decorateChunk(nullptr, region, currentSeed());
        } catch (std::exception const& e) {
            features::detail::warnOnce("decorate error", std::string("chunk decoration failed: ") + e.what());
        }
        addStats(true, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        return true;
    }

private:
    /// The writable region of loadChunk: the chunk itself (Java's ProtoChunk stages).
    class ChunkOnlyRegion final : public Region {
    public:
        ChunkOnlyRegion(Chunk& c, std::int64_t seed, int dim) : mC(c), mSeed(seed), mDimId(dim) {}
        int          centerChunkX() const override { return mC.chunkX(); }
        int          centerChunkZ() const override { return mC.chunkZ(); }
        int          minY() const override { return mC.minY(); }
        int          maxY() const override { return mC.maxY(); }
        bool         isInside(int x, int z) const override { return (x >> 4) == mC.chunkX() && (z >> 4) == mC.chunkZ(); }
        Block const& getBlock(BlockPos const& p) const override {
            if (!isInside(p.x, p.z)) return features::detail::air();
            return mC.getBlock(p.x & 15, p.y, p.z & 15);
        }
        bool setBlock(BlockPos const& p, Block const& b, int) override {
            if (!isInside(p.x, p.z)) return false;
            mC.setBlock(p.x & 15, p.y, p.z & 15, b);
            return true;
        }
        int          height(Heightmap t, int x, int z) const override { return isInside(x, z) ? mC.height(t, x & 15, z & 15) : mC.minY(); }
        std::string  biome(BlockPos const& p) const override { return mC.biome(p.x & 15, p.y, p.z & 15); }
        std::int64_t seed() const override { return mSeed; }
        int          dimension() const override { return mDimId; }

    private:
        Chunk&       mC;
        std::int64_t mSeed;
        int          mDimId;
    };

    Dimension& mDim;
};

bool isTwilightDimension(Dimension const& d) { return *d.mName == world::kDimensionId; }

} // namespace

} // namespace detail

namespace ext {

std::unique_ptr<WorldGenerator> createGenerator(Dimension& dimension) {
    detail::setSeed(detail::currentSeed());
    gf::log().info("worldgen: Twilight Forest generator created for {}", *dimension.mName);
    return std::make_unique<detail::TwilightGenerator>(dimension, dimension.getDefaultBiomeId());
}

bool generatorActive() { return detail::gActive.load() > 0; }

} // namespace ext

namespace detail {

LL_TYPE_INSTANCE_HOOK(
    TwilightGeneratorHook,
    ll::memory::HookPriority::Normal,
    CustomDimension,
    &CustomDimension::$createGenerator,
    std::unique_ptr<WorldGenerator>,
    br::worldgen::StructureSetRegistry const& registry
) {
    if (isTwilightDimension(*this)) {
        gf::log().info("worldgen: Twilight Forest generator installed in custom dimension {}", *mName);
        return std::make_unique<TwilightGenerator>(*this, *mDefaultBiome);
    }
    return origin(registry);
}

class GeneratorModule {
public:
    void load() { hook(); }
    void install() {
        hook();
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] { clearWorldCaches(); });
        }
    }
    void uninstall() {
        if (mHooked) TwilightGeneratorHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        TwilightGeneratorHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace detail

} // namespace gf::worldgen

GF_REGISTER_MODULE_ORDERED(gf::worldgen::detail::GeneratorModule, gf::ModuleOrder::Contracts);
