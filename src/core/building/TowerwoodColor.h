#pragma once

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/SimplexNoiseHelper.java
//   src/main/java/gloamingforest/util/ColorUtil.java
//   src/main/java/gloamingforest/client/event/ColorHandler.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the towerwood colour (ColorHandler: towerwood, cracked and mossy towerwood; the
// infested one is U26's block) from upstream's own 3D simplex noise (SimplexNoiseHelper, Stefan
// Gustavson's public domain simplex noise in float precision) and ColorUtil#hsvToRGB. Pure C++.

#include <cstdint>

namespace gf::core::building {

/// SimplexNoiseHelper#noise: 3D simplex noise in [-1, 1], in Java's float arithmetic.
[[nodiscard]] float simplex3(float x, float y, float z);
/// SimplexNoiseHelper#getFractalNoise(iteration, size, pos).
[[nodiscard]] float fractalNoise(int iterations, float size, int x, int y, int z);
/// SimplexNoiseHelper#rippleFractalNoise(iterations, size, pos, minimum, maximum, frequency).
[[nodiscard]] float rippleFractalNoise(int iterations, float size, int x, int y, int z, float minimum, float maximum, float frequency);
/// ColorUtil#hsvToRGB: 0xRRGGBB.
[[nodiscard]] std::uint32_t hsvToRgb(float hue, float saturation, float value);
/// ColorHandler's towerwood colour at a block position (0xRRGGBB): f = rippleFractalNoise(2, 32,
/// pos, 0.4, 1, 2), hsvToRGB(0.1, 1 - f, (f + 2) / 3).
[[nodiscard]] std::uint32_t towerwoodColor(int x, int y, int z);

} // namespace gf::core::building
