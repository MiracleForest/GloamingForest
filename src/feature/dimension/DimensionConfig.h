// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/GloamingCommonConfig.java
//   src/main/java/gloamingforest/config/GloamingConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Config section "dimension" (rule R13, docs/spec/config.md): upstream's "Dimension Settings" and
// "Portal Settings" keys with upstream's names, defaults and ranges.

#include <string>

namespace gf::feature::dimension {

struct DimensionConfig {
    int version = 1;

    // -- Dimension Settings (config.gloamingforest.dim_settings) --
    /// spawn_in_gf: new players spawn in the Gloaming Forest (and respawn there without a bed).
    bool newPlayersSpawnInGloaming = false;
    /// portal_for_new_player: such players arrive next to a fresh (one-way) portal.
    bool portalForNewPlayer = false;

    // -- Portal Settings (config.gloamingforest.portal_settings) --
    /// origin_dimension: where Gloaming portals lead back to.
    std::string originDimension = "minecraft:overworld";
    /// portals_in_other_dimensions: portals may be lit outside the origin and the Gloaming Forest.
    bool allowPortalsInOtherDimensions = false;
    /// portal_permission: permission level (0..4) needed to light a portal.
    int portalCreationPermission = 0;
    /// disable_portal: portals can't be lit at all.
    bool disablePortalCreation = false;
    /// check_portal_placement: check the destination for safety before lighting (false = check
    /// when lighting and refuse with "It doesn't seem safe here...").
    bool checkPortalPlacement = true;
    /// destructive_portal_lighting: the lightning that lights a portal strikes entities around it.
    bool destructivePortalLightning = true;
    /// portal_return: portals made at the destination can be used to return.
    bool shouldReturnPortalBeUsable = true;
    /// portal_unlocked_by_advancement: advancement id a player needs to light / use portals ("" = none).
    std::string portalUnlockedByAdvancement;
    /// max_portal_size: the largest pool (in water blocks) that lights, 4..2147483647.
    int maxPortalSize = 64;
};

} // namespace gf::feature::dimension
