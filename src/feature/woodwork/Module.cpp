// Unit U4 (woodwork): the module that installs the unit's parts (banisters, drying racks, hollow
// logs, boats). The eight wood sets themselves are F11 family data.

#include "feature/woodwork/Woodwork.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::woodwork {

namespace {

class WoodworkModule {
public:
    void install() {
        installBanisters();
        installDryingRacks();
        installHollowLogs();
        installBoats();
        installBookshelves();
        log().info("woodwork: installed ({} banisters, {} drying racks, {} hollow log sets)", banisterIds().size(), dryingRackIds().size(), hollowLogs().size());
    }
    void uninstall() {
        uninstallBookshelves();
        uninstallBoats();
        uninstallHollowLogs();
        uninstallDryingRacks();
        uninstallBanisters();
    }
};

} // namespace

} // namespace gf::feature::woodwork

GF_REGISTER_MODULE(gf::feature::woodwork::WoodworkModule);
