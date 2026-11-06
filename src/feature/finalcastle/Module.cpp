// U30 (finalcastle): the unit's module: the castle blocks, the thornlands, the castle's mobs and the
// final castle structure.
#include "feature/finalcastle/Parts.h"
#include "mod/Module.h"

namespace gf::feature::finalcastle {

class FinalCastleModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        blocks::install();
        mobs::install();
        castle::install();
    }
    void uninstall() {}
};

} // namespace gf::feature::finalcastle

GF_REGISTER_MODULE(gf::feature::finalcastle::FinalCastleModule);
