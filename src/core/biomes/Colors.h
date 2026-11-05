// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/BiomeColorAlgorithms.java
//   src/main/java/twilightforest/client/FoliageColorHandler.java
//   src/main/java/twilightforest/TFEnumExtensions.java
//   src/main/java/twilightforest/util/ColorUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U1 `biomes`, pure part: TF's biome colours (BiomeColorAlgorithms) as the grass colour modifiers
// (TFGrassColorModifierEnumExtension, TFEnumExtensions#GrassColorModifier_*) and the foliage
// handler (FoliageColorHandler, applied to every foliage colour lookup by tf-asm's
// FoliageColorResolverTransformer) use them, and Java's per-position biome blend
// (ClientLevel#calculateBlockTint). Colours are 0xRRGGBB.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace gf::core::biomes::colors {

/// ColorUtil#blendColors(a, b, ratio) (Java int arithmetic).
[[nodiscard]] int blendColors(int a, int b, double ratio);

/// BiomeColorAlgorithms#enchanted(originalColor, x, z): bands by the distance to the nearest
/// landmark centre (the quest grove), replacing the blue byte.
[[nodiscard]] int enchanted(int originalColor, int x, int z);
/// BiomeColorAlgorithms#swamp / #darkForest (flat colours of the vanilla colour maps).
[[nodiscard]] int swamp(bool foliage);
[[nodiscard]] int darkForest(bool foliage);
/// BiomeColorAlgorithms#darkForestCenterGrass / #darkForestCenterFoliage.
[[nodiscard]] int darkForestCenterGrass(double x, double z);
[[nodiscard]] int darkForestCenterFoliage(double x, double z);
/// BiomeColorAlgorithms#spookyNoise / #spookyGrass / #spookyFoliage.
[[nodiscard]] double spookyNoise(double x, double z);
[[nodiscard]] int    spookyGrass(double x, double z);
[[nodiscard]] int    spookyFoliage(double x, double z);

/// The vanilla colour-map values TF's flat colours start from (GrassColor.get / FoliageColor.get
/// at (0.8, 0.9) and (0.7, 0.8)); see Colors.cpp for where they come from.
inline constexpr int kGrassSwampMap      = 0x6AC44E;
inline constexpr int kFoliageSwampMap    = 0x44B420;
inline constexpr int kGrassForestMap     = 0x79C05A;
inline constexpr int kFoliageForestMap   = 0x59AE30;

/// Whether `biomeId` changes its colour by position (the grass modifier or foliage handler reads
/// x / z): enchanted forest, dark forest centre, spooky forest.
[[nodiscard]] bool isPositional(std::string_view biomeId);
/// Whether `biomeId` has a TF grass modifier / foliage handler at all (the three above plus the
/// swamp and the dark forest).
[[nodiscard]] bool hasModifier(std::string_view biomeId);

/// Biome#getGrassColor(x, z) of a TF biome with a modifier: `base` is the biome's grass colour
/// before the modifier (its JSON grass_color, or the colour map). nullopt: no modifier.
[[nodiscard]] std::optional<int> grass(std::string_view biomeId, int base, double x, double z);
/// FoliageColorHandler#get(o, biome, x, z): `base` is the biome's foliage colour. nullopt: no handler.
[[nodiscard]] std::optional<int> foliage(std::string_view biomeId, int base, double x, double z);

/// ClientLevel#calculateBlockTint: the average over the (2r+1)^2 columns around (x, z) of
/// `colorAt(dx, dz)` (r = biomeBlendRadius, Java's default 2), channel by channel, truncating.
[[nodiscard]] int blend(int x, int z, int radius, std::function<int(int x, int z)> const& colorAt);

} // namespace gf::core::biomes::colors
