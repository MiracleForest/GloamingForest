// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/entity/GrowingBeanstalkBlockEntity.java
//   src/main/java/twilightforest/client/model/block/giantblock/GiantBlockModel.java
//   src/main/java/twilightforest/block/CloudBlock.java
//   src/main/java/twilightforest/asmhooks/BlockHooks.java
//   src/main/java/twilightforest/loot/modifiers/GiantToolGroupingModifier.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/giants/Giants.h"

#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/jrand/Mth.h"

namespace gf::core::giants {

namespace {

std::vector<float> const& sinTable() {
    static std::vector<float> const table = [] {
        std::vector<float> t(65536);
        for (int i = 0; i < 65536; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(static_cast<double>(i) * 3.141592653589793 * 2.0 / 65536.0));
        return t;
    }();
    return table;
}

int fm4(int v) { return mth::floorMod(v, 4); }

/// GrowingBeanstalkBlockEntity#tryToPlaceStalk.
bool tryToPlaceStalk(BeanstalkState& s, BeanstalkWorld& world, int x, int y, int z, bool checkBlocked) {
    Cell c = world.at(x, y, z);
    if (c == Cell::Air || c == Cell::Replaceable || c == Cell::WispyCloud || c == Cell::Leaves || c == Cell::FluffyCloud) {
        world.setStalk(x, y, z);
        if (y > 150) {
            for (int i = 0; i < 7; i++) {
                Cell up = world.at(x, y + i, z);
                if (up == Cell::WispyCloud || up == Cell::FluffyCloud) world.setAir(x, y + i, z);
            }
        }
        return true;
    }
    if (c != Cell::Stalk && checkBlocked) s.blocksSkipped++;
    return s.blocksSkipped < 15;
}

/// GrowingBeanstalkBlockEntity#tryToPlaceLeaves.
void tryToPlaceLeaves(BeanstalkWorld& world, int x, int y, int z, int distance) {
    Cell c = world.at(x, y, z);
    if (c == Cell::Air || c == Cell::Leaves) world.setLeaves(x, y, z, distance);
}

/// GrowingBeanstalkBlockEntity#placeLeaves.
void placeLeaves(BeanstalkWorld& world, int x, int y, int z) {
    // stalk at center
    world.setStalk(x, y, z);
    // small squares
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            int distance = std::abs(dx) + std::abs(dz) + 1;
            tryToPlaceLeaves(world, x + dx, y - 1, z + dz, distance);
            tryToPlaceLeaves(world, x + dx, y + 1, z + dz, distance);
        }
    }
    // larger square
    for (int dx = -2; dx <= 2; dx++) {
        for (int dz = -2; dz <= 2; dz++) {
            if (!((dx == 2 || dx == -2) && (dz == 2 || dz == -2))) tryToPlaceLeaves(world, x + dx, y, z + dz, std::max(std::abs(dx) + std::abs(dz), 1));
        }
    }
}

} // namespace

float mthSin(float value) { return sinTable()[static_cast<std::size_t>(static_cast<int>(value * 10430.378F) & 65535)]; }
float mthCos(float value) { return sinTable()[static_cast<std::size_t>(static_cast<int>(value * 10430.378F + 16384.0F) & 65535)]; }

nlohmann::json BeanstalkState::toJson() const {
    return {{"ticker", ticker},
            {"layer", layer},
            {"isAreaClearEnough", isAreaClearEnough},
            {"nextLeafY", nextLeafY},
            {"beardifierGroundDelta", yOffset},
            {"cScale", cScale},
            {"rScale", rScale},
            {"maxY", maxY},
            {"blocksSkipped", blocksSkipped}};
}

BeanstalkState BeanstalkState::fromJson(nlohmann::json const& j) {
    BeanstalkState s;
    if (!j.is_object()) return s;
    s.ticker            = j.value("ticker", 0);
    s.layer             = j.value("layer", 0);
    s.isAreaClearEnough = j.value("isAreaClearEnough", true);
    s.nextLeafY         = j.value("nextLeafY", 0);
    s.yOffset           = j.value("beardifierGroundDelta", 0);
    s.cScale            = j.value("cScale", 0.0F);
    s.rScale            = j.value("rScale", 0.0F);
    s.maxY              = j.value("maxY", 0);
    s.blocksSkipped     = j.value("blocksSkipped", 0);
    return s;
}

int beanstalkMaxY(int y) { return std::max(y + 100, 175); }

Step tickBeanstalk(BeanstalkState& te, BeanstalkWorld& world, jrand::RandomSource& random, int px, int py, int pz) {
    te.ticker++;
    if (te.ticker == 1) {
        te.nextLeafY = py + 10 + random.nextInt(10);
        te.yOffset   = random.nextInt(100);
        te.cScale    = random.nextFloat() * 0.25F + 0.125F; // spiral tightness scaling
        te.rScale    = random.nextFloat() * 0.25F + 0.125F; // radius change scaling
        te.maxY      = beanstalkMaxY(py);
    }
    if (!(te.ticker > 100 && te.ticker % 2 == 0)) return Step::Waiting;
    int layerYPos = py + te.layer;
    if (!(te.isAreaClearEnough && layerYPos < te.maxY)) return Step::Finished;

    float x = static_cast<float>(px);
    float z = static_cast<float>(pz);

    float radius = 4.0F + mthSin(static_cast<float>(py + te.yOffset) * te.rScale) * 3.0F; // make radius a little wavy
    x -= mthSin(static_cast<float>(py + te.yOffset) * te.cScale) * radius;
    z -= mthCos(static_cast<float>(py + te.yOffset) * te.cScale) * radius;

    // make radius a little wavy
    radius = 5.0F + mthSin(static_cast<float>(layerYPos + te.yOffset) * te.rScale) * 2.5F;

    // find center of stalk
    float cx = x + mthSin(static_cast<float>(layerYPos + te.yOffset) * te.cScale) * radius;
    float cz = z + mthCos(static_cast<float>(layerYPos + te.yOffset) * te.cScale) * radius;

    float stalkThickness = 2.5F;
    // reduce thickness near top
    if (te.maxY - layerYPos < 5) stalkThickness *= static_cast<float>(te.maxY - layerYPos) / 5.0F;

    int minX = mth::floor(static_cast<double>(x - radius - stalkThickness));
    int maxX = mth::ceil(static_cast<double>(x + radius + stalkThickness));
    int minZ = mth::floor(static_cast<double>(z - radius - stalkThickness));
    int maxZ = mth::ceil(static_cast<double>(z + radius + stalkThickness));

    // generate stalk
    for (int dx = minX; dx < maxX && te.isAreaClearEnough; dx++) {
        for (int dz = minZ; dz < maxZ && te.isAreaClearEnough; dz++) {
            float fx     = static_cast<float>(dx) - cx;
            float fz     = static_cast<float>(dz) - cz;
            float circle = fx * fx + fz * fz;
            if (circle < stalkThickness * stalkThickness) {
                te.isAreaClearEnough = tryToPlaceStalk(te, world, dx, layerYPos, dz, true);
                if (circle < stalkThickness) {
                    tryToPlaceStalk(te, world, dx, layerYPos + 1, dz, false);
                    if (circle < stalkThickness / 2) tryToPlaceStalk(te, world, dx, layerYPos + 2, dz, false);
                }
            }
        }
    }

    // reset skipped blocks as we're moving on to a new layer
    te.blocksSkipped = 0;

    // leaves?
    if (layerYPos == te.nextLeafY) {
        bool  wasAnEvenNumber = te.nextLeafY % 2 == 0;
        float v               = radius + (wasAnEvenNumber ? stalkThickness : -stalkThickness);
        int   lx              = static_cast<int>(x + mthSin(static_cast<float>(layerYPos + te.yOffset) * te.cScale) * v);
        int   lz              = static_cast<int>(z + mthCos(static_cast<float>(layerYPos + te.yOffset) * te.cScale) * v);
        placeLeaves(world, lx, layerYPos, lz);
        te.nextLeafY = layerYPos + 5 + random.nextInt(10);
        if ((te.nextLeafY % 2 == 0) == wasAnEvenNumber) te.nextLeafY++;
    }
    te.layer++;
    return Step::Grew;
}

// ---- giant blocks ----------------------------------------------------------------------------------

std::array<int, 3> cellLocal(int state) {
    int i = state - 1;
    if (i < 0 || i > 63) return {0, 0, 0};
    return {i & 3, (i >> 2) & 3, (i >> 4) & 3};
}

std::array<int, 2> giantFaceUv(Face side, int x, int y, int z) {
    // pos.offset(magicOffsetFromDir(side))
    switch (side) {
    case Face::Down: z += 2; break;
    case Face::Up: z -= 1; break;
    case Face::North:
    case Face::South: y += 1; break;
    case Face::West:
    case Face::East:
        y += 1;
        z -= 1;
        break;
    }
    int u, v;
    if (side == Face::Down || side == Face::Up) {
        int stepY = side == Face::Up ? 1 : -1;
        u         = fm4(x);
        v         = fm4(stepY * z + 1);
    } else if (side == Face::North || side == Face::South) {
        u = fm4(x);
        v = fm4(-y);
    } else {
        u = fm4(z + 1);
        v = fm4(-y);
    }
    if (side == Face::North || side == Face::East) u = 3 - u;
    return {u * 4, v * 4};
}

bool giantFaceVisible(Face side, int lx, int ly, int lz) {
    switch (side) {
    case Face::Down: return ly == 0;
    case Face::Up: return ly == 3;
    case Face::North: return lz == 0;
    case Face::South: return lz == 3;
    case Face::West: return lx == 0;
    case Face::East: return lx == 3;
    }
    return false;
}

// ---- clouds ----------------------------------------------------------------------------------------

int highestRainyBlock(int cloudY, int distance, OpaqueAt const& opaque) {
    int highest = cloudY - 1;
    for (int y = cloudY - 1; y > cloudY - distance; y--) {
        if (!opaque(y)) highest = y - 1;
        else break;
    }
    return highest;
}

int precipitationStopY(int cloudY, int distance, OpaqueAt const& opaque) { return highestRainyBlock(cloudY, distance, opaque) + 1; }

bool rainingAbove(int y, int distance, std::function<RainScan(int y)> const& at) {
    for (int yy = y; yy < y + distance; yy++) {
        auto r = at(yy);
        if (r == RainScan::Raining) return true;
        if (r == RainScan::Opaque) return false;
    }
    return false;
}

// ---- giant pickaxe ---------------------------------------------------------------------------------

bool takeGiantConversion(GiantPickMining& state) {
    int left = state.conversion;
    state.conversion = left - 1;
    return left == 64;
}

std::string giantConversionOf(std::string const& itemId) {
    if (itemId == "minecraft:cobblestone") return "twilightforest:giant_cobblestone";
    if (itemId == "minecraft:oak_log") return "twilightforest:giant_log";
    if (itemId == "minecraft:oak_leaves") return "twilightforest:giant_leaves";
    if (itemId == "minecraft:obsidian") return "twilightforest:giant_obsidian";
    return {};
}

} // namespace gf::core::giants
