// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/icetower/floordecorators/FloorParts.java
//   src/main/java/twilightforest/world/components/structures/icetower/floordecorators/FloorTypesAuroraPalace.java
//   src/main/java/twilightforest/world/components/structures/icetower/IceTowerWingComponent.java (createFloorPlan)
//   src/main/java/twilightforest/util/WorldUtil.java (getRandomElementWithWeights)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/aurora/FloorPlan.h"

#include <array>

namespace gf::core::aurora {

namespace {

FloorPart rotateOnce(FloorPart p) {
    switch (p) {
    case FloorPart::LeftFront: return FloorPart::RightFront;
    case FloorPart::Front: return FloorPart::Right;
    case FloorPart::RightFront: return FloorPart::RightBack;
    case FloorPart::Right: return FloorPart::Back;
    case FloorPart::RightBack: return FloorPart::LeftBack;
    case FloorPart::Back: return FloorPart::Left;
    case FloorPart::LeftBack: return FloorPart::LeftFront;
    case FloorPart::Left: return FloorPart::Front;
    default: return p;
    }
}

constexpr PartSet set(std::initializer_list<FloorPart> parts) {
    PartSet s = 0;
    for (auto p : parts) s = static_cast<PartSet>(s | bit(p));
    return s;
}

using P = FloorPart;

} // namespace

FloorPart rotateClockwise(FloorPart part, int times) {
    // Java: for (i < times % 4) (a negative count turns nothing)
    for (int i = 0; i < times % 4; i++) part = rotateOnce(part);
    return part;
}

PartSet rotateSet(PartSet s, int times) {
    PartSet out = 0;
    for (int i = 0; i < 10; i++)
        if (s & (1u << i)) out = static_cast<PartSet>(out | bit(rotateClockwise(static_cast<FloorPart>(i), times)));
    return out;
}

FloorDef const& floorDef(FloorType type) {
    static std::array<FloorDef, kFloorTypeCount> const defs = {
        FloorDef{set({P::Left, P::LeftBack}), set({P::LeftBack, P::Left, P::LeftFront}), 1.0F},                            // FAR_WALL_STEPS
        FloorDef{set({P::Left, P::Mid, P::Right, P::LeftFront, P::Front, P::RightFront, P::Pillars}), set({P::Mid, P::Right, P::Pillars}), 6.0F}, // PILLAR_PARKOUR
        FloorDef{set({P::LeftBack, P::Back, P::RightBack, P::Left, P::Mid, P::Right, P::Pillars}),
                 set({P::LeftBack, P::Back, P::Left, P::Mid, P::LeftFront, P::Front, P::Pillars}), 6.0F},                  // PILLAR_PLATFORMS
        FloorDef{set({P::RightBack, P::Right, P::RightFront}), set({P::LeftBack, P::RightBack, P::Left, P::LeftFront, P::Pillars}), 1.0F}, // PILLAR_PLATFORMS_OUTSIDE
        FloorDef{set({P::LeftBack, P::Left}), set({P::RightBack, P::Back, P::Pillars}), 1.0F},                             // PLATFORM
        FloorDef{set({P::Left, P::LeftFront}), set({P::Back, P::RightBack, P::Pillars}), 1.0F},                            // QUAD_PILLAR_STAIRS
        FloorDef{0, set({P::Mid, P::Pillars}), 0.0F},                                                                      // TOP
        FloorDef{set({P::LeftBack, P::Left}), set({P::RightFront}), 1.0F},                                                 // WRAPAROUND_WALL_STEPS
        FloorDef{set({P::Left, P::LeftBack}), set({P::LeftBack, P::LeftFront, P::RightFront, P::Pillars}), 1.0F},          // WRAPAROUND_WALL_STEPS_PILLARS
    };
    return defs[static_cast<std::size_t>(type)];
}

PartSet partsBlockedByDoors(std::vector<Door> const& doors, int bottom, int top) {
    struct Zone {
        FloorPart part;
        int       minX, minZ, maxX, maxZ;
    };
    static constexpr Zone zones[] = {
        {P::LeftBack, 0, 0, 3, 3},  {P::Left, 4, 0, 6, 3},  {P::LeftFront, 7, 0, 10, 3}, {P::Back, 0, 4, 3, 6},
        {P::Front, 7, 4, 10, 6},    {P::RightBack, 0, 7, 3, 10}, {P::Right, 4, 7, 6, 10}, {P::RightFront, 7, 7, 10, 10},
    };
    PartSet out = 0;
    for (auto const& z : zones) {
        // isNoDoorAreaRotated(minX, bottom, minZ, maxX, top, maxZ, NONE): BoundingBox#isInside
        for (auto const& d : doors) {
            if (d.x >= z.minX && d.x <= z.maxX && d.y >= bottom && d.y <= top && d.z >= z.minZ && d.z <= z.maxZ) {
                out = static_cast<PartSet>(out | bit(z.part));
                break;
            }
        }
    }
    return out;
}

std::vector<PlannedFloor> createFloorPlan(int floorCount, std::vector<Door> const& doors, jrand::RandomSource& rng, std::function<void()> const& onGiveUp) {
    constexpr int kMaxTries = 10000;
    std::vector<PlannedFloor> plan;
    int                       tries = 0;

    // Java keeps topBlockedPartsInit / bottomBlockedPartsInit as sets and, after a restart, makes the
    // working sets the very same objects: clearing them after 10000 tries clears the init sets too.
    PartSet topInit    = static_cast<PartSet>(floorDef(FloorType::Top).blocked | partsBlockedByDoors(doors, floorCount * 10, (floorCount + 1) * 10));
    PartSet bottomInit = partsBlockedByDoors(doors, (floorCount - 1) * 10, floorCount * 10);
    PartSet top        = topInit;
    PartSet bottom     = bottomInit;
    bool    topAliased = false, bottomAliased = false;

    while (static_cast<int>(plan.size()) < floorCount) {
        if (tries > kMaxTries) {
            top    = 0;
            bottom = 0;
            if (topAliased) topInit = 0;
            if (bottomAliased) bottomInit = 0;
            if (onGiveUp) onGiveUp();
        }
        // possibleFloors: every type but TOP with its allowed rotations, the empty ones removed
        struct Candidate {
            FloorType        type;
            std::vector<int> rotations;
        };
        std::vector<Candidate> candidates;
        for (int t = 0; t < kFloorTypeCount; t++) {
            auto type = static_cast<FloorType>(t);
            if (type == FloorType::Top) continue;
            auto const&      def = floorDef(type);
            std::vector<int> allowed;
            for (int r = 0; r < 4; r++)
                if ((rotateSet(def.required, r) & top) == 0 && (rotateSet(def.blocked, r) & bottom) == 0) allowed.push_back(r);
            if (!allowed.empty()) candidates.push_back({type, std::move(allowed)});
        }
        if (candidates.empty()) {
            plan.clear();
            top           = topInit;
            bottom        = bottomInit;
            topAliased    = true;
            bottomAliased = true;
            tries++;
            continue;
        }
        // WorldUtil.getRandomElementWithWeights
        double sum = 0;
        for (auto const& c : candidates) sum += static_cast<double>(floorDef(c.type).weight);
        float  total  = static_cast<float>(sum);
        float  value  = rng.nextFloat() * total;
        int    chosen = -1;
        for (int i = 0; i < static_cast<int>(candidates.size()); i++) {
            value -= floorDef(candidates[static_cast<std::size_t>(i)].type).weight;
            if (value < 0) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0) chosen = rng.nextInt(static_cast<int>(candidates.size()));
        auto const& c        = candidates[static_cast<std::size_t>(chosen)];
        int         rotation = c.rotations[static_cast<std::size_t>(rng.nextInt(static_cast<int>(c.rotations.size())))];
        plan.push_back({c.type, rotation});

        int n         = static_cast<int>(plan.size());
        top           = static_cast<PartSet>(rotateSet(floorDef(c.type).blocked, rotation) | partsBlockedByDoors(doors, (floorCount - n) * 10, (floorCount - n + 1) * 10));
        bottom        = partsBlockedByDoors(doors, (floorCount - n - 1) * 10, (floorCount - n) * 10);
        topAliased    = false;
        bottomAliased = false;
    }
    return plan;
}

} // namespace gf::core::aurora
