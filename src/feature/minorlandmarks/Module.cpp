// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// The U18 module (minorlandmarks): the hedge, the hollow hills, the hedge maze, the hollow trees,
// the fallen trunks and TF's decorative feature types.

#include "feature/minorlandmarks/Internal.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::minorlandmarks {

struct MinorLandmarksModule {
    void install() {
        static bool registered = false;
        if (!registered) {
            registered = true;
            installLandmarks();
            installHollowTrees();
            installFallenTrunks();
            installFeatures();
        }
        installHedge();
        gf::log().info("minorlandmarks: installed");
    }
    void uninstall() { uninstallHedge(); }
};

} // namespace gf::feature::minorlandmarks

GF_REGISTER_MODULE(gf::feature::minorlandmarks::MinorLandmarksModule);
