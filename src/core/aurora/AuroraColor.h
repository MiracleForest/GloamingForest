// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/SimplexNoiseHelper.java
//   src/main/java/twilightforest/util/ColorUtil.java (rgbToHSV, hsvToRGB)
//   src/main/java/twilightforest/client/event/ColorHandler.java (aurora block colours)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
// SimplexNoiseHelper's 3D simplex noise is Stefan Gustavson's public domain example code.
#pragma once

// The aurora blocks' colours and texture variants (pure, gtested): TF's SimplexNoiseHelper (float
// arithmetic kept), ColorUtil's HSV conversions and ColorHandler's aurora tints.

#include <array>

namespace gf::core::aurora {

/// SimplexNoiseHelper#noise (3D simplex, float).
[[nodiscard]] float simplex3(float x, float y, float z);
/// SimplexNoiseHelper#getFractalNoise(iteration, size, pos).
[[nodiscard]] float fractalNoiseSum(int iteration, float size, int x, int y, int z);
/// SimplexNoiseHelper#fractalNoise (the sum divided by the iterations).
[[nodiscard]] float fractalNoise(int iterations, float size, int x, int y, int z);
/// SimplexNoiseHelper#rippleFractalNoise.
[[nodiscard]] float rippleFractalNoise(int iterations, float size, int x, int y, int z, float minimum, float maximum, float frequency);
/// SimplexNoiseHelper#calcVariant(pos, variants): the aurora block's texture variant.
[[nodiscard]] int calcVariant(int x, int y, int z, int variants = 16);

/// ColorUtil#hsvToRGB (0xRRGGBB).
[[nodiscard]] unsigned hsvToRGB(float hue, float saturation, float value);
/// ColorUtil#rgbToHSV ({h, s, v}; TF's `1 / 2` integer division kept).
[[nodiscard]] std::array<float, 3> rgbToHSV(int r, int g, int b);

/// ColorHandler: the aurora block's colour at a position (0xRRGGBB).
[[nodiscard]] unsigned auroraBlockColor(int x, int y, int z);
/// The aurora block's colour without a level (items): hue 0.45.
[[nodiscard]] unsigned auroraBlockItemColor();
/// ColorHandler: aurora pillar / slab / auroralized glass, the block colour made pale.
[[nodiscard]] unsigned auroraPaleColor(unsigned auroraColor);

} // namespace gf::core::aurora
