// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ClientGameEvents.java (setMusicInDimension)
//   src/main/java/twilightforest/world/registration/biomes/BiomeHelper.java (backgroundMusic)
//   src/main/java/twilightforest/init/TFConfiguredFeatures.java (TFMUSICTYPE)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), client mod: the Twilight Forest's music. Every TF biome's background music is
// Music(music.twilightforest.twilight_forest, 1200, 12000, replace = true); in the dimension the
// client also plays it where vanilla would pick the creative or underwater music
// (setMusicInDimension). Through F12's music provider the game plays the music definition
// "twilightforest:twilight_forest" (rp/music_definitions.d/provisions.json: the event, min delay
// 60 s, max delay 600 s) in the dimension and in any TF biome.
#ifdef GF_CLIENT

#include <optional>
#include <string>

#include "client/world/Sky.h"
#include "feature/dimension/World.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::provisions {

namespace {

inline constexpr char kMusicDefinition[] = "twilightforest:twilight_forest";

std::optional<std::string> pick(std::string const& biomeId, int dimension) {
    bool inTwilight = biomeId.starts_with("twilightforest:");
    if (!inTwilight)
        if (auto tf = ::gf::world::twilight()) inTwilight = dimension == static_cast<int>(*tf);
    if (!inTwilight) return std::nullopt;
    return std::string(kMusicDefinition);
}

class ProvisionsClientModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        ::gf::client::music::setProvider(&pick);
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::provisions

GF_REGISTER_MODULE(gf::client::provisions::ProvisionsClientModule);

#endif
