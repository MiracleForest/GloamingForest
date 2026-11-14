// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/passive/QuestRam.java
//   src/main/java/gloamingforest/client/model/entity/QuestRamModel.java
//   src/main/java/gloamingforest/world/components/feature/templates/TemplateFeature.java
//   src/main/java/gloamingforest/world/components/feature/templates/GraveyardFeature.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U19 (questcamps), the game-independent parts: dye colours and the Questing Ram's colour flags,
// the ram model's segment layout (QuestRamModel#prepareMobModel), the template features' ground
// averaging (offsetToAverageGroundLevel's statistics) and the graveyard's grave grid.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace gf::core::questcamps {

// ---- dye colours (DyeColor, 1.21.1) -------------------------------------------------------------------------

/// DyeColor#getSerializedName by id (white 0 .. black 15).
inline constexpr std::array<std::string_view, 16> kDyeNames = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                                               "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
/// The dye id of a serialized name (-1: none).
[[nodiscard]] int dyeId(std::string_view name);
/// DyeColor#getTextureDiffuseColor (0xRRGGBB).
[[nodiscard]] unsigned textureDiffuse(int dyeId);
/// Sheep#getColor (Sheep.createSheepColor): white 0xE6E6E6, the others the diffuse colour * 0.75.
[[nodiscard]] unsigned sheepColor(int dyeId);

// ---- the Questing Ram ----------------------------------------------------------------------------------------

/// QuestRam#isColorPresent.
[[nodiscard]] constexpr bool colorPresent(int flags, int dyeId) { return (flags & (1 << dyeId)) > 0; }
/// QuestRam#countColorsSet (Integer.bitCount).
[[nodiscard]] int countColors(int flags);
/// Whether the quest is complete (countColorsSet() > 15).
[[nodiscard]] inline bool complete(int flags) { return countColors(flags) > 15; }

/// QuestRamModel#colorOrder: the order the wool segments line up along the body.
inline constexpr std::array<int, 16> kColorOrder = {0, 8, 7, 15, 14, 1, 4, 5, 13, 3, 9, 11, 10, 2, 6, 12};
/// QuestRamModel#prepareMobModel (no Jappa pack): the z of each part, Java model units.
struct ModelPose {
    int                  count = 0;
    float                headZ = -11, neckZ = -11, frontTorsoZ = 0, backTorsoZ = 0;
    float                backLegZ = 9, frontLegZ = -11;
    std::array<bool, 16> visible{};
    std::array<int, 16>  segmentZ{}; ///< by dye id (meaningful when visible)
};
[[nodiscard]] ModelPose modelPose(int flags);

// ---- template features -----------------------------------------------------------------------------------------

/// TemplateFeature / GraveyardFeature#offsetToAverageGroundLevel's statistics over the column
/// heights (StatsAccumulator): nullopt when the population standard deviation is above 2.0.
struct Ground {
    int    baseY = 0; ///< (int)(mean + 0.5) (TemplateFeature) or Math.round(mean) (graveyard)
    int    maxY  = 0;
    double mean  = 0, deviation = 0;
};
[[nodiscard]] std::optional<Ground> averageGround(std::vector<int> const& heights, bool mathRound);

/// Java's Rotation and Mirror (ordinal order).
enum class Rot { None, Cw90, Cw180, Ccw90 };
enum class Mir { None, LeftRight, FrontBack };
struct Vec3i {
    int  x = 0, y = 0, z = 0;
    bool operator==(Vec3i const&) const = default;
};
/// StructureTemplate#getSize(rotation): x and z swapped by a quarter turn.
[[nodiscard]] Vec3i rotatedSize(Vec3i size, Rot rotation);
/// BlockPos#rotate (vanilla): CW_90 (-z, y, x), CW_180 (-x, y, -z), CCW_90 (z, y, -x).
[[nodiscard]] Vec3i rotate(Vec3i p, Rot rotation);

/// GraveyardFeature's grave grid, relative to its startPos (the averaged ground corner):
/// `transformedSize` = base.getSize(rotation), `transformedGraveSize` = grave_full.getSize(rotation).
struct GraveGrid {
    Vec3i                           start;     ///< startPos + (1, 1, 0)
    Vec3i                           size;      ///< transformedSize - (1, 0, 1)
    Vec3i                           graveSize; ///< transformedGraveSize - (1, 0, 1)
    Vec3i                           innerSize; ///< (size.x - 4, 0, size.z - 4)
    Vec3i                           fixed;     ///< inner + the rotation / mirror shift
    std::vector<std::pair<int, int>> cells;    ///< (x, z) offsets from `fixed`, in loop order (skips removed)
};
[[nodiscard]] GraveGrid graveGrid(Vec3i transformedSize, Vec3i transformedGraveSize, Rot rotation, Mir mirror);

} // namespace gf::core::questcamps
