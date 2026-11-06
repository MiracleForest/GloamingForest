// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/BerryBushFeature.java
//   src/main/java/twilightforest/world/components/feature/config/BerryBushConfig.java
//   src/main/java/twilightforest/world/components/feature/EnchantedForestVinesFeature.java
//   src/main/java/twilightforest/world/components/feature/FallenLeavesFeature.java
//   src/main/java/twilightforest/world/components/feature/HugeLilypadFeature.java
//   src/main/java/twilightforest/world/components/feature/HugeWaterLilyFeature.java
//   src/main/java/twilightforest/world/components/feature/UndergroundPlantFeature.java
//   src/main/java/twilightforest/util/WorldUtil.java (getRandomElementWithWeights)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora: TF's 8 flora feature types, registered by id so the imported configured / placed
// features (and U1's biome lists) resolve. Random draws follow Java's order.

#include "feature/flora/Flora.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"

#include "core/flora/FloraMath.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::feature::flora {

namespace kit = features::kit;
using features::FeatureContext;

namespace {

bool inside(worldgen::Region& r, BlockPos const& p) { return p.y >= r.minY() && p.y < r.maxY() && r.isInside(p.x, p.z); }

Block const& at(worldgen::Region& r, BlockPos const& p) {
    static Block const* air = kit::javaBlock("minecraft:air");
    return inside(r, p) ? r.getBlock(p) : *air;
}

bool isEmpty(worldgen::Region& r, BlockPos const& p) { return kit::isAir(at(r, p)); }

bool set(worldgen::Region& r, BlockPos const& p, Block const& b) { return inside(r, p) && r.setBlock(p, b, 2); }

/// BlockState#canSurvive in world generation: the flora rules, else the kit's.
bool canSurvive(worldgen::Region& r, BlockPos const& p, Block const& b) {
    if (auto ok = survivesIn(r, p, b)) return *ok;
    return kit::canSurvive(r, p, b);
}

// ---- berry_bush ------------------------------------------------------------------------------------------

/// Biome#shouldSnow for the bush features: TF's cold biomes (snowy forest, glacier).
bool snowyBiome(std::string const& biome) {
    return biome == "twilightforest:snowy_forest" || biome == "twilightforest:glacier" || biome.find("snow") != std::string::npos
        || biome.find("frozen") != std::string::npos;
}

struct BerryBush {
    worldgen::Region& level;
    Block const&      bush;
    std::string       generatesOn;
    bool              snowy;

    bool setBush(BlockPos const& pos, RandomSource& random) { return setBush(pos, random.nextFloat() < 0.2f ? 3 : 2); }

    bool setBush(BlockPos const& pos, int age) {
        if (!inside(level, pos)) return false;
        Block const& replace = level.getBlock(pos);
        if (!kit::hasTag(replace, "twilightforest:tf_berry_bushes_replace") || kit::hasTag(replace, "minecraft:features_cannot_replace") || kit::isFluid(replace))
            return false;
        BlockPos     downPos = relative(pos, Down);
        Block const& below   = at(level, downPos);
        if (!kit::hasTag(below, generatesOn) && age < 2) return false;
        Block const* place = &kit::withProperty(bush, "age", std::to_string(age));
        if (snowy && below.getTypeName() != bush.getTypeName()) place = &kit::withProperty(*place, "layers", "1");
        level.setBlock(pos, *place, 3);
        if (snowy && age >= 2) {
            if (auto const* snow = kit::javaBlock("minecraft:snow")) set(level, relative(pos, Up), *snow);
        }
        return true;
    }

    bool large(BlockPos const& pos, RandomSource& random) {
        bool placed = false;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) placed |= setBush(offset(pos, dx, -2, dz), random);
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -1; dy <= 0; ++dy)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::abs(dx) + std::abs(dz) < 4) placed |= setBush(offset(pos, dx, dy, dz), random);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) placed |= setBush(offset(pos, dx, 1, dz), random);
        return placed;
    }

    bool medium(BlockPos const& pos, RandomSource& random) {
        bool placed = false;
        for (int dy = -1; dy <= 2; ++dy) {
            int maxTaxicab = std::min(2 - dy, 2);
            for (int dx = -maxTaxicab; dx <= maxTaxicab; ++dx)
                for (int dz = -maxTaxicab; dz <= maxTaxicab; ++dz)
                    if (std::abs(dx) + std::abs(dz) < 2 * maxTaxicab || random.nextBoolean()) placed |= setBush(offset(pos, dx, dy, dz), random);
        }
        return placed;
    }

    bool small(BlockPos const& pos, RandomSource& random) {
        bool placed = setBush(pos, random);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 0; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    if (std::abs(dx) + std::abs(dz) == 1 && random.nextBoolean()) placed |= setBush(offset(pos, dx, dy, dz), random.nextInt(4));
        return placed;
    }
};

bool berryBush(FeatureContext const& c) {
    Block const* bush = kit::javaState(c.config.value("bush", Json::object()));
    if (!bush) return false;
    std::string generatesOn = c.config.value("generates_on", std::string{});
    if (!generatesOn.empty() && generatesOn[0] == '#') generatesOn.erase(0, 1);
    if (!kit::hasTag(at(c.region, relative(c.origin, Down)), generatesOn)) return false;
    bool      snowy = c.config.value("can_generate_snowy", false) && snowyBiome(c.region.biome(c.origin));
    BerryBush b{c.region, *bush, generatesOn, snowy};
    // WorldUtil.getRandomElementWithWeights: LARGE 1, MEDIUM 2, SMALL 4, TINY 3
    switch (core::flora::chooseBushNode(c.random.nextFloat())) {
    case core::flora::BushNode::Large: return b.large(c.origin, c.random);
    case core::flora::BushNode::Medium: return b.medium(c.origin, c.random);
    case core::flora::BushNode::Small: return b.small(c.origin, c.random);
    default: return b.setBush(c.origin, c.random.nextInt(4));
    }
}

// ---- enchanted_forest_vines -----------------------------------------------------------------------------------

/// VineBlock.isAcceptableNeighbour: the neighbour's face towards the vine is full.
bool acceptableNeighbour(worldgen::Region& r, BlockPos const& pos, int dir) {
    if (!inside(r, pos)) return false;
    Block const& b = r.getBlock(pos);
    if (b.isAir()) return false;
    return b.canProvideSupport(static_cast<uchar>(opposite(dir)), BlockSupportType::Edge);
}

bool enchantedForestVines(FeatureContext const& c) {
    auto& r = c.region;
    for (int x = c.origin.x; x < c.origin.x + 16; ++x)
        for (int z = c.origin.z; z < c.origin.z + 16; ++z) {
            // the highest MOTION_BLOCKING of the four neighbours
            int maxY = std::max(std::max(r.height(worldgen::Heightmap::MotionBlocking, x - 1, z), r.height(worldgen::Heightmap::MotionBlocking, x + 1, z)),
                                std::max(r.height(worldgen::Heightmap::MotionBlocking, x, z - 1), r.height(worldgen::Heightmap::MotionBlocking, x, z + 1)));
            for (int y = -10; y <= maxY; ++y) {
                BlockPos pos{x, y, z};
                if (c.random.nextInt(7) > 0 || !isEmpty(r, pos) || r.biome(pos) != "twilightforest:enchanted_forest") continue;
                Json properties = Json::object();
                bool empty = true, isTree = true;
                // Direction.values(): DOWN, UP, NORTH, SOUTH, WEST, EAST
                for (int dir : {Down, Up, North, South, West, East}) {
                    BlockPos rel = relative(pos, dir);
                    if (dir == Down || !acceptableNeighbour(r, rel, dir) || at(r, rel).getTypeName() == "twilightforest:rainbow_oak_leaves") continue;
                    Block const& n = at(r, rel);
                    if (!(kit::hasTag(n, "minecraft:logs") || kit::isLeaves(n))) isTree = false;
                    properties[dirName(dir)] = "true";
                    if (dir != Up) empty = false;
                }
                if (isTree && c.random.nextInt(8) > 0) continue;
                if (!empty) {
                    if (auto const* vine = kit::javaBlock("minecraft:vine", properties)) set(r, pos, *vine);
                }
            }
        }
    return true;
}

// ---- fallen_leaves -------------------------------------------------------------------------------------------

struct FallenLeaves {
    worldgen::Region& level;
    Block const&      state;

    bool canPlace(BlockPos const& pos) {
        Block const& here  = at(level, pos);
        Block const& below = at(level, relative(pos, Down));
        if (here.getTypeName() == ids::FallenLeaves) return false;
        bool free = kit::isAir(here) || here.getTypeName() == ids::Mayapple || kit::isReplaceable(here);
        return free && (kit::hasTag(below, "minecraft:dirt") || kit::isWater(below));
    }

    void placeLayers(BlockPos const& pos, int layers) {
        if (!canSurvive(level, pos, state)) return;
        set(level, pos, kit::withProperty(state, "layers", std::to_string(std::clamp(layers, 1, 8))));
    }

    bool flatPileOnWater(BlockPos const& pos, RandomSource& random) {
        for (int x = 0; x < 5; ++x)
            for (int z = 0; z < 5; ++z) {
                if (random.nextInt(3) != 0) continue;
                bool found = false;
                int  y     = 2;
                do {
                    if (canPlace(offset(pos, x, y, z))) {
                        found = true;
                        break;
                    }
                    --y;
                } while (y >= -2);
                if (!found) continue;
                placeLayers(offset(pos, x, y, z), 1);
            }
        return true;
    }

    void leafPile(BlockPos const& pos, int pileLayer) {
        bool found = false;
        int  y     = 0;
        do {
            if (canPlace(offset(pos, 0, y, 0))) {
                found = true;
                break;
            }
            --y;
        } while (y >= -2);
        if (!found) return;
        placeLayers(offset(pos, 0, y, 0), pileLayer);
    }

    void circle(BlockPos const& origin, RandomSource& random, int radius, int height) {
        for (int x = origin.x - radius; x <= origin.x + radius; ++x)
            for (int z = origin.z - radius; z <= origin.z + radius; ++z) {
                int dx = x - origin.x, dz = z - origin.z;
                if (dx * dx + dz * dz <= radius * radius) {
                    int trueHeight = height - random.nextInt(3);
                    if (trueHeight > 0) leafPile(BlockPos{x, origin.y, z}, trueHeight);
                }
            }
    }
};

bool fallenLeaves(FeatureContext const& c) {
    auto const* state = kit::javaBlock(ids::FallenLeaves);
    if (!state) return false;
    BlockPos     pos{c.origin.x, c.region.height(worldgen::Heightmap::WorldSurfaceWg, c.origin.x, c.origin.z), c.origin.z};
    FallenLeaves f{c.region, *state};
    if (!f.canPlace(pos)) return false;
    if (kit::isFluid(at(c.region, relative(pos, Down)))) return f.flatPileOnWater(pos, c.random);
    int startHeight = c.random.nextInt(6) + 1;
    set(c.region, pos, kit::withProperty(*state, "layers", std::to_string(startHeight)));
    for (int i = 0; i < startHeight; ++i) {
        f.circle(pos, c.random, i, startHeight - i - 1);
        if (c.random.nextInt(3) == 0) ++i;
    }
    return true;
}

// ---- huge lily pads and water lilies ----------------------------------------------------------------------------

bool waterBelow(worldgen::Region& r, BlockPos const& p) { return kit::isWater(at(r, relative(p, Down))); }

bool hugeLilyPad(FeatureContext const& c) {
    auto& r = c.region;
    for (int i = 0; i < 10; ++i) {
        BlockPos d = offset(c.origin, c.random.nextInt(8) - c.random.nextInt(8), c.random.nextInt(4) - c.random.nextInt(4), c.random.nextInt(8) - c.random.nextInt(8));
        bool ok = isEmpty(r, d) && waterBelow(r, d) && isEmpty(r, offset(d, 1, 0, 0)) && waterBelow(r, offset(d, 1, 0, 0)) && isEmpty(r, offset(d, 0, 0, 1))
               && waterBelow(r, offset(d, 0, 0, 1)) && isEmpty(r, offset(d, 1, 0, 1)) && waterBelow(r, offset(d, 1, 0, 1));
        // Level#isAreaLoaded(pos, 1): the 3x3 around it in the window
        ok = ok && inside(r, offset(d, -1, 0, -1)) && inside(r, offset(d, 1, 0, 1));
        if (!ok) continue;
        // Direction.from2DDataValue: 0 south, 1 west, 2 north, 3 east
        static constexpr char const* kFacing[4]{"south", "west", "north", "east"};
        char const* facing = kFacing[c.random.nextInt(4)];
        for (auto [dx, dz, piece] : {std::tuple{0, 0, "nw"}, std::tuple{1, 0, "ne"}, std::tuple{1, 1, "se"}, std::tuple{0, 1, "sw"}}) {
            if (auto const* pad = kit::javaState(Json{{"Name", ids::HugeLilyPad}, {"Properties", {{"facing", facing}, {"piece", piece}}}})) set(r, offset(d, dx, 0, dz), *pad);
        }
    }
    return true;
}

bool hugeWaterLily(FeatureContext const& c) {
    auto&       r    = c.region;
    auto const* lily = kit::javaBlock(ids::HugeWaterLily);
    for (int i = 0; i < 4; ++i) {
        BlockPos p = offset(c.origin, c.random.nextInt(8) - c.random.nextInt(8), c.random.nextInt(4) - c.random.nextInt(4), c.random.nextInt(8) - c.random.nextInt(8));
        if (lily && isEmpty(r, p) && waterBelow(r, p)) set(r, p, *lily);
    }
    return true;
}

// ---- underground_plants / _in_structure / oreberry_bushes -------------------------------------------------------

bool undergroundPlants(FeatureContext const& c, int maxCount, bool inStructure) {
    Block const* configured = kit::javaState(c.config.value("state", Json::object()));
    if (!configured) return false;
    auto& r      = c.region;
    int   x      = c.origin.x, z = c.origin.z;
    int   placed = 0;
    for (int y = c.origin.y; y > r.minY(); --y) {
        if (placed >= maxCount) break;
        BlockPos pos{x, y, z};
        if (!isEmpty(r, pos) || c.random.nextInt(6) == 0) {
            x = c.origin.x + c.random.nextInt(4) - c.random.nextInt(4);
            z = c.origin.z + c.random.nextInt(4) - c.random.nextInt(4);
            continue;
        }
        Block const* state = configured;
        if (state->getTypeName() == "twilightforest:trollvidr" && c.random.nextInt(10) == 0) {
            if (auto const* unripe = kit::javaBlock("twilightforest:unripe_trollber")) state = unripe;
        }
        if (!inside(r, pos) || !canSurvive(r, pos, *state)) continue;
        if (!inStructure && r.dimension() >= 0) {
            // LandmarkUtil.locateNearestLandmarkStart: not inside a landmark's bounding box
            if (landmark::startAt(r.dimension(), pos)) continue;
        }
        r.setBlock(pos, *state, 2);
        ++placed;
    }
    return placed > 0;
}

} // namespace

void registerFeatures() {
    features::registerType("twilightforest:berry_bush", berryBush);
    features::registerType("twilightforest:enchanted_forest_vines", enchantedForestVines);
    features::registerType("twilightforest:fallen_leaves", fallenLeaves);
    features::registerType("twilightforest:huge_lily_pad", hugeLilyPad);
    features::registerType("twilightforest:huge_water_lily", hugeWaterLily);
    features::registerType("twilightforest:underground_plants", [](FeatureContext const& c) { return undergroundPlants(c, 1 << 30, false); });
    features::registerType("twilightforest:underground_plants_in_structure", [](FeatureContext const& c) { return undergroundPlants(c, 1 << 30, true); });
    features::registerType("twilightforest:oreberry_bushes", [](FeatureContext const& c) { return undergroundPlants(c, 1, false); });
}

} // namespace gf::feature::flora
