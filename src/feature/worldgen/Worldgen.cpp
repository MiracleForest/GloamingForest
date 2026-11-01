// Neutral stub of the "worldgen" contract (F0). F5 replaces this file (rule R18).
#include "feature/worldgen/Worldgen.h"

#include "core/Stub.h"

namespace gf::worldgen {

void        setBiomeSource(BiomeSourceFn) { GF_STUB("worldgen.setBiomeSource"); }
void        setMainBiomeSource(MainBiomeFn) { GF_STUB("worldgen.setMainBiomeSource"); }
std::string biomeAt(BlockPos const&) {
    GF_STUB("worldgen.biomeAt");
    return {};
}
std::string mainBiomeAt(int, int) {
    GF_STUB("worldgen.mainBiomeAt");
    return {};
}
void         setTerrain(TerrainProvider) { GF_STUB("worldgen.setTerrain"); }
void         setSurface(SurfaceProvider) { GF_STUB("worldgen.setSurface"); }
void         registerCarver(std::string const&, CarverFn) { GF_STUB("worldgen.registerCarver"); }
void         registerBlanket(std::string const&, BlanketFn) { GF_STUB("worldgen.registerBlanket"); }
void         addStage(Stage, std::string const&, int, ChunkFn) { GF_STUB("worldgen.addStage"); }
std::int64_t seed() {
    GF_STUB("worldgen.seed");
    return 0;
}

} // namespace gf::worldgen
