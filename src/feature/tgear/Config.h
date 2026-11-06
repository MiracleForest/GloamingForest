// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFClientConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "tgear" (docs/spec/config.md): TF's client keys of Travellers' Gear.
#pragma once

namespace gf::feature::tgear {

struct ItemDisplayConfig {
    /// client."Item Display Modifier Settings".screenOffsetX / screenOffsetY / screenScale
    int    screenOffsetX = 4;
    int    screenOffsetY = 4;
    double screenScale   = 1.0;
    /// twentyFourHourFormat: TF defaults to the locale's clock (TFConfig.use24HourTimeDefault);
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
