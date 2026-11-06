// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFMapDecorations.java (HYDRA_LAIR)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only. The map decoration twilightforest:hydra_lair (U24's): its sprite
// textures/map/decorations/hydra_lair.png (imported by F1 to textures/twilightforest/map/...), drawn
// by F12's map decoration renderer wherever a TF map (U13's magic map) carries the decoration.
// Vanilla clients get the nearest vanilla decoration from the magic map's own mapping (U13).

#ifdef GF_CLIENT

#include "client/world/Sky.h"
#include "mod/Module.h"

namespace gf::client::hydra {

inline constexpr char kLairIcon[]        = "twilightforest:hydra_lair";
inline constexpr char kLairIconTexture[] = "textures/twilightforest/map/decorations/hydra_lair";

class MapIconModule {
public:
    void install() { gf::client::maps::setIcon(kLairIcon, kLairIconTexture); }
    void uninstall() {}
};

} // namespace gf::client::hydra

GF_REGISTER_MODULE(gf::client::hydra::MapIconModule);

#endif // GF_CLIENT
