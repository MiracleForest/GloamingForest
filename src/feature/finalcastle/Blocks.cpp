// U30 (finalcastle): the blocks part: castle doors (Door.cpp), force fields (ForceField.cpp), thorns
// and the thorn rose (Thorns.cpp), the thorns feature (ThornFeature.cpp) and the cinder furnace
// (CinderFurnace.cpp), on the scheduled-tick / device runtime of BlocksCommon.cpp. Module.cpp calls
// install() once per process (the interaction and block event registries have no removal).
#include "feature/finalcastle/BlocksCommon.h"
#include "feature/finalcastle/Parts.h"

namespace gf::feature::finalcastle::blocks {

void install() {
    installRuntime();
    installDoors();
    installForceFields();
    installThorns();
    installThornFeature();
    installCinderFurnace();
}

} // namespace gf::feature::finalcastle::blocks
