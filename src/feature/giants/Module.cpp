// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U29 (giants): the unit's module and its config section.
#include <random>

#include "feature/giants/Giants.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::giants {

GF_REGISTER_CONFIG("giants", GiantsConfig);

GiantsConfig const& config() { return gf::config::get<GiantsConfig>(); }

core::jrand::RandomSource& random() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 20 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

namespace {

class GiantsModule {
public:
    void install() {
        static bool done = false;
        if (done) return;
        done = true;
        installBeanstalk();
        installClouds();
        installGiantBlocks();
        installMobs();
        installStructure();
        gf::log().info("giants: beanstalk, clouds, giant blocks, giants and the cloud castle registered");
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::giants

GF_REGISTER_MODULE(gf::feature::giants::GiantsModule);
