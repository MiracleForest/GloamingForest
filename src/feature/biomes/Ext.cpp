// U1 `biomes`: the server half of feature/ext/biomes/Biomes.h.

#include "feature/ext/biomes/Biomes.h"

#include "feature/biomes/Biomes.h"

namespace gf::biomes::ext {

std::optional<DensityData> sampleTerrain(int blockX, int blockZ) {
    auto p = feature::biomes::provider();
    if (!p) return std::nullopt;
    auto d = p->sampleTerrain(blockX, blockZ);
    return DensityData{d.depth, d.scale};
}

std::string keyBiome(int quartX, int quartZ) {
    auto p = feature::biomes::provider();
    return p ? p->keyBiome(quartX, quartZ) : std::string{};
}

} // namespace gf::biomes::ext
