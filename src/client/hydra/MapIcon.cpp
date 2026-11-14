// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/GloamingMapDecorations.java (HYDRA_LAIR)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only. The map decoration gloamingforest:hydra_lair (U24's): its sprite
// textures/map/decorations/hydra_lair.png (imported by F1 to textures/gloamingforest/map/...), drawn
// by F12's map decoration renderer wherever an upstream map (U13's magic map) carries the decoration.
// Vanilla clients get the nearest vanilla decoration from the magic map's own mapping (U13).

#ifdef GF_CLIENT

#include "client/world/Sky.h"
#include "mod/Module.h"

namespace gf::client::hydra {

inline constexpr char kLairIcon[]        = "gloamingforest:hydra_lair";
inline constexpr char kLairIconTexture[] = "textures/gloamingforest/map/decorations/hydra_lair";

class MapIconModule {
public:
    void install() { gf::client::maps::setIcon(kLairIcon, kLairIconTexture); }
    void uninstall() {}
};

} // namespace gf::client::hydra

GF_REGISTER_MODULE(gf::client::hydra::MapIconModule);

#endif // GF_CLIENT
