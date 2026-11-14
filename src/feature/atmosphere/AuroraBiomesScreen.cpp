// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/GloamingClientConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), server side of the aurora biome list editor: GloamingClientConfig's auroraBiomes entry
// ("Aurora Shader Biomes", config.gloamingforest.aurora_biomes) is a list edited with an "Edit
// Biomes" button (.button) and explained by its tooltip (.tooltip). The setting belongs to the
// client: a companion gets the native editor (client/atmosphere/AuroraBiomesScreen.cpp), which edits
// the client's own config/atmosphere.json (a client joined to a remote server included). A vanilla
// client draws no aurora, so its form only explains the setting.
//
// The screen type is "atmosphere.aurora_biomes" (screens::open(player, "atmosphere.aurora_biomes")).

#include "feature/common/Screens.h"
#include "feature/ui/Ui.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

#include "mc/world/actor/player/Player.h"

namespace gf::feature::atmosphere {

namespace {

class AuroraBiomesScreenModule {
public:
    void install() {
        screens::registerType({
            .id   = "atmosphere.aurora_biomes",
            .data = [](Player&, nlohmann::json const&) -> std::optional<nlohmann::json> { return nlohmann::json::object(); },
            .action = [](Player&, nlohmann::json const&, nlohmann::json const&) {},
            .form =
                [](Player& p, nlohmann::json const&) {
                    gf::ui::form(
                        p,
                        trFor(p, "config.gloamingforest.aurora_biomes", "Aurora Shader Biomes"),
                        trFor(p, "config.gloamingforest.aurora_biomes.tooltip", "Defines which biomes the aurora shader effect will appear in. Leave the list empty to disable the effect.") + "\n\n"
                            + trFor(p, "gf.atmosphere.aurora_biomes.client_mod", "The aurora is drawn by the GloamingForest client mod; its biomes are set in the client's own configuration."),
                        {}
                    );
                },
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::atmosphere

GF_REGISTER_MODULE(gf::feature::atmosphere::AuroraBiomesScreenModule);
