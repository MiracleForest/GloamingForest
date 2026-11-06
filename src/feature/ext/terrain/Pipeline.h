// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Extension added by U2 (terrain), rule R18 (folded by A8 in W2b): questions about F5's generator
// pipeline that worldgen's contract cannot answer yet. This file is the only one of U2 that reads
// F5's internals (feature/worldgen/Internal.h: Providers, LevelChunkAdapter, MemoryWorld); A8's fold
// candidates are a "who writes between the providers of this pass" query in worldgen's contract and
// worldgen::Chunk::fluid(lx, y, lz) / clearFluid(lx, y, lz).
//
// - What runs between two providers: the terrain generator hands the block grid it computed in the
//   noise step to the surface step (and the surface step's to the blankets) instead of reading the
//   chunk back, which is only right when nothing else can write the chunk in between: no
//   Stage::Noise step (between setTerrain's provider and setSurface's), no Stage::Surface step and
//   no blanket registered under an id that sorts before the reader (F5 runs the blankets in id
//   order) between setSurface's provider and the blanket. F5 may run a pass on any Providers
//   object (its own selftests build some), so the answer comes from the pass itself: a probe step
//   (the last Stage::Noise step) tells whether the pass runs on F5's current global snapshot, the
//   only one whose steps are known; any other pass counts as unknown.
// - Fluids in Bedrock's liquid layer: a Java block that is waterlogged ("waterlogged=true") keeps
//   its water in the subchunk's extra block layer in Bedrock, which worldgen::Chunk::getBlock does
//   not show. TF's carver asks BlockState#getFluidState, so it must see that water.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/worldgen/Worldgen.h"

class Block;
class BlockSource;
class LevelChunk;

namespace gf::terrain::ext {

/// The ids of the steps registered for `stage` in F5's global providers, in the order F5 runs them.
[[nodiscard]] std::vector<std::string> stageStepIds(worldgen::Stage stage);
/// The ids of the registered chunk blankets, in the order F5 runs them.
[[nodiscard]] std::vector<std::string> blanketIds();

// ---- the writers of a pass ------------------------------------------------------------------------------

/// Registers the probe step (once, at install): the last Stage::Noise step, which records what the
/// running pass holds between the providers; `readerBlanketId` is the blanket that takes the
/// surface grid.
void installPassProbe(std::string readerBlanketId);
/// Forgets this thread's pass (the terrain provider calls it when a chunk's pass begins).
void beginPass();

struct PassWriters {
    bool known   = false; ///< the probe ran in this thread's pass of the chunk on F5's global providers
    bool noise   = false; ///< a Stage::Noise step other than the probe
    bool surface = false; ///< a Stage::Surface step or a blanket F5 runs before the reader
};
/// What can write chunk (chunkX, chunkZ) between the providers in the pass running on this thread
/// (after its Stage::Noise steps). Not `known`: the pass runs on another Providers object, or the
/// probe did not run: treat every stage as written.
[[nodiscard]] PassWriters passWriters(int chunkX, int chunkZ);

/// Selftest support: generates chunk (chunkX, chunkZ) of an in-memory world (y minY .. maxY) on a
/// copy of F5's current global providers with `extra` added as a Stage::Noise step (another unit's
/// writer the global providers do not hold), then calls `inspect` with the chunk.
void generateOnCopyWithNoiseStep(
    std::int64_t                                 seed,
    int                                          chunkX,
    int                                          chunkZ,
    int                                          minY,
    int                                          maxY,
    worldgen::ChunkFn                            extra,
    std::function<void(worldgen::Chunk&)> const& inspect
);

// ---- Bedrock's liquid layer ------------------------------------------------------------------------------

/// The liquid (extra) layer of a chunk the game is generating. Empty (false) for a chunk that is no
/// game chunk (an in-memory test chunk, whose blocks have no liquid layer) and while the subchunk
/// index order is not known to be the game's: the order (x, z, y) is checked against
/// LevelChunk::getBlock on every chunk until a chunk with variety agrees (or three disagree: a wrong
/// order logs one warning and the layer stays unread); the verdict lasts until the world closes.
class LiquidLayer {
public:
    LiquidLayer() = default;
    /// The layer of `chunk` (checks the order as above).
    [[nodiscard]] static LiquidLayer of(worldgen::Chunk& chunk);
    explicit                         operator bool() const { return mChunk != nullptr; }
    /// The block of the layer at a local position (nullptr: none, air, or outside the chunk).
    [[nodiscard]] Block const* at(int lx, int y, int lz) const;
    /// Empties the layer at a local position (Java replacing a waterlogged state replaces its fluid
    /// too; Bedrock's setBlock keeps the liquid layer).
    void clear(int lx, int y, int lz, Block const& air) const;

private:
    LevelChunk* mChunk = nullptr;
    int         mMinY = 0, mMaxY = 0;
};

/// Forgets the subchunk order verdict (the terrain module calls it when a world closes: the client
/// loads many worlds in one process, and each one checks the order again).
void resetLiquidLayerOrder();

/// The dimension (engine id) of a chunk the game is generating; nullopt for a chunk that is no game
/// chunk (an in-memory pass: F5's runInMemory, a test copy of the providers).
[[nodiscard]] std::optional<int> gameChunkDimension(worldgen::Chunk& chunk);

/// Selftest support: a worldgen::Chunk over a chunk `source` holds loaded (the game's LevelChunk, as
/// F5's generator wraps it); nullptr when it is not loaded.
[[nodiscard]] std::unique_ptr<worldgen::Chunk> loadedGameChunk(BlockSource& source, int chunkX, int chunkZ);

} // namespace gf::terrain::ext
