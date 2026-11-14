// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java (the mood settings)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/biomes/Mood.h"

#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)

namespace gf::core::biomes::mood {

bool step(float& moodiness, int sky, int block, Settings const& settings) {
    // Level#getMaxLightLevel() is 15
    if (sky > 0) moodiness -= static_cast<float>(sky) / 15.0F * 0.001F;
    else moodiness -= static_cast<float>(block - 1) / static_cast<float>(settings.tickDelay);
    if (moodiness >= 1.0F) {
        moodiness = 0.0F;
        return true;
    }
    moodiness = std::max(moodiness, 0.0F);
    return false;
}

Vec soundPosition(Vec const& eye, int blockX, int blockY, int blockZ, Settings const& settings) {
    double d0 = blockX + 0.5 - eye.x;
    double d1 = blockY + 0.5 - eye.y;
    double d2 = blockZ + 0.5 - eye.z;
    double d3 = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    double d4 = d3 + settings.offset;
    return Vec{eye.x + d0 / d3 * d4, eye.y + d1 / d3 * d4, eye.z + d2 / d3 * d4};
}

} // namespace gf::core::biomes::mood
