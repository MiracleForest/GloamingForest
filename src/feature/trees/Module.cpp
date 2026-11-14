// U3 trees: the module (feature types, tree placers, the cores, the leaves) and the config section
// "trees" (upstream's Magic Trees ranges, docs/spec/config.md).
#include "feature/trees/Trees.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("trees", gf::feature::trees::TreesConfig);

namespace gf::feature::trees {

TreesConfig const& config() { return gf::config::get<TreesConfig>(); }

namespace {

struct TreesModule {
    void install() {
        static bool registered = false;
        if (!registered) {
            // feature types and tree parts are registered once (the features contract keeps them)
            registered = true;
            registerTreePlacers();
            registerTreeFeatures();
            registerMushrooms();
        }
        installCores();
        installLeaves();
        gf::log().info("trees: installed (13 feature types, 7 tree placers, 4 magic cores)");
    }
    void uninstall() {
        uninstallLeaves();
        uninstallCores();
    }
};

} // namespace

} // namespace gf::feature::trees

GF_REGISTER_MODULE(gf::feature::trees::TreesModule);
