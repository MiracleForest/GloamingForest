// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFClientConfig.java
//   src/main/java/twilightforest/config/TFConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), the unit's own header (server and client parts of the unit only; other units
// talk to it by string: the net channels below and the fog definition ids).
//
// - Config section "atmosphere" (rule R13): TF's client key auroraBiomes, plus switches for the
//   client mod's sky, fog, weather and aurora (GloamingForest additions, all on by default).
// - Net channels (rule R15, server -> client mod):
//   - "atmosphere.ur_ghast" {"active": bool}: the Ur-Ghast near the player is in its tantrum (U26
//     sends it on change; TF's UrGhast sets TFWeatherRenderer.urGhastAlive every client tick while
//     it is alive and in its tantrum). The client raises its Ur-Ghast rain while it is active.
//   - "atmosphere.preview" {...}: development previews of the e2e scenarios (a biome, locked
//     biomes, protected boxes, the aurora) sent by the atmosphere.show_* selftests to players
//     tagged gf_atmosphere_show; {} clears it.
// - Resource pack fog definitions (assets/pack/rp/fogs/atmosphere/): "twilightforest:<biome>" for
//   each of TF's 22 biomes (the biome's fog colour, the vanilla distances; U1's client biomes
//   reference them by that id), plus the near fogs "twilightforest:dark_forest_near" and
//   "twilightforest:dark_forest_center_near". The spooky forest's own fog is the near fog.
//   Vanilla clients get them through PlayerFogPacket (fogStackFor).

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gf::feature::atmosphere {

struct AtmosphereConfig {
    int version = 1;
    /// TFClientConfig "auroraBiomes" (config.twilightforest.aurora_biomes): the biomes the aurora
    /// shows in; an empty list disables it (TFConfig#getValidAuroraBiomes: invalid ids are warned
    /// about and skipped).
    std::vector<std::string> auroraBiomes{"twilightforest:glacier"};
    /// Client mod switches (GloamingForest): TF's sky (no sun or moon, bright stars), its fog
    /// (FogHandler, fog colours), the locked-biome weather and the aurora.
    bool sky     = true;
    bool fog     = true;
    bool weather = true;
    bool aurora  = true;
};

inline constexpr char kChannelUrGhast[] = "atmosphere.ur_ghast";
inline constexpr char kChannelPreview[] = "atmosphere.preview";
inline constexpr char kShowTag[]        = "gf_atmosphere_show";

/// The fog stack a vanilla client gets in the Twilight Forest (PlayerFogPacket): the biome's fog
/// definition, the near fog of the dark forests below y 20 (TwilightForestRenderInfo#isFoggyAt);
/// empty for a biome that is not TF's.
[[nodiscard]] std::vector<std::string> fogStackFor(std::string_view biomeId, double y);

/// The ids of the 24 fog definitions this unit ships.
[[nodiscard]] std::vector<std::string> fogDefinitionIds();

} // namespace gf::feature::atmosphere
