// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Extension added by U2 (terrain), rule R18: see Pipeline.h.
#include "feature/ext/terrain/Pipeline.h"

#include <atomic>

#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"

#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/worldgen/Internal.h"
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

namespace {

/// SubChunk's block index (Bedrock stores x, then z, then y).
std::uint16_t subChunkIndex(int lx, int ly, int lz) { return static_cast<std::uint16_t>((lx << 8) | (lz << 4) | ly); }

SubChunkStorage<Block> const* layerOf(LevelChunk& lc, int chunkMinY, int y, int layer, int* ly) {
    auto& subs = lc.mSubChunks.get();
    int   rel  = y - chunkMinY;
    if (rel < 0) return nullptr;
    auto idx = static_cast<std::size_t>(rel >> 4);
    if (idx >= subs.size()) return nullptr;
    *ly = rel & 15;
    return subs[idx].mBlocksReadPtr.get()[layer];
}

/// Whether the index order above is the game's: the standard layer read through it equals
/// LevelChunk::getBlock at a few positions of the first non-uniform subchunk (checked once).
bool indexOrderVerified(LevelChunk& lc, int chunkMinY, int chunkMaxY) {
    static std::atomic<int> verdict{-1}; // -1 unknown, 0 wrong, 1 right
    int v = verdict.load();
    if (v >= 0) return v == 1;
    int checked = 0, wrong = 0;
    for (int y = chunkMinY; y < chunkMaxY && checked < 64; y += 3)
        for (int k = 0; k < 4; ++k) {
            int lx = (y * 7 + k * 5) & 15, lz = (y * 3 + k * 11) & 15, ly = 0;
            auto const* st = layerOf(lc, chunkMinY, y, 0, &ly);
            if (!st) continue;
            Block const& viaIndex = st->getElement(subChunkIndex(lx, ly, lz));
            Block const& viaChunk = lc.getBlock(ChunkBlockPos{static_cast<uchar>(lx), ChunkLocalHeight{static_cast<short>(y - chunkMinY)}, static_cast<uchar>(lz)});
            ++checked;
            if (&viaIndex != &viaChunk) ++wrong;
        }
    // a chunk of one block everywhere proves nothing: decide on one with variety
    bool         varied = false;
    Block const* first  = nullptr;
    for (int y = chunkMinY; y < chunkMaxY && !varied; y += 5) {
        Block const& b = lc.getBlock(ChunkBlockPos{static_cast<uchar>(y & 15), ChunkLocalHeight{static_cast<short>(y - chunkMinY)}, static_cast<uchar>((y * 3) & 15)});
        if (!first) first = &b;
        else if (first != &b) varied = true;
    }
    if (!varied || checked == 0) return wrong == 0 && checked > 0;    int expected = -1;
    verdict.compare_exchange_strong(expected, wrong == 0 ? 1 : 0);
    if (wrong) gf::log().warn("terrain: the subchunk block order is not x-z-y ({} of {} probes differ): waterlogged blocks stay unseen by the carvers", wrong, checked);
    return wrong == 0;
}

} // namespace

bool hasLiquidLayer(worldgen::Chunk const& chunk) { return dynamic_cast<worldgen::detail::LevelChunkAdapter const*>(&chunk) != nullptr; }

Block const* liquidLayerBlock(worldgen::Chunk const& chunk, int lx, int y, int lz) {
    if (lx < 0 || lz < 0 || lx > 15 || lz > 15 || y < chunk.minY() || y >= chunk.maxY()) return nullptr;
    auto const* adapter = dynamic_cast<worldgen::detail::LevelChunkAdapter const*>(&chunk);
    if (!adapter) return nullptr;
    auto& lc = const_cast<worldgen::detail::LevelChunkAdapter*>(adapter)->levelChunk();
    if (!indexOrderVerified(lc, chunk.minY(), chunk.maxY())) return nullptr;
    int         ly = 0;
    auto const* st = layerOf(lc, chunk.minY(), y, 1, &ly);
    if (!st) return nullptr;
    Block const& b = st->getElement(subChunkIndex(lx, ly, lz));
    return features::kit::isAir(b) ? nullptr : &b;
}

void clearLiquidLayer(worldgen::Chunk& chunk, int lx, int y, int lz, Block const& air) {
    if (!liquidLayerBlock(chunk, lx, y, lz)) return;
    auto& lc = static_cast<worldgen::detail::LevelChunkAdapter&>(chunk).levelChunk();
    lc.setExtraBlockSimple(ChunkBlockPos{static_cast<uchar>(lx), ChunkLocalHeight{static_cast<short>(y - chunk.minY())}, static_cast<uchar>(lz)}, air);
}

} // namespace gf::terrain::ext
