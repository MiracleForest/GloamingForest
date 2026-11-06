// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFMapDecorations.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U21 (lich), client mod: the lich tower's magic map icon (TFMapDecorations.LICH_TOWER, the
// sprite twilightforest:map/decorations/lich_tower). The magic map (U13) draws the decoration
// type twilightforest:lich_tower with this texture.

#ifdef GF_CLIENT

#include <string>

#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace gf::client::lich {

namespace {

constexpr char const* kDecoration = "twilightforest:lich_tower";
constexpr char const* kTexture    = "textures/twilightforest/map/decorations/lich_tower";

class LichMapIconModule {
public:
    void install() { gf::client::maps::setIcon(kDecoration, kTexture); }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::lich

GF_REGISTER_MODULE(gf::client::lich::LichMapIconModule);

// The lich tower's map decoration has its icon on the client.
GF_REGISTER_SELFTEST("lich.map_icon", [](gf::selftest::Context& ctx) {
    ctx.check(gf::client::maps::iconOf("twilightforest:lich_tower") == "textures/twilightforest/map/decorations/lich_tower",
              "twilightforest:lich_tower draws textures/twilightforest/map/decorations/lich_tower");
});

#endif
