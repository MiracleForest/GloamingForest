// U5 (building): the unit's module (building blocks and boss collectibles) and its config section.

#include "feature/building/Building.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("building", gf::feature::building::BuildingConfig);

namespace gf::feature::building {

namespace {

struct BuildingModule {
    void install() {
        installTrophies();
        installNagastone();
        installRope();
        installStorage();
        log().info("building: installed");
    }
    void uninstall() {
        uninstallTrophies();
        uninstallNagastone();
        uninstallRope();
    }
};

} // namespace

} // namespace gf::feature::building

GF_REGISTER_MODULE(gf::feature::building::BuildingModule);
