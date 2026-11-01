// Neutral stub of the "features" contract (F0). F5 replaces this file (rule R18).
#include "feature/features/Features.h"

#include "core/Stub.h"

namespace gf::features {

void registerType(std::string const&, FeatureFn) { GF_STUB("features.registerType"); }
void registerPlacement(std::string const&, PlacementFn) { GF_STUB("features.registerPlacement"); }
void registerTrunkPlacer(std::string const&, TrunkPlacerFn) { GF_STUB("features.registerTrunkPlacer"); }
void registerFoliagePlacer(std::string const&, FoliagePlacerFn) { GF_STUB("features.registerFoliagePlacer"); }
void registerTreeDecorator(std::string const&, TreeDecoratorFn) { GF_STUB("features.registerTreeDecorator"); }
void registerRootPlacer(std::string const&, RootPlacerFn) { GF_STUB("features.registerRootPlacer"); }
void registerStateProvider(std::string const&, StateProviderFn) { GF_STUB("features.registerStateProvider"); }

bool place(std::string const&, Region&, BlockPos const&, RandomSource&) {
    GF_STUB("features.place");
    return false;
}

bool placeJson(Json const&, Region&, BlockPos const&, RandomSource&) {
    GF_STUB("features.placeJson");
    return false;
}

void        LargeWriter::setBlock(int, BlockPos const&, Block const&) { GF_STUB("features.LargeWriter"); }
std::size_t LargeWriter::pending(int) { return 0; }

} // namespace gf::features
