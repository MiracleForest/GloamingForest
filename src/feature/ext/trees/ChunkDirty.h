#pragma once

// Contract extension of "worldgen" for U3 (rule R18: new file; A8 folds it into F5's
// src/feature/worldgen/Worldgen.h or src/game/). Java's LevelChunk#setUnsaved(true) for a change that
// edits no block: upstream's transformation core rewrites a column's biomes (TransLogCoreBlock), and the
// chunk must be saved with them.
//
// Bedrock 26.51 has no callable way to mark a chunk's data unsaved without a block edit (checked in
// the LeviLamina 26.51.7 headers: LevelChunk, ChunkSource, LevelChunkSaveManagerProxy and
// DirtyTicksCounter export no setter, touch or dirty mark; LevelChunk#_setBiome does not touch a
// counter). The chunk keeps one DirtyTicksCounter {totalTime, lastChange} per LevelChunkDataField
// (mFullChunkDirtyTicksCounters); measured on BDS 1.26.51 (trees.transformation_core_biome):
//   - a field that never changed keeps totalTime < 0 (a clean counter);
//   - the engine's own touch on a change sets lastChange = totalTime (starting a clean counter at 0)
//     and the counter then counts ticks;
//   - LevelChunk#nonActorDataNeedsSaving(wait, maxWait) (MCAPI) turns true for a touched
//     BiomeState counter, and the level storage then writes the chunk's Data3D (biomes) on its next
//     save pass (ChunkSource#_saveDirtyChunks).
// markBiomesUnsaved does exactly that touch on the BiomeState counter and nothing else; it is the
// only place in U3 that writes engine counter fields. A8: replace it with an exported touch when
// one appears, or fold it into F5's contract.

class LevelChunk;

namespace gf::feature::ext::trees {

/// LevelChunk#setUnsaved(true) for a biome-only change (the BiomeState field's counter is touched).
void markBiomesUnsaved(LevelChunk& chunk);

/// Whether the chunk's BiomeState counter was touched and the chunk's non-actor data now needs
/// saving (selftests).
[[nodiscard]] bool biomesUnsaved(LevelChunk const& chunk);

} // namespace gf::feature::ext::trees
