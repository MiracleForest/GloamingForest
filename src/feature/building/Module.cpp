// U5 (building): the unit's module (building blocks and boss collectibles) and its config section.

#include "feature/building/Building.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("building", gf::feature::building::BuildingConfig);

namespace gf::feature::building {

namespace {

struct BuildingModule {
    // TF's banner patterns join Bedrock's registry before the game hands out pattern pointers
    // (spike S6: an append later is only safe with spare capacity)
    void load() {
        int n = registerBannerPatterns();
        log().info("building: {} banner patterns registered", n);
    }
    void install() {
        if (!bannerPatternsRegistered()) log().warn("building: banner patterns missing from the registry: {} appended now", registerBannerPatterns());
        installBannerHooks();
        installTrophies();
        installNagastone();
        installRope();
        installStorage();
        log().info("building: installed");
    }
    void uninstall() {
        uninstallBannerHooks();
        uninstallTrophies();
        uninstallNagastone();
        uninstallRope();
    }
};

} // namespace

} // namespace gf::feature::building

GF_REGISTER_MODULE(gf::feature::building::BuildingModule);
