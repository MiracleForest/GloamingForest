// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFClientConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), server side of the aurora biome list editor: TFClientConfig's auroraBiomes entry
// ("Aurora Shader Biomes", config.twilightforest.aurora_biomes) is a list edited with an "Edit
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
                        trFor(p, "config.twilightforest.aurora_biomes", "Aurora Shader Biomes"),
                        trFor(p, "config.twilightforest.aurora_biomes.tooltip", "Defines which biomes the aurora shader effect will appear in. Leave the list empty to disable the effect.") + "\n\n"
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
