// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U23 (labyrinth): the unit's module: the labyrinth landmark, the minotaurs and the maze slime, the
// maze slime block and the mazebreaker tool rule.
#include "feature/labyrinth/Blocks.h"
#include "feature/labyrinth/Mobs.h"
#include "feature/labyrinth/Structure.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::labyrinth {

class LabyrinthModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        // the dispatches of interactions / blockevents / entity / combat have no removal for these
        // registrations: register once per process (the client enables the mod once)
        registered = true;
        structure::install();
        mobs::install();
        blocks::install();
        log().info("labyrinth: installed (labyrinth, minotaurs, maze slime, mazebreaker rule)");
    }
    void uninstall() {}
};

} // namespace gf::feature::labyrinth

GF_REGISTER_MODULE(gf::feature::labyrinth::LabyrinthModule);
