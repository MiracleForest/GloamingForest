#pragma once

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/TFCommonConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "items" (F9, docs/spec/config.md): config/items.json.

namespace gf::feature::item {

struct ItemsConfig {
    int version = 1;
    /// upstream's common.showEnchantmentsOnItems (config.gloamingforest.default_item_enchantments; world
    /// restart): the creative catalog's ironwood, steeleaf, yeti, naga and mazebreaker gear carries
    /// its default enchantments (TFCreativeTabs#generateGearWithEnchants).
    bool showEnchantmentsOnItems = true;
};

} // namespace gf::feature::item
