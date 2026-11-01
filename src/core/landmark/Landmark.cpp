// Neutral stub of the pure landmark functions (F0). F6 replaces this file (rule R18).
#include "core/landmark/Landmark.h"

#include "core/Stub.h"

namespace gf::core::landmark {

ColumnXZ nearestCenter(int chunkX, int chunkZ) {
    GF_STUB("landmark.nearestCenter");
    return {chunkX * 16 + 8, chunkZ * 16 + 8};
}

bool chunkHasCenter(int, int) {
    GF_STUB("landmark.chunkHasCenter");
    return false;
}

int manhattanDistanceFromCenter(int, int) {
    GF_STUB("landmark.manhattanDistanceFromCenter");
    return 0;
}

std::string pickVariety(std::int64_t, int, int) {
    GF_STUB("landmark.pickVariety");
    return {};
}

std::string pickForChunk(std::int64_t, int, int, BiomeAtFn const&) {
    GF_STUB("landmark.pickForChunk");
    return {};
}

} // namespace gf::core::landmark
