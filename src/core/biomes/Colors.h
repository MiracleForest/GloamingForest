// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/BiomeColorAlgorithms.java
//   src/main/java/gloamingforest/client/FoliageColorHandler.java
//   src/main/java/gloamingforest/GloamingEnumExtensions.java
//   src/main/java/gloamingforest/util/ColorUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U1 `biomes`, pure part: upstream's biome colours (BiomeColorAlgorithms) as the grass colour modifiers
// (GloamingGrassColorModifierEnumExtension, GloamingEnumExtensions#GrassColorModifier_*) and the foliage
// handler (FoliageColorHandler, applied to every foliage colour lookup by upsrc-asm's
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

/// The vanilla colour-map values upstream's flat colours start from (GrassColor.get / FoliageColor.get
/// at (0.8, 0.9) and (0.7, 0.8)); see Colors.cpp for where they come from.
inline constexpr int kGrassSwampMap      = 0x6AC44E;
inline constexpr int kFoliageSwampMap    = 0x44B420;
inline constexpr int kGrassForestMap     = 0x79C05A;
inline constexpr int kFoliageForestMap   = 0x59AE30;

/// Whether `biomeId` changes its colour by position (the grass modifier or foliage handler reads
/// x / z): enchanted forest, dark forest centre, spooky forest.
[[nodiscard]] bool isPositional(std::string_view biomeId);
/// Whether `biomeId` has an upstream grass modifier / foliage handler at all (the three above plus the
/// swamp and the dark forest).
[[nodiscard]] bool hasModifier(std::string_view biomeId);

/// Biome#getGrassColor(x, z) of an upstream biome with a modifier: `base` is the biome's grass colour
/// before the modifier (its JSON grass_color, or the colour map). nullopt: no modifier.
[[nodiscard]] std::optional<int> grass(std::string_view biomeId, int base, double x, double z);
/// FoliageColorHandler#get(o, biome, x, z): `base` is the biome's foliage colour. nullopt: no handler.
[[nodiscard]] std::optional<int> foliage(std::string_view biomeId, int base, double x, double z);

/// The grass / foliage colour of an upstream biome without a modifier (Biome#getGrassColor /
/// getFoliageColor): upstream's JSON grass_color / foliage_color (fire swamp, snowy forest), else the
/// vanilla colour map at the biome's climate (GrassColor.get / FoliageColor.get(temperature,
/// downfall); the values are Bedrock's colour maps, which differ from Java's by at most one step in
/// one channel). nullopt: not an upstream biome, or a biome whose modifier gives every colour.
[[nodiscard]] std::optional<int> staticGrass(std::string_view biomeId);
[[nodiscard]] std::optional<int> staticFoliage(std::string_view biomeId);
/// upstream's grass / foliage colour of `biomeId` at block column (x, z) before the blend: the modifier's
/// (or the foliage handler's) colour where the biome has one, else the static colour. nullopt: not a
/// upstream biome.
[[nodiscard]] std::optional<int> grassAt(std::string_view biomeId, double x, double z);
[[nodiscard]] std::optional<int> foliageAt(std::string_view biomeId, double x, double z);
/// BiomeSpecialEffects#waterColor / #waterFogColor of an upstream biome (nullopt: not an upstream biome).
[[nodiscard]] std::optional<int> water(std::string_view biomeId);
[[nodiscard]] std::optional<int> waterFog(std::string_view biomeId);
/// Whether `biomeId` is in upstream's has_closer_water_fog tag (Java's water fog ends at 85% there).
[[nodiscard]] bool closerWaterFog(std::string_view biomeId);

/// ClientLevel#calculateBlockTint: the average over the (2r+1)^2 columns around (x, z) of
/// `colorAt(dx, dz)` (r = biomeBlendRadius, Java's default 2), channel by channel, truncating.
[[nodiscard]] int blend(int x, int z, int radius, std::function<int(int x, int z)> const& colorAt);

} // namespace gf::core::biomes::colors
