// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Extension added by U2 (terrain), rule R18: see Pipeline.h. The only U2 file that includes F5's
// internals (feature/worldgen/Internal.h).
#include "feature/ext/terrain/Pipeline.h"

#include <algorithm>
#include <atomic>
#include <limits>

#include "ll/api/service/Bedrock.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/worldgen/Internal.h"
#include "game/Compat.h"
#include "mod/Log.h"

namespace gf::terrain::ext {

std::vector<std::string> stageStepIds(worldgen::Stage stage) {
    std::vector<std::string> out;
    auto                     p = worldgen::detail::providers();
    if (!p) return out;
    if (auto it = p->stages.find(stage); it != p->stages.end())
        for (auto const& s : it->second) out.push_back(s.id);
    return out;
}

std::vector<std::string> blanketIds() {
    std::vector<std::string> out;
    auto                     p = worldgen::detail::providers();
    if (!p) return out;
    for (auto const& [id, fn] : p->blankets) out.push_back(id);
    return out;
}

// ---- the writers of a pass --------------------------------------------------------------------------------

namespace {

constexpr char kProbeId[] = "terrain.pass_probe";

std::string& readerBlanket() {
    static std::string id;
    return id;
}

struct PassRecord {
    bool        recorded = false;
    int         chunkX = 0, chunkZ = 0;
    PassWriters writers;
};
thread_local PassRecord tPass;

/// The probe: F5 calls it on the Providers object that runs the pass. Its own address tells which
/// one: every snapshot holds its own copy of the probe, so the pass runs on the global snapshot
/// exactly when this copy is the one that snapshot holds.
struct PassProbe {
    void operator()(worldgen::Chunk& chunk, worldgen::Region&) const {
        tPass          = {};
        tPass.recorded = true;
        tPass.chunkX   = chunk.chunkX();
        tPass.chunkZ   = chunk.chunkZ();
        auto snap      = worldgen::detail::providers();
        if (!snap) return;
        auto noise = snap->stages.find(worldgen::Stage::Noise);
        if (noise == snap->stages.end()) return;
        bool mine = false, others = false;
        for (auto const& step : noise->second) {
            if (step.id != kProbeId) others = true;
            else if (step.fn.target<PassProbe>() == this) mine = true;
        }
        if (!mine) return; // a pass on another Providers object: its steps are unknown
        tPass.writers.known   = true;
        tPass.writers.noise   = others;
        auto surface          = snap->stages.find(worldgen::Stage::Surface);
        tPass.writers.surface = (surface != snap->stages.end() && !surface->second.empty())
                             || (!snap->blankets.empty() && snap->blankets.begin()->first < readerBlanket());
    }
};

} // namespace

void installPassProbe(std::string readerBlanketId) {
    readerBlanket() = std::move(readerBlanketId);
    worldgen::addStage(worldgen::Stage::Noise, kProbeId, std::numeric_limits<int>::max(), PassProbe{});
}

void beginPass() { tPass = {}; }

PassWriters passWriters(int chunkX, int chunkZ) {
    if (!tPass.recorded || tPass.chunkX != chunkX || tPass.chunkZ != chunkZ) return {};
    return tPass.writers;
}

void generateOnCopyWithNoiseStep(
    std::int64_t                                 seed,
    int                                          chunkX,
    int                                          chunkZ,
    int                                          minY,
    int                                          maxY,
    worldgen::ChunkFn                            extra,
    std::function<void(worldgen::Chunk&)> const& inspect
) {
    auto snap = worldgen::detail::providers();
    if (!snap) return;
    worldgen::detail::Providers p = *snap;
    p.stages[worldgen::Stage::Noise].insert(
        p.stages[worldgen::Stage::Noise].begin(),
        worldgen::detail::Step{"gf_test.terrain_noise_writer", 0, 0, std::move(extra)}
    );
    worldgen::detail::MemoryWorld     world(minY, maxY, seed);
    auto&                             c = worldgen::detail::asChunk(world.chunk(chunkX, chunkZ));
    worldgen::detail::ChunkOnlyRegion region(c, seed, -1, p.biomeSource);
    worldgen::detail::generateChunk(p, c, region, seed);
    if (inspect) inspect(c);
}

// ---- Bedrock's liquid layer --------------------------------------------------------------------------------

namespace {

/// SubChunk's block index (Bedrock stores x, then z, then y).
std::uint16_t subChunkIndex(int lx, int ly, int lz) { return static_cast<std::uint16_t>((lx << 8) | (lz << 4) | ly); }

ChunkBlockPos chunkPos(int lx, int y, int lz, int minY) {
    return ChunkBlockPos{
        static_cast<uchar>(lx),
        ChunkLocalHeight{static_cast<short>(y - minY)},
        static_cast<uchar>(lz)
    };
}

SubChunkStorage<Block> const* layerOf(LevelChunk& lc, int chunkMinY, int y, int layer, int* ly) {
    auto& subs = lc.mSubChunks.get();
    int   rel  = y - chunkMinY;
    if (rel < 0) return nullptr;
    auto idx = static_cast<std::size_t>(rel >> 4);
    if (idx >= subs.size()) return nullptr;
    *ly = rel & 15;
    return subs[idx].mBlocksReadPtr[layer]; // a trivial TypedStorage is the array itself
}

std::atomic<int>& orderVerdict() { // -1 unknown, 0 wrong, 1 right
    static std::atomic<int> v{-1};
    return v;
}
/// Varied chunks whose probes disagreed with LevelChunk::getBlock while the order was unknown.
std::atomic<int>& orderMismatches() {
    static std::atomic<int> v{0};
    return v;
}
/// How many varied chunks must disagree before the order counts as wrong: one disagreement can be a
/// transient (a subchunk storage swapped while it is read); a wrong order disagrees on every chunk.
constexpr int kMismatchesForWrong = 3;

/// Whether the index order above is the game's: the standard layer read through it equals
/// LevelChunk::getBlock at a few positions. A chunk with variety whose probes all agree settles it
/// (until the world closes); kMismatchesForWrong varied chunks that disagree settle it as wrong; a
/// chunk of one block everywhere proves nothing. Until it is settled the layer stays unread.
bool indexOrderVerified(LevelChunk& lc, int chunkMinY, int chunkMaxY) {
    int v = orderVerdict().load();
    if (v >= 0) return v == 1;
    int          checked = 0, wrong = 0;
    bool         varied  = false; // the probed positions themselves hold different blocks
    Block const* first   = nullptr;
    // 16 rows of 4 probes spread over the chunk's whole height
    int const step = std::max(1, (chunkMaxY - chunkMinY) / 16);
    for (int y = chunkMinY; y < chunkMaxY && checked < 64; y += step)
        for (int k = 0; k < 4; ++k) {
            int lx = (y * 7 + k * 5) & 15, lz = (y * 3 + k * 11) & 15, ly = 0;
            auto const* st = layerOf(lc, chunkMinY, y, 0, &ly);
            if (!st) continue;
            Block const& viaIndex = st->getElement(subChunkIndex(lx, ly, lz));
            Block const& viaChunk = lc.getBlock(chunkPos(lx, y, lz, chunkMinY));
            ++checked;
            if (&viaIndex != &viaChunk) ++wrong;
            if (!first) first = &viaChunk;
            else if (first != &viaChunk) varied = true;
        }
    if (!varied || checked == 0)
        return false; // unproven: the layer stays unread in this chunk (one block everywhere carves nothing)
    int expected = -1;
    if (wrong == 0) {
        orderVerdict().compare_exchange_strong(expected, 1);
        return orderVerdict().load() == 1;
    }
    if (orderMismatches().fetch_add(1) + 1 < kMismatchesForWrong) return false; // not yet: maybe a transient
    if (orderVerdict().compare_exchange_strong(expected, 0))
        gf::log().warn(
            "terrain: the subchunk block order is not x-z-y ({} varied chunks disagree, the last in {} of {} probes): "
            "waterlogged blocks stay unseen by the carvers in this world",
            kMismatchesForWrong,
            wrong,
            checked
        );
    return orderVerdict().load() == 1;
}

} // namespace

void resetLiquidLayerOrder() {
    orderVerdict()    = -1;
    orderMismatches() = 0;
}

std::optional<int> gameChunkDimension(worldgen::Chunk& chunk) {
    auto* adapter =
        dynamic_cast<worldgen::detail::LevelChunkAdapter*>(&chunk); // a GloamingForest type, not a game type
    if (!adapter) return std::nullopt;
    return static_cast<int>(adapter->levelChunk().mDimension.getDimensionId());
}

LiquidLayer LiquidLayer::of(worldgen::Chunk& chunk) {
    LiquidLayer out;
    auto*       adapter =
        dynamic_cast<worldgen::detail::LevelChunkAdapter*>(&chunk); // a GloamingForest type, not a game type
    if (!adapter) return out;
    auto& lc = adapter->levelChunk();
    if (!indexOrderVerified(lc, chunk.minY(), chunk.maxY())) return out;
    out.mChunk = &lc;
    out.mMinY  = chunk.minY();
    out.mMaxY  = chunk.maxY();
    return out;
}

Block const* LiquidLayer::at(int lx, int y, int lz) const {
    if (!mChunk || lx < 0 || lz < 0 || lx > 15 || lz > 15 || y < mMinY || y >= mMaxY) return nullptr;
    int         ly = 0;
    auto const* st = layerOf(*mChunk, mMinY, y, 1, &ly);
    if (!st) return nullptr;
    Block const& b = st->getElement(subChunkIndex(lx, ly, lz));
    return features::kit::isAir(b) ? nullptr : &b;
}

void LiquidLayer::clear(int lx, int y, int lz, Block const& air) const {
    if (!at(lx, y, lz)) return;
    gf::game::compat::setExtraBlockSimple(*mChunk, chunkPos(lx, y, lz, mMinY), air);
}

std::unique_ptr<worldgen::Chunk> loadedGameChunk(BlockSource& source, int chunkX, int chunkZ) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    LevelChunk* lc = source.getChunk(chunkX, chunkZ);
    if (!lc) return nullptr;
    return std::make_unique<worldgen::detail::LevelChunkAdapter>(
        *lc,
        level->getBiomeRegistry(),
        source.getMinHeight(),
        source.getMaxHeight()
    );
}

} // namespace gf::terrain::ext
