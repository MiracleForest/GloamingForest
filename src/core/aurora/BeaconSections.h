// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/AuroralizedGlassBlock.java (getBeaconColorMultiplier, as the
//   NeoForge BeaconBlockEntity#tick of upstream's Minecraft 1.21.1 applies it)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The beacon beam's colour sections (pure, gtested): BeaconBlockEntity#tick's colour rule, which
// the client-only beacon extension (src/feature/ext/aurora/BeaconColor) applies to Bedrock's beam.

#include <optional>
#include <vector>

namespace gf::core::aurora {

struct BeamSection {
    unsigned rgb;    ///< 0xRRGGBB
    int      height; ///< blocks
};

/// FastColor.ARGB32#average of two opaque colours (each channel (a + b) / 2, integer).
[[nodiscard]] unsigned averageColor(unsigned a, unsigned b);

/// BeaconBlockEntity#tick over the blocks above a beacon (index 0 = one above it): each entry is
/// the block's colour multiplier, or nullopt for a block without one (it lengthens the current
/// section). The first section is the beacon's own (`beaconRgb`, height 0: Bedrock counts the
/// blocks above the beacon); the first coloured block replaces it with a new section of its colour
/// (sections.size() <= 1), a further colour equal to the current section's lengthens it, any other
/// starts a section of the average of the current section's colour and the new one.
[[nodiscard]] std::vector<BeamSection> beaconSections(std::vector<std::optional<unsigned>> const& above, unsigned beaconRgb);

} // namespace gf::core::aurora
