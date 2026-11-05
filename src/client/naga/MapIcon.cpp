// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFMapDecorations.java (NAGA_COURTYARD)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The map decoration type twilightforest:naga_courtyard (U20): TFMapDecorations registers it as
// MapDecorationType(twilightforest:naga_courtyard, showOnItemFrame true, mapColor -1,
// explorationMapElement false, trackCount true), drawn with the map_decorations atlas sprite
// textures/map/decorations/naga_courtyard.png (imported by F1 to
// textures/twilightforest/map/decorations/naga_courtyard). The magic map (U13) puts the
// decoration on the maps where the courtyard's structure_icon says so; F12 draws it with this icon.

#ifdef GF_CLIENT

#include "client/world/Sky.h"
#include "mod/Module.h"

namespace gf::client::naga {

inline constexpr char kDecoration[] = "twilightforest:naga_courtyard";
inline constexpr char kIcon[]       = "textures/twilightforest/map/decorations/naga_courtyard";

struct NagaMapIconModule {
    void install() { gf::client::maps::setIcon(kDecoration, kIcon); }
    void uninstall() {}
};

} // namespace gf::client::naga

GF_REGISTER_MODULE(gf::client::naga::NagaMapIconModule);

#endif
