// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/darktower/DarkTowerWingComponent.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U26 (darktower): small helpers shared by the dark tower pieces (Tower.cpp, TowerMain.cpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "mc/world/level/BlockPos.h"

#include "feature/structure/Structure.h"

class Block;

namespace gf::feature::darktower::tower::util {

using ::gf::structure::BlockBox;
using ::gf::structure::Json;
using ::gf::structure::Orientation;
using ::gf::structure::Region;
using ::gf::structure::Rotation;

inline constexpr char kLootCache[] = "twilightforest:darktower_cache";
inline constexpr char kLootKey[]   = "twilightforest:darktower_key";

/// RotationUtil.ROTATIONS.
inline constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

/// A Java block state as a Bedrock block (blocks::java).
[[nodiscard]] Block const& J(char const* id, Json const& props = Json::object());
/// getLeverState(LEVER, face, direction, powered).
[[nodiscard]] Block const& lever(char const* face, Orientation facing, bool powered);
/// A ladder facing `facing`.
[[nodiscard]] Block const& ladder(Orientation facing);
/// Rotation#getRotated.
[[nodiscard]] Rotation rot(Rotation a, Rotation b);
/// The Java name of a horizontal direction.
[[nodiscard]] char const* dirName(Orientation o);
/// A modulo that stays an index (TF indexes ROTATIONS with `%`; a negative value would throw in Java).
[[nodiscard]] inline int javaMod(int v, int m) { return ((v % m) + m) % m; }
/// The distance of DarkTowerWingComponent#drawBlob / netherTransformBlob (byte arithmetic).
[[nodiscard]] inline int blobDist(int dx, int dy, int dz) {
    auto part = [](int a, int b) { return static_cast<int>(static_cast<std::int8_t>(std::max(a, b) * 0.5 + std::min(a, b) * 0.25)); };
    if (dx >= dy && dx >= dz) return static_cast<std::int8_t>(dx + part(dy, dz));
    if (dy >= dx && dy >= dz) return static_cast<std::int8_t>(dy + part(dx, dz));
    return static_cast<std::int8_t>(dz + part(dx, dy));
}
[[nodiscard]] bool isAirBlock(Block const& block);
/// RandomSource.create(world.getSeed() + (minX * 321534781L) ^ (minZ * 756839L)) seed.
[[nodiscard]] std::int64_t decoSeed(Region& region, BlockBox const& box);
/// BoundingBoxUtils.getComponentToAddBoundingBox(..., centerBounds = false).
[[nodiscard]] BlockBox componentBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir);
/// Records a tower device (antibuilder, builder, reactor, ghast trap) generated at world `pos` so the
/// device runtime ticks it once its chunk is loaded (Java: the block entity / POI the chunk saves).
void noteGenerated(Region& region, Block const& block, BlockPos const& pos, BlockBox const& chunkBox);

} // namespace gf::feature::darktower::tower::util
