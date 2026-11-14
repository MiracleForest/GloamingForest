// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// The U19 module (questcamps): the Questing Ram and its quest, the quest grove, the camps and upstream's
// template features (druid huts, wells, graveyards, grove ruins, stone circles).

#include <utility>

#include "feature/ext/questcamps/W0Internals.h"
#include "feature/questcamps/Questcamps.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::questcamps {

struct QuestcampsModule {
    void install() {
        static bool once = false;
        if (!std::exchange(once, true)) gf::feature::item::data::onReload([] { reloadQuest(); });
        reloadQuest();
        installRam();
        installGrove();
        installCamp();
        installFeatures();
        gf::log().info("questcamps: registered the quest grove, camps and template features");
    }
    void uninstall() { uninstallRam(); }
};

} // namespace gf::feature::questcamps

GF_REGISTER_CONFIG("questcamps", gf::feature::questcamps::QuestcampsConfig);
GF_REGISTER_MODULE(gf::feature::questcamps::QuestcampsModule);
