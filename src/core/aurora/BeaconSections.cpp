// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/AuroralizedGlassBlock.java (getBeaconColorMultiplier, as the
//   NeoForge BeaconBlockEntity#tick of TF's Minecraft 1.21.1 applies it)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/aurora/BeaconSections.h"

namespace gf::core::aurora {

unsigned averageColor(unsigned a, unsigned b) {
    auto ch = [&](int shift) { return ((((a >> shift) & 255U) + ((b >> shift) & 255U)) / 2U) << shift; };
    return ch(16) | ch(8) | ch(0);
}

std::vector<BeamSection> beaconSections(std::vector<std::optional<unsigned>> const& above, unsigned beaconRgb) {
    std::vector<BeamSection> out{{beaconRgb & 0xFFFFFFU, 0}};
    for (auto const& c : above) {
        if (!c) {
            out.back().height++;
            continue;
        }
        unsigned rgb = *c & 0xFFFFFFU;
        if (out.size() <= 1) out.push_back({rgb, 1});
        else if (rgb == out.back().rgb) out.back().height++;
        else out.push_back({averageColor(out.back().rgb, rgb), 1});
    }
    return out;
}

} // namespace gf::core::aurora
