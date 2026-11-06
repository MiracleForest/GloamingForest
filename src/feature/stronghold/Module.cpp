// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U25 (stronghold): the unit's module: the knight stronghold structure, the trophy pedestal and the
// stronghold shield, the Knight Phantoms with their thrown weapons, the goblins and the helmet crab.
#include "feature/stronghold/Blocks.h"
#include "feature/stronghold/Goblins.h"
#include "feature/stronghold/Phantom.h"
#include "feature/stronghold/Structure.h"
#include "mod/Module.h"

namespace gf::feature::stronghold {

class StrongholdModule {
public:
    void install() {
        // the dispatches of interactions / blockevents / entity / combat have no removal: register
        // once per process (the client enables the mod once, BDS too)
        static bool registered = false;
        if (registered) return;
        registered = true;
        structure::install();
        blocks::install();
        phantom::install();
        goblins::install();
    }
    void uninstall() {}
};

} // namespace gf::feature::stronghold

GF_REGISTER_MODULE(gf::feature::stronghold::StrongholdModule);
