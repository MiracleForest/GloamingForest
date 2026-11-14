// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U16 (fauna): installs the passive and ambient fauna (boar, bighorn sheep, deer, penguin, tiny bird,
// raven, squirrel, dwarf rabbit, kobold, mosquito swarm), their spawn rules, the prey goals of
// vanilla predators and the parrots' imitations of upstream's mobs.

#include "feature/fauna/Fauna.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::fauna {

struct FaunaModule {
    void install() {
        installAnimals();
        installBirds();
        installMonsters();
        installWorld();
        log().info("fauna: installed (10 mobs, spawn rules, prey, parrot imitations)");
    }
    void uninstall() {
        uninstallMonsters();
        uninstallAnimals();
    }
};

} // namespace gf::feature::fauna

GF_REGISTER_MODULE(gf::feature::fauna::FaunaModule);
