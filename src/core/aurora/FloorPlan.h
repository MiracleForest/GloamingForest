// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorParts.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorTypesAuroraPalace.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/IFloorDecorator.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorWith3x3Map.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorFarWallSteps.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorPillarParkour.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorPillarPlatforms.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorPillarPlatformsOutside.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorPlatform.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorQuadPillarStairs.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorTop.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorWraparoundWallSteps.java
//   src/main/java/gloamingforest/world/components/structures/icetower/floordecorators/FloorWraparoundWallStepsPillars.java
//   src/main/java/gloamingforest/world/components/structures/icetower/IceTowerWingComponent.java (createFloorPlan)
//   src/main/java/gloamingforest/util/WorldUtil.java (getRandomElementWithWeights)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The aurora palace's floor plans (pure, gtested): the 3 x 3 anchor zones of an 11 x 11 wing floor,
// the floor types with their required / blocked zones and weights, and IceTowerWingComponent
// #createFloorPlan, which picks the lower floors' types and rotations from the top down.

#include <cstdint>
#include <functional>
#include <vector>

#include "core/jrand/JRand.h"

namespace gf::core::aurora {

/// FloorParts, in declaration order.
enum class FloorPart { LeftFront, Front, RightFront, Left, Mid, Right, LeftBack, Back, RightBack, Pillars };
/// FloorParts#rotateClockwise(times).
[[nodiscard]] FloorPart rotateClockwise(FloorPart part, int times);

/// A set of FloorParts (bit i = part i).
using PartSet = std::uint16_t;
[[nodiscard]] constexpr PartSet bit(FloorPart p) { return static_cast<PartSet>(1u << static_cast<unsigned>(p)); }
/// The set with every part turned `times` clockwise.
[[nodiscard]] PartSet rotateSet(PartSet set, int times);

/// FloorTypesAuroraPalace, in declaration order.
enum class FloorType { FarWallSteps, PillarParkour, PillarPlatforms, PillarPlatformsOutside, Platform, QuadPillarStairs, Top, WraparoundWallSteps, WraparoundWallStepsPillars };
inline constexpr int kFloorTypeCount = 9;

struct FloorDef {
    PartSet required = 0; ///< REQUIRED_FLOOR_PARTS: must be free on the floor above
    PartSet blocked  = 0; ///< BLOCKED_FLOOR_PARTS: blocked for the floor below
    float   weight   = 0;
};
[[nodiscard]] FloorDef const& floorDef(FloorType type);

/// A door of the wing in piece-local coordinates (TowerWing openings).
struct Door {
    int x = 0, y = 0, z = 0;
};
/// IceTowerWingComponent#getPartsBlockedByDoors: the zones holding a door between `bottom` and
/// `top` (inclusive).
[[nodiscard]] PartSet partsBlockedByDoors(std::vector<Door> const& doors, int bottom, int top);

struct PlannedFloor {
    FloorType type     = FloorType::FarWallSteps;
    int       rotation = 0;
    friend bool operator==(PlannedFloor const&, PlannedFloor const&) = default;
};

/// IceTowerWingComponent#createFloorPlan(floorCount, decoRNG): one entry per lower floor, the
/// first entry for the floor just under the top. The candidates are taken in enum order (upstream's
/// HashMap order is per JVM run; see docs/spec/aurora.md section 1.2). `onGiveUp` runs on every
/// iteration once the plan restarted more than 10000 times (upstream logs an error there).
[[nodiscard]] std::vector<PlannedFloor> createFloorPlan(int floorCount, std::vector<Door> const& doors, jrand::RandomSource& rng,
                                                        std::function<void()> const& onGiveUp = {});

} // namespace gf::core::aurora
