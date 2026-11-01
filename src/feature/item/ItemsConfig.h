#pragma once

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFCommonConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "items" (F9, docs/spec/config.md): config/items.json.

namespace gf::feature::item {

struct ItemsConfig {
    int version = 1;
    /// TF's common.showEnchantmentsOnItems (config.twilightforest.default_item_enchantments; world
    /// restart): the creative catalog's ironwood, steeleaf, yeti, naga and mazebreaker gear carries
    /// its default enchantments (TFCreativeTabs#generateGearWithEnchants).
    bool showEnchantmentsOnItems = true;
};

} // namespace gf::feature::item
