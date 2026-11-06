// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U24 (hydra): installs the Hydra, its mortar, the fire jets and smokers, the hydra lair and the
// fire jet feature type.

#include "feature/hydra/Hydra.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::hydra {

struct HydraModule {
    void install() {
        installHydra();
        installMortar();
        installJets();
        installLair();
        log().info("hydra: installed (Hydra, mortar, fire jets, smokers, hydra lair)");
    }
    void uninstall() {
        uninstallLair();
        uninstallJets();
        uninstallMortar();
        uninstallHydra();
    }
};

} // namespace gf::feature::hydra

GF_REGISTER_MODULE(gf::feature::hydra::HydraModule);
