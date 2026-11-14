// U6 flora: the module (plants, bushes, critters, jars, feature types) and the config section
// "flora" (upstream's client cicada switches, docs/spec/config.md).

#include "feature/flora/Flora.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/level/Level.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("flora", gf::feature::flora::FloraConfig);

namespace gf::feature::flora {

FloraConfig const& config() { return gf::config::get<FloraConfig>(); }

namespace {

struct FloraModule {
    void install() {
        static bool registered = false;
        if (!registered) {
            // handlers and feature types are kept by their contracts across worlds
            registered = true;
            registerFeatures();
            installBushes();
            installPlants();
            installCritters();
            installJars();
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            tick();
            if (auto level = ll::service::getLevel()) tickCritters(*level);
        });
        gf::log().info("flora: installed (12 bushes, 3 critters, 3 jars, 8 feature types)");
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick = nullptr;
    }
    ll::event::ListenerPtr mTick;
};

} // namespace

} // namespace gf::feature::flora

GF_REGISTER_MODULE(gf::feature::flora::FloraModule);
