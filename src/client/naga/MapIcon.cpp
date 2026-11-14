// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFMapDecorations.java (NAGA_COURTYARD)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The map decoration type gloamingforest:naga_courtyard (U20): TFMapDecorations registers it as
// MapDecorationType(gloamingforest:naga_courtyard, showOnItemFrame true, mapColor -1,
// explorationMapElement false, trackCount true), drawn with the map_decorations atlas sprite
// textures/map/decorations/naga_courtyard.png (imported by F1 to
// textures/gloamingforest/map/decorations/naga_courtyard). The magic map (U13) puts the
// decoration on the maps where the courtyard's structure_icon says so; F12 draws it with this icon.

#ifdef GF_CLIENT

#include "client/world/Sky.h"
#include "mod/Module.h"

namespace gf::client::naga {

inline constexpr char kDecoration[] = "gloamingforest:naga_courtyard";
inline constexpr char kIcon[]       = "textures/gloamingforest/map/decorations/naga_courtyard";

struct NagaMapIconModule {
    void install() { gf::client::maps::setIcon(kDecoration, kIcon); }
    void uninstall() {}
};

} // namespace gf::client::naga

GF_REGISTER_MODULE(gf::client::naga::NagaMapIconModule);

#endif
