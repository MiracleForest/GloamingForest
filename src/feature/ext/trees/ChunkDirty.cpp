// U3 trees: gf::feature::ext::trees::markBiomesUnsaved (see ChunkDirty.h for the measured semantics).
#include "feature/ext/trees/ChunkDirty.h"

#include "mc/world/level/chunk/DirtyTicksCounter.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkDataField.h"

namespace gf::feature::ext::trees {

namespace {
DirtyTicksCounter& biomeCounter(LevelChunk& chunk) { return chunk.mFullChunkDirtyTicksCounters[static_cast<int>(LevelChunkDataField::BiomeState)].get(); }
} // namespace

void markBiomesUnsaved(LevelChunk& chunk) {
    auto& counter = biomeCounter(chunk);
    if (counter.totalTime < 0) counter.totalTime = 0; // a clean counter starts counting
    counter.lastChange = counter.totalTime;           // the change is now
}

bool biomesUnsaved(LevelChunk const& chunk) {
    auto const& counter = chunk.mFullChunkDirtyTicksCounters[static_cast<int>(LevelChunkDataField::BiomeState)].get();
    return counter.totalTime >= 0 && counter.lastChange == counter.totalTime && chunk.nonActorDataNeedsSaving(0, 0);
}

} // namespace gf::feature::ext::trees
