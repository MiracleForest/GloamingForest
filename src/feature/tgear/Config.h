// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/GloamingClientConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "tgear" (docs/spec/config.md): upstream's client keys of Travellers' Gear.
#pragma once

namespace gf::feature::tgear {

struct ItemDisplayConfig {
    /// client."Item Display Modifier Settings".screenOffsetX / screenOffsetY / screenScale
    int    screenOffsetX = 4;
    int    screenOffsetY = 4;
    double screenScale   = 1.0;
    /// twentyFourHourFormat: upstream defaults to the locale's clock (GloamingConfig.use24HourTimeDefault);
    /// GloamingForest's file default is 24 h, the client picks 12 h for English (US) locales while
    /// `followLocale` is on.
    bool twentyFourHourFormat = true;
    bool followLocale         = true;
};

struct TgearConfig {
    int version = 1;
    /// client.firstPersonGloveOverlay
    bool              firstPersonGloveOverlay = true;
    ItemDisplayConfig itemDisplay;
};

} // namespace gf::feature::tgear
