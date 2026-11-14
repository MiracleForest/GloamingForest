// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (setMusicInDimension)
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java (backgroundMusic)
//   src/main/java/gloamingforest/init/TFConfiguredFeatures.java (TFMUSICTYPE)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), client mod: the Gloaming Forest's music. Every upstream biome's background music is
// Music(music.gloamingforest.gloaming_forest, 1200, 12000, replace = true); in the dimension the
// client also plays it where vanilla would pick the creative or underwater music
// (setMusicInDimension). Through F12's music provider the game plays the music definition
// "gloamingforest:gloaming_forest" (rp/music_definitions.d/provisions.json: the event, min delay
// 60 s, max delay 600 s) in the dimension and in any upstream biome.
#ifdef GF_CLIENT

#include <optional>
#include <string>

#include "client/world/Sky.h"
#include "feature/dimension/World.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::provisions {

namespace {

inline constexpr char kMusicDefinition[] = "gloamingforest:gloaming_forest";

std::optional<std::string> pick(std::string const& biomeId, int dimension) {
    bool inGloaming = biomeId.starts_with("gloamingforest:");
    if (!inGloaming)
        if (auto tf = ::gf::world::gloaming()) inGloaming = dimension == static_cast<int>(*tf);
    if (!inGloaming) return std::nullopt;
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
