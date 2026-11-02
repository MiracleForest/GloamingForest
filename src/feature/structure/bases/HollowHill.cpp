// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/HollowHillComponent.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/bases/HollowHill.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "mc/world/level/block/Block.h"

#include "feature/structure/Internal.h"

namespace gf::structure::bases {

namespace {

constexpr float kChestSpawnChance   = 0.025f;
constexpr float kSpawnerSpawnChance = 0.025f;
constexpr float kSpecialSpawnChance = kChestSpawnChance + kSpawnerSpawnChance;

/// BoundingBoxUtils.getComponentToAddBoundingBox(..., SOUTH, centerBounds = true).
BlockBox hillBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ) {
    x += (spanX + minX) / 4;
    y += (spanY + minY) / 4;
    z += (spanZ + minZ) / 4;
    return BlockBox{x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ};
}

} // namespace

HollowHill::HollowHill(std::string typeId, int depth, int size, int x, int y, int z, std::string configId)
: ComponentOld(std::move(typeId), depth, x, y, z),
  hillSize(size),
  radius((size * 2 + 1) * 8 - 6),
  hdiam((size * 2 + 1) * 16),
  speleothemConfigId(std::move(configId)) {
    setOrientation(Orientation::South);
    bbox = hillBox(x, y, z, -radius, -(3 + hillSize), -radius, radius * 2, radius / (hillSize == 1 ? 2 : hillSize), radius * 2);
}

Json HollowHill::save() const {
    Json j         = ComponentOld::save();
    j["hillSize"]  = hillSize;
    j["config_id"] = speleothemConfigId;
    return j;
}

void HollowHill::load(Json const& data) {
    ComponentOld::load(data);
    hillSize           = data.value("hillSize", hillSize);
    radius             = (hillSize * 2 + 1) * 8 - 6;
    hdiam              = (hillSize * 2 + 1) * 16;
    speleothemConfigId = data.value("config_id", speleothemConfigId);
}

void HollowHill::postProcess(Region& region, RandomSource& rand, BlockBox const& writeableBounds, BlockPos const&) {
    BlockPos center           = boxCenter(bbox);
    float    shortenedRadiusSq = static_cast<float>(radius * radius) * 0.85f;
    float    drainRadius       = static_cast<float>(hillSize) * 16.5f;
    drainWater(writeableBounds, bbox, region, hillSize * 3 + 2, blocks::java("minecraft:cave_air"), center.x, center.z,
               static_cast<double>(drainRadius * drainRadius), blocks::java("minecraft:stone"));

    // two rectangle lattices simulate the hexagonal packing of stalactites, stalagmites, chests and spawners
    auto lattice = speleothems::lattice(speleothemConfigId);
    for (BlockPos latticePos : speleothems::latticePositions(lattice, writeableBounds, 0)) {
        float distSq = getDistSqFromCenter(center, latticePos);
        if (distSq > shortenedRadiusSq) continue;

        // setFeatures
        rand.setSeed(rand.nextLong() ^ pure::blockPosAsLong(latticePos.x, latticePos.y, latticePos.z));
        auto ceilingY = [&](float d) { return worldY(pure::mthCeilF(pure::mthCos(std::sqrt(d) / static_cast<float>(hdiam) * pure::kMthPi) *
                                                                   (static_cast<float>(hdiam) / 4.0f))); };
        auto floorY   = [&](float d) {
            float h = static_cast<float>(hillSize * 2) - pure::mthCos(std::sqrt(d) / static_cast<float>(hdiam) * pure::kMthPi) * (static_cast<float>(hdiam) / 20.0f) + 1;
            return worldY(pure::mthFloorF(h + 0.25f));
        };

        // placeCeilingFeature
        if (speleothems::shouldDoAStalactite(speleothemConfigId, rand)) {
            BlockPos ceiling{latticePos.x, ceilingY(distSq), latticePos.z};
            speleothems::spike(region, ceiling, speleothemConfigId, rand, true, std::numeric_limits<int>::max());
        }

        // placeFloorFeature
        int   fy          = floorY(distSq);
        float floatChance = rand.nextFloat();
        if (floatChance < kSpecialSpawnChance) {
            float    angle = rand.nextFloat() * pure::kMthTwoPi;
            float    sqrt2 = std::sqrt(2.0f);
            BlockPos pos{pure::javaRound(pure::mthCos(angle) * sqrt2) + latticePos.x, fy, pure::javaRound(pure::mthSin(angle) * sqrt2) + latticePos.z};
            BlockPos above{pos.x, pos.y + 1, pos.z};
            if (floatChance < kSpawnerSpawnChance) {
                setSpawnerInWorld(region, writeableBounds, getMobID(rand), {}, above);
            } else {
                char const* table = hillSize == 3 ? "twilightforest:hill_3" : hillSize == 2 ? "twilightforest:hill_2" : "twilightforest:hill_1";
                placeTreasureAtWorldPosition(region, table, false, writeableBounds, above);
            }
            region.setBlock(BlockPos{pos.x, pos.y - 1, pos.z}, blocks::java("minecraft:cobblestone"), 2);
            region.setBlock(pos, blocks::java("minecraft:cobblestone"), 2);
        } else if (speleothems::shouldDoAStalagmite(speleothemConfigId, rand)) {
            int      cy = ceilingY(distSq);
            BlockPos pos{latticePos.x, fy, latticePos.z};
            speleothems::spike(region, pos, speleothemConfigId, rand, false, cy - fy + 4);
        }
    }
}

void HollowHill::drainWater(BlockBox const& chunkBox, BlockBox const& structureBox, Region& region, int maxDepth, Block const& airBlock, int xCenter,
                            int zCenter, double radiusSq, Block const& undergroundBlock) {
    auto bounds = intersection(chunkBox, structureBox);
    if (!bounds) return;
    int seaLevel = kSeaLevel;
    int minY     = seaLevel - maxDepth;
    for (int z = bounds->minZ; z <= bounds->maxZ; z++) {
        int dZ = zCenter - z;
        for (int x = bounds->minX; x <= bounds->maxX; x++) {
            int   dX     = xCenter - x;
            float distSq = static_cast<float>(dX * dX + dZ * dZ);
            if (distSq >= radiusSq) continue;
            int  maxY         = std::min(seaLevel, region.height(worldgen::Heightmap::OceanFloorWg, x, z) - 1);
            bool crossedFloor = false;
            for (int y = maxY; y >= minY; y--) {
                BlockPos     p{x, y, z};
                Block const& stateAt = region.getBlock(p);
                if (blocks::isWater(stateAt)) region.setBlock(p, airBlock, 3);
                else crossedFloor = true;
                if (crossedFloor && (blocks::is(stateAt, "minecraft:dirt") || blocks::is(stateAt, "minecraft:sand"))) region.setBlock(p, undergroundBlock, 3);
            }
        }
    }
}

float HollowHill::getDistSqFromCenter(BlockPos const& center, BlockPos const& to) {
    float x = static_cast<float>(to.x - center.x) - 0.5f;
    float z = static_cast<float>(to.z - center.z) - 0.5f;
    return x * x + z * z;
}

void HollowHill::generateSpeleothem(Region& region, BlockPos const& pos, BlockBox const& sbb, bool hanging) {
    if (!sbb.contains(pos.x, pos.y, pos.z) || blocks::is(region.getBlock(pos), "minecraft:spawner")) return;
    JavaRandom stalRNG(region.seed() + static_cast<std::int64_t>(pos.x) * pos.z);
    speleothems::spike(region, pos, speleothemConfigId, stalRNG, hanging, std::numeric_limits<int>::max());
}

std::string HollowHill::getMobID(RandomSource& random) const { return getMobID(random, hillSize); }

std::string HollowHill::getMobID(RandomSource& random, int level) const {
    if (level == 1) return getLevel1Mob(random);
    if (level == 2) return getLevel2Mob(random);
    if (level == 3) return getLevel3Mob(random);
    return "minecraft:spider";
}

std::string HollowHill::getLevel1Mob(RandomSource& random) const {
    switch (random.nextInt(10)) {
    case 3:
    case 4:
    case 5:
        return "minecraft:spider";
    case 6:
    case 7:
        return "minecraft:zombie";
    case 8:
        return "minecraft:silverfish";
    case 9:
        return "twilightforest:redcap";
    default:
        return "twilightforest:swarm_spider";
    }
}

std::string HollowHill::getLevel2Mob(RandomSource& random) const {
    switch (random.nextInt(10)) {
    case 3:
    case 4:
    case 5:
        return "minecraft:zombie";
    case 6:
    case 7:
        return "minecraft:skeleton";
    case 8:
        return "twilightforest:swarm_spider";
    case 9:
        return "minecraft:cave_spider";
    default:
        return "twilightforest:redcap";
    }
}

std::string HollowHill::getLevel3Mob(RandomSource& random) const {
    switch (random.nextInt(11)) {
    case 0:
        return "twilightforest:slime_beetle";
    case 1:
        return "twilightforest:fire_beetle";
    case 2:
        return "twilightforest:pinch_beetle";
    case 3:
    case 4:
    case 5:
        return "minecraft:skeleton";
    case 6:
    case 7:
    case 8:
        return "minecraft:cave_spider";
    case 9:
        return "minecraft:creeper";
    default:
        return "twilightforest:wraith";
    }
}

} // namespace gf::structure::bases
