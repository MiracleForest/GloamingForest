// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U27 (caves): troll caves and the yeti cave. Installs the structures and landmark generators,
// the blocks, the feature types and the mobs.
#include "feature/caves/Caves.h"
#include "feature/caves/Structures.h"
#include "mod/Module.h"

namespace gf::feature::caves {

namespace {

class CavesModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        installStructures();
        installFeatures();
        installBlocks();
        installTroll();
        installYeti();
        installAlphaYeti();
        installFallingIce();
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::caves

GF_REGISTER_MODULE(gf::feature::caves::CavesModule);
