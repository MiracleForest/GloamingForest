#pragma once

// Contract extension of "worldgen" (F5, rule R18; to be folded into Worldgen.h by A8): what a
// carver gets besides (Chunk, Region, RandomSource), matching Java's ChunkGenerator#applyCarvers.
//
// The Carvers stage of chunk C loops over every source chunk S within 8 chunks of C (17 x 17),
// takes the configured carvers of S's biome (the biome source's noise biome at S's minimum block,
// quart y 0; "carvers" -> "air" of the biome JSON) and, for the carver at index k of that list,
// calls the CarverFn registered for its configured carver id (or, failing that, its "type") once
// with the random set by WorldgenRandom#setLargeFeatureSeed(seed + k, S.x, S.z). The carver then
// runs WorldCarver#isStartChunk(random) itself (upstream's highland caves override it) and carves the
// part of S's tunnels that falls inside C. The context below says which S, k and configured carver
// the call is for and holds C's carving mask (shared by every call for C, like Java's
// ProtoChunk#getOrCreateCarvingMask(AIR)). The mask is kept until the chunk is decorated, where
// the minecraft:carving_mask placement modifier (CarvingMaskPlacement) streams its blocks.
//
//     gf::worldgen::registerCarver("gloamingforest:tf_caves", [](Chunk& c, Region& r, RandomSource& random) {
//         auto& ctx = *gf::worldgen::ext::carverContext();
//         if (!gf::worldgen::ext::isStartChunk(random, ctx)) return;
//         ...  // ctx.sourceChunkX / ctx.sourceChunkZ, ctx.config, ctx.mask
//     });

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/worldgen/Worldgen.h"

namespace gf::worldgen::ext {

/// Java's CarvingMask: the blocks of the chunk being carved that a carver already carved.
class CarvingMask {
public:
    CarvingMask(int minY, int maxY);
    [[nodiscard]] bool get(int localX, int y, int localZ) const;
    void               set(int localX, int y, int localZ);
    [[nodiscard]] int  count() const { return mCount; }
    [[nodiscard]] int  minY() const { return mMinY; }
    /// Calls fn(localX, y, localZ) for every carved block in CarvingMask#stream's order (ascending
    /// index: x, then z, then y).
    void forEach(std::function<void(int localX, int y, int localZ)> const& fn) const;

private:
    [[nodiscard]] std::size_t index(int localX, int y, int localZ) const;
    std::vector<bool> mBits;
    int               mMinY, mMaxY, mCount = 0;
};

struct CarverContext {
    int                   sourceChunkX = 0; ///< the chunk whose seed started the tunnels (Java's chunkPos argument)
    int                   sourceChunkZ = 0;
    int                   index        = 0; ///< the carver's index in the source biome's list
    std::string           configuredId;     ///< "gloamingforest:tf_caves"
    nlohmann::json const* config = nullptr; ///< the configured carver's "config" ({} when unknown)
    CarvingMask*          mask   = nullptr; ///< the carving mask of the chunk being carved
};

/// The context of the CarverFn call running on this thread (nullptr outside a carver call).
[[nodiscard]] CarverContext* carverContext();

/// WorldCarver#isStartChunk: random.nextFloat() <= config.probability.
[[nodiscard]] bool isStartChunk(RandomSource& random, CarverContext const& context);

} // namespace gf::worldgen::ext
